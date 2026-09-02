#pragma once

#include "sound/limits.h"

namespace sound {

// Tempo-locked delay (§6.3, §12): one repeat every dotted eighth, dark feedback, mono
// into both channels (D-079). A tempo change crossfades the read point over 10 ms;
// a change that arrives during a crossfade waits for it to finish, so a knob sweep
// is a chain of complete fades and never a jump.
class Delay {
 public:
  Delay();
  // bpm outside §6.3's 60–180 is clamped to it, infinities included; a NaN changes nothing.
  void set_tempo(float bpm);
  // The dotted eighth in frames the delay is at, or heading to.
  int length() const { return pending_length_; }
  // Adds the wet signal to left and right.
  void process(const float* in, float* left, float* right, int count);

 private:
  static constexpr float kFeedback = 0.4f;
  static constexpr float kDampingHz = 3000.0f;
  static constexpr int kCrossfadeFrames = kSampleRate / 100;

  static int frames_of(float bpm);
  void begin_crossfade(int length);
  float read(int frames_ago) const;

  float buffer_[kMaxDelayFrames];
  int write_;
  int length_;           // the read point being faded in, or settled on
  int previous_length_;  // the read point being faded out
  int pending_length_;   // the latest tempo, taken up when no fade is running
  int crossfade_left_;
  float damp_state_;
  float damp_coef_;
};

}  // namespace sound
