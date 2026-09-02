// Teensy 4.1 HAL. Nothing here has run on real hardware yet; pin numbers, SPI
// clocks and I2C addresses, when they arrive, come from datasheets and library docs
// and stay unverified until the bring-up runbook says otherwise (CLAUDE.md, Landmines).
#include <Arduino.h>

#include "hal/hal.h"

namespace hal {

void init() {}

uint32_t now_ms() { return millis(); }

bool poll() { return true; }

void present() {}

}  // namespace hal
