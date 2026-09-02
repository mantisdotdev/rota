#include "sound/delay.h"

#include <algorithm>
#include <cmath>

#include "sound/dsp.h"

namespace sound {

Delay::Delay()
    : buffer_{},
      write_(0),
      length_(kMaxDelayFrames),
      previous_length_(kMaxDelayFrames),
      crossfade_left_(0),
      damp_state_(0.0f),
      damp_coef_(one_pole_coef(1.0f / (kTwoPi * kDampingHz), static_cast<float>(kSampleRate))) {
  set_tempo(100.0f);
  previous_length_ = length_;
  crossfade_left_ = 0;
}

void Delay::set_tempo(float bpm) {
  const float seconds = static_cast<float>(kDottedEighthSecondsTimesBpm) / bpm;
  const int frames = static_cast<int>(std::lround(seconds * static_cast<float>(kSampleRate)));
  const int length = std::min(std::max(frames, 1), kMaxDelayFrames);
  if (length == length_) return;
  previous_length_ = length_;
  length_ = length;
  crossfade_left_ = kCrossfadeFrames;
}

float Delay::read(int frames_ago) const {
  int index = write_ - frames_ago;
  if (index < 0) index += kMaxDelayFrames;
  return buffer_[index];
}

void Delay::process(const float* in, float* left, float* right, int count) {
  for (int i = 0; i < count; ++i) {
    float delayed = read(length_);
    if (crossfade_left_ > 0) {
      const float mix = static_cast<float>(crossfade_left_) / static_cast<float>(kCrossfadeFrames);
      delayed = delayed + (read(previous_length_) - delayed) * mix;
      --crossfade_left_;
    }
    damp_state_ += (delayed - damp_state_) * damp_coef_;
    buffer_[write_] = in[i] + damp_state_ * kFeedback + kAntiDenormal;
    if (++write_ >= kMaxDelayFrames) write_ = 0;
    left[i] += delayed;
    right[i] += delayed;
  }
}

}  // namespace sound
