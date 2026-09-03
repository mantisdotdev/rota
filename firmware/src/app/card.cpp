#include "app/card.h"

#include <cstdio>

#include "engine/state.h"
#include "hal/hal.h"
#include "io/store.h"

namespace app {

namespace {

// Nothing is saved by hand (§9.6), so the card takes the song a second after the
// player stops changing it: a write never lands mid-gesture, continuous editing
// costs one write a second, and a card that refuses is tried again at that rate
// rather than on every frame (D-104).
constexpr uint64_t kQuietUs = 1000000;
constexpr int kStatusFormatCapacity = 32;

// The songs and the settings live here rather than on the stack: a song is 1.4 KB
// and firmware allocates nothing after init (§12 rule 4).
engine::Song staged;  // what the model holds now
engine::Song saved;   // what the card took, when it did
engine::Song loaded;  // the slot a pick asked for, or the boot song
io::Settings saved_settings = io::kDefaultSettings;
io::Settings boot_settings = io::kDefaultSettings;
Slot boot_slots[engine::kSongSlotCount];
// The open slot holds a file nothing could be read from, and the player has not
// changed anything since. Writing the power-on song over it would destroy it for
// nothing, so it is left alone until an edit means to replace it (T-97).
bool boot_active_invalid = false;
bool active_invalid = false;
// False while the card is not known to hold `saved` or `saved_settings` — before the
// first write, and after one it refused. A refused write must not look like a card
// that agrees, or the song it lost would never be written again.
bool card_holds_song = false;
bool card_holds_settings = false;
bool song_dirty = false;
bool settings_dirty = false;
uint64_t song_changed_us = 0;
uint64_t settings_changed_us = 0;
bool erase_tried = false;
uint64_t erase_at_us = 0;

int slot_index(int slot) { return slot - io::kFirstSlot; }

// The card and the model agree: nothing to write until the player changes something.
void agree(const engine::Song& song, const io::Settings& settings) {
  saved = song;
  saved_settings = settings;
  card_holds_song = true;
  card_holds_settings = true;
  song_dirty = false;
  settings_dirty = false;
}

// A pick the card would not let through (§9.6): the player stays where they are,
// with the loop they have. Refusing is the only answer that keeps their work — a
// switch would drop what the card just refused to take, or copy over a file that
// did not parse and might still be somebody's song.
void refuse_pick(Model& model, uint64_t now_us, int picked, const char* text) {
  hal::lock();
  if (model.picked_song == picked) {  // a newer pick is not this one
    model.picked_song = io::kNoSlot;
    model.replace_picked = false;
  }
  say(model.status, now_us, kStatusUs, text);
  hal::unlock();
}

Slot state_of(const engine::Song& song) { return is_empty(song) ? Slot::empty : Slot::filled; }

// A pick from the song view (§9.6, T-56): what is on screen goes back to its own
// slot, and the slot picked either comes back from the card or, being empty, becomes
// a copy of what is on screen — which is a save, not a change of what is playing.
void switch_song(int slot, uint64_t now_us, Model& model, const engine::Kit& kit) {
  const int from = model.settings.song;
  const bool replace = model.replace_picked;  // a hold: copy over the slot, do not read it (D-107)
  const bool leave_alone = active_invalid && staged == saved;  // nothing of the player's to write over it
  char refusal[kStatusFormatCapacity];
  if (!leave_alone && !io::save_song(from, kit, staged)) {
    card_holds_song = false;  // the edit stays in hand and is written as soon as a card takes it
    std::snprintf(refusal, sizeof refusal, "song %d did not save", from);
    refuse_pick(model, now_us, slot, refusal);
    return;
  }
  const io::LoadResult result = replace ? io::LoadResult::missing : io::load_song(slot, kit, loaded);
  if (result == io::LoadResult::invalid) {
    // A file that went bad since boot: mark it so the hold that replaces it works (D-107).
    hal::lock();
    model.song_slots[slot_index(slot)] = Slot::unreadable;
    hal::unlock();
    std::snprintf(refusal, sizeof refusal, "hold to replace song %d", slot);
    refuse_pick(model, now_us, slot, refusal);
    return;
  }
  const bool has_song = result == io::LoadResult::loaded && !is_empty(loaded);
  const bool copied = has_song || io::save_song(slot, kit, staged);

  io::Settings settings;
  hal::lock();
  if (has_song) set_song(model, loaded);
  model.settings.song = slot;
  model.song_slots[slot_index(from)] = leave_alone ? Slot::unreadable : state_of(staged);  // its file is still there
  model.song_slots[slot_index(slot)] = has_song ? state_of(loaded) : state_of(staged);
  if (model.picked_song == slot) {  // a newer pick is not this one
    model.picked_song = io::kNoSlot;
    model.replace_picked = false;
  }
  settings = model.settings;
  hal::unlock();

  saved = has_song ? loaded : staged;
  saved_settings = settings;
  card_holds_song = copied;  // a refused copy is written again a second from now, not lost
  card_holds_settings = io::save_settings(settings);
  song_dirty = !card_holds_song;
  settings_dirty = !card_holds_settings;
  song_changed_us = now_us;
  settings_changed_us = now_us;
  active_invalid = false;  // whatever the new slot is, it is not a file that would not parse
}

// A factory reset (§9.4): every slot back to an empty song, so what the song view's
// tiles say and what the card holds agree. The model is already the power-on one, so
// `staged` is that empty song. Kept pending until the card takes every write and
// tried again a second later, never eight failing writes a frame.
void erase_card(uint64_t now_us, Model& model, const engine::Kit& kit) {
  if (erase_tried && now_us - erase_at_us < kQuietUs) return;
  erase_tried = true;
  erase_at_us = now_us;
  bool written = true;
  for (int slot = io::kFirstSlot; slot <= io::kLastSlot; ++slot) written = io::save_song(slot, kit, staged) && written;
  written = io::save_settings(io::kDefaultSettings) && written;
  if (!written) return;
  hal::lock();
  model.erase_pending = false;
  hal::unlock();
  agree(staged, io::kDefaultSettings);
  erase_tried = false;
}

// True when the card should be written now: it does not hold what the model has, and
// the model has stopped changing for a second.
bool due(bool differs, uint64_t now_us, bool& dirty, uint64_t& changed_us) {
  if (!differs) {
    dirty = false;
    return false;
  }
  if (!dirty) {
    dirty = true;
    changed_us = now_us;
    return false;
  }
  if (now_us - changed_us < kQuietUs) return false;
  changed_us = now_us;  // whether the card takes it or not, the next try is a second off
  return true;
}

}  // namespace

void read_card(const engine::Kit& kit) {
  io::load_settings(boot_settings);
  io::LoadResult current = io::LoadResult::missing;
  for (int slot = io::kFirstSlot; slot <= io::kLastSlot; ++slot) {
    const bool is_current = slot == boot_settings.song;
    engine::Song& into = is_current ? loaded : staged;  // the one being opened is kept; the rest only count
    const io::LoadResult result = io::load_song(slot, kit, into);
    if (is_current) current = result;
    // A file that did not parse still means the slot is taken: the song view draws it
    // filled, a pick refuses it and a hold replaces it, rather than copying over
    // somebody's song on a tap (T-97, D-107).
    boot_slots[slot_index(slot)] = result == io::LoadResult::invalid ? Slot::unreadable
                                   : result == io::LoadResult::loaded ? state_of(into)
                                                                      : Slot::empty;
  }
  boot_active_invalid = current == io::LoadResult::invalid;
  if (current != io::LoadResult::loaded) {  // no file, or one nothing could be read from: the power-on song
    for (engine::State& section : loaded.sections) section = engine::make_state(kit);
    loaded.arrangement_length = 0;
    loaded.lineage[0] = '\0';
  }
}

void apply_card(Model& model) {
  model.settings = boot_settings;
  for (int i = 0; i < engine::kSongSlotCount; ++i) model.song_slots[i] = boot_slots[i];
  set_song(model, loaded);
  song_of(model, staged);
  // An absent file and an empty song are the same thing, so a boot writes nothing:
  // the first thing the player plays is what makes a file.
  agree(staged, model.settings);
  active_invalid = boot_active_invalid;
  erase_tried = false;
}

void keep_card(uint64_t now_us, Model& model, const engine::Kit& kit) {
  io::Settings settings;
  int picked;
  bool erase;
  hal::lock();
  song_of(model, staged);
  settings = model.settings;
  picked = model.picked_song;
  erase = model.erase_pending;
  hal::unlock();

  if (erase) {
    erase_card(now_us, model, kit);
    return;
  }
  if (picked != io::kNoSlot) {
    switch_song(picked, now_us, model, kit);
    return;
  }
  if (due(!card_holds_song || staged != saved, now_us, song_dirty, song_changed_us) &&
      io::save_song(settings.song, kit, staged)) {
    saved = staged;
    card_holds_song = true;
    song_dirty = false;
    active_invalid = false;  // the file that would not parse has been replaced by the player's own
  }
  if (due(!card_holds_settings || settings != saved_settings, now_us, settings_dirty, settings_changed_us) &&
      io::save_settings(settings)) {
    saved_settings = settings;
    card_holds_settings = true;
    settings_dirty = false;
  }
}

}  // namespace app
