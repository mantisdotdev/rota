#pragma once

#include <atomic>
#include <cstdint>

#include "app/queue.h"
#include "app/scheduler.h"
#include "engine/events.h"
#include "engine/kit.h"
#include "sound/engine.h"
#include "sound/voice.h"

// The audio side (D-084): pops the triggers due in the block from the scheduler's
// queue and the auditions from the immediate queue, renders through the sound
// engine, and reports what fired back to the control side for the ring's flashes.
// Everything here runs inside hal's audio callback except init and the readers.
namespace app {

// A pad pressed now: rendered at the start of the next block (D-085).
struct Immediate {
  engine::Event event;
  uint64_t pressed_us;  // when the platform saw the press; 0 = do not measure
};

// A hit the sound engine was handed, with the frame it started on.
struct Fired {
  int64_t sample;
  engine::Event event;
  bool audition;
};

constexpr int kImmediateQueueCapacity = 64;
constexpr int kFiredQueueCapacity = 256;
constexpr int kMaxTriggersPerBlock = 32;  // two grid points of eight tracks, plus auditions

// The control side's memory of what fired: the ring flashes hits younger than
// 250 ms (§9.1) and the tests read the exact samples. `total` only grows; an entry
// is reachable while total − seq < kCapacity.
struct FiredLog {
  static constexpr int kCapacity = 256;
  Fired items[kCapacity];
  uint32_t total;

  void append(const Fired& fired) {
    items[total % kCapacity] = fired;
    total += 1;
  }
  const Fired& at(uint32_t seq) const { return items[seq % kCapacity]; }
};

class AudioPath {
 public:
  AudioPath();
  // Back to the state after construction: empty queues, position 0.
  void reset();
  // `engine` lives wherever the platform put it (HAL_BULK_MEMORY); frames in the
  // bank stay owned by the caller.
  void init(sound::Engine& engine, const engine::Kit& kit, const sound::SampleBank& samples);

  // The hal::AudioCallback body.
  void render(float* left, float* right);

  // Frames rendered so far: the start of the next block. Readable anywhere.
  int64_t position() const;

  // Audition latency (§7.4, T-78): press to render pickup, measured on every
  // audition that carried a press time. Returns false until a new one lands.
  bool take_latency(uint32_t& last_us, uint32_t& worst_us, uint32_t& count);

  TriggerQueue scheduled;
  SpscQueue<Immediate, kImmediateQueueCapacity> immediate;
  SpscQueue<Fired, kFiredQueueCapacity> fired;
  Mailbox<sound::Params> params;

 private:
  int collect(int64_t block_start, sound::Trigger* triggers);

  sound::Engine* engine_;
  std::atomic<uint32_t> blocks_;
  std::atomic<uint32_t> latency_last_us_;
  std::atomic<uint32_t> latency_worst_us_;
  std::atomic<uint32_t> latency_count_;
  uint32_t latency_seen_;
  sound::StereoBlock block_;
};

}  // namespace app
