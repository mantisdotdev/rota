#include "app/clock.h"

#include "sound/limits.h"

namespace app {

namespace {

constexpr int kSecondsPerMinute = 60;
constexpr int64_t kMicrosecondsPerSecond = 1000000;

int pulses_per_beat(int port) {
  return port == static_cast<int>(hal::ClockPort::midi) ? kMidiPulsesPerBeat : kSyncPulsesPerBeat;
}

}  // namespace

Clock::Clock()
    : anchor_{0, 0},
      anchored_(false),
      running_(false),
      start_pending_(false),
      enabled_{true, true},
      beat_start_(0),
      beat_frames_(0),
      next_pulse_{0, 0} {}

// One beat is 60 / bpm seconds, rounded to a frame; a cycle is four of them (§6.1).
int Clock::beat_frames(int bpm) const { return (sound::kSampleRate * kSecondsPerMinute + bpm / 2) / bpm; }

void Clock::set_ports(bool midi_out, bool sync_out) {
  enabled_[static_cast<int>(hal::ClockPort::midi)] = midi_out;
  enabled_[static_cast<int>(hal::ClockPort::sync)] = sync_out;
}

void Clock::start_transport() {
  running_ = true;
  start_pending_ = enabled_[static_cast<int>(hal::ClockPort::midi)];
}

void Clock::stop_transport() {
  running_ = false;
  start_pending_ = false;
  // Now, not on a grid: the sound stops now. The sync jack carries no transport, so
  // there is nothing to tell the Pocket Operator except the absence of pulses.
  if (enabled_[static_cast<int>(hal::ClockPort::midi)] && hal::midi_port_open()) {
    hal::send_clock_out(hal::ClockPort::midi, hal::ClockPulse::stop, hal::now_us());
  }
}

int Clock::begin_beat(int64_t at, int bpm) {
  beat_start_ = at;
  beat_frames_ = beat_frames(bpm);
  for (int port = 0; port < hal::kClockPortCount; ++port) next_pulse_[port] = 0;
  return beat_frames_;
}

void Clock::emit_until(int64_t horizon, AudioPath& audio) {
  AudioAnchor fresh;
  if (audio.anchor.take(fresh)) {  // take() answers false until the next publish, so latch it
    anchor_ = fresh;
    anchored_ = true;
  }
  if (!running_ || !anchored_ || beat_frames_ <= 0) return;
  if (!hal::midi_port_open()) return;  // no wire in this build: count nothing, arm nothing
  arm(static_cast<int>(hal::ClockPort::midi), hal::ClockPort::midi, horizon);
  arm(static_cast<int>(hal::ClockPort::sync), hal::ClockPort::sync, horizon);
}

uint64_t Clock::deadline_of(int64_t frame) const {
  const int64_t ahead = frame - anchor_.frames + hal::audio_buffer_frames();  // signed: a frame behind is behind
  const int64_t us = ahead * kMicrosecondsPerSecond / sound::kSampleRate;
  return static_cast<uint64_t>(static_cast<int64_t>(anchor_.time_us) + us);
}

// Pulse `index` of this beat, spaced over the beat's own length so a tempo change
// lands on the beat as everything else does.
int64_t Clock::pulse_frame(int port, int index) const {
  return beat_start_ + static_cast<int64_t>(index) * beat_frames_ / pulses_per_beat(port);
}

void Clock::arm(int port, hal::ClockPort wire, int64_t horizon) {
  const int per_beat = pulses_per_beat(port);
  while (next_pulse_[port] < per_beat) {
    const int64_t frame = pulse_frame(port, next_pulse_[port]);
    if (frame >= horizon) return;
    if (!enabled_[port]) {  // the row is off: the count still moves, so switching it on lands in phase
      next_pulse_[port] += 1;
      continue;
    }
    // A Start byte and the tick it belongs to cannot leave at once — a byte is 320 µs
    // of wire — so Start goes one byte early and the tick lands on the beat.
    if (start_pending_ && wire == hal::ClockPort::midi) {
      const uint64_t at_us = deadline_of(frame);
      if (!hal::send_clock_out(wire, hal::ClockPulse::start, at_us - hal::kMidiByteUs)) return;
      start_pending_ = false;
    }
    // A refused pulse is offered again on the next tick rather than dropped: the
    // ports carry a tempo and no downbeat, so a lost pulse is a phase error nothing
    // afterwards can correct, while a late one costs at most one timer period.
    if (!hal::send_clock_out(wire, hal::ClockPulse::tick, deadline_of(frame))) return;
    next_pulse_[port] += 1;
  }
}

}  // namespace app
