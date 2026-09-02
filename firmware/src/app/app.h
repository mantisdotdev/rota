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

// `samples` holds the kit's WAVs, or empty samples: the platform entry point
// provides them (the host reads spec/kits/, the device waits for io/). Once at
// start-up on a platform; the test harness calls it again between cases, which
// is safe because its audio callback runs only when the test calls it.
void init(const sound::SampleBank& samples);

// Input, holds and timeouts, the fired log, a frame when one is due.
void tick();

// For the entry points, the tests and diagnostics: read on the main loop only.
const Model& model();
const FiredLog& fired_log();
int64_t audio_position();

}  // namespace app
