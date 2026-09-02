#pragma once

#include <cmath>

#include "sound/dsp.h"
#include "sound/limits.h"

namespace sound {

// The filter knob and a track's tone (§6.3, §6.2): 220 Hz at 0, fully open at 1,
// exponential in between (D-077).
constexpr float kFilterClosedHz = 220.0f;
constexpr float kFilterOpenHz = 20000.0f;
constexpr float kButterworthQ = 0.7071f;

inline float cutoff_of_knob(float knob) {
  return kFilterClosedHz * std::pow(kFilterOpenHz / kFilterClosedHz, knob);
}

// Two-pole state-variable low-pass (Simper's trapezoidal form): stable at any cutoff
// below Nyquist, coefficients set once per block, a short multiply-add chain per sample.
class Svf {
 public:
  Svf();
  void set(float cutoff_hz, float q);
  float low_pass(float x) {
    const float v3 = x - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.0f * v1 - ic1_;
    ic2_ = 2.0f * v2 - ic2_;
    return v2;
  }
  void process(float* buffer, int count);  // low-pass in place
  void flush();  // end of block: state below kFlushThreshold becomes exactly 0
  void reset();

 private:
  float a1_;
  float a2_;
  float a3_;
  float ic1_;
  float ic2_;
};

}  // namespace sound
