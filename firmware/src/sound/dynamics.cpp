#include "sound/dynamics.h"

#include <algorithm>
#include <cmath>

#include "sound/dsp.h"

namespace sound {

namespace {

constexpr float kSilentEnvelope = 1e-9f;  // −180 dB: keeps the log finite

}  // namespace

Compressor::Compressor()
    : envelope_(0.0f),
      attack_coef_(one_pole_coef(kAttackSeconds, static_cast<float>(kSampleRate))),
      release_coef_(one_pole_coef(kReleaseSeconds, static_cast<float>(kSampleRate))),
      reduction_db_(0.0f) {}

void Compressor::process(float* left, float* right, int count) {
  const float slope = 1.0f - 1.0f / kRatio;
  const float half_knee = kKneeDb / 2.0f;
  for (int i = 0; i < count; ++i) {
    const float peak = std::max(std::fabs(left[i]), std::fabs(right[i]));
    envelope_ += (peak - envelope_) * (peak > envelope_ ? attack_coef_ : release_coef_);
    const float over = db_of_gain(std::max(envelope_, kSilentEnvelope)) - kThresholdDb;
    float reduction;
    if (over <= -half_knee) {
      reduction = 0.0f;
    } else if (over >= half_knee) {
      reduction = over * slope;
    } else {
      const float into_knee = over + half_knee;
      reduction = slope * into_knee * into_knee / (2.0f * kKneeDb);
    }
    reduction_db_ = reduction;
    const float gain = gain_of_db(kMakeupDb - reduction);
    left[i] *= gain;
    right[i] *= gain;
  }
  envelope_ = flushed(envelope_);
}

float SoftClipper::shape(float x) {
  const float magnitude = std::fabs(x);
  if (magnitude <= kKnee) return x;
  const float headroom = 1.0f - kKnee;
  const float shaped = kKnee + headroom * std::tanh((magnitude - kKnee) / headroom);
  return x < 0.0f ? -shaped : shaped;
}

void SoftClipper::process(float* left, float* right, int count) {
  for (int i = 0; i < count; ++i) {
    left[i] = shape(left[i]);
    right[i] = shape(right[i]);
  }
}

Limiter::Limiter()
    : ceiling_(gain_of_db(kCeilingDb)),
      release_coef_(one_pole_coef(kReleaseSeconds, static_cast<float>(kSampleRate))),
      delayed_left_{},
      delayed_right_{},
      required_{},
      minima_{},
      minima_sum_(static_cast<float>(kWindow)),
      gain_(1.0f),
      delay_index_(0),
      index_(0) {
  for (int i = 0; i < kWindow; ++i) {
    required_[i] = 1.0f;
    minima_[i] = 1.0f;
  }
}

void Limiter::process(float* left, float* right, int count) {
  for (int i = 0; i < count; ++i) {
    const float in_left = left[i];
    const float in_right = right[i];
    const float peak = std::max(std::fabs(in_left), std::fabs(in_right));
    const float required = peak > ceiling_ ? ceiling_ / peak : 1.0f;

    // The oldest entry in each ring is the one this sample replaces: the input from
    // kLookaheadFrames ago comes out now.
    const float out_left = delayed_left_[delay_index_];
    const float out_right = delayed_right_[delay_index_];
    delayed_left_[delay_index_] = in_left;
    delayed_right_[delay_index_] = in_right;
    if (++delay_index_ >= kLookaheadFrames) delay_index_ = 0;
    required_[index_] = required;

    float minimum = 1.0f;
    for (int k = 0; k < kWindow; ++k) minimum = std::min(minimum, required_[k]);
    minima_sum_ += minimum - minima_[index_];
    minima_[index_] = minimum;
    const float average = minima_sum_ / static_cast<float>(kWindow);

    gain_ += (1.0f - gain_) * release_coef_;
    if (average < gain_) gain_ = average;

    left[i] = out_left * gain_;
    right[i] = out_right * gain_;
    if (++index_ >= kWindow) index_ = 0;
  }
  // The running sum drifts by rounding over millions of samples; the rebuild is cheap.
  minima_sum_ = 0.0f;
  for (int k = 0; k < kWindow; ++k) minima_sum_ += minima_[k];
}

}  // namespace sound
