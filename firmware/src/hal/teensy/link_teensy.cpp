// The MIDI wire and the sync jack on the Teensy (PRD §7.6, §11, D-111, D-114). NONE of
// this has run on real hardware (CLAUDE.md, Landmines): the baud, the pins, the edge
// polarity, the pulse width and the timestamp accuracy are all from datasheets and
// library docs, and the bring-up runbook (T-118) is what decides them.
//
// MIDI is one UART, Serial1: bytes in on pin 0, bytes out on pin 1. The four System
// Real Time bytes are lifted out where they arrive and handed to read_clock_in with a
// timestamp, because one of them may sit between any two bytes of any other message; the
// rest of the wire stays on midi_read for io/ to parse (D-114). The sync jack is a
// rising-edge interrupt in on pin 32 and a 5 ms active-high pulse out on pin 34 (C-01).
//
// Timestamps: a byte is stamped when poll() reads it, and several bytes buffered between
// two polls are spread back by one byte time each, since MIDI delivers them 320 µs apart
// — a stand-in for the per-byte receive interrupt a bring-up bench would add if the jitter
// matters. The sync edge is stamped in its own interrupt, which is exact.
#include <Arduino.h>

#include <atomic>

#include "hal/hal.h"
#include "hal/teensy/pins.h"
#include "hal/teensy/teensy_internal.h"

namespace {

// C-01: 2 PPQN, tip, active-high, 5 ms wide, from Teenage Engineering's PO-16 guide as
// the BOM cites it (hardware/BOM.md, "sync in/out"); UNVERIFIED until a scope confirms it (T-118).
constexpr uint32_t kSyncPulseWidthUs = 5000;

// A byte a status code names, or 0 if it names none this firmware follows.
bool real_time_pulse(uint8_t byte, hal::ClockPulse& pulse) {
  switch (byte) {
    case 0xF8: pulse = hal::ClockPulse::tick; return true;
    case 0xFA: pulse = hal::ClockPulse::start; return true;
    case 0xFB: pulse = hal::ClockPulse::resume; return true;
    case 0xFC: pulse = hal::ClockPulse::stop; return true;
    default: return false;
  }
}

uint8_t status_of(hal::ClockPulse pulse) {
  switch (pulse) {
    case hal::ClockPulse::tick: return 0xF8;
    case hal::ClockPulse::start: return 0xFA;
    case hal::ClockPulse::resume: return 0xFB;
    case hal::ClockPulse::stop: return 0xFC;
  }
  return 0xF8;
}

// One ring for the clock pulses of both ports, filled by poll() (MIDI) and the sync
// interrupt and drained by read_clock_in, all on one core. Access is bracketed by
// noInterrupts() so the sync interrupt cannot tear a push; the ring keeps one slot empty.
constexpr int kRing = 2 * hal::kClockInCapacity + 1;
hal::ClockIn clock_ring_[kRing];
volatile int clock_head_ = 0;
volatile int clock_tail_ = 0;

void push_clock(hal::ClockPort port, hal::ClockPulse pulse, uint64_t time_us) {
  const int next = (clock_tail_ + 1) % kRing;
  if (next == clock_head_) return;  // full: drop the newest, as the per-port rings do (D-121)
  clock_ring_[clock_tail_] = hal::ClockIn{port, pulse, time_us};
  clock_tail_ = next;
}

// The rest of the wire: whole bytes for io/, one ring deep enough for a jam message and
// a little of the wire behind it (hal::kMidiInputCapacity). Producer and consumer are
// both the main loop, so no interrupt guard is needed.
uint8_t midi_ring_[hal::kMidiInputCapacity + 1];
int midi_head_ = 0;
int midi_tail_ = 0;

void push_midi(uint8_t byte) {
  const int next = (midi_tail_ + 1) % (hal::kMidiInputCapacity + 1);
  if (next == midi_head_) return;
  midi_ring_[midi_tail_] = byte;
  midi_tail_ = next;
}

// One pulse each port may have armed for a future deadline. `armed` is atomic because
// send_clock_out publishes from the 2 ms timer and link_poll consumes on the main loop;
// the pulse and deadline are written before armed is set and read after it is seen, so
// the release/acquire pair keeps a deadline from tearing and a pulse from being missed.
struct Pending {
  std::atomic<bool> armed{false};
  hal::ClockPulse pulse{hal::ClockPulse::tick};
  uint64_t at_us{0};
};
Pending midi_out_;
Pending sync_out_;
bool sync_high_ = false;
uint64_t sync_low_at_ = 0;
uint64_t next_midi_send_us_ = 0;  // a payload byte may leave only once the last one has (D-114)

void sync_isr() { push_clock(hal::ClockPort::sync, hal::ClockPulse::tick, hal::now_us()); }

}  // namespace

