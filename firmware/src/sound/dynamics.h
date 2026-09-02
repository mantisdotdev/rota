#pragma once

#include "sound/limits.h"

namespace sound {

// Glue compressor (§12: about 4:1, 3 ms attack, 150 ms release), stereo-linked on the
// peak, soft knee; threshold, knee and makeup are D-080's.
class Compressor {
 public:
  static constexpr float kThresholdDb = -12.0f;
  static constexpr float kRatio = 4.0f;
  static constexpr float kKneeDb = 6.0f;
  static constexpr float kMakeupDb = 3.0f;
  static constexpr float kAttackSeconds = 0.003f;
  static constexpr float kReleaseSeconds = 0.150f;

  Compressor();
  void process(float* left, float* right, int count);
  // Gain reduction at the last sample, in dB (0 = none).
  float reduction_db() const { return reduction_db_; }

 private:
  float envelope_;
  float attack_coef_;
  float release_coef_;
  float reduction_db_;
};

// Soft clipper (§12): linear to −6 dBFS, then a tanh curve that never exceeds ±1 (D-080).
class SoftClipper {
 public:
  static constexpr float kKnee = 0.5f;
  static float shape(float x);
  void process(float* left, float* right, int count);
};

// Brickwall limiter at −1 dBFS (§12) with 1 ms of lookahead (D-080). The gain applied
// to a sample is an average of window minima that each include that sample's own
// required gain, so the ceiling holds by construction; the release keeps the gain from
// snapping back.
class Limiter {
 public:
  static constexpr float kCeilingDb = -1.0f;
  static constexpr int kLookaheadFrames = kSampleRate / 1000;
  static constexpr float kReleaseSeconds = 0.060f;

  Limiter();
  void process(float* left, float* right, int count);
  float ceiling() const { return ceiling_; }

 private:
  // The audio runs kLookaheadFrames behind the gain rings, which hold one more entry:
  // that is what puts the sample being output inside every window minimum averaged.
  static constexpr int kWindow = kLookaheadFrames + 1;

  float ceiling_;
  float release_coef_;
  float delayed_left_[kLookaheadFrames];
  float delayed_right_[kLookaheadFrames];
  float required_[kWindow];  // gain each of the last kWindow inputs needs
  float minima_[kWindow];    // window minimum at each of the last kWindow samples
  float minima_sum_;
  float gain_;
  int delay_index_;
  int index_;
};

}  // namespace sound
