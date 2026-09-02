#include "sound/engine.h"

#include <algorithm>
#include <cstring>

#include "engine/state.h"
#include "sound/dsp.h"

namespace sound {

namespace {

constexpr float kTenths = 10.0f;
constexpr float kDefaultBpm = 100.0f;
constexpr float kDefaultLevel = 0.8f;  // §6.2
constexpr float kOpenTone = 1.0f;
// Per block, a knob's smoothed position covers this share of the distance to the knob.
constexpr float kKnobSmoothing = 0.3f;

// Velocity to gain is the square (D-075): sub-hits sit 4 dB and ghosts 10 dB under a full hit.
float gain_of_velocity(float velocity) { return velocity * velocity; }

void ramp_multiply(float* buffer, float from, float to) {
  const float step = (to - from) / static_cast<float>(kBlockSize);
  float gain = from;
  for (int i = 0; i < kBlockSize; ++i) {
    buffer[i] *= gain;
    gain += step;
  }
}

}  // namespace

Params default_params(const engine::Kit& kit) {
  Params params{};
  params.bpm = kDefaultBpm;
  params.filter = static_cast<float>(kit.filter) / kTenths;
  params.fx = static_cast<float>(kit.fx) / kTenths;
  params.master = 1.0f;
  for (int i = 0; i < kTrackCount; ++i) {
    params.tracks[i].level = kDefaultLevel;
    params.tracks[i].tone = kOpenTone;
    params.tracks[i].send = static_cast<float>(kit.pads[i].send) / kTenths;
  }
  return params;
}

bool Engine::Voice::active() const {
  switch (kind) {
    case Kind::sample:
      return sample.active();
    case Kind::synth:
      return synth.active();
    case Kind::none:
      break;
  }
  return false;
}

float Engine::Voice::loudness() const {
  switch (kind) {
    case Kind::sample:
      return sample.loudness();
    case Kind::synth:
      return synth.loudness();
    case Kind::none:
      break;
  }
  return 0.0f;
}

void Engine::Voice::release() {
  if (kind == Kind::sample) sample.release();
  if (kind == Kind::synth) synth.release();
}

void Engine::Voice::render(float* out, int from, int to) {
  if (kind == Kind::sample) sample.render(out, from, to);
  if (kind == Kind::synth) synth.render(out, from, to);
}

Engine::Engine()
    : kit_(nullptr),
      samples_{},
      presets_{},
      voices_{},
      target_{},
      level_{},
      send_{},
      master_(1.0f),
      tone_knob_{},
      filter_knob_(kOpenTone),
      tone_{},
      dry_filter_(),
      send_filter_(),
      sidechain_(),
      delay_(),
      reverb_(),
      compressor_(),
      clipper_(),
      limiter_(),
      track_{},
      duck_{},
      dry_{},
      send_bus_{} {
  for (Voice& voice : voices_) {
    voice.kind = Voice::Kind::none;
    voice.pad = 0;
  }
}

void Engine::init(const engine::Kit& kit, const SampleBank& samples) {
  kit_ = &kit;
  samples_ = samples;
  for (int i = 0; i < kTrackCount; ++i) {
    const engine::KitPad& pad = kit.pads[i];
    presets_[i] = pad.voice == engine::Voice::synth ? preset_named(pad.source) : nullptr;
  }
  sidechain_.set(kit.sidechain);
  set_params(default_params(kit));
  for (int i = 0; i < kTrackCount; ++i) {
    level_[i] = target_.tracks[i].level;
    send_[i] = target_.tracks[i].send * target_.fx;
    tone_knob_[i] = target_.tracks[i].tone;
  }
  master_ = target_.master;
  filter_knob_ = target_.filter;
  delay_.set_tempo(target_.bpm);
}

void Engine::set_params(const Params& params) { target_ = params; }

int Engine::active_voices() const {
  int count = 0;
  for (const Voice& voice : voices_) count += voice.active() ? 1 : 0;
  return count;
}

void Engine::release_pad(int pad) {
  for (Voice& voice : voices_) {
    if (voice.active() && voice.pad == pad) voice.release();
  }
}

// A free voice, else the one with the least left to hear (D-074).
Engine::Voice& Engine::allocate() {
  Voice* quietest = &voices_[0];
  for (Voice& voice : voices_) {
    if (!voice.active()) return voice;
    if (voice.loudness() < quietest->loudness()) quietest = &voice;
  }
  return *quietest;
}

void Engine::start(const engine::Event& event) {
  const int pad = engine::index_of(event.track);
  const engine::KitPad& kit_pad = kit_->pads[pad];
  const float gain = gain_of_velocity(event.velocity);
  if (kit_pad.voice == engine::Voice::sample) {
    const Sample& sample = samples_.samples[pad];
    if (sample.frame_count <= 0) return;
    release_pad(pad);  // every sample pad is monophonic: the next hit takes over (D-075)
    Voice& voice = allocate();
    voice.kind = Voice::Kind::sample;
    voice.pad = pad;
    voice.sample.start(sample, kit_pad, gain);
    return;
  }
  const Preset* preset = presets_[pad];
  if (preset == nullptr) return;
  if (preset->mono) release_pad(pad);
  // A chord event carries its upper tones (D-041); anything else is one pitch.
  const uint8_t pitches[kMaxTones] = {event.note, event.chord_upper[0], event.chord_upper[1]};
  const int tone_count = event.chord_upper[0] != 0 ? kMaxTones : 1;
  Voice& voice = allocate();
  voice.kind = Voice::Kind::synth;
  voice.pad = pad;
  voice.synth.start(*preset, pitches, tone_count, gain);
}

void Engine::render_voices(int from, int to) {
  if (from >= to) return;
  for (Voice& voice : voices_) {
    if (voice.active()) voice.render(track_[voice.pad], from, to);
  }
}

void Engine::mix_tracks() {
  std::memset(dry_, 0, sizeof dry_);
  std::memset(send_bus_, 0, sizeof send_bus_);
  for (int i = 0; i < kTrackCount; ++i) {
    float* buffer = track_[i];
    const TrackMix& mix = target_.tracks[i];
    ramp_multiply(buffer, level_[i], mix.level);
    level_[i] = mix.level;
    tone_knob_[i] += (mix.tone - tone_knob_[i]) * kKnobSmoothing;
    tone_[i].set(cutoff_of_knob(tone_knob_[i]), kButterworthQ);
    tone_[i].process(buffer, kBlockSize);
    tone_[i].flush();
    if (!engine::is_drum(engine::pad_at(i))) {
      for (int k = 0; k < kBlockSize; ++k) buffer[k] *= duck_[k];
    }
    const float send_to = mix.send * target_.fx;
    const float send_step = (send_to - send_[i]) / static_cast<float>(kBlockSize);
    float send = send_[i];
    for (int k = 0; k < kBlockSize; ++k) {
      dry_[k] += buffer[k];
      send_bus_[k] += buffer[k] * send;
      send += send_step;
    }
    send_[i] = send_to;
  }
}

void Engine::master_chain(StereoBlock& out) {
  filter_knob_ += (target_.filter - filter_knob_) * kKnobSmoothing;
  const float cutoff = cutoff_of_knob(filter_knob_);
  dry_filter_.set(cutoff, kButterworthQ);
  send_filter_.set(cutoff, kButterworthQ);
  dry_filter_.process(dry_, kBlockSize);
  send_filter_.process(send_bus_, kBlockSize);
  dry_filter_.flush();
  send_filter_.flush();

  std::memset(out.left, 0, sizeof out.left);
  std::memset(out.right, 0, sizeof out.right);
  delay_.set_tempo(target_.bpm);
  delay_.process(send_bus_, out.left, out.right, kBlockSize);
  reverb_.process(send_bus_, out.left, out.right, kBlockSize);
  for (int k = 0; k < kBlockSize; ++k) {
    out.left[k] += dry_[k];
    out.right[k] += dry_[k];
  }

  compressor_.process(out.left, out.right, kBlockSize);
  clipper_.process(out.left, out.right, kBlockSize);
  limiter_.process(out.left, out.right, kBlockSize);
  ramp_multiply(out.left, master_, target_.master);
  ramp_multiply(out.right, master_, target_.master);
  master_ = target_.master;
}

void Engine::render(const Trigger* triggers, int trigger_count, StereoBlock& out) {
  std::memset(track_, 0, sizeof track_);

  // The duck curve first: every kick in the block resets it at its own sample.
  int next = 0;
  for (int k = 0; k < kBlockSize; ++k) {
    while (next < trigger_count && triggers[next].offset <= k) {
      if (triggers[next].event.track == engine::Pad::kick) sidechain_.duck();
      ++next;
    }
    duck_[k] = sidechain_.next();
  }

  // Voices, segment by segment, so a voice starts (and the pad's previous voice lets
  // go) at the trigger's sample, not at the block edge.
  int from = 0;
  for (int t = 0; t < trigger_count; ++t) {
    const int at = std::min(std::max(triggers[t].offset, from), kBlockSize - 1);
    render_voices(from, at);
    start(triggers[t].event);
    from = at;
  }
  render_voices(from, kBlockSize);

  mix_tracks();
  master_chain(out);
}

}  // namespace sound
