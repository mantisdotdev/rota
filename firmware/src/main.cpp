// Device entry point. The Teensy Arduino core owns main(): it calls setup() once
// and loop() forever. Everything below hal:: is portable and shared with host/main.cpp.
#include <Arduino.h>

#include "app/app.h"
#include "hal/hal.h"

void setup() {
  hal::init();
  // The kit the card names, its WAVs, the songs and the settings: app::init reads them
  // all. Whatever is missing — a card, the PSRAM, one file — costs only what depends on
  // it, so the instrument comes up either way.
  app::init();
}

void loop() {
  hal::poll();
  app::tick();
}
