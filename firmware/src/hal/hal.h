#pragma once

#include <cstdint>

// Hardware abstraction layer (PRD §12, D-088). One interface, one implementation per
// platform: hal/teensy/ for the device, hal/sdl/ for the host simulator (D-016), and
// tests/hal_fake.cpp for the scripted harness. Nothing in engine/ or sound/ may
// include this header (§12 rule 1). Everything here is called from the main loop
// unless the comment says otherwise.
namespace hal {

constexpr int kScreenWidth = 320;   // §7.3, RGB565
constexpr int kScreenHeight = 240;
constexpr int kPadCount = 8;
constexpr int kAudioSampleRate = 48000;  // §7.4; equals sound::kSampleRate
constexpr int kAudioBlockFrames = 128;   // equals sound::kBlockSize

// The round and section buttons in the order of §7.2.
enum class Button : uint8_t { split, swap, skip, undo, dice, show, play, section_a, section_b, section_c, section_d };
constexpr int kButtonCount = 11;

// The four knobs of §8.3 plus the volume control, which is an encoder on the EVT
// unit (D-054).
enum class Encoder : uint8_t { speed, filter, fx, chance, volume };
constexpr int kEncoderCount = 5;

enum class InputKind : uint8_t { pad_down, pad_up, button_down, button_up, encoder_turn, encoder_down, encoder_up };

struct InputEvent {
  InputKind kind;
  uint8_t index;     // pad 0–7, or a Button or Encoder cast to its integer
  int8_t detents;    // encoder_turn only: positive is clockwise
  uint64_t time_us;  // now_us() when the platform saw it, for latency measurement
};

// Fills one block of planar float audio, exactly kAudioBlockFrames per channel.
// Runs on the platform's audio thread or interrupt: no allocation, no locks, no
// logging inside it (§12 rule 4).
using AudioCallback = void (*)(float* left, float* right);

// Runs on the platform's timer thread or interrupt, every period the app asked for.
using TimerCallback = void (*)();

// Brings the platform up. Call once, before anything else.
void init();

// Monotonic microseconds since init. Safe from the audio and timer callbacks.
uint64_t now_us();

// Services platform events and gathers input. Returns false when the host asks to
// quit (window closed or Escape pressed); the device never asks.
bool poll();

// Moves the input seen since the last call into `out`, oldest first. Returns how many.
int read_input(InputEvent* out, int capacity);

// Starts the audio output; the callback is called for every block from then on.
// Once per process: a later call replaces the callback and starts nothing new.
void start_audio(AudioCallback callback);

// Frames the platform holds between the callback and the output: what a trigger at
// the start of a block waits before it is heard, over and above the block itself.
int audio_buffer_frames();

// Starts a periodic timer; the callback runs off the main loop from then on. Once
// per process: a later call replaces the callback and keeps the first period.
void start_timer(uint32_t period_us, TimerCallback callback);

// Mutual exclusion between the main loop and the timer callback. Held for
// microseconds; never taken from the audio callback.
void lock();
void unlock();

// The screen. The app draws into the buffer and present() pushes it (§7.3).
uint16_t* framebuffer();
void present();

// The RGB LED under each pad (§7.2). set_led stages; show_leds sends all eight.
void set_led(int pad, uint8_t red, uint8_t green, uint8_t blue);
void show_leds();

// Whole files on the device's storage, by a path relative to its root. read_file
// stores at most `capacity` bytes and the file's size; false when the file is
// missing or larger than the buffer.
bool read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size);
bool write_file(const char* path, const uint8_t* data, uint32_t size);

// Power (§7.7, §7.4): 0–100, and whether the headphone jack has a plug in it.
int battery_percent();
bool headphones_inserted();

// One line of diagnostics: stdout on the host, USB serial on the device.
void log(const char* line);

// Where the big objects live: the sound engine and the framebuffer, a few hundred
// kilobytes each. The Teensy puts them in RAM2 (its DMAMEM section, not zeroed at
// boot, so only for objects a constructor or the first frame fills); the host
// anywhere. Declared here so app/ and ui/ need no platform header.
#if defined(ARDUINO_TEENSY41)
#define HAL_BULK_MEMORY __attribute__((section(".dmabuffers"), used))
#else
#define HAL_BULK_MEMORY
#endif

}  // namespace hal
