#include "app/card.h"

#include "hal/hal.h"
#include "io/store.h"

namespace app {

namespace {

// Nothing is saved by hand (§9.6), so the card takes the song a second after the
// player stops changing it: a write never lands mid-gesture, continuous editing
// costs one write a second, and a card that refuses is tried again at that rate
// rather than on every frame (D-104).
constexpr uint64_t kQuietUs = 1000000;

// The two songs and the settings live here rather than on the stack: a song is
// 1.4 KB and firmware allocates nothing after init (§12 rule 4).
engine::Song staged;  // what the model holds now
engine::Song saved;   // what the card last took
engine::Song loaded;  // the slot a pick asked for
io::Settings saved_settings = io::kDefaultSettings;
bool song_dirty = false;
bool settings_dirty = false;
uint64_t song_changed_us = 0;
uint64_t settings_changed_us = 0;

int slot_index(int slot) { return slot - io::kFirstSlot; }

// The card and the model agree: nothing to write until the player changes something.
void agree(const engine::Song& song, const io::Settings& settings) {
  saved = song;
  saved_settings = settings;
  song_dirty = false;
  settings_dirty = false;
}

// A pick from the song view (§9.6, T-56): what is on screen goes back to its own
// slot, and the slot picked either comes back from the card or, being empty, becomes
// a copy of what is on screen — which is a save, not a change of what is playing.
void switch_song(int slot, Model& model, const engine::Kit& kit) {
  const int from = model.settings.song;
  io::save_song(from, kit, staged);
  const bool has_song = io::load_song(slot, kit, loaded) && !is_empty(loaded);
  if (!has_song) io::save_song(slot, kit, staged);

  io::Settings settings;
  hal::lock();
  if (has_song) set_song(model, loaded);
  model.settings.song = slot;
  model.song_filled[slot_index(from)] = !is_empty(staged);
  model.song_filled[slot_index(slot)] = has_song || !is_empty(staged);
  if (model.picked_song == slot) model.picked_song = io::kNoSlot;  // a newer pick is not this one
  settings = model.settings;
  hal::unlock();

  io::save_settings(settings);
  agree(has_song ? loaded : staged, settings);
}

// A factory reset (§9.4): every slot back to an empty song, so what the song view's
// tiles say and what the card holds agree. The model is already the power-on one,
// so `staged` is that empty song. One attempt, whatever the card says: a card that
// refuses must not turn the main loop into eight failing writes a frame.
void erase_card(Model& model, const engine::Kit& kit) {
  for (int slot = io::kFirstSlot; slot <= io::kLastSlot; ++slot) io::save_song(slot, kit, staged);
  io::save_settings(io::kDefaultSettings);
  hal::lock();
  model.erase_pending = false;
  hal::unlock();
  agree(staged, io::kDefaultSettings);
}

}  // namespace

void read_card(Model& model, const engine::Kit& kit) {
  io::load_settings(model.settings);
  for (int slot = io::kFirstSlot; slot <= io::kLastSlot; ++slot) {
    if (!io::load_song(slot, kit, loaded)) continue;  // no file, or one that did not load: an empty slot
    model.song_filled[slot_index(slot)] = !is_empty(loaded);
    if (slot == model.settings.song) set_song(model, loaded);
  }
  song_of(model, staged);  // the card holds this already: nothing to write until the player changes it
  agree(staged, model.settings);
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
    erase_card(model, kit);
    return;
  }
  if (picked != io::kNoSlot) {
    switch_song(picked, model, kit);
    return;
  }

  if (staged != saved) {
    if (!song_dirty) {
      song_dirty = true;
      song_changed_us = now_us;
    } else if (now_us - song_changed_us >= kQuietUs) {
      song_changed_us = now_us;
      if (io::save_song(settings.song, kit, staged)) {
        saved = staged;
        song_dirty = false;
      }
    }
  } else {
    song_dirty = false;
  }

  if (settings != saved_settings) {
    if (!settings_dirty) {
      settings_dirty = true;
      settings_changed_us = now_us;
    } else if (now_us - settings_changed_us >= kQuietUs) {
      settings_changed_us = now_us;
      if (io::save_settings(settings)) {
        saved_settings = settings;
        settings_dirty = false;
      }
    }
  } else {
    settings_dirty = false;
  }
}

}  // namespace app
