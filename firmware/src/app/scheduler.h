#pragma once

#include <cstdint>

#include "app/audio_path.h"
#include "app/model.h"
#include "engine/events.h"
#include "engine/kit.h"
#include "sound/limits.h"

// The clock (D-084). The audio callback's frame counter is the master clock; every
// timer tick the scheduler looks kLookaheadFrames ahead of it and hands every hit
// due in that window to the audio side with its exact sample. It works a beat at a
// time: at each beat boundary it snapshots the playing section (that is where an
// edit commits, §6.7, D-003), and at each cycle boundary it moves sections and
// steps the song (§6.8). Time inside a cycle stays a Fraction until it becomes a
// sample here.
namespace app {

constexpr uint32_t kTimerPeriodUs = 2000;
constexpr int kLookaheadFrames = sound::kSampleRate / 100;  // 10 ms: the window an edit, a mute or a knob cannot reach
constexpr int kBeatsPerCycle = 4;                           // §6.7: a beat is a quarter cycle
constexpr int kRollsPerBeat = 4;                            // §8.2: the roll retriggers at 1/16 cycle
constexpr int kStartDelayBlocks = 2;                        // the first beat begins this far after play

class Scheduler {
 public:
  explicit Scheduler(const engine::Kit& kit);

  // The session's seed for chance and humanize (D-034); set once at init.
  void set_seed(uint32_t seed);

  // Transport: the first beat begins kStartDelayBlocks after the audio side's
  // position. Both moves the generation on, so a stop drops the hits already
  // handed over and a start plays only its own (T-82).
  void start(Model& model, AudioPath& audio);
  void stop(AudioPath& audio);
  bool running() const { return running_; }

  // From the timer, under hal::lock(): hands the audio side every hit due before
  // its position plus the lookahead.
  void tick(Model& model, AudioPath& audio);

  // Where the playhead is at `position`, as a fraction of the cycle the audio is
  // in; 0 when stopped. The scheduler itself runs up to the lookahead ahead of the
  // audio, so just after it crosses into a cycle the audio is still in the last one.
  engine::Fraction playhead(int64_t position) const;
  uint32_t cycle_index() const { return cycle_index_; }

  // The MIDI root of the chord sounding at the playhead, for the bass audition;
  // 0 when no chord plays this cycle.
  uint8_t chord_root_at(int64_t position) const;

 private:
  void begin_beat(Model& model, int64_t at, bool first, AudioPath& audio);
  void cross_cycle(Model& model, bool first);
  bool push_window(const Model& model, int64_t until, TriggerQueue& out);
  int64_t sample_of(engine::Fraction time) const;
  engine::Fraction fraction_of(int64_t sample) const;

  const engine::Kit* kit_;
  uint32_t seed_;
  uint32_t generation_;
  bool running_;
  int64_t beat_start_;
  int beat_frames_;
  int beat_in_cycle_;
  uint32_t cycle_index_;
  int64_t previous_cycle_start_;   // the cycle before this one, for the audio still inside it
  int64_t previous_cycle_frames_;  // 0 until a cycle has been crossed
  int64_t scheduled_until_;  // every hit before this sample has been handed over
  int64_t next_roll_;        // the next 1/16 grid point the roll has not covered
  int next_roll_track_;      // the first pad of that grid point not yet handed over
  engine::State playing_;    // the beat's snapshot, mutes cleared
  engine::EventList list_;   // this cycle's events from that snapshot
  int next_event_;           // first item of list_ not yet handed over
};

}  // namespace app
