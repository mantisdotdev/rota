#pragma once

#include <cmath>

// Small helpers shared by every stage of sound/. Units: gains are linear factors, dB
// are decibels relative to full scale, times are seconds unless a name says frames.
namespace sound {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

inline float gain_of_db(float db) { return std::pow(10.0f, db / 20.0f); }
inline float db_of_gain(float gain) { return 20.0f * std::log10(gain); }

// MIDI pitch to frequency: A4 (69) is 440 Hz.
inline float hz_of_midi(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

// Per-sample coefficient of a one-pole smoother that covers 63% of a step in `seconds`.
inline float one_pole_coef(float seconds, float sample_rate) {
  if (seconds <= 0.0f) return 1.0f;
  return 1.0f - std::exp(-1.0f / (seconds * sample_rate));
}

// Per-sample multiplier that takes a value to `ratio` of itself after `seconds`.
inline float decay_multiplier(float seconds, float ratio, float sample_rate) {
  if (seconds <= 0.0f) return 0.0f;
  return std::pow(ratio, 1.0f / (seconds * sample_rate));
}

// Denormals: a recursive state left to decay forever ends in subnormal floats, which
// are slow on any FPU without flush-to-zero and are never audible. Filters and
// envelopes flush their state once it is below this; delay lines, which cannot be
// flushed sample by sample, carry a DC offset far below the 16-bit floor instead.
constexpr float kFlushThreshold = 1e-15f;
constexpr float kAntiDenormal = 1e-18f;

inline float flushed(float x) { return std::fabs(x) < kFlushThreshold ? 0.0f : x; }

// One period of a sine, filled once at start-up so no voice calls std::sin in the audio path.
class SineTable {
 public:
  SineTable();
  // phase in [0, 1)
  float at(float phase) const {
    const float scaled = phase * static_cast<float>(kSize);
    const int index = static_cast<int>(scaled);
    const float fraction = scaled - static_cast<float>(index);
    return table_[index] + (table_[index + 1] - table_[index]) * fraction;
  }

 private:
  static constexpr int kSize = 2048;
  float table_[kSize + 1];  // one guard entry so the interpolation never reads past the end
};

extern const SineTable kSine;

}  // namespace sound
