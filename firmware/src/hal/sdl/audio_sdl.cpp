// Host audio: SDL2 at 48 kHz float stereo, asking for the engine's 128-frame
// buffer. Whatever buffer the driver grants, the app's callback is fed whole blocks
// of kAudioBlockFrames, with the remainder carried across calls.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>

#include "hal/hal.h"

#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#endif

namespace {

constexpr int kChannels = 2;

#if defined(__APPLE__)
// macOS pulls audio from SDL in the output device's own IO buffer, 512 frames by
// default, so SDL's 128-frame callback ran four times in a burst every 10.7 ms and
// an audition waited up to that long. Asking the device for 128-frame IO buffers
// makes the callback run once per block. Prints what the device settled on.
void request_device_block(int frames) {
  AudioObjectPropertyAddress device_address{kAudioHardwarePropertyDefaultOutputDevice,
                                            kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof device;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &device_address, 0, nullptr, &size, &device) != noErr) return;
  AudioObjectPropertyAddress buffer_address{kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeOutput,
                                            kAudioObjectPropertyElementMain};
  UInt32 wanted = static_cast<UInt32>(frames);
  const OSStatus set = AudioObjectSetPropertyData(device, &buffer_address, 0, nullptr, sizeof wanted, &wanted);
  UInt32 granted = 0;
  size = sizeof granted;
  if (AudioObjectGetPropertyData(device, &buffer_address, 0, nullptr, &size, &granted) != noErr) return;
  std::printf("hal/sdl: output device IO buffer %u frames (asked for %d%s)\n", static_cast<unsigned>(granted), frames,
              set == noErr ? "" : ", refused");
}
#else
void request_device_block(int /*frames*/) {}
#endif

hal::AudioCallback audio_callback_ = nullptr;
SDL_AudioDeviceID device_ = 0;
int granted_frames_ = hal::kAudioBlockFrames;

float left_[hal::kAudioBlockFrames];
float right_[hal::kAudioBlockFrames];
int carried_ = 0;  // frames of left_/right_ rendered but not yet handed to the driver

void fill(void* /*userdata*/, Uint8* stream, int length) {
  float* out = reinterpret_cast<float*>(stream);
  int frames = length / static_cast<int>(sizeof(float) * kChannels);
  while (frames > 0) {
    if (carried_ == 0) {
      audio_callback_(left_, right_);
      carried_ = hal::kAudioBlockFrames;
    }
    const int from = hal::kAudioBlockFrames - carried_;
    const int count = carried_ < frames ? carried_ : frames;
    for (int i = 0; i < count; ++i) {
      *out++ = left_[from + i];
      *out++ = right_[from + i];
    }
    carried_ -= count;
    frames -= count;
  }
}

}  // namespace

namespace hal {

void start_audio(AudioCallback callback) {
  audio_callback_ = callback;
  request_device_block(kAudioBlockFrames);
  SDL_AudioSpec want{};
  want.freq = kAudioSampleRate;
  want.format = AUDIO_F32SYS;
  want.channels = kChannels;
  want.samples = kAudioBlockFrames;
  want.callback = fill;
  SDL_AudioSpec have{};
  device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  if (device_ == 0) {
    std::fprintf(stderr, "hal/sdl: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    std::exit(EXIT_FAILURE);
  }
  granted_frames_ = have.samples;
  std::printf("hal/sdl: audio %d Hz, %d-frame buffer (asked for %d)\n", have.freq, have.samples, kAudioBlockFrames);
  std::fflush(stdout);
  SDL_PauseAudioDevice(device_, 0);
}

int audio_buffer_frames() { return granted_frames_; }

}  // namespace hal
