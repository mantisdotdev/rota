#pragma once

#include <cstdint>

#include "app/audio_path.h"
#include "app/model.h"
#include "sound/voice.h"

// The portable top layer (PRD §12): the scheduler, the input grammar, sections and
// songs, glued to the HAL. Both entry points (firmware/src/main.cpp and
// host/main.cpp) drive it the same way: init once, tick from the main loop; the
// audio and timer callbacks are registered with the HAL by init.
namespace app {

// Brings the app up on whatever the card holds: the kit the settings name and its
// samples, the songs, the settings themselves. Once at start-up on a platform; the
// test harness calls it again between cases, which is safe because its audio callback
// runs only when the test calls it.
void init();

// The kit being played, which is the card's or the one built in (D-109).
const engine::Kit& kit();

// Input, holds and timeouts, the fired log, a frame when one is due.
void tick();

// For the entry points, the tests and diagnostics: read on the main loop only.
const Model& model();
const FiredLog& fired_log();
int64_t audio_position();

}  // namespace app
