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
void push(const hal::InputEvent& event);

hal::AudioCallback audio_callback();
hal::TimerCallback timer_callback();
uint32_t timer_period_us();

const std::vector<std::string>& log();
Led led(int pad);
const uint16_t* framebuffer();
int frames_presented();

}  // namespace hal_fake
