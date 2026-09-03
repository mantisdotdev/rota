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

// Room for one kit: eight pads of the two seconds D-081 allows each, and a little
// over so a sample of exactly two seconds still has room for its file's header while
// io/ reads it in place.
constexpr uint32_t kMaxSampleFramesPerPad = kAudioSampleRate * 2;
constexpr uint32_t kSampleMemoryFrames = kPadCount * kMaxSampleFramesPerPad + 64;

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

// The clock ports (§7.6, §11): one MIDI wire at 31250 baud and one Pocket Operator
// sync jack, both open all the time. Which of them the device listens to or talks
// on is a settings row (§9.4), not something the HAL decides.
enum class ClockPort : uint8_t { midi, sync };
constexpr int kClockPortCount = 2;
constexpr uint32_t kMidiByteUs = 320;  // 31250 baud, ten bits a byte: 3125 bytes a second and no more

// One pulse of somebody's clock. `tick` is a MIDI clock byte or one edge on the
// sync jack; the other three are MIDI's transport messages, which the sync wire has
// no room for and never carries.
enum class ClockPulse : uint8_t { tick, start, resume, stop };

// A pulse a port saw, stamped by the platform where it arrived and not where it is
// read: a clock's whole value is when it came, and the main loop's gap is
// milliseconds, so reading late must cost how soon an estimate moves and never how
// right it is. InputEvent carries a time for the same reason (D-088). MIDI's four
// System Real Time bytes are lifted out of the byte stream by the platform, because
// one of them may sit between any two bytes of any other message, a SysEx included,
// and only the code holding the UART can stamp it where it lands; every other byte
// stays below for io/ to parse (D-114).
struct ClockIn {
  ClockPort port;
  ClockPulse pulse;
  uint64_t time_us;
};

// Each port has its own ring this deep, so a shorted sync jack cannot crowd out the
// MIDI clock it is meant to lose to. A full ring drops the newest pulse: a ring that
// fills means nothing is draining it, and then a gap is honest where a stale stamp
// would not be.
constexpr int kClockInCapacity = 64;

// Moves the pulses seen since the last call into `out`, oldest first, the two ports
// interleaved by time. Returns how many. From the main loop, beside read_input.
int read_clock_in(ClockIn* out, int capacity);

// Sends one pulse on `port` at `at_us`, from the platform's own one-shot rather than
// in the caller's context. The beat is decided in a 2 ms timer and 2 ms is most of
// the 3 ms §11 allows between two linked devices, so what is handed over is the
// deadline and not the byte. A deadline already past is sent at once — which is all
// a platform without a one-shot ever does — and "already past" is a signed
// comparison, because these are microseconds since init and the difference wraps the
// other way otherwise. False when the port still has a pulse armed, and the caller
// offers that one again on its next tick. From the timer callback; never from the
// audio callback.
bool send_clock_out(ClockPort port, ClockPulse pulse, uint64_t at_us);

// The MIDI wire itself, minus the real-time bytes read_clock_in already took: the
// jam link's SysEx (§11) and nothing else this firmware knows. midi_read moves what
// arrived since the last call into `out`, oldest first. midi_send takes at most one
// byte and refuses until that byte has left the platform, so a pulse armed by
// send_clock_out waits behind at most one byte time; io/ then offers a payload byte
// every other byte time, which leaves the wire half empty and the worst wait for a
// clock byte at 320 µs. Neither call blocks, waits or spins whatever the platform's
// own driver would do, since a spin under lock() is a hang with interrupts off
// rather than a delay.
constexpr int kMidiInputCapacity = 640;  // a 512-character message and a quarter second of wire behind it
int midi_read(uint8_t* out, int capacity);
int midi_send(const uint8_t* bytes, int count);

// Whether this build has a MIDI port at all: the device has one, the simulator only
// when it was started as one end of a link, the fake when a test gives it one. Said
// in a result rather than left to a send that takes nothing, which is what a busy
// wire looks like too; app/ then leaves the clock and the jam gestures quiet and says
// so, as a board with no PSRAM leaves the sample pads silent (T-100). A cable is a
// different question: no MIDI or sync jack can detect one, so an unplugged link is
// silence, which app/ reads as a clock that stopped (T-20).
bool midi_port_open();

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
// per process: a later call replaces the callback and keeps the first period. The
// period is a whole number of milliseconds (the host's timer counts them); the app
// asks for 2 ms (D-084).
void start_timer(uint32_t period_us, TimerCallback callback);

// Mutual exclusion between the main loop and the timer callback. Held for
// microseconds; never taken from the audio callback.
void lock();
void unlock();

// The screen. The app draws into the buffer and present() pushes it (§7.3).
uint16_t* framebuffer();
void present();

// The RGB LED under each pad and the backlight under each button (§7.2, §8.6,
// D-099). set_led and set_button_led stage; show_leds sends what changed.
void set_led(int pad, uint8_t red, uint8_t green, uint8_t blue);
void set_button_led(Button button, uint8_t red, uint8_t green, uint8_t blue);
void show_leds();

// The screen's backlight, 0–100 (§9.4).
void set_brightness(int percent);

// Whether a file could be read. `ok` means `out` holds the whole file and `*size` is
// its length; `unusable` means the file is there but did not come back — too big for
// the buffer, or a read that failed; `missing` means there is no such file. A card is
// a boundary and io/ has to tell an absent file from one it cannot use, so existence
// is said in the result rather than encoded in a size (D-104).
enum class FileRead : uint8_t { missing, unusable, ok };

// Whole files on the device's storage, by a path relative to its root. read_file
// stores at most `capacity` bytes; `*size` is meaningful only with `ok`.
FileRead read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size);
bool write_file(const char* path, const uint8_t* data, uint32_t size);

// Where a kit's samples are kept once io/ has read them off the card: the PSRAM of
// §7.5 on the device, ordinary memory on the host. Megabytes, so not the RAM2 that
// HAL_BULK_MEMORY names. Returns nullptr and 0 frames when the board has no PSRAM
// fitted — which every board does until bring-up — and io/ then leaves the sample
// pads silent rather than writing to memory that is not there.
int16_t* sample_memory(uint32_t* frames);

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
