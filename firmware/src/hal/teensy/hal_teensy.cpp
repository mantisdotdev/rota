// Teensy 4.1 HAL (PRD §12, D-088): the core loop pieces. Nothing here has run on
// real hardware yet (CLAUDE.md, Landmines). Audio, display, input, storage and
// power live in their own files beside this one.
#include <Arduino.h>
#include <IntervalTimer.h>

#include <atomic>

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
std::atomic<hal::TimerCallback> timer_callback_{nullptr};  // read in the timer's interrupt
bool timer_started_ = false;

void timer_trampoline() {
  const hal::TimerCallback callback = timer_callback_.load(std::memory_order_acquire);
  if (callback != nullptr) callback();
}

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
  timer_callback_.store(callback, std::memory_order_release);
  if (timer_started_) return;  // already ticking: the new callback is all that changes
  timer_.priority(kTimerPriority);
  timer_started_ = timer_.begin(timer_trampoline, period_us);  // false: every hardware timer is taken
  if (!timer_started_) Serial.println("hal/teensy: no hardware timer free for the scheduler");
}

// One core: keeping the timer's interrupt out is the whole lock. Inside that
// interrupt the pair is a no-op with a harmless re-enable at the end.
void lock() { noInterrupts(); }
void unlock() { interrupts(); }

// The MIDI wire and the sync jack, still undriven: Serial1 and the pins WIRING.md
// reserves are wired up in link_teensy.cpp, which arrives with the commit that
// puts a scope on them. Until then the device says it has no port, which is the
// truth about this firmware and not about the hardware.
int read_clock_in(ClockIn*, int) { return 0; }
bool send_clock_out(ClockPort, ClockPulse, uint64_t) { return false; }
int midi_read(uint8_t*, int) { return 0; }
int midi_send(const uint8_t*, int) { return 0; }
bool midi_port_open() { return false; }

void log(const char* line) {
  if (Serial) Serial.println(line);
}

}  // namespace hal
