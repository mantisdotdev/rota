#include "app/scheduler.h"

#include "app/audition.h"
#include "app/params.h"

namespace app {

namespace {

constexpr int kSecondsPerMinute = 60;

// One beat is 60 / bpm seconds, rounded to a frame; a cycle is four of them (§6.1).
int frames_of_beat(int bpm) { return (sound::kSampleRate * kSecondsPerMinute + bpm / 2) / bpm; }

bool before(engine::Fraction time, int beat_in_cycle) {
  return static_cast<int64_t>(time.num) * kBeatsPerCycle < static_cast<int64_t>(beat_in_cycle) * time.den;
}

engine::State without_mutes(const engine::State& state) {
  engine::State copy = state;
  for (int i = 0; i < engine::kTrackCount; ++i) copy.tracks[i].mute = false;
  return copy;
}

}  // namespace

Scheduler::Scheduler(const engine::Kit& kit)
    : kit_(&kit),
      seed_(0),
      running_(false),
      beat_start_(0),
      beat_frames_(frames_of_beat(engine::kDefaultBpm)),
      beat_in_cycle_(0),
      cycle_index_(0),
      scheduled_until_(0),
      next_roll_(0),
      playing_{},
      list_{},
      next_event_(0) {}

void Scheduler::set_seed(uint32_t seed) { seed_ = seed; }

void Scheduler::start(Model& model, int64_t position, Mailbox<sound::Params>& params) {
  running_ = true;
  const int64_t at = position + static_cast<int64_t>(kStartDelayBlocks) * sound::kBlockSize;
  scheduled_until_ = at;
  begin_beat(model, at, true, params);
}

void Scheduler::stop() { running_ = false; }

void Scheduler::tick(Model& model, int64_t position, TriggerQueue& out, Mailbox<sound::Params>& params) {
  if (!running_) return;
  const int64_t horizon = position + kLookaheadFrames;
  while (scheduled_until_ < horizon) {
    const int64_t beat_end = beat_start_ + beat_frames_;
    if (scheduled_until_ >= beat_end) {
      begin_beat(model, beat_end, false, params);
      continue;
    }
    const int64_t until = beat_end < horizon ? beat_end : horizon;
    if (!push_window(model, until, out)) return;  // the queue is full: the rest waits for the next tick
    scheduled_until_ = until;
  }
}

// A beat boundary: where an edit lands (§6.7). The playing section's live state
// becomes this beat's pattern, its bpm this beat's length, and its events are asked
// for again with the same cycle index, which rolls the same dice (D-034).
void Scheduler::begin_beat(Model& model, int64_t at, bool first, Mailbox<sound::Params>& params) {
  beat_start_ = at;
  next_roll_ = at;
  if (first) {
    beat_in_cycle_ = 0;
    cycle_index_ = 0;
  } else {
    beat_in_cycle_ = (beat_in_cycle_ + 1) % kBeatsPerCycle;
    if (beat_in_cycle_ == 0) cycle_index_ += 1;
  }
  if (beat_in_cycle_ == 0) cross_cycle(model, first);
  const engine::State& live = model.sections[model.playing].state();
  playing_ = without_mutes(live);
  beat_frames_ = frames_of_beat(playing_.bpm);
  engine::events(playing_, *kit_, cycle_index_, seed_, list_);
  next_event_ = 0;
  while (next_event_ < list_.count && before(list_.items[next_event_].time, beat_in_cycle_)) ++next_event_;
  params.publish(params_of(live, *kit_, model.master_volume));
}

// A cycle boundary: where sections switch and the song steps (§6.8, T-17, T-39).
void Scheduler::cross_cycle(Model& model, bool first) {
  if (model.song_start_pending) {
    model.song_start_pending = false;
    model.song_mode = model.arrangement.length > 0;
    model.song_position = 0;
    model.pending_section = kNoSection;
  } else if (model.song_mode && !first) {
    model.song_position = (model.song_position + 1) % model.arrangement.length;
  }
  if (model.song_mode) {
    model.playing = model.arrangement.letters[model.song_position] - 'A';
    model.current = model.playing;
  } else if (model.pending_section != kNoSection) {
    model.playing = model.pending_section;
    model.pending_section = kNoSection;
  }
}

// Hands over every hit with a sample in [scheduled_until_, until): the pattern's,
// skipping tracks whose pad is held (§8.1, mute), then the roll's. false when the
// queue refused one; next_event_ and next_roll_ then point at it for the retry.
bool Scheduler::push_window(const Model& model, int64_t until, TriggerQueue& out) {
  const engine::State& held = model.sections[model.current].state();
  while (next_event_ < list_.count) {
    const engine::Event& event = list_.items[next_event_];
    const int64_t sample = sample_of(event.time);
    if (sample >= until) break;
    if (!engine::track_of(held, event.track).mute) {
      if (!out.push(ScheduledTrigger{sample, event})) return false;
    }
    ++next_event_;
  }
  const int64_t roll_step = beat_frames_ / kRollsPerBeat;
  while (next_roll_ < until) {
    if (model.roll) {
      const uint8_t root = chord_root_at(next_roll_);
      for (int i = 0; i < engine::kTrackCount; ++i) {
        if (!held.tracks[i].mute) continue;
        const engine::Event event = audition(held, *kit_, engine::pad_at(i), root, fraction_of(next_roll_));
        if (!out.push(ScheduledTrigger{next_roll_, event})) return false;
      }
    }
    next_roll_ += roll_step;
  }
  return true;
}

// The virtual start of this cycle is beat_in_cycle_ beats before the beat; the
// cycle is four of this beat's lengths, so a tempo change lands on the beat too.
int64_t Scheduler::sample_of(engine::Fraction time) const {
  const int64_t cycle_start = beat_start_ - static_cast<int64_t>(beat_in_cycle_) * beat_frames_;
  const int64_t cycle_frames = static_cast<int64_t>(kBeatsPerCycle) * beat_frames_;
  return cycle_start + (static_cast<int64_t>(time.num) * cycle_frames) / time.den;
}

engine::Fraction Scheduler::fraction_of(int64_t sample) const {
  const int64_t cycle_start = beat_start_ - static_cast<int64_t>(beat_in_cycle_) * beat_frames_;
  const int64_t cycle_frames = static_cast<int64_t>(kBeatsPerCycle) * beat_frames_;
  int64_t within = sample - cycle_start;
  if (within < 0) within = 0;
  if (within >= cycle_frames) within = cycle_frames - 1;
  return engine::reduced(within, cycle_frames);
}

engine::Fraction Scheduler::playhead(int64_t position) const {
  if (!running_ || position < beat_start_) return engine::Fraction{0, 1};
  return fraction_of(position);
}

uint8_t Scheduler::chord_root_at(int64_t position) const {
  if (!running_) return 0;
  const engine::Fraction now = playhead(position);
  uint8_t before_now = 0;
  uint8_t last = 0;
  for (int i = 0; i < list_.count; ++i) {
    const engine::Event& event = list_.items[i];
    if (event.track != engine::Pad::chord || event.is_ghost) continue;
    last = event.note;
    if (event.time <= now) before_now = event.note;
  }
  return before_now != 0 ? before_now : last;  // wrapping round, as the bass does (D-037)
}

}  // namespace app
