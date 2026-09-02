#include "engine/events.h"

#include <algorithm>

#include "engine/prng.h"
#include "engine/scale.h"

namespace engine {

namespace {

constexpr int kSwingHundredths = 100;
constexpr int kPlacementsPerHit = 2;  // speed 2 plays the list twice per cycle

bool plays_this_cycle(Alt alt, uint32_t cycle) {
  switch (alt) {
    case Alt::every:
      return true;
    case Alt::a:
      return cycle % 2 == 0;
    case Alt::b:
      return cycle % 2 == 1;
    case Alt::fourth:
      return cycle % 4 == 3;
  }
  return true;
}

// Maps a point of one pass of the step list, in [0, 1), onto this cycle (§6.2,
// D-022): speed 1 as is; speed 2 twice, a half cycle apart; speed 0.5 stretched over
// a cycle pair, even cycles taking the first half and odd cycles the second.
int place(Speed speed, uint32_t cycle, Fraction list_time, Fraction out[kPlacementsPerHit]) {
  switch (speed) {
    case Speed::one:
      out[0] = list_time;
      return 1;
    case Speed::two:
      out[0] = reduced(list_time.num, int64_t{list_time.den} * 2);
      out[1] = out[0] + Fraction{1, 2};
      return 2;
    case Speed::half: {
      const Fraction pair = reduced(int64_t{list_time.num} * 2, list_time.den);
      const bool first_cycle_of_pair = pair < Fraction{1, 1};
      if ((cycle % 2 == 0) != first_cycle_of_pair) return 0;
      out[0] = first_cycle_of_pair ? pair : pair - Fraction{1, 1};
      return 1;
    }
  }
  return 0;
}

// Swing (§6.3): a point on an odd eighth moves later by swing × 1/24.
Fraction swung(Fraction time, uint8_t swing_hundredths) {
  const int64_t eighths_num = int64_t{time.num} * kEighthsPerCycle;
  if (eighths_num % time.den != 0) return time;
  const int64_t eighth = eighths_num / time.den;
  if (eighth % 2 == 0) return time;
  return time + Fraction{swing_hundredths, kSwingHundredths * kSwingDenominator};
}

float humanized(Prng& rng) { return kMinHitVelocity + rng.unit() * (kMaxHitVelocity - kMinHitVelocity); }

// The chord step sounding at a point of this cycle, as a degree of the mode: the
// step whose span holds the point, or, on a rest, the last chord hit before it
// (wrapping round the cycle); the key root when the chord track has no hit (D-037).
int active_chord_degree(const State& state, const Kit& kit, uint32_t cycle, Fraction time) {
  const Track& chord = track_of(state, Pad::chord);
  if (is_empty(chord)) return 0;
  Fraction list_time = time;
  if (chord.speed == Speed::two) {
    const Fraction doubled = reduced(int64_t{time.num} * 2, time.den);
    list_time = doubled < Fraction{1, 1} ? doubled : doubled - Fraction{1, 1};
  } else if (chord.speed == Speed::half) {
    const Fraction pair = cycle % 2 == 0 ? time : time + Fraction{1, 1};
    list_time = reduced(pair.num, int64_t{pair.den} * 2);
  }
  const int n = chord.step_count;
  int index = static_cast<int>((int64_t{list_time.num} * n) / list_time.den);
  for (int tried = 0; tried < n; ++tried) {
    const Step step = chord.steps[(index - tried + n) % n];
    if (!is_rest(step)) return chord_degree(kit, state.key.mode, step.note);
  }
  return 0;
}

void set_pitches(Event& event, const State& state, const Kit& kit, uint32_t cycle, Step step, Fraction time) {
  const Key key = state.key;
  switch (event.track) {
    case Pad::bass:
      event.note = pitch_of_degree(key, pad_of(kit, Pad::bass).octave, active_chord_degree(state, kit, cycle, time));
      return;
    case Pad::chord: {
      const int degree = chord_degree(kit, key.mode, step.note);
      const ChordPitches chord = chord_pitches(key, pad_of(kit, Pad::chord).octave, degree);
      event.note = chord.tones[0];
      event.chord_upper[0] = chord.tones[1];
      event.chord_upper[1] = chord.tones[2];
      return;
    }
    case Pad::pluck:
      event.note = pitch_of_degree(key, pad_of(kit, Pad::pluck).octave, pluck_degree(kit, step.note));
      return;
    default:
      return;  // drums carry note 0
  }
}

void emit(EventList& out, const Event& event) {
  if (out.count < kMaxEventsPerCycle) out.items[out.count++] = event;
}

bool earlier(const Event& a, const Event& b) {
  if (a.time != b.time) return a.time < b.time;
  if (a.track != b.track) return index_of(a.track) < index_of(b.track);
  if (a.sub_index != b.sub_index) return a.sub_index < b.sub_index;
  return !a.is_ghost && b.is_ghost;
}

}  // namespace

float effective_chance(const State& state, Pad pad) {
  return static_cast<float>(state.chance) * static_cast<float>(track_of(state, pad).chance) /
         static_cast<float>(kTenthsMax * kTenthsMax);
}

void events(const State& state, const Kit& kit, uint32_t cycle_index, uint32_t seed, EventList& out) {
  out.count = 0;
  Prng rng = Prng::for_cycle(seed, cycle_index);
  for (int t = 0; t < kTrackCount; ++t) {
    const Pad pad = pad_at(t);
    const Track& track = state.tracks[t];
    if (is_empty(track) || track.mute || !plays_this_cycle(track.alt, cycle_index)) continue;

    const float p = effective_chance(state, pad);
    const bool chord_protected = pad == Pad::chord && p < kChordDropThreshold;
    const float drop_probability = chord_protected ? 0.0f : kDropShare * p;
    const float ghost_probability = kGhostShare * p;
    const int n = track.step_count;
    Fraction placed[kPlacementsPerHit];

    for (int i = 0; i < n; ++i) {
      const Step step = track.steps[i];
      for (int k = 0; k < step.hits; ++k) {
        const Fraction list_time = reduced(int64_t{i} * step.hits + k, int64_t{n} * step.hits);
        const int placements = place(track.speed, cycle_index, list_time, placed);
        for (int r = 0; r < placements; ++r) {
          // Every hit draws the same numbers whatever the chance, so chance 0 restores
          // the authored velocities too (§6.5).
          const float velocity = humanized(rng) * (k == 0 ? 1.0f : kSubHitScale);
          const bool dropped = rng.unit() < drop_probability;
          if (dropped) continue;
          Event event{};
          event.track = pad;
          event.time = swung(placed[r], state.swing);
          event.velocity = velocity;
          event.sub_index = static_cast<uint8_t>(k);
          event.is_ghost = false;
          set_pitches(event, state, kit, cycle_index, step, placed[r]);
          emit(out, event);
        }
      }
      if (!is_drum(pad)) continue;  // melodic tracks never gain ghosts (§6.5)
      const Fraction midpoint = reduced(int64_t{i} * 2 + 1, int64_t{n} * 2);
      const int placements = place(track.speed, cycle_index, midpoint, placed);
      for (int r = 0; r < placements; ++r) {
        const float velocity = humanized(rng) * kGhostScale;
        const bool ghost = rng.unit() < ghost_probability;
        if (!ghost) continue;
        Event event{};
        event.track = pad;
        event.time = swung(placed[r], state.swing);
        event.velocity = velocity;
        event.sub_index = 0;
        event.is_ghost = true;
        emit(out, event);
      }
    }
  }
  std::sort(out.items, out.items + out.count, earlier);
}

}  // namespace engine
