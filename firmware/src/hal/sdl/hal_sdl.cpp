// Host HAL (PRD §12, D-016, D-090): an SDL2 window is the screen with a strip of
// the eight pad LEDs and the eleven button backlights under it, the keyboard and
// mouse wheel are the controls, SDL's timer thread is the scheduler's clock and a
// mutex is the lock. Audio, storage and screenshots are in their own files beside
// this one.
#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "hal/hal.h"
#include "hal/sdl/sdl_internal.h"

namespace {

constexpr int kWindowScale = 2;
constexpr int kLedStripHeight = 28;  // logical pixels under the screen: a row of pads, a row of buttons
constexpr int kLedSize = 8;
constexpr int kLedPitch = 30;
constexpr int kLedLeft = (hal::kScreenWidth - kLedPitch * hal::kPadCount + (kLedPitch - kLedSize)) / 2;
constexpr int kPadRowTop = hal::kScreenHeight + 4;
constexpr int kButtonPitch = 20;
constexpr int kButtonLeft = (hal::kScreenWidth - kButtonPitch * hal::kButtonCount + (kButtonPitch - kLedSize)) / 2;
constexpr int kButtonRowTop = hal::kScreenHeight + 16;
constexpr int kPercentFull = 100;
constexpr Uint8 kColourModFull = 255;
constexpr int kLogicalHeight = hal::kScreenHeight + kLedStripHeight;
constexpr int kInputCapacity = 256;
constexpr int kPollWaitMs = 1;
constexpr uint64_t kMicrosecondsPerSecond = 1000000;

// Appendix D: body #EAE3D1 around the screen, legends #6B665C for an unlit pad.
constexpr Uint8 kBodyRed = 0xEA, kBodyGreen = 0xE3, kBodyBlue = 0xD1;
constexpr Uint8 kUnlitRed = 0x6B, kUnlitGreen = 0x66, kUnlitBlue = 0x5C;

const char* const kEncoderNames[hal::kEncoderCount] = {"speed", "filter", "fx", "chance", "volume"};

struct Led {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* screen = nullptr;
SDL_mutex* control_lock = nullptr;
uint16_t framebuffer_[hal::kScreenWidth * hal::kScreenHeight];
Led leds_[hal::kPadCount];
Led button_leds_[hal::kButtonCount];
hal::InputEvent input_[kInputCapacity];
int input_count_ = 0;
int selected_encoder_ = 0;
uint64_t counter_start_ = 0;
uint64_t counter_frequency_ = 1;
bool quit_requested = false;
// The D key is dice, or section D with shift. Shift is often released first, so the
// release goes to whichever button the press went to and never leaves one held.
bool d_held_section = false;
std::atomic<hal::TimerCallback> timer_callback_{nullptr};  // read on SDL's timer thread
SDL_TimerID timer_ = 0;

[[noreturn]] void fail(const char* what) {
  std::fprintf(stderr, "hal/sdl: %s failed: %s\n", what, SDL_GetError());
  std::exit(EXIT_FAILURE);
}

void push(hal::InputKind kind, int index, int detents, Uint32 timestamp_ms) {
  if (input_count_ >= kInputCapacity) return;  // the main loop is far behind; better to lose input than to block
  // The event waited in SDL's queue since `timestamp_ms`, at millisecond resolution;
  // that rounding must not stamp an event before the one ahead of it.
  static uint64_t last_time_us = 0;
  const uint64_t waited_us = static_cast<uint64_t>(SDL_GetTicks() - timestamp_ms) * 1000;
  const uint64_t now = hal::now_us();
  uint64_t time_us = now > waited_us ? now - waited_us : 0;
  if (time_us < last_time_us) time_us = last_time_us;
  last_time_us = time_us;
  input_[input_count_++] = hal::InputEvent{kind, static_cast<uint8_t>(index), static_cast<int8_t>(detents), time_us};
}

void push_button(hal::Button button, bool down, Uint32 timestamp_ms) {
  push(down ? hal::InputKind::button_down : hal::InputKind::button_up, static_cast<int>(button), 0, timestamp_ms);
}

void turn(int detents, Uint32 timestamp_ms) {
  push(hal::InputKind::encoder_turn, selected_encoder_, detents, timestamp_ms);
}

void select_encoder(int step) {
  selected_encoder_ = (selected_encoder_ + step + hal::kEncoderCount) % hal::kEncoderCount;
  std::printf("hal/sdl: knob %s (up/down or wheel turns it, return pushes it)\n", kEncoderNames[selected_encoder_]);
  std::fflush(stdout);
}

// D-090: 1–8 pads; S W K Z D E space for split, swap, skip, undo, dice, show, play;
// A B C and shift+D sections (the D key is dice); up/down turn the selected knob,
// left/right select it, return pushes it; - and = are the volume control; P saves
// a screenshot (D-100).
void on_key(const SDL_KeyboardEvent& key) {
  const bool down = key.type == SDL_KEYDOWN;
  const SDL_Keycode code = key.keysym.sym;
  const bool shifted = (key.keysym.mod & KMOD_SHIFT) != 0;
  const Uint32 at = key.timestamp;
  if (code == SDLK_ESCAPE) {
    quit_requested = true;
    return;
  }
  if (code == SDLK_UP || code == SDLK_DOWN) {  // repeats keep the knob turning
    if (down) turn(code == SDLK_UP ? 1 : -1, at);
    return;
  }
  if (code == SDLK_MINUS || code == SDLK_EQUALS) {
    if (down) push(hal::InputKind::encoder_turn, static_cast<int>(hal::Encoder::volume), code == SDLK_EQUALS ? 1 : -1, at);
    return;
  }
  if (key.repeat != 0) return;
  if (code == SDLK_p) {
    if (down) hal::sdl::save_screenshot(framebuffer_);
    return;
  }
  if (code >= SDLK_1 && code <= SDLK_8) {
    push(down ? hal::InputKind::pad_down : hal::InputKind::pad_up, static_cast<int>(code - SDLK_1), 0, at);
    return;
  }
  switch (code) {
    case SDLK_s: push_button(hal::Button::split, down, at); return;
    case SDLK_w: push_button(hal::Button::swap, down, at); return;
    case SDLK_k: push_button(hal::Button::skip, down, at); return;
    case SDLK_z: push_button(hal::Button::undo, down, at); return;
    case SDLK_d: {
      if (down) d_held_section = shifted;
      push_button(d_held_section ? hal::Button::section_d : hal::Button::dice, down, at);
      return;
    }
    case SDLK_e: push_button(hal::Button::show, down, at); return;
    case SDLK_SPACE: push_button(hal::Button::play, down, at); return;
    case SDLK_a: push_button(hal::Button::section_a, down, at); return;
    case SDLK_b: push_button(hal::Button::section_b, down, at); return;
    case SDLK_c: push_button(hal::Button::section_c, down, at); return;
    case SDLK_LEFT: if (down) select_encoder(-1); return;
    case SDLK_RIGHT: if (down) select_encoder(1); return;
    case SDLK_RETURN:
      push(down ? hal::InputKind::encoder_down : hal::InputKind::encoder_up, selected_encoder_, 0, at);
      return;
    default:
      break;
  }
}

void on_event(const SDL_Event& event) {
  switch (event.type) {
    case SDL_QUIT:
      quit_requested = true;
      return;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      on_key(event.key);
      return;
    case SDL_MOUSEWHEEL: {
      int detents = event.wheel.y;
      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) detents = -detents;
      if (detents != 0) turn(detents, event.wheel.timestamp);
      return;
    }
    default:
      return;
  }
}

// An unlit LED shows as the legend colour so its place is visible.
void draw_led(Led led, int x, int y) {
  const bool lit = led.red != 0 || led.green != 0 || led.blue != 0;
  SDL_SetRenderDrawColor(renderer, lit ? led.red : kUnlitRed, lit ? led.green : kUnlitGreen, lit ? led.blue : kUnlitBlue,
                         SDL_ALPHA_OPAQUE);
  const SDL_Rect square{x, y, kLedSize, kLedSize};
  SDL_RenderFillRect(renderer, &square);
}

Uint32 timer_trampoline(Uint32 interval, void* /*unused*/) {
  const hal::TimerCallback callback = timer_callback_.load(std::memory_order_acquire);
  if (callback != nullptr) callback();
  return interval;
}

}  // namespace

