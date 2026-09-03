#include "hal_fake.h"

#include <cstring>
#include <deque>
#include <map>

namespace {

uint64_t now_us_ = 0;
std::deque<hal::InputEvent> input_;
hal::AudioCallback audio_callback_ = nullptr;
hal::TimerCallback timer_callback_ = nullptr;
uint32_t timer_period_us_ = 0;
std::vector<std::string> log_;
hal_fake::Led leds_[hal::kPadCount];
hal_fake::Led button_leds_[hal::kButtonCount];
int brightness_ = 100;
bool refuse_writes_ = false;
int writes_ = 0;
uint16_t framebuffer_[hal::kScreenWidth * hal::kScreenHeight];
int presented_ = 0;
std::map<std::string, std::vector<uint8_t>> files_;

}  // namespace

namespace hal_fake {

void reset() {
  now_us_ = 0;
  input_.clear();
  audio_callback_ = nullptr;
  timer_callback_ = nullptr;
  timer_period_us_ = 0;
  log_.clear();
  std::memset(leds_, 0, sizeof leds_);
  std::memset(button_leds_, 0, sizeof button_leds_);
  brightness_ = 100;
  refuse_writes_ = false;
  writes_ = 0;
  std::memset(framebuffer_, 0, sizeof framebuffer_);
  presented_ = 0;
  files_.clear();
}

void set_time_us(uint64_t now_us) { now_us_ = now_us; }
void refuse_writes(bool refuse) { refuse_writes_ = refuse; }
void push(const hal::InputEvent& event) { input_.push_back(event); }
hal::AudioCallback audio_callback() { return audio_callback_; }
hal::TimerCallback timer_callback() { return timer_callback_; }
uint32_t timer_period_us() { return timer_period_us_; }
int writes() { return writes_; }
const std::vector<std::string>& log() { return log_; }
Led led(int pad) { return leds_[pad]; }
Led button_led(int button) { return button_leds_[button]; }
int brightness() { return brightness_; }
const uint16_t* framebuffer() { return framebuffer_; }
int frames_presented() { return presented_; }

}  // namespace hal_fake

namespace hal {

void init() { hal_fake::reset(); }
uint64_t now_us() { return now_us_; }
bool poll() { return true; }

int read_input(InputEvent* out, int capacity) {
  int count = 0;
  while (count < capacity && !input_.empty()) {
    out[count++] = input_.front();
    input_.pop_front();
  }
  return count;
}

void start_audio(AudioCallback callback) { audio_callback_ = callback; }
int audio_buffer_frames() { return kAudioBlockFrames; }

void start_timer(uint32_t period_us, TimerCallback callback) {
  timer_period_us_ = period_us;
  timer_callback_ = callback;
}

void lock() {}
void unlock() {}

uint16_t* framebuffer() { return framebuffer_; }
void present() { presented_ += 1; }

void set_led(int pad, uint8_t red, uint8_t green, uint8_t blue) {
  if (pad >= 0 && pad < kPadCount) leds_[pad] = hal_fake::Led{red, green, blue};
}
void set_button_led(Button button, uint8_t red, uint8_t green, uint8_t blue) {
  const int index = static_cast<int>(button);
  if (index >= 0 && index < kButtonCount) button_leds_[index] = hal_fake::Led{red, green, blue};
}
void show_leds() {}
void set_brightness(int percent) { brightness_ = percent; }

bool read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size) {
  const auto found = files_.find(path);
  if (found == files_.end() || found->second.size() > capacity) return false;
  std::memcpy(out, found->second.data(), found->second.size());
  *size = static_cast<uint32_t>(found->second.size());
  return true;
}

bool write_file(const char* path, const uint8_t* data, uint32_t size) {
  writes_ += 1;
  if (refuse_writes_) return false;
  files_[path] = std::vector<uint8_t>(data, data + size);
  return true;
}

int battery_percent() { return 87; }
bool headphones_inserted() { return false; }
void log(const char* line) { log_.emplace_back(line); }

}  // namespace hal
