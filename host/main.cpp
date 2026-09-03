// Host simulator entry point (PRD §12). The loop is the same as firmware/src/main.cpp;
// only the HAL underneath differs. Deliberately includes no SDL header: hal/sdl/ owns SDL.
// The kit's samples come off the simulator's "SD card" through io/, exactly as they do
// on the device: host/CMakeLists.txt seeds that card with the kits the repo ships.
#include <cstdio>

#include "app/app.h"
#include "engine/kits/lofi.h"
#include "hal/hal.h"
#include "io/kit.h"
#include "sound/voice.h"

int main() {
  hal::init();
  sound::SampleBank samples;
  io::load_samples(engine::kits::kLofi, samples);
  app::init(samples);
  std::puts("simulator: 1-8 pads; s w k z d e space = split swap skip undo dice show play; a b c shift+d sections;");
  std::puts("simulator: up/down or the wheel turn the selected knob, left/right pick it, - = volume; Escape quits");
  std::fflush(stdout);
  while (hal::poll()) app::tick();
  return 0;
}
