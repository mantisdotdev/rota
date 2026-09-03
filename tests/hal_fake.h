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

// The clock ports and the MIDI wire (§7.6, §11). A test scripts what arrives and
// reads back what left, so a follower's arithmetic is checked against a partner it
// writes itself rather than against a second device.
//
// A pulse is pushed with the time it arrived, since that stamp is the whole of what
// a clock says; the fake takes it as given and does not stamp it with its own clock,
// so a test can place a pulse anywhere against the audio it renders.
void push_clock_in(hal::ClockPort port, hal::ClockPulse pulse, uint64_t time_us);
void push_midi(const uint8_t* bytes, int count);

// A wire at all. Off to begin with, as both platforms are until their link files
// land, so a case that says nothing about the ports behaves exactly as it did before
// there were any.
void set_midi_port_open(bool open);

// A port that still has a pulse armed, so every send_clock_out is refused: the path
// where the app has to offer the same pulse again (T-103).
void refuse_clock_out(bool refuse);

// A wire that will not take a byte, for the path where a send has to be offered
// again: every midi_send takes nothing while this is on.
void choke_midi(bool choke);

// One pulse the app armed, in the order the ports were asked.
struct ClockOut {
  hal::ClockPort port;
  hal::ClockPulse pulse;
  uint64_t at_us;
};
const std::vector<ClockOut>& clock_out();

// Every byte the app put on the wire, in order, the pulses excluded: those carry a
// deadline rather than a byte and are in clock_out().
const std::vector<uint8_t>& midi_sent();

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
