// The HAL the tests link instead of hal/sdl/ or hal/teensy/: a scripted clock, an
// input queue the test fills, and the audio and timer callbacks handed back to the
// test so it can run the world frame by frame (D-088).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/hal.h"

namespace hal_fake {

struct Led {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

// Everything back to zero: the clock, the queues, the callbacks, the log.
void reset();

void set_time_us(uint64_t now_us);

// Makes every hal::write_file fail, for the paths that must survive a full card.
void refuse_writes(bool refuse);
void push(const hal::InputEvent& event);

hal::AudioCallback audio_callback();
hal::TimerCallback timer_callback();
uint32_t timer_period_us();

// A board with no PSRAM fitted, which is every board until bring-up (T-100).
void refuse_sample_memory(bool refuse);

// Every hal::write_file the card was asked for, refused ones included (T-99); with
// a path, only the ones for that file.
int writes();
int writes(const char* path);

const std::vector<std::string>& log();
Led led(int pad);
Led button_led(int button);
int brightness();
const uint16_t* framebuffer();
int frames_presented();

}  // namespace hal_fake
