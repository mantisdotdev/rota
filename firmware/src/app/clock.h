#pragma once

#include <cstdint>

#include "app/audio_path.h"
#include "hal/hal.h"

// Where the beat's length comes from, and where the beat goes out (PRD §6.1, §7.6,
// §11). The scheduler asks how long every beat is and lays that beat's hits out over
// the answer; the same grid is what the MIDI clock and the sync jack carry, which is
// why one object owns both directions instead of two computing it twice.
//
// The answer is the playing section's own tempo — unless a wire is speaking, and then
// it is the tempo measured off that wire (D-117). Following changes only the beat's
// length and phase; engine::State::bpm is never written by the wire, so the share
// code, the undo stack and the card are untouched by a cable in the socket (D-112).
//
// Frames, never seconds: the audio callback's frame counter is the master clock
// (D-084). Nothing in engine/ sees this; a time there is a fraction of one cycle.
namespace app {

// MIDI clock is 24 PPQN and a Rota beat is a quarter note (§6.1). The sync jack
// carries the Pocket Operator's 2 PPQN, one pulse every eighth of a cycle.
constexpr int kMidiPulsesPerBeat = 24;
constexpr int kSyncPulsesPerBeat = 2;

// A cycle is four beats (§6.1), so 96 MIDI pulses — the only cycle reference the wire
// carries. The follower counts from the leader's Start modulo 96 to lock cycles and
// not just beats (D-112, D-120). Kept here so clock/ needs no scheduler.h.
constexpr int kBeatsPerCycleClock = 4;
constexpr int kMidiPulsesPerCycle = kMidiPulsesPerBeat * kBeatsPerCycleClock;  // 96

// The followed beat length is clamped to the §6.3 range in frames so a garbage-fast
// or garbage-slow wire can never drive roll_step = beat_frames/kRollsPerBeat to 0 and
// spin push_window with interrupts off. beat_frames(180) = 16000, beat_frames(60) =
// 48000. Within one bound of these ends the phase pull below becomes one-sided, which
// real leaders inside 60–180 never reach (D-117).
constexpr int kMinBeatFrames = 16000;
constexpr int kMaxBeatFrames = 48000;

// Tracking nudges a beat's length by at most nominal/kPhasePullDenom (~0.8%, ~3.9 ms
// at 120 bpm): inaudible per beat, and enough to close jitter and drift because
// acquisition already put the first beat on the leader's downbeat (D-117).
constexpr int kPhasePullDenom = 128;

// The per-tick interval is an exponential moving average; MIDI carries twelve times
// the pulses sync does, so it smooths harder. shift 3 is alpha 1/8, shift 2 alpha 1/4.
constexpr int kEmaShiftMidi = 3;
constexpr int kEmaShiftSync = 2;

// Sync-only debounce, as a fraction of that port's own running interval: an absolute
// threshold near 12.5 ms would swallow a real 13.9 ms MIDI pulse at 180 bpm, so the
// rule cannot be shared and MIDI is never debounced (D-118).
constexpr int kSyncBounceDivisor = 4;

// An interval wider than this many mean ticks is a discontinuity — a restart or a
// cable event — not a gap to interpolate: the port reacquires (D-118). A tempo halved
// in one step looks like one interval of two ticks, which the estimator interpolates
// rather than reacquires; that is an accepted limit, the loop stays audible and its
// playhead monotone through it (D-118).
constexpr int kMaxTicksPerInterval = 4;

// Silence this long on the followed port ends the follow and adopts the last measured
// tempo (D-119): the one path that announces loss. A MIDI Stop ends it sooner.
constexpr int64_t kQuietTimeoutUs = 1500000;  // 1.5 s

// An anchor older than this means the audio callback itself stalled, not that the wire
// went quiet: the follower freezes the beat at its last length and announces nothing
// (D-119). Well above one host audio burst (512 frames = 10.7 ms) and far below
// kQuietTimeoutUs, so the two loss outcomes never collide.
constexpr int64_t kAnchorStaleUs = 50000;  // 50 ms

// Enough to empty both 64-deep rings (hal::kClockInCapacity) in one main-loop pass;
// follow() bounds its own input, as everything crossing the boundary must.
constexpr int kClockInDrain = 2 * hal::kClockInCapacity;  // 128

// The wire the length and phase come from now; none = free-run on the section's bpm.
// Deliberately not aligned to hal::ClockPort's values: following a port and driving it
// are two different questions (D-113).
enum class Source : uint8_t { none, midi, sync };

// One wire's tempo and phase, rebuilt from the pulses it delivered. Written only in
// the main loop under hal::lock() (where read_clock_in is drained, D-114); read in the
// timer's begin_beat / cycle_boundary. per_tick_us is a rate from time_us deltas
// alone — the anchor never touches the tempo, only the phase projection uses it — so
// the host's bursty audio callback cannot jitter the followed tempo (D-118).
struct PortFollow {
  int64_t last_us;      // arrival of the last accepted tick; -1 = none since reset
  int64_t per_tick_us;  // EMA of one tick's interval; 0 = not yet established
  int64_t beat_us;      // arrival of the most recent beat boundary (index % ppb == 0)
  int64_t cycle_us;     // the most recent acquire reference: a MIDI cycle downbeat after
                        // a Start, else the last beat (D-120)
  int index;            // ticks since Start (MIDI) or the first tick (sync), mod 96
  int locked_ticks;     // accepted ticks since the last (re)acquire, saturating (hysteresis)
  bool usable;          // this pass's ring did not saturate (D-121)
  bool saw_start;       // MIDI: a Start has fixed the cycle phase; false = beat-lock only
};

class Clock {
 public:
  Clock();

