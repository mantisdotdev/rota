// Shared helpers for the sound tests (spec/scenarios.md T-63 onward). Signals are
// synthesised in the test so every assertion is about a known input; the real lofi
// samples are loaded only where the scenario is about them.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "engine/events.h"
#include "engine/kits/lofi.h"
#include "sound/engine.h"

namespace sound_support {

using namespace sound;

constexpr float kTwoPiF = 6.283185307f;

inline const engine::Kit& lofi() { return engine::kits::kLofi; }

inline float db(float gain) { return 20.0f * std::log10(std::max(gain, 1e-12f)); }

inline std::vector<float> sine(float hz, float amplitude, int frames) {
  std::vector<float> out(static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    out[static_cast<size_t>(i)] = amplitude * std::sin(kTwoPiF * hz * static_cast<float>(i) / kSampleRate);
  }
  return out;
}

inline std::vector<int16_t> sine16(float hz, float amplitude, int frames) {
  std::vector<int16_t> out(static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    const float x = amplitude * std::sin(kTwoPiF * hz * static_cast<float>(i) / kSampleRate);
    out[static_cast<size_t>(i)] = static_cast<int16_t>(std::lround(x * 32767.0f));
  }
  return out;
}

inline float peak_of(const std::vector<float>& v, size_t from = 0, size_t to = 0) {
  if (to == 0) to = v.size();
  float peak = 0.0f;
  for (size_t i = from; i < to && i < v.size(); ++i) peak = std::max(peak, std::fabs(v[i]));
  return peak;
}

inline float rms_of(const std::vector<float>& v, size_t from, size_t to) {
  double sum = 0.0;
  size_t count = 0;
  for (size_t i = from; i < to && i < v.size(); ++i) {
    sum += static_cast<double>(v[i]) * v[i];
    ++count;
  }
  return count == 0 ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

// Goertzel magnitude of `v` at `hz`, normalised so a sine of amplitude a reads about a.
inline float magnitude_at(const std::vector<float>& v, float hz) {
  const double w = 2.0 * M_PI * hz / kSampleRate;
  const double coef = 2.0 * std::cos(w);
  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
  for (float x : v) {
    s0 = x + coef * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double power = s1 * s1 + s2 * s2 - coef * s1 * s2;
  return static_cast<float>(2.0 * std::sqrt(std::max(power, 0.0)) / static_cast<double>(v.size()));
}

// The frequency with the most energy among [from, to] Hz in steps of `step`.
inline float loudest_frequency(const std::vector<float>& v, float from, float to, float step = 1.0f) {
  float best_hz = from;
  float best = -1.0f;
  for (float hz = from; hz <= to; hz += step) {
    const float m = magnitude_at(v, hz);
    if (m > best) {
      best = m;
      best_hz = hz;
    }
  }
  return best_hz;
}

// Runs a stage over a mono signal in blocks, the way the engine does.
template <typename Stage>
std::vector<float> through_stereo(Stage& stage, const std::vector<float>& in) {
  std::vector<float> left = in;
  std::vector<float> right = in;
  for (size_t at = 0; at < left.size(); at += kBlockSize) {
    const int count = static_cast<int>(std::min<size_t>(kBlockSize, left.size() - at));
    stage.process(left.data() + at, right.data() + at, count);
  }
  return left;
}

inline engine::Event hit(engine::Pad pad, float velocity = 1.0f, uint8_t note = 0, uint8_t upper0 = 0,
                         uint8_t upper1 = 0) {
  engine::Event event{};
  event.track = pad;
  event.time = engine::Fraction{0, 1};
  event.note = note;
  event.chord_upper[0] = upper0;
  event.chord_upper[1] = upper1;
  event.velocity = velocity;
  event.sub_index = 0;
  event.is_ghost = false;
  return event;
}

// A bank whose frames live in the test; synth pads and unset pads stay empty.
struct TestBank {
  std::vector<int16_t> frames[kTrackCount];
  void set(engine::Pad pad, const std::vector<int16_t>& sample) { frames[engine::index_of(pad)] = sample; }
  SampleBank bank() const {
    SampleBank bank{};
    for (int i = 0; i < kTrackCount; ++i) {
      bank.samples[i].frames = frames[i].empty() ? nullptr : frames[i].data();
      bank.samples[i].frame_count = static_cast<int>(frames[i].size());
    }
    return bank;
  }
};

// A trigger placed in a given block of a Harness render.
struct Cue {
  int block;
  Trigger trigger;
};

inline Cue cue(int block, int offset, const engine::Event& event) { return Cue{block, Trigger{event, offset}}; }

// Drives an Engine block by block and keeps both channels.
struct Harness {
  std::unique_ptr<Engine> engine = std::make_unique<Engine>();
  std::vector<float> left;
  std::vector<float> right;

  void init(const engine::Kit& kit, const SampleBank& bank) { engine->init(kit, bank); }

  void render(int blocks, std::vector<Cue> cues = {}) {
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) {
      return a.block != b.block ? a.block < b.block : a.trigger.offset < b.trigger.offset;
    });
    StereoBlock out;
    std::vector<Trigger> triggers;
    size_t next = 0;
    for (int b = 0; b < blocks; ++b) {
      triggers.clear();
      while (next < cues.size() && cues[next].block == b) triggers.push_back(cues[next++].trigger);
      engine->render(triggers.data(), static_cast<int>(triggers.size()), out);
      left.insert(left.end(), out.left, out.left + kBlockSize);
      right.insert(right.end(), out.right, out.right + kBlockSize);
    }
  }
};

inline int blocks_of_seconds(float seconds) { return static_cast<int>(seconds * kSampleRate / kBlockSize); }

// The lofi kit with the chord pad turned into a sample pad, so a steady known signal
// can sit on a melodic (ducked) track.
inline engine::Kit kit_with_sample_chord() {
  engine::Kit kit = lofi();
  engine::KitPad& chord = kit.pads[engine::index_of(engine::Pad::chord)];
  chord.voice = engine::Voice::sample;
  chord.source = "tone.wav";
  chord.pitch_semitones = 0;
  chord.start = 0.0f;
  chord.decay = 1.0f;
  return kit;
}

}  // namespace sound_support
