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
bool refuse_sample_memory_ = false;
int writes_ = 0;
std::map<std::string, int> writes_by_path_;
uint16_t framebuffer_[hal::kScreenWidth * hal::kScreenHeight];
int presented_ = 0;
std::map<std::string, std::vector<uint8_t>> files_;
// One ring a port, as the device has: a flooded sync jack must not crowd out MIDI.
std::deque<hal::ClockIn> clock_in_[hal::kClockPortCount];
std::vector<hal_fake::ClockOut> clock_out_;
std::deque<uint8_t> midi_in_;
std::vector<uint8_t> midi_sent_;
bool midi_port_open_ = false;  // a test that wants a wire asks for one
bool choke_midi_ = false;
bool refuse_clock_out_ = false;

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
  refuse_sample_memory_ = false;
  writes_ = 0;
  writes_by_path_.clear();
  std::memset(framebuffer_, 0, sizeof framebuffer_);
  presented_ = 0;
  files_.clear();
  for (auto& port : clock_in_) port.clear();
  clock_out_.clear();
  midi_in_.clear();
  midi_sent_.clear();
  // Reserved, never grown mid-run: a push_back that allocates inside the timer
  // callback would break the very rule T-79 exists to check (§12 rule 4).
  clock_out_.reserve(16384);
  midi_sent_.reserve(4096);
  midi_port_open_ = false;
  choke_midi_ = false;
  refuse_clock_out_ = false;
}

void set_time_us(uint64_t now_us) { now_us_ = now_us; }

void push_clock_in(hal::ClockPort port, hal::ClockPulse pulse, uint64_t time_us) {
  std::deque<hal::ClockIn>& ring = clock_in_[static_cast<int>(port)];
  if (static_cast<int>(ring.size()) >= hal::kClockInCapacity) return;  // full: the newest goes, as on the device
  ring.push_back(hal::ClockIn{port, pulse, time_us});
}

void push_midi(const uint8_t* bytes, int count) {
  for (int i = 0; i < count; ++i) {
    if (static_cast<int>(midi_in_.size()) >= hal::kMidiInputCapacity) return;
    midi_in_.push_back(bytes[i]);
  }
}

void set_midi_port_open(bool open) { midi_port_open_ = open; }
void choke_midi(bool choke) { choke_midi_ = choke; }
void refuse_clock_out(bool refuse) { refuse_clock_out_ = refuse; }
const std::vector<ClockOut>& clock_out() { return clock_out_; }
const std::vector<uint8_t>& midi_sent() { return midi_sent_; }
void refuse_writes(bool refuse) { refuse_writes_ = refuse; }
void refuse_sample_memory(bool refuse) { refuse_sample_memory_ = refuse; }
void push(const hal::InputEvent& event) { input_.push_back(event); }
hal::AudioCallback audio_callback() { return audio_callback_; }
hal::TimerCallback timer_callback() { return timer_callback_; }
uint32_t timer_period_us() { return timer_period_us_; }
int writes() { return writes_; }
int writes(const char* path) {
  const auto found = writes_by_path_.find(path);
  return found == writes_by_path_.end() ? 0 : found->second;
}
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

// Oldest first with the two ports interleaved by time, as the device delivers them:
// a test that pushes onto both gets them in the order they happened, not per port.
int read_clock_in(ClockIn* out, int capacity) {
  int count = 0;
  while (count < capacity) {
    int from = -1;
    for (int port = 0; port < kClockPortCount; ++port) {
      if (clock_in_[port].empty()) continue;
      if (from < 0 || clock_in_[port].front().time_us < clock_in_[from].front().time_us) from = port;
    }
    if (from < 0) break;
    out[count++] = clock_in_[from].front();
    clock_in_[from].pop_front();
  }
  return count;
}

// The deadline is recorded, not waited for: a test reads clock_out() and checks the
// microsecond each pulse was armed for against the frame it belongs to.
bool send_clock_out(ClockPort port, ClockPulse pulse, uint64_t at_us) {
  if (refuse_clock_out_) return false;
  clock_out_.push_back(hal_fake::ClockOut{port, pulse, at_us});
  return true;
}

int midi_read(uint8_t* out, int capacity) {
  int count = 0;
  while (count < capacity && !midi_in_.empty()) {
    out[count++] = midi_in_.front();
    midi_in_.pop_front();
  }
  return count;
}

// One byte at a time, as hal.h promises, so a caller that offers more has to come
// back for the rest and the test sees the pacing the wire would impose.
int midi_send(const uint8_t* bytes, int count) {
  if (choke_midi_ || count <= 0) return 0;
  midi_sent_.push_back(bytes[0]);
  return 1;
}

bool midi_port_open() { return midi_port_open_; }

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

FileRead read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size) {
  *size = 0;
  const auto found = files_.find(path);
  if (found == files_.end()) return FileRead::missing;
  if (found->second.size() > capacity) return FileRead::unusable;
  std::memcpy(out, found->second.data(), found->second.size());
  *size = static_cast<uint32_t>(found->second.size());
  return FileRead::ok;
}

bool write_file(const char* path, const uint8_t* data, uint32_t size) {
  writes_ += 1;
  writes_by_path_[path] += 1;
  if (refuse_writes_) return false;
  files_[path] = std::vector<uint8_t>(data, data + size);
  return true;
}

int16_t* sample_memory(uint32_t* frames) {
  static int16_t memory[kSampleMemoryFrames];
  if (refuse_sample_memory_) {
    *frames = 0;
    return nullptr;
  }
  *frames = kSampleMemoryFrames;
  return memory;
}

int battery_percent() { return 87; }
bool headphones_inserted() { return false; }
void log(const char* line) { log_.emplace_back(line); }

}  // namespace hal