namespace hal::teensy {

void link_init() {
  Serial1.begin(hal::kMidiBaud);
  pinMode(hal::pins::kSyncIn, INPUT_PULLDOWN);  // an empty jack idles low (C-04)
  pinMode(hal::pins::kSyncOut, OUTPUT);
  digitalWriteFast(hal::pins::kSyncOut, LOW);
  attachInterrupt(digitalPinToInterrupt(hal::pins::kSyncIn), sync_isr, RISING);
}

// From hal::poll(), every main-loop pass.
void link_poll() {
  const uint64_t now = hal::now_us();

  // Drain MIDI RX. The bytes waiting arrived one byte time apart, so spread their stamps
  // back from now rather than stamping them all alike, which would feed the follower a
  // zero interval.
  int waiting = Serial1.available();
  for (int i = 0; waiting > 0 && i < waiting; ++i) {
    const uint8_t byte = static_cast<uint8_t>(Serial1.read());
    const uint64_t stamp = now - static_cast<uint64_t>(waiting - 1 - i) * hal::kMidiByteUs;
    hal::ClockPulse pulse;
    if (real_time_pulse(byte, pulse)) {
      noInterrupts();  // the sync interrupt writes the same ring; keep it out for this push
      push_clock(hal::ClockPort::midi, pulse, stamp);
      interrupts();
    } else {
      push_midi(byte);  // the midi ring has one producer, this loop, so it needs no guard
    }
  }

  // Emit a due out pulse. A deadline already past fires now, which is the past-deadline
  // rule hal.h states; the signed difference is what makes "past" mean past.
  if (midi_out_.armed.load(std::memory_order_acquire) && static_cast<int64_t>(now - midi_out_.at_us) >= 0) {
    if (Serial1.availableForWrite() > 0) {
      Serial1.write(status_of(midi_out_.pulse));
      midi_out_.armed.store(false, std::memory_order_release);
    }
  }
  if (sync_out_.armed.load(std::memory_order_acquire) && static_cast<int64_t>(now - sync_out_.at_us) >= 0) {
    digitalWriteFast(hal::pins::kSyncOut, HIGH);
    sync_high_ = true;
    sync_low_at_ = now + kSyncPulseWidthUs;
    sync_out_.armed.store(false, std::memory_order_release);
  }
  if (sync_high_ && static_cast<int64_t>(now - sync_low_at_) >= 0) {
    digitalWriteFast(hal::pins::kSyncOut, LOW);
    sync_high_ = false;
  }
}

}  // namespace hal::teensy

namespace hal {

int read_clock_in(ClockIn* out, int capacity) {
  int count = 0;
  noInterrupts();
  while (count < capacity && clock_head_ != clock_tail_) {
    out[count++] = clock_ring_[clock_head_];
    clock_head_ = (clock_head_ + 1) % kRing;
  }
  interrupts();
  return count;
}

bool send_clock_out(ClockPort port, ClockPulse pulse, uint64_t at_us) {
  Pending& pending = port == ClockPort::midi ? midi_out_ : sync_out_;
  if (pending.armed.load(std::memory_order_acquire)) return false;  // still holding one; offered again next tick
  pending.pulse = pulse;
  pending.at_us = at_us;
  pending.armed.store(true, std::memory_order_release);  // publish after the fields are written
  return true;
}

int midi_read(uint8_t* out, int capacity) {
  int count = 0;
  while (count < capacity && midi_head_ != midi_tail_) {
    out[count++] = midi_ring_[midi_head_];
    midi_head_ = (midi_head_ + 1) % (kMidiInputCapacity + 1);
  }
  return count;
}

// One byte at a time, paced so a clock byte never waits behind more than one payload
// byte (D-114). Without the pace, the caller's pump would fill the UART's tens-of-bytes
// TX buffer in one pass and a clock pulse would queue milliseconds deep behind it; with
// it, at most one payload byte is in flight, so a pulse armed by send_clock_out leaves
// within one byte time. availableForWrite gates it too, so the write never blocks — a
// spin would hang with interrupts off.
int midi_send(const uint8_t* bytes, int count) {
  const uint64_t now = hal::now_us();
  if (count <= 0 || now < next_midi_send_us_ || Serial1.availableForWrite() <= 0) return 0;
  Serial1.write(bytes[0]);
  next_midi_send_us_ = now + hal::kMidiByteUs;  // hold the wire for the byte to leave
  return 1;
}

bool midi_port_open() { return true; }

}  // namespace hal
