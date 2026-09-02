#pragma once

#include "sound/limits.h"

namespace sound {

// Tempo-locked delay (§6.3, §12): one repeat every dotted eighth, dark feedback, mono
// into both channels (D-079). A tempo change crossfades the read point over 10 ms so
// turning the speed knob does not click.
class Delay {
 public:
  Delay();
  void set_tempo(float bpm);
  int length() const { return length_; }
  // Adds the wet signal to left and right.
  void process(const float* in, float* left, float* right, int count);

 private:
  static constexpr float kFeedback = 0.4f;
  static constexpr float kDampingHz = 3000.0f;
  static constexpr int kCrossfadeFrames = kSampleRate / 100;

  float read(int frames_ago) const;

  float buffer_[kMaxDelayFrames];
  int write_;
  int length_;
  int previous_length_;
  int crossfade_left_;
  float damp_state_;
  float damp_coef_;
};

}  // namespace sound
