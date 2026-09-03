#pragma once

// Where the beat's length comes from (PRD §6.1). The scheduler asks at every beat
// boundary and lays that beat's hits out over the answer. Today the answer is always
// the playing section's own tempo; §11's clock in, sync in and jam link will answer
// it from a wire instead. It is an object and not the line it replaces because that
// answer is about to hold state the scheduler has no business owning, and because the
// scheduler will not stay its only caller.
//
// Frames, never seconds: the audio callback's frame counter is the master clock
// (D-084). Nothing in engine/ sees this; a time there is a fraction of one cycle.
namespace app {

class Clock {
 public:
  // How many frames long a beat at `bpm` is. §6.3 keeps bpm in 60–180, so the answer
  // is 16000 to 48000 frames at 48 kHz.
  int beat_frames(int bpm) const;
};

}  // namespace app
