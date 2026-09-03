#pragma once

#include <cstdint>

#include "app/audio_path.h"
#include "hal/hal.h"

// Where the beat's length comes from, and where the beat goes out (PRD §6.1, §7.6,
// §11). The scheduler asks how long every beat is and lays that beat's hits out over
// the answer; the same grid is what the MIDI clock and the sync jack carry, which is
// why one object owns both directions instead of two computing it twice.
//
// Today the answer is always the playing section's own tempo; §11's clock in and sync
// in will answer it from a wire instead.
//
// Frames, never seconds: the audio callback's frame counter is the master clock
// (D-084). Nothing in engine/ sees this; a time there is a fraction of one cycle.
namespace app {

// MIDI clock is 24 PPQN and a Rota beat is a quarter note (§6.1), so a cycle is 96
// pulses — which is what lets a follower find the leader's cycle and not just its
// beat (D-112). The sync jack carries the Pocket Operator's 2 PPQN, one pulse every
// eighth of a cycle.
constexpr int kMidiPulsesPerBeat = 24;
constexpr int kSyncPulsesPerBeat = 2;

class Clock {
 public:
  Clock();

  // How many frames long a beat at `bpm` is. §6.3 keeps bpm in 60–180, so the answer
  // is 16000 to 48000 frames at 48 kHz.
  int beat_frames(int bpm) const;

  // Which out ports the player has turned on (§9.4). Set from the main loop under
  // the lock, every pass: two stores cannot drift from the settings they mirror. The
  // rows gate the arming and never the counting, so a row switched back on lands the
  // next pulse in phase instead of firing a burst to catch up.
  void set_ports(bool midi_out, bool sync_out);

  // Play and stop, from the scheduler's own transport. Start goes out with the first
  // pulse of the first beat, so a listener's pulse count and ours agree from the same
  // instant; Stop leaves at once, since the hits already handed over stop sounding at
  // once too (T-82). Continue is never sent: a Rota stop always rewinds.
  void start_transport();
  void stop_transport();

  // The beat starting at frame `at`, under a section playing at `bpm`. Returns the
  // beat's length in frames and takes the grid the out ports count over.
  int begin_beat(int64_t at, int bpm);

  // Arms every pulse of this beat due before `horizon`, each with the microsecond its
  // frame will be heard at. From the timer callback, under hal::lock(); it reads the
  // audio side's anchor mailbox and never touches the wire itself.
  void emit_until(int64_t horizon, AudioPath& audio);

 private:
  // The microsecond `frame` reaches the output, which is later than the microsecond it
  // was rendered by whatever the platform holds between the two: our own pulse has to
  // coincide with our own sound, not with the render that produced it.
  uint64_t deadline_of(int64_t frame) const;
  int64_t pulse_frame(int port, int index) const;
  void arm(int port, hal::ClockPort wire, int64_t horizon);

  AudioAnchor anchor_;  // the newest pair the audio side has published
  bool anchored_;       // false until it has published one: nothing can be timed before that
  bool running_;
  bool start_pending_;  // the Start byte, waiting for the first pulse to give it a time
  bool enabled_[hal::kClockPortCount];
  int64_t beat_start_;
  int beat_frames_;
  int next_pulse_[hal::kClockPortCount];  // the first pulse of this beat not yet armed
};

}  // namespace app