namespace hal {

void init() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) fail("SDL_Init");
  std::atexit(SDL_Quit);
  counter_frequency_ = SDL_GetPerformanceFrequency();
  counter_start_ = SDL_GetPerformanceCounter();
  control_lock = SDL_CreateMutex();
  if (control_lock == nullptr) fail("SDL_CreateMutex");
  window = SDL_CreateWindow("Rota", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreenWidth * kWindowScale,
                            kLogicalHeight * kWindowScale, SDL_WINDOW_SHOWN);
  if (window == nullptr) fail("SDL_CreateWindow");
  // No vsync: present() must return at once so a key press is never behind a frame.
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == nullptr) fail("SDL_CreateRenderer");
  SDL_RenderSetLogicalSize(renderer, kScreenWidth, kLogicalHeight);
  screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight);
  if (screen == nullptr) fail("SDL_CreateTexture");
  for (Led& led : leds_) led = Led{0, 0, 0};
  for (Led& led : button_leds_) led = Led{0, 0, 0};
  std::printf("hal/sdl: window %dx%d (screen %dx%d, scale %d)\n", kScreenWidth * kWindowScale,
              kLogicalHeight * kWindowScale, kScreenWidth, kScreenHeight, kWindowScale);
  std::fflush(stdout);
}

