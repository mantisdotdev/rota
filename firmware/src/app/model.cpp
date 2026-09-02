#include "app/model.h"

namespace app {

Model::Model(const engine::Kit& kit)
    : sections{engine::Section(engine::make_state(kit)), engine::Section(engine::make_state(kit)),
               engine::Section(engine::make_state(kit)), engine::Section(engine::make_state(kit))},
      current(0),
      playing(0),
      pending_section(kNoSection),
      arrangement{0, {}},
      song_mode(false),
      song_position(0),
      song_start_pending(false),
      transport(false),
      roll(false),
      song_hint_dismissed(false),
      view(View::ring),
      status{{}, 0, 0},
      knob{{}, 0, 0},
      settings{0, kDefaultBrightness, kDefaultSleepMinutes, true, true, true, true},
      tutorial{false, 0, false},
      master_volume(kDefaultMasterVolume) {}

bool is_empty(const engine::State& state) {
  for (int i = 0; i < engine::kTrackCount; ++i) {
    if (!engine::is_empty(state.tracks[i])) return false;
  }
  return true;
}

}  // namespace app
