#include "sound/synth.h"

#include <cstring>

#include "sound/dsp.h"

namespace sound {

namespace {

// The presets the lofi kit names (Appendix A). Numbers are D-076's.
constexpr Preset kPresets[] = {
    // name, waveform, detune, sub, octave, cutoff, env, env decay, q, A, D, S, R, hold, mono, gain
    {"sub-saw", Waveform::saw, 0.0f, 0.7f, 0.0f, 240.0f, 500.0f, 0.25f, 0.6f, 0.004f, 0.5f, 0.75f, 0.08f, 2.0f, true, 0.45f},
    {"warm-poly", Waveform::saw, 5.0f, 0.15f, 0.0f, 700.0f, 900.0f, 0.5f, 0.5f, 0.04f, 1.2f, 0.65f, 0.3f, 4.0f, true, 0.45f},
    {"keys", Waveform::sine, 0.0f, 0.0f, 0.35f, 2500.0f, 3000.0f, 0.25f, 0.5f, 0.002f, 0.9f, 0.0f, 0.2f, 1.0f, false, 0.4f},
};

constexpr float kSilence = 1e-4f;   // −80 dB: the voice ends here
constexpr float kSixtyDbRatio = 0.001f;
constexpr float kCentsPerOctave = 1200.0f;

// A band-limited saw: the naive ramp with the discontinuity smoothed over one sample
// each side (PolyBLEP), so chords at octave 4 do not alias. It falls, so its
// fundamental is in phase with the sub sine added under it rather than cancelling it.
float poly_blep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

float saw(float phase, float increment) { return 1.0f - 2.0f * phase + poly_blep(phase, increment); }

float advanced(float phase, float increment) {
  phase += increment;
  return phase >= 1.0f ? phase - 1.0f : phase;
}

}  // namespace

const Preset* preset_named(const char* name) {
  for (const Preset& preset : kPresets) {
    if (std::strcmp(preset.name, name) == 0) return &preset;
  }
  return nullptr;
}

SynthVoice::SynthVoice()
    : preset_(nullptr),
      tones_{},
      tone_count_(0),
      filter_(),
      filter_env_(0.0f),
      filter_decay_mul_(0.0f),
      stage_(Stage::release),
      level_(0.0f),
      attack_step_(0.0f),
      decay_mul_(0.0f),
      release_mul_(0.0f),
      hold_remaining_(0),
      gain_(0.0f),
      active_(false) {}

void SynthVoice::start(const Preset& preset, const uint8_t* midi_pitches, int tone_count, float gain) {
  const float sample_rate = static_cast<float>(kSampleRate);
  preset_ = &preset;
  tone_count_ = tone_count < kMaxTones ? tone_count : kMaxTones;
  const float below = std::pow(2.0f, -preset.detune_cents / kCentsPerOctave);
  const float above = std::pow(2.0f, preset.detune_cents / kCentsPerOctave);
  for (int t = 0; t < tone_count_; ++t) {
    const float hz = hz_of_midi(static_cast<float>(midi_pitches[t]));
    tones_[t].phase_a = 0.0f;
    tones_[t].phase_b = 0.5f;  // half a period apart so two saws do not start as one
    tones_[t].inc_a = hz * below / sample_rate;
    tones_[t].inc_b = hz * above / sample_rate;
  }
  filter_.reset();
  filter_env_ = 1.0f;
  filter_decay_mul_ = decay_multiplier(preset.filter_decay_s, kSixtyDbRatio, sample_rate);
  stage_ = Stage::attack;
  level_ = 0.0f;
  attack_step_ = preset.attack_s > 0.0f ? 1.0f / (preset.attack_s * sample_rate) : 1.0f;
  decay_mul_ = decay_multiplier(preset.decay_s, kSixtyDbRatio, sample_rate);
  release_mul_ = decay_multiplier(preset.release_s, kSixtyDbRatio, sample_rate);
  hold_remaining_ = static_cast<int>(preset.max_hold_s * sample_rate);
  // Loudness per tone, so a chord and a single note of the same preset sit at one level.
  gain_ = gain * preset.gain / static_cast<float>(tone_count_ > 0 ? tone_count_ : 1);
  active_ = tone_count_ > 0;
}

void SynthVoice::release() {
  if (active_) stage_ = Stage::release;
}

float SynthVoice::loudness() const { return active_ ? level_ * gain_ : 0.0f; }

float SynthVoice::oscillate(Tone& tone) const {
  const Preset& preset = *preset_;
  float sum;
  if (preset.waveform == Waveform::saw) {
    sum = saw(tone.phase_a, tone.inc_a);
    if (preset.detune_cents > 0.0f) sum += saw(tone.phase_b, tone.inc_b);
  } else {
    sum = kSine.at(tone.phase_a);
  }
  if (preset.sub_level > 0.0f) sum += preset.sub_level * kSine.at(tone.phase_a);
  if (preset.octave_level > 0.0f) {
    float doubled = tone.phase_a + tone.phase_a;
    if (doubled >= 1.0f) doubled -= 1.0f;
    sum += preset.octave_level * kSine.at(doubled);
  }
  tone.phase_a = advanced(tone.phase_a, tone.inc_a);
  tone.phase_b = advanced(tone.phase_b, tone.inc_b);
  return sum;
}

void SynthVoice::render(float* out, int from, int to) {
  if (!active_) return;
  const Preset& preset = *preset_;
  // The filter follows its envelope once per segment; the amp envelope runs per sample.
  filter_.set(preset.cutoff_hz + preset.filter_env_hz * filter_env_, preset.q);
  for (int i = from; i < to; ++i) {
    float mix = 0.0f;
    for (int t = 0; t < tone_count_; ++t) mix += oscillate(tones_[t]);
    const float shaped = filter_.low_pass(mix);
    out[i] += shaped * level_ * gain_;

    filter_env_ *= filter_decay_mul_;
    switch (stage_) {
      case Stage::attack:
        level_ += attack_step_;
        if (level_ >= 1.0f) {
          level_ = 1.0f;
          stage_ = Stage::decay;
        }
        break;
      case Stage::decay:
        level_ = preset.sustain + (level_ - preset.sustain) * decay_mul_;
        if (preset.sustain <= 0.0f && level_ < kSilence) {
          active_ = false;
          return;
        }
        break;
      case Stage::release:
        level_ *= release_mul_;
        if (level_ < kSilence) {
          active_ = false;
          return;
        }
        break;
    }
    if (stage_ != Stage::release && --hold_remaining_ <= 0) stage_ = Stage::release;
  }
  filter_.flush();
  filter_env_ = flushed(filter_env_);
}

}  // namespace sound