uint64_t now_us() {
  const uint64_t elapsed = SDL_GetPerformanceCounter() - counter_start_;
  return elapsed / counter_frequency_ * kMicrosecondsPerSecond +
         (elapsed % counter_frequency_) * kMicrosecondsPerSecond / counter_frequency_;
}

// Waits up to a millisecond for input so the main loop idles between frames
// without sleeping through a key press.
bool poll() {
  SDL_Event event;
  if (SDL_WaitEventTimeout(&event, kPollWaitMs) != 0) {
    on_event(event);
    while (SDL_PollEvent(&event) != 0) on_event(event);
  }
  return !quit_requested;
}

int read_input(InputEvent* out, int capacity) {
  const int count = input_count_ < capacity ? input_count_ : capacity;
  for (int i = 0; i < count; ++i) out[i] = input_[i];
  for (int i = count; i < input_count_; ++i) input_[i - count] = input_[i];
  input_count_ -= count;
  return count;
}

void start_timer(uint32_t period_us, TimerCallback callback) {
  timer_callback_.store(callback, std::memory_order_release);
  if (timer_ != 0) return;  // already ticking: the new callback is all that changes
  if (period_us == 0 || period_us % 1000 != 0) {
    std::fprintf(stderr, "hal/sdl: start_timer wants whole milliseconds, got %u us\n", static_cast<unsigned>(period_us));
    std::exit(EXIT_FAILURE);
  }
  timer_ = SDL_AddTimer(period_us / 1000, timer_trampoline, nullptr);
  if (timer_ == 0) fail("SDL_AddTimer");
}

void lock() { SDL_LockMutex(control_lock); }
void unlock() { SDL_UnlockMutex(control_lock); }

uint16_t* framebuffer() { return framebuffer_; }

void present() {
  SDL_UpdateTexture(screen, nullptr, framebuffer_, kScreenWidth * static_cast<int>(sizeof(uint16_t)));
  SDL_SetRenderDrawColor(renderer, kBodyRed, kBodyGreen, kBodyBlue, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer);
  const SDL_Rect screen_rect{0, 0, kScreenWidth, kScreenHeight};
  SDL_RenderCopy(renderer, screen, nullptr, &screen_rect);
  for (int i = 0; i < kPadCount; ++i) draw_led(leds_[i], kLedLeft + i * kLedPitch, kPadRowTop);
  for (int i = 0; i < kButtonCount; ++i) draw_led(button_leds_[i], kButtonLeft + i * kButtonPitch, kButtonRowTop);
  SDL_RenderPresent(renderer);
}

void set_led(int pad, uint8_t red, uint8_t green, uint8_t blue) {
  if (pad < 0 || pad >= kPadCount) return;
  leds_[pad] = Led{red, green, blue};
}

void set_button_led(Button button, uint8_t red, uint8_t green, uint8_t blue) {
  const int index = static_cast<int>(button);
  if (index < 0 || index >= kButtonCount) return;
  button_leds_[index] = Led{red, green, blue};
}

void show_leds() {}  // drawn with the next present()

// The window's screen dims like the panel's backlight would.
void set_brightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > kPercentFull) percent = kPercentFull;
  const Uint8 level = static_cast<Uint8>(kColourModFull * percent / kPercentFull);
  SDL_SetTextureColorMod(screen, level, level, level);
}

int battery_percent() {
  int percent = -1;
  SDL_GetPowerInfo(nullptr, &percent);
  return percent < 0 ? 100 : percent;  // a desk machine has no battery to report
}

bool headphones_inserted() { return false; }

void log(const char* line) {
  std::puts(line);
  std::fflush(stdout);
}

}  // namespace hal
