// Teensy 4.1 HAL (PRD §12, D-088): the core loop pieces. Nothing here has run on
// real hardware yet (CLAUDE.md, Landmines). Audio, display, input, storage and
// power live in their own files beside this one.
#include <Arduino.h>
#include <IntervalTimer.h>

#include "hal/hal.h"
#include "hal/teensy/teensy_internal.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
// Under the audio library's update (208) so the scheduler never delays a block;
// above the main loop, which is what a timer is for.
constexpr uint8_t kTimerPriority = 224;

// micros() wraps every 71 minutes; poll() folds it into 64 bits often enough. The
// pair is written with interrupts off, so a reader in an interrupt sees one epoch.
struct ClockBase {
  uint64_t us;
  uint32_t micros;
};
ClockBase clock_base_{0, 0};

IntervalTimer timer_;

}  // namespace

namespace hal {

void init() {
  Serial.begin(kSerialBaud);  // never waited for: the device boots with nothing attached
  clock_base_ = ClockBase{0, micros()};
  teensy::storage_init();
  teensy::power_init();
  teensy::input_init();
  teensy::display_init();
}

uint64_t now_us() {
  const ClockBase base = clock_base_;
  return base.us + static_cast<uint32_t>(micros() - base.micros);
}

bool poll() {
  noInterrupts();
  const uint32_t now = micros();
  clock_base_.us += static_cast<uint32_t>(now - clock_base_.micros);
  clock_base_.micros = now;
  interrupts();
  teensy::input_read();
  return true;
}

void start_timer(uint32_t period_us, TimerCallback callback) {
  timer_.priority(kTimerPriority);
  timer_.begin(callback, period_us);
}

// One core: keeping the timer's interrupt out is the whole lock. Inside that
// interrupt the pair is a no-op with a harmless re-enable at the end.
void lock() { noInterrupts(); }
void unlock() { interrupts(); }

void log(const char* line) {
  if (Serial) Serial.println(line);
}

}  // namespace hal
