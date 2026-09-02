// Teensy audio: the sound engine's blocks go out through the Teensy Audio Library to
// the audio shield's SGTL5000 over I2S (BOM, D-050). The library's block is 128
// samples like the engine's; its rate is retuned to 48 kHz by the build flag in
// platformio.ini, and the codec's own rate register is set here (D-083).
#include <Arduino.h>
#include <Audio.h>

#include <atomic>

#include "hal/hal.h"

namespace {

constexpr int kAudioMemoryBlocks = 8;  // the library's pool: two per output plus slack
constexpr float kHeadphoneVolume = 0.6f;
constexpr float kInt16FullScale = 32767.0f;
constexpr int kOutputPipelineBlocks = 2;  // the I2S output's DMA ring holds two blocks (unverified)

// SGTL5000 CHIP_CLK_CTRL (datasheet rev 6.0, register 0x0004): SYS_FS bits 3:2 with
// 0x2 = 48 kHz, MCLK_FREQ bits 1:0 with 0x0 = 256 × Fs. The library writes 0x0004
// (44.1 kHz) in enable(); this overrides it (unverified on a board).
constexpr unsigned int kChipClkCtrl = 0x0004;
constexpr unsigned int kSysFs48kMclk256Fs = 0x0008;

std::atomic<hal::AudioCallback> callback_{nullptr};  // read in the audio library's interrupt
bool started_ = false;
float left_[hal::kAudioBlockFrames];
float right_[hal::kAudioBlockFrames];

int16_t to_int16(float sample) {
  const float clamped = sample < -1.0f ? -1.0f : sample > 1.0f ? 1.0f : sample;
  return static_cast<int16_t>(clamped * kInt16FullScale);
}

// The engine as an audio library source: update() runs in the library's software
// interrupt once per block and pulls one block through the app's callback.
class EngineSource : public AudioStream {
 public:
  EngineSource() : AudioStream(0, nullptr) {}

  void update() override {
    const hal::AudioCallback callback = callback_.load(std::memory_order_acquire);
    if (callback == nullptr) return;
    audio_block_t* left = allocate();
    if (left == nullptr) return;
    audio_block_t* right = allocate();
    if (right == nullptr) {
      release(left);
      return;
    }
    callback(left_, right_);
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
      left->data[i] = to_int16(left_[i]);
      right->data[i] = to_int16(right_[i]);
    }
    transmit(left, 0);
    transmit(right, 1);
    release(left);
    release(right);
  }
};

class CodecAt48k : public AudioControlSGTL5000 {
 public:
  bool set_48k() { return write(kChipClkCtrl, kSysFs48kMclk256Fs); }
};

EngineSource source_;
AudioOutputI2S i2s_;
AudioConnection patch_left_(source_, 0, i2s_, 0);
AudioConnection patch_right_(source_, 1, i2s_, 1);
CodecAt48k codec_;

}  // namespace

namespace hal {

void start_audio(AudioCallback callback) {
  static_assert(AUDIO_BLOCK_SAMPLES == kAudioBlockFrames, "the audio library's block is the engine's block");
  static_assert(static_cast<int>(AUDIO_SAMPLE_RATE_EXACT) == kAudioSampleRate,
                "platformio.ini must set AUDIO_SAMPLE_RATE_EXACT to 48000 (D-083)");
  callback_.store(callback, std::memory_order_release);
  if (started_) return;  // already running: the new callback is all that changes
  started_ = true;
  AudioMemory(kAudioMemoryBlocks);
  const bool enabled = codec_.enable();
  const bool at_48k = enabled && codec_.set_48k();
  if (!at_48k) {
    // Never play at the wrong rate (§7.4, D-083): a codec that answered but refused
    // the register stays muted; one that did not answer plays nothing anyway.
    if (enabled) {
      codec_.muteHeadphone();
      codec_.muteLineout();
    }
    Serial.println(enabled ? "hal/teensy: the codec refused 48 kHz; outputs muted"
                           : "hal/teensy: the codec did not answer");
    return;
  }
  codec_.volume(kHeadphoneVolume);
}

int audio_buffer_frames() { return kOutputPipelineBlocks * AUDIO_BLOCK_SAMPLES; }

}  // namespace hal
