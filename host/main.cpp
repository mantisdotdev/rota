// Host simulator entry point (PRD §12). The loop is the same as firmware/src/main.cpp;
// only the HAL underneath differs. Deliberately includes no SDL header: hal/sdl/ owns SDL.
// The kit and its samples come off the simulator's "SD card" through io/, exactly as
// they do on the device: host/CMakeLists.txt seeds that card with the kits the repo ships.
#include <cstdio>

#include "app/app.h"
#include "hal/hal.h"

int main() {
  hal::init();
  app::init();
  std::puts("simulator: 1-8 pads; s w k z d e space = split swap skip undo dice show play; a b c shift+d sections;");
  std::puts("simulator: up/down or the wheel turn the selected knob, left/right pick it, - = volume; Escape quits");
  std::fflush(stdout);
  while (hal::poll()) app::tick();
  return 0;
}
