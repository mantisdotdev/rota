#include "sound/filter.h"

#include <algorithm>

namespace sound {

namespace {

constexpr float kLowestCutoffHz = 20.0f;

}  // namespace

Svf::Svf() : a1_(0.0f), a2_(0.0f), a3_(0.0f), ic1_(0.0f), ic2_(0.0f) { set(kFilterOpenHz, kButterworthQ); }

void Svf::set(float cutoff_hz, float q) {
  const float cutoff = std::min(std::max(cutoff_hz, kLowestCutoffHz), kFilterOpenHz);
  const float g = std::tan(kPi * cutoff / static_cast<float>(kSampleRate));
  const float k = 1.0f / q;
  a1_ = 1.0f / (1.0f + g * (g + k));
  a2_ = g * a1_;
  a3_ = g * a2_;
}

void Svf::process(float* buffer, int count) {
  for (int i = 0; i < count; ++i) buffer[i] = low_pass(buffer[i]);
}

void Svf::flush() {
  ic1_ = flushed(ic1_);
  ic2_ = flushed(ic2_);
}

void Svf::reset() {
  ic1_ = 0.0f;
  ic2_ = 0.0f;
}

}  // namespace sound
