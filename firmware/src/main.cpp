// Device entry point. The Teensy Arduino core owns main(): it calls setup() once
// and loop() forever. Everything below hal:: is portable and shared with host/main.cpp.
#include <Arduino.h>

#include "app/app.h"
#include "engine/kits/lofi.h"
#include "hal/hal.h"
#include "io/kit.h"
#include "sound/voice.h"

void setup() {
  hal::init();
  // The kit's WAVs come off the card into PSRAM. Whatever is missing — a card, the
  // PSRAM, one file — costs those pads their sound and nothing else: io/ says what it
  // could not read over the serial log and the synth pads play either way.
  sound::SampleBank samples;
  io::load_samples(engine::kits::kLofi, samples);
  app::init(samples);
}

void loop() {
  hal::poll();
  app::tick();
}
