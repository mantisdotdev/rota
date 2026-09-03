#include "app/model.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace app {

Model::Model(const engine::Kit& kit)
    : sections{engine::Section(engine::make_state(kit)), engine::Section(engine::make_state(kit)),
               engine::Section(engine::make_state(kit)), engine::Section(engine::make_state(kit))},
      current(0),
      playing(0),
      pending_section(kNoSection),
      arrangement{0, {}},
      song_lineage{},
      song_mode(false),
      song_position(0),
      song_start_pending(false),
      transport(false),
      roll(false),
      song_hint_dismissed(false),
      view(View::ring),
      status{{}, 0, 0},
      knob{{}, 0, 0},
      settings(io::kDefaultSettings),
      settings_cursor(0),
      song_slots{},
      picked_song(io::kNoSlot),
      replace_picked(false),
      erase_pending(false),
      tutorial{false, 0, false},
      master_volume(kDefaultMasterVolume) {}

void say(Status& status, uint64_t at_us, uint32_t duration_us, const char* text) {
  std::snprintf(status.text, sizeof status.text, "%s", text);
  status.shown_at_us = at_us;
  status.duration_us = duration_us;
}

bool is_empty(const engine::State& state) {
  for (int i = 0; i < engine::kTrackCount; ++i) {
    if (!engine::is_empty(state.tracks[i])) return false;
  }
  return true;
}

// An empty slot is one the song view draws as an outline and a pick copies over:
// nothing tapped anywhere and nothing arranged (§9.6).
bool is_empty(const engine::Song& song) {
  if (song.arrangement_length > 0) return false;
  for (const engine::State& section : song.sections) {
    if (!is_empty(section)) return false;
  }
  return true;
}

// Mute is transient — a pad is muted while it is held (§6.2) — and no code carries
// it, so the copy drops it: two songs that differ only in a held pad are the same
// song, and holding one writes no card.
void song_of(const Model& model, engine::Song& song) {
  for (int i = 0; i < engine::kSectionCount; ++i) {
    song.sections[i] = model.sections[i].state();
    for (engine::Track& track : song.sections[i].tracks) track.mute = false;
  }
  song.arrangement_length = model.arrangement.length;
  std::memcpy(song.arrangement, model.arrangement.letters, model.arrangement.length);
  std::strcpy(song.lineage, model.song_lineage);  // what the card gave us, back as it was
}

// Placement new, as app::init and the factory reset use it: a section is 21 KB of
// undo levels, far too much to build on the stack (§12 rule 4).
void set_song(Model& model, const engine::Song& song) {
  for (int i = 0; i < engine::kSectionCount; ++i) new (&model.sections[i]) engine::Section(song.sections[i]);
  model.arrangement.length = song.arrangement_length;
  std::memcpy(model.arrangement.letters, song.arrangement, song.arrangement_length);
  std::strcpy(model.song_lineage, song.lineage);
}

}  // namespace app
