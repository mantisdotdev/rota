// Host simulator entry point (PRD §12). The loop is the same as firmware/src/main.cpp;
// only the HAL underneath differs. Deliberately includes no SDL header: hal/sdl/ owns SDL.
#include <cstdio>

#include "app/app.h"
#include "hal/hal.h"

int main() {
  hal::init();
  app::init();
  std::puts("simulator: running; close the window or press Escape to quit");
  std::fflush(stdout);
  while (hal::poll()) {
    app::tick(hal::now_ms());
    hal::present();
  }
  return 0;
}
