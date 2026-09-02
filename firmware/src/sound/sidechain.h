#pragma once

#include "engine/kit.h"

namespace sound {

// Kick ducking (§12 sound spec): every kick event drops the melodic bus to −duck_db at
// once, and the gain climbs back linearly in dB over release_ms (D-078). Depth and
// release come from the kit; off in the kit means the gain stays at 1.
class Sidechain {
 public:
  Sidechain();
  void set(const engine::Sidechain& settings);
  void duck();
  // The gain for the next sample; recovers one step.
  float next() {
    const float gain = static_cast<float>(gain_);
    gain_ *= recovery_;
    if (gain_ > 1.0) gain_ = 1.0;
    return gain;
  }
  float gain() const { return static_cast<float>(gain_); }

 private:
  // Doubles: 5760 float multiplies in a row would land 0.0015 dB short of 0 dB.
  bool on_;
  double floor_;
  double recovery_;
  double gain_;
};

}  // namespace sound
