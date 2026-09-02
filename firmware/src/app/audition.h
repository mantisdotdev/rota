#pragma once

#include <cstdint>

#include "engine/events.h"
#include "engine/kit.h"
#include "engine/state.h"

namespace app {

// The sound a pad makes the moment it is pressed (§6.7, D-085): a full-velocity hit
// with the pitch the next tap would add. A drum is its sample; the chord pad sounds
// the next chord of the progression and the pluck pad the next note of its
// sequence (D-036); the bass sounds `chord_root_midi`, the root of the chord playing
// now, or the key root when that is 0 (§6.4). `at` is where the playhead is, so the
// ring can place the flash.
engine::Event audition(const engine::State& state, const engine::Kit& kit, engine::Pad pad, uint8_t chord_root_midi,
                       engine::Fraction at);

}  // namespace app
