// Teensy power sensing. The EVT unit has no fuel gauge (that is the production
// BQ27427, D-072), so the percentage is a straight line between two cell voltages
// read through a 2:1 divider: honest near full and empty, rough in between. The
// headphone detect reads the jack's tip switch as an analog level (D-065).
#include <Arduino.h>

#include "hal/hal.h"
#include "hal/teensy/pins.h"
#include "hal/teensy/teensy_internal.h"

namespace {

constexpr int kAdcBits = 12;
constexpr int kAdcMax = (1 << kAdcBits) - 1;
constexpr float kAdcReference = 3.3f;
constexpr float kDividerRatio = 2.0f;   // two 100 kΩ resistors (unverified)
constexpr float kCellEmptyVolts = 3.3f;  // the protection cut-off is a little under this
constexpr float kCellFullVolts = 4.15f;
// A plug in the jack opens the tip switch and the pull-up takes the pin high
// (unverified: the switch polarity is read from the jack's datasheet, D-065).
constexpr int kHeadphoneThreshold = kAdcMax / 2;

}  // namespace

namespace hal::teensy {

void power_init() {
  analogReadResolution(kAdcBits);
  pinMode(pins::kBatterySense, INPUT);
  pinMode(pins::kHeadphoneSense, INPUT_PULLUP);
}

}  // namespace hal::teensy

namespace hal {

int battery_percent() {
  const float volts = static_cast<float>(analogRead(pins::kBatterySense)) * kAdcReference * kDividerRatio / kAdcMax;
  const float share = (volts - kCellEmptyVolts) / (kCellFullVolts - kCellEmptyVolts);
  const int percent = static_cast<int>(share * 100.0f + 0.5f);
  return percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

bool headphones_inserted() { return analogRead(pins::kHeadphoneSense) > kHeadphoneThreshold; }

}  // namespace hal
