// Device entry point. The Teensy Arduino core owns main(): it calls setup() once
// and loop() forever. Everything below hal:: is portable and shared with host/main.cpp.
// The kit's samples arrive when io/ reads them from the card; until then the sample
// pads are silent and the synth pads play.
#include <Arduino.h>

#include "app/app.h"
#include "hal/hal.h"
#include "sound/voice.h"

void setup() {
  hal::init();
  const sound::SampleBank silent{};
  app::init(silent);
}

void loop() {
  hal::poll();
  app::tick();
}