  // How many frames long a beat at `bpm` is. §6.3 keeps bpm in 60–180, so the answer
  // is 16000 to 48000 frames at 48 kHz.
  int beat_frames(int bpm) const;

  // Which out ports the player has turned on (§9.4). Set from the main loop under the
  // lock, every pass. The rows gate the arming and never the counting, so a row
  // switched back on lands the next pulse in phase instead of firing a burst.
  void set_ports(bool midi_out, bool sync_out);

  // Play and stop, from the scheduler's transport. Start goes out with the first pulse
  // of the first beat, unless MIDI is the source, when nothing is sent on it (D-113);
  // Stop leaves at once. Continue is never sent: a Rota stop always rewinds.
  void start_transport();
  void stop_transport();

  // Drain the pulses read_clock_in handed the main loop, fold each into its port's
  // estimate, and re-run arbitration. From the main loop under hal::lock(), beside
  // set_ports (D-114). `pulses` is oldest-first with the ports interleaved; `count` is
  // bounded by kClockInDrain. `now_us` times the quiet timeout, never the anchor. It
  // also latches the audio anchor, since it runs every main-loop pass whether or not
  // the transport is playing — so a clock arriving at a stopped device is still seen.
  void follow(const hal::ClockIn* pulses, int count, uint64_t now_us, AudioPath& audio);

  // The beat starting at frame `at`, under a section playing at `bpm`. When following
  // it returns the measured length pulled toward the leader's boundary (bounded,
  // clamped); frozen at the last length while the anchor is stale; the section's own
  // beat_frames(bpm) when free-running. The pulse spacing it lays the out ports over is
  // (i * beat_frames) / pulses_per_beat, 16000/24 apart at 180 bpm.
  int begin_beat(int64_t at, int bpm);

  // Arms every out pulse of this beat due before `horizon`, each timed to when its
  // frame is heard; the followed port is never driven (D-113). From the timer callback
  // under hal::lock(); reads the anchor follow() latched and never touches the wire.
  void emit_until(int64_t horizon, AudioPath& audio);

  // A wire is being followed right now: a source is chosen and the latched anchor is
  // fresh enough to place its phase. The scheduler asks at play to choose the count-in,
  // the controller to show `ext`.
  bool following() const;

  // D-112 cycle lock. True once the follower can commit the leader's next acquire
  // boundary at a frame >= `not_before`, which it writes to `at`: a MIDI cycle downbeat
  // when a Start has been seen, else the next beat (D-120). False keeps the count-in
  // waiting. From the timer (Scheduler::tick) under the lock.
  bool cycle_boundary(int64_t not_before, int64_t& at) const;

  // The tempo to write into the sections a knob would reach when a follow just ended
  // (whole bpm, clamped 60–180); 0 when nothing was lost. Polled once by the
  // controller, which applies it through the tap-tempo path (D-122). The wire never
  // writes bpm itself.
  int take_lost_bpm();

  // The followed tempo for the ring's top-left corner (whole bpm); 0 when not following.
  int measured_bpm() const;

 private:
  uint64_t deadline_of(int64_t frame) const;
  int64_t pulse_frame(int port, int index) const;
  void arm(int port, hal::ClockPort wire, int64_t horizon);
  // The audio frame whose sound is heard at `time_us`: the exact inverse of
  // deadline_of, the same audio_buffer_frames() term subtracted. Every subtraction is
  // int64, because an arriving pulse is almost always stamped before the newest anchor.
  int64_t frame_of(uint64_t time_us) const;
  bool anchor_fresh(uint64_t now_us) const;
  int followed_length(int64_t at) const;

  AudioAnchor anchor_;  // the newest pair the audio side has published
  bool anchored_;       // false until it has published one: nothing can be timed before that
  bool running_;
  bool start_pending_;  // the Start byte, waiting for the first pulse to give it a time
  bool enabled_[hal::kClockPortCount];
  int64_t beat_start_;
  int beat_frames_;
  int next_pulse_[hal::kClockPortCount];  // the first pulse of this beat not yet armed

  PortFollow ports_[hal::kClockPortCount];
  Source source_;              // the wire being followed, chosen by arbitration in follow()
  bool stopped_by_byte_;       // a MIDI Stop ended the follow at once; cleared on the next Start
  int64_t follow_beat_us_;     // published: microseconds per beat of the source
  int64_t follow_boundary_us_; // published: the source's most recent beat boundary
  int64_t follow_cycle_us_;    // published: the source's most recent acquire reference
  int last_follow_frames_;     // the last length begin_beat returned while following; 0 = none.
                               // Held across a stale anchor so a freeze keeps the tempo.
  int lost_bpm_;               // one-shot: the tempo to adopt on loss; 0 = none
};

}  // namespace app
