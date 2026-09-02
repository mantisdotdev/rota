#include "sound/voice.h"

#include <algorithm>
#include <cmath>

namespace sound {

namespace {

constexpr float kSemitonesPerOctave = 12.0f;
constexpr float kInt16Scale = 1.0f / 32768.0f;
constexpr double kFixedOne = 4294967296.0;  // 2^32

uint64_t fixed_of(double frames) { return static_cast<uint64_t>(frames * kFixedOne); }
double frames_of(uint64_t fixed) { return static_cast<double>(fixed) / kFixedOne; }

}  // namespace

SampleVoice::SampleVoice()
    : frames_(nullptr),
      frame_count_(0),
      position_(0),
      rate_(0),
      end_(0),
      started_at_(0),
      gain_(0.0f),
      fades_(false),
      active_(false) {}

void SampleVoice::start(const Sample& sample, const engine::KitPad& pad, float gain) {
  active_ = false;
  if (sample.frame_count <= 0 || sample.frames == nullptr) return;
  const double count = static_cast<double>(sample.frame_count);
  const double start_frame = std::min(std::max(static_cast<double>(pad.start), 0.0), 1.0) * count;
  const double decay = std::min(std::max(static_cast<double>(pad.decay), 0.0), 1.0);
  const double end_frame = start_frame + decay * (count - start_frame);
  if (end_frame <= start_frame) return;
  frames_ = sample.frames;
  frame_count_ = sample.frame_count;
  position_ = fixed_of(start_frame);
  started_at_ = position_;
  end_ = fixed_of(end_frame);
  rate_ = fixed_of(std::pow(2.0, static_cast<double>(pad.pitch_semitones) / kSemitonesPerOctave));
  gain_ = gain;
  fades_ = end_frame < count;
  active_ = true;
}

void SampleVoice::release() {
  if (!active_) return;
  const uint64_t fade_end = position_ + (static_cast<uint64_t>(kSampleFadeFrames) << kFractionBits);
  end_ = std::min(end_, fade_end);
  fades_ = true;
}

float SampleVoice::loudness() const {
  if (!active_ || end_ <= started_at_) return 0.0f;
  const double remaining = frames_of(end_ - position_);
  const double total = frames_of(end_ - started_at_);
  return gain_ * static_cast<float>(remaining / total);
}

void SampleVoice::render(float* out, int from, int to) {
  if (!active_) return;
  const float scale = gain_ * kInt16Scale;
  const float fade_frames = static_cast<float>(kSampleFadeFrames);
  for (int i = from; i < to; ++i) {
    if (position_ >= end_) {
      active_ = false;
      return;
    }
    const int index = static_cast<int>(position_ >> kFractionBits);
    const int next = index + 1 < frame_count_ ? index + 1 : index;
    const float fraction = static_cast<float>(position_ & 0xffffffffu) * (1.0f / 4294967296.0f);
    const float sample =
        static_cast<float>(frames_[index]) + (static_cast<float>(frames_[next]) - static_cast<float>(frames_[index])) * fraction;
    const float remaining = static_cast<float>(frames_of(end_ - position_));
    const float fade = fades_ && remaining < fade_frames ? remaining / fade_frames : 1.0f;
    out[i] += sample * scale * fade;
    position_ += rate_;
  }
  if (position_ >= end_) active_ = false;
}

}  // namespace sound
