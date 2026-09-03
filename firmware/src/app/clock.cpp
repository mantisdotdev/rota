#include "app/clock.h"

#include "sound/limits.h"

namespace app {

namespace {

constexpr int kSecondsPerMinute = 60;
constexpr int64_t kMicrosecondsPerSecond = 1000000;

int pulses_per_beat(int port) {
  return port == static_cast<int>(hal::ClockPort::midi) ? kMidiPulsesPerBeat : kSyncPulsesPerBeat;
}

int clamp_bpm(int64_t bpm) {
  if (bpm < sound::kMinBpm) return sound::kMinBpm;
  if (bpm > sound::kMaxBpm) return sound::kMaxBpm;
  return static_cast<int>(bpm);
}

int64_t clamp_length(int64_t frames) {
  if (frames < kMinBeatFrames) return kMinBeatFrames;
  if (frames > kMaxBeatFrames) return kMaxBeatFrames;
  return frames;
}

PortFollow fresh_port() { return PortFollow{-1, 0, 0, 0, 0, 0, true, false}; }

}  // namespace

Clock::Clock()
    : anchor_{0, 0},
      anchored_(false),
      running_(false),
      start_pending_(false),
      enabled_{true, true},
      beat_start_(0),
      beat_frames_(0),
      next_pulse_{0, 0},
      ports_{fresh_port(), fresh_port()},
      source_(Source::none),
      stopped_by_byte_(false),
      follow_beat_us_(0),
      follow_boundary_us_(0),
      follow_cycle_us_(0),
      last_follow_frames_(0),
      lost_bpm_(0) {}

// One beat is 60 / bpm seconds, rounded to a frame; a cycle is four of them (§6.1).
int Clock::beat_frames(int bpm) const { return (sound::kSampleRate * kSecondsPerMinute + bpm / 2) / bpm; }

void Clock::set_ports(bool midi_out, bool sync_out) {
  enabled_[static_cast<int>(hal::ClockPort::midi)] = midi_out;
  enabled_[static_cast<int>(hal::ClockPort::sync)] = sync_out;
}

void Clock::start_transport() {
  running_ = true;
  // A followed MIDI port is never driven, so no Start goes back to the leader (D-113).
  start_pending_ = enabled_[static_cast<int>(hal::ClockPort::midi)] && source_ != Source::midi;
}

void Clock::stop_transport() {
  running_ = false;
  start_pending_ = false;
  if (enabled_[static_cast<int>(hal::ClockPort::midi)] && source_ != Source::midi && hal::midi_port_open()) {
    hal::send_clock_out(hal::ClockPort::midi, hal::ClockPulse::stop, hal::now_us());
  }
}

int64_t Clock::frame_of(uint64_t time_us) const {
  const int64_t since = static_cast<int64_t>(time_us) - static_cast<int64_t>(anchor_.time_us);  // signed
  const int64_t frames = since * static_cast<int64_t>(sound::kSampleRate) / kMicrosecondsPerSecond;
  return anchor_.frames - static_cast<int64_t>(hal::audio_buffer_frames()) + frames;
}

bool Clock::anchor_fresh(uint64_t now_us) const {
  return anchored_ && static_cast<int64_t>(now_us) - static_cast<int64_t>(anchor_.time_us) <= kAnchorStaleUs;
}

void Clock::follow(const hal::ClockIn* pulses, int count, uint64_t now_us, AudioPath& audio) {
  AudioAnchor fresh;
  if (audio.anchor.take(fresh)) {  // the sole consumer of the mailbox; emit_until reads the latch
    anchor_ = fresh;
    anchored_ = true;
  }
  // A port whose own ring saturated this pass is chattering or shorted; it is excluded
  // until it settles, and the other port keeps being followed (D-121).
  int seen[hal::kClockPortCount] = {0, 0};
  for (int i = 0; i < count; ++i) seen[static_cast<int>(pulses[i].port)] += 1;
  for (int p = 0; p < hal::kClockPortCount; ++p) ports_[p].usable = seen[p] < hal::kClockInCapacity;

  for (int i = 0; i < count; ++i) {
    const hal::ClockIn& e = pulses[i];
    const int p = static_cast<int>(e.port);
    PortFollow& f = ports_[p];
    const bool is_midi = e.port == hal::ClockPort::midi;

    if (e.pulse == hal::ClockPulse::start) {  // sync never carries transport
      if (is_midi) {
        f.index = 0;
        f.saw_start = true;
        f.per_tick_us = 0;
        f.last_us = -1;
        f.beat_us = static_cast<int64_t>(e.time_us);
        f.cycle_us = f.beat_us;
        f.locked_ticks = 1;
        stopped_by_byte_ = false;
      }
      continue;
    }
    if (e.pulse == hal::ClockPulse::stop) {
      if (is_midi) stopped_by_byte_ = true;
      continue;
    }
    if (e.pulse == hal::ClockPulse::resume) continue;  // Rota never sends it; ignored

    const int ppb = pulses_per_beat(p);
    const int64_t t = static_cast<int64_t>(e.time_us);

    if (!is_midi && f.per_tick_us > 0 && f.last_us >= 0 && (t - f.last_us) < f.per_tick_us / kSyncBounceDivisor) {
      continue;  // a sync contact bouncing; not a pulse (D-118)
    }
    if (f.last_us < 0) {  // bootstrap
      f.last_us = t;
      f.beat_us = t;
      if (!(is_midi && f.saw_start)) f.cycle_us = t;
      f.locked_ticks = 1;
      continue;
    }
    const int64_t raw = t - f.last_us;  // per-port stamps are monotone, but the device may stamp two bytes alike
    if (raw <= 0) continue;  // same-instant duplicates carry no interval; folding a 0 would halve the EMA
    int ticks;
    if (f.per_tick_us == 0) {
      ticks = 1;
      f.per_tick_us = raw;  // seed
    } else {
      ticks = static_cast<int>((raw + f.per_tick_us / 2) / f.per_tick_us);  // >= 1.5x mean -> 2 (D-118)
      if (ticks < 1) ticks = 1;
      if (ticks > kMaxTicksPerInterval) {  // a discontinuity: this port reacquires
        f.per_tick_us = 0;
        f.last_us = t;
        f.beat_us = t;
        if (!(is_midi && f.saw_start)) f.cycle_us = t;
        f.locked_ticks = 1;
        continue;
      }
      const int shift = is_midi ? kEmaShiftMidi : kEmaShiftSync;
      const int64_t per = raw / ticks;
      const int64_t delta = per - f.per_tick_us;
      const int64_t half = static_cast<int64_t>(1) << (shift - 1);
      f.per_tick_us += (delta + (delta >= 0 ? half : -half)) >> shift;  // EMA, rounded to nearest so bpm has no bias
    }
    f.index = (f.index + ticks) % kMidiPulsesPerCycle;
    if (f.index % ppb == 0) {  // a beat boundary
      f.beat_us = t;
      if (is_midi && f.saw_start) {
        if (f.index == 0) f.cycle_us = t;  // the true cycle downbeat
      } else {
        f.cycle_us = t;  // sync or no Start: the beat is the reference (D-120)
      }
    }
    f.last_us = t;
    f.locked_ticks += ticks;
    if (f.locked_ticks > 2 * kMidiPulsesPerCycle) f.locked_ticks = 2 * kMidiPulsesPerCycle;
  }

  // Arbitration, on now_us and never the anchor. MIDI wins whenever MIDI is alive; a
  // source is followed only after a whole beat of ticks, so one stray pulse cannot
  // grab the phase (D-118 hysteresis).
  auto alive = [&](int p) {
    return ports_[p].usable && ports_[p].last_us >= 0 && ports_[p].per_tick_us > 0 &&
           static_cast<int64_t>(now_us) - ports_[p].last_us <= kQuietTimeoutUs;
  };
  auto ready = [&](int p) { return alive(p) && ports_[p].locked_ticks >= pulses_per_beat(p); };
  const int midi = static_cast<int>(hal::ClockPort::midi);
  const int sync = static_cast<int>(hal::ClockPort::sync);
  const Source next = stopped_by_byte_ ? Source::none : ready(midi) ? Source::midi : ready(sync) ? Source::sync
                                                                                                 : Source::none;

  if (source_ != Source::none && next == Source::none) {  // the one loss-announce edge (D-119)
    const int p = source_ == Source::midi ? midi : sync;
    const int64_t beat_us = ports_[p].per_tick_us * pulses_per_beat(p);
    // The reacquire path can zero per_tick_us in the very pass that loses the source,
    // so fall back to the last tempo that was actually published rather than divide by
    // zero; if there is none, keep the section's bpm (lost_bpm_ stays 0).
    if (beat_us > 0) {
      lost_bpm_ = clamp_bpm((kSecondsPerMinute * kMicrosecondsPerSecond + beat_us / 2) / beat_us);
    } else if (follow_beat_us_ > 0) {
      lost_bpm_ = clamp_bpm((kSecondsPerMinute * kMicrosecondsPerSecond + follow_beat_us_ / 2) / follow_beat_us_);
    }
    last_follow_frames_ = 0;
  }
  source_ = next;
  if (source_ != Source::none) {
    const int p = source_ == Source::midi ? midi : sync;
    follow_beat_us_ = ports_[p].per_tick_us * pulses_per_beat(p);
    follow_boundary_us_ = ports_[p].beat_us;
    follow_cycle_us_ = ports_[p].cycle_us;
  }
}

int Clock::followed_length(int64_t at) const {
  const int64_t nominal =
      clamp_length((follow_beat_us_ * static_cast<int64_t>(sound::kSampleRate) + kMicrosecondsPerSecond / 2) /
                   kMicrosecondsPerSecond);
  const int64_t b = frame_of(static_cast<uint64_t>(follow_boundary_us_));  // the leader's beat in frame-space
  int64_t k = (at + nominal - b + nominal / 2) / nominal;                  // whole beats to near (at + nominal)
  if (k < 1) k = 1;
  int64_t phase_err = (b + k * nominal) - (at + nominal);
  const int64_t bound = nominal / kPhasePullDenom;
  if (phase_err > bound) phase_err = bound;
  if (phase_err < -bound) phase_err = -bound;
  return static_cast<int>(clamp_length(nominal + phase_err));
}

int Clock::begin_beat(int64_t at, int bpm) {
  beat_start_ = at;
  for (int port = 0; port < hal::kClockPortCount; ++port) next_pulse_[port] = 0;
  if (source_ != Source::none && anchor_fresh(hal::now_us())) {
    beat_frames_ = followed_length(at);
    last_follow_frames_ = beat_frames_;
  } else if (source_ != Source::none && last_follow_frames_ > 0) {
    beat_frames_ = last_follow_frames_;  // freeze at the last followed length: the anchor is stale (D-119)
  } else {
    beat_frames_ = beat_frames(bpm);  // free-run; a quiet loss already wrote bpm, or the wire was never followed
    last_follow_frames_ = 0;
  }
  return beat_frames_;
}

bool Clock::cycle_boundary(int64_t not_before, int64_t& at) const {
  if (source_ == Source::none) return false;
  if (!anchor_fresh(hal::now_us())) return false;  // no fresh anchor: stay in the count-in
  const int p = source_ == Source::midi ? static_cast<int>(hal::ClockPort::midi) : static_cast<int>(hal::ClockPort::sync);
  const PortFollow& f = ports_[p];
  if (f.locked_ticks < pulses_per_beat(p)) return false;  // not confident yet
  const int64_t nominal = clamp_length(f.per_tick_us * pulses_per_beat(p) * static_cast<int64_t>(sound::kSampleRate) /
                                       kMicrosecondsPerSecond);
  const bool bar = source_ == Source::midi && f.saw_start;  // MIDI with a Start locks the cycle; else the beat (D-120)
  const int64_t step = bar ? kBeatsPerCycleClock * nominal : nominal;
  const int64_t base = frame_of(static_cast<uint64_t>(f.cycle_us));
  int64_t n = (not_before - base + step - 1) / step;  // the first boundary at or after not_before
  if (n < 0) n = 0;
  at = base + n * step;
  return true;
}

int Clock::take_lost_bpm() {
  const int v = lost_bpm_;
  lost_bpm_ = 0;
  return v;
}

int Clock::measured_bpm() const {
  if (source_ == Source::none || follow_beat_us_ <= 0) return 0;
  return clamp_bpm((kSecondsPerMinute * kMicrosecondsPerSecond + follow_beat_us_ / 2) / follow_beat_us_);
}

bool Clock::following() const { return source_ != Source::none && anchor_fresh(hal::now_us()); }

void Clock::emit_until(int64_t horizon, AudioPath& audio) {
  (void)audio;  // the anchor is latched by follow(), which runs every pass; this only reads it
  if (!running_ || !anchored_ || beat_frames_ <= 0) return;
  if (!hal::midi_port_open()) return;  // no wire in this build: count nothing, arm nothing
  if (source_ != Source::midi) arm(static_cast<int>(hal::ClockPort::midi), hal::ClockPort::midi, horizon);
  if (source_ != Source::sync) arm(static_cast<int>(hal::ClockPort::sync), hal::ClockPort::sync, horizon);
}

uint64_t Clock::deadline_of(int64_t frame) const {
  const int64_t ahead = frame - anchor_.frames + hal::audio_buffer_frames();  // signed: a frame behind is behind
  const int64_t us = ahead * kMicrosecondsPerSecond / sound::kSampleRate;
  return static_cast<uint64_t>(static_cast<int64_t>(anchor_.time_us) + us);
}

// Pulse `index` of this beat, spaced over the beat's own length so a tempo change lands
// on the beat as everything else does.
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
    // A Start byte and the tick it belongs to cannot leave at once — a byte is 320 µs of
    // wire — so Start goes one byte early and the tick lands on the beat.
    if (start_pending_ && wire == hal::ClockPort::midi) {
      const uint64_t at_us = deadline_of(frame);
      if (!hal::send_clock_out(wire, hal::ClockPulse::start, at_us - hal::kMidiByteUs)) return;
      start_pending_ = false;
    }
    if (!hal::send_clock_out(wire, hal::ClockPulse::tick, deadline_of(frame))) return;
    next_pulse_[port] += 1;
  }
}

}  // namespace app
