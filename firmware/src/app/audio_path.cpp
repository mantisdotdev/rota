#include "app/audio_path.h"

#include <cstring>

#include "hal/hal.h"

namespace app {

static_assert(hal::kAudioBlockFrames == sound::kBlockSize, "the HAL block is the sound engine's block");
static_assert(hal::kAudioSampleRate == sound::kSampleRate, "the HAL rate is the sound engine's rate");

AudioPath::AudioPath()
    : scheduled(),
      immediate(),
      fired(),
      params(),
      live_generation(0),
      engine_(nullptr),
      blocks_(0),
      latency_last_us_(0),
      latency_worst_us_(0),
      latency_count_(0),
      latency_seen_(0),
      block_{} {}

void AudioPath::reset() {
  ScheduledTrigger trigger;
  while (scheduled.pop(trigger)) {
  }
  Immediate now;
  while (immediate.pop(now)) {
  }
  Fired done;
  while (fired.pop(done)) {
  }
  sound::Params stale;
  params.take(stale);
  blocks_.store(0);
  latency_last_us_.store(0);
  latency_worst_us_.store(0);
  latency_count_.store(0);
  latency_seen_ = 0;
}

void AudioPath::init(sound::Engine& engine, const engine::Kit& kit, const sound::SampleBank& samples) {
  engine_ = &engine;
  engine_->init(kit, samples);
}

int64_t AudioPath::position() const {
  return static_cast<int64_t>(blocks_.load(std::memory_order_acquire)) * sound::kBlockSize;
}

// Auditions first, at the block's first frame; then every scheduled trigger due
// before the block ends, at its own frame (a late one plays at once). Sorted by
// offset for the engine; a full array leaves the rest for the next block.
int AudioPath::collect(int64_t block_start, sound::Trigger* triggers) {
  int count = 0;
  Immediate now;
  while (count < kMaxTriggersPerBlock && immediate.pop(now)) {
    if (now.pressed_us != 0) {
      const uint64_t picked_up = hal::now_us();
      const uint32_t latency = picked_up > now.pressed_us ? static_cast<uint32_t>(picked_up - now.pressed_us) : 0;
      latency_last_us_.store(latency, std::memory_order_relaxed);
      if (latency > latency_worst_us_.load(std::memory_order_relaxed)) {
        latency_worst_us_.store(latency, std::memory_order_relaxed);
      }
      latency_count_.fetch_add(1, std::memory_order_release);
    }
    triggers[count++] = sound::Trigger{now.event, 0};
    fired.push(Fired{block_start, now.event, true});
  }
  ScheduledTrigger due;
  const uint32_t live = live_generation.load(std::memory_order_acquire);
  while (count < kMaxTriggersPerBlock && scheduled.peek(due) && due.sample < block_start + sound::kBlockSize) {
    scheduled.drop();
    if (due.generation != live) continue;  // handed over before a stop: never sounds (T-82)
    int offset = static_cast<int>(due.sample - block_start);
    if (offset < 0) offset = 0;
    triggers[count++] = sound::Trigger{due.event, offset};
    fired.push(Fired{block_start + offset, due.event, false});
  }
  for (int i = 1; i < count; ++i) {  // insertion sort: a handful of items, usually in order already
    const sound::Trigger item = triggers[i];
    int j = i - 1;
    while (j >= 0 && triggers[j].offset > item.offset) {
      triggers[j + 1] = triggers[j];
      --j;
    }
    triggers[j + 1] = item;
  }
  return count;
}

void AudioPath::render(float* left, float* right) {
  const int64_t block_start = position();
  sound::Trigger triggers[kMaxTriggersPerBlock];
  const int count = collect(block_start, triggers);
  sound::Params fresh;
  if (params.take(fresh)) engine_->set_params(fresh);
  engine_->render(triggers, count, block_);
  std::memcpy(left, block_.left, sizeof block_.left);
  std::memcpy(right, block_.right, sizeof block_.right);
  blocks_.fetch_add(1, std::memory_order_release);
}

bool AudioPath::take_latency(uint32_t& last_us, uint32_t& worst_us, uint32_t& count) {
  const uint32_t total = latency_count_.load(std::memory_order_acquire);
  if (total == latency_seen_) return false;
  latency_seen_ = total;
  last_us = latency_last_us_.load(std::memory_order_relaxed);
  worst_us = latency_worst_us_.load(std::memory_order_relaxed);
  count = total;
  return true;
}

}  // namespace app
