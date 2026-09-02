#include "sound/delay.h"

#include <algorithm>
#include <cmath>

#include "sound/dsp.h"

namespace sound {

namespace {

constexpr float kDefaultBpm = 100.0f;

}  // namespace

Delay::Delay()
    : buffer_{},
      write_(0),
      length_(frames_of(kDefaultBpm)),
      previous_length_(length_),
      pending_length_(length_),
      crossfade_left_(0),
      damp_state_(0.0f),
      damp_coef_(one_pole_coef(1.0f / (kTwoPi * kDampingHz), static_cast<float>(kSampleRate))) {}

int Delay::frames_of(float bpm) {
  const float clamped = std::min(std::max(bpm, static_cast<float>(kMinBpm)), static_cast<float>(kMaxBpm));
  const float seconds = static_cast<float>(kDottedEighthSecondsTimesBpm) / clamped;
  const int frames = static_cast<int>(std::lround(seconds * static_cast<float>(kSampleRate)));
  return std::min(std::max(frames, 1), kMaxDelayFrames);
}

void Delay::set_tempo(float bpm) {
  if (!std::isfinite(bpm)) return;  // a NaN would slip through the clamp into lround
  pending_length_ = frames_of(bpm);
  if (crossfade_left_ == 0 && pending_length_ != length_) begin_crossfade(pending_length_);
}

void Delay::begin_crossfade(int length) {
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
      if (--crossfade_left_ == 0 && pending_length_ != length_) begin_crossfade(pending_length_);
    }
    damp_state_ += (delayed - damp_state_) * damp_coef_;
    buffer_[write_] = in[i] + damp_state_ * kFeedback + kAntiDenormal;
    if (++write_ >= kMaxDelayFrames) write_ = 0;
    left[i] += delayed;
    right[i] += delayed;
  }
}

}  // namespace sound
