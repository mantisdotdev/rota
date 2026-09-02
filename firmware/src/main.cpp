// Device entry point. The Teensy Arduino core owns main(): it calls setup() once
// and loop() forever. Everything below hal:: is portable and shared with host/main.cpp.
#include <Arduino.h>

#include "app/app.h"
#include "hal/hal.h"

void setup() {
  hal::init();
  app::init();
}

void loop() {
  hal::poll();
  app::tick(hal::now_ms());
  hal::present();
}
