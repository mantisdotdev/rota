#include "engine/edits.h"

#include "engine/share.h"

namespace engine {

namespace {

constexpr int kSpeedLevels = 3;
constexpr int kAltCount = 4;

void copy_pattern(const Track& from, Track& to) {
  to.alt = from.alt;
  to.speed = from.speed;
  to.step_count = from.step_count;
  for (int i = 0; i < from.step_count; ++i) to.steps[i] = from.steps[i];
}

bool matches(const Track& track, const TapTemplate& tap_template) {
  if (track.step_count != tap_template.step_count) return false;
  for (int i = 0; i < track.step_count; ++i) {
    if (track.steps[i] != tap_template.steps[i]) return false;
  }
  return true;
}

// Template progress is read off the steps themselves (D-033): an empty track takes
// the first template; a track equal to template k takes template k + 1; anything
// else, including a track past the last template, appends a hit.
int next_template(const Track& track, const KitPad& pad) {
  if (pad.template_count == 0) return -1;
  if (is_empty(track)) return 0;
  for (int k = 0; k + 1 < pad.template_count; ++k) {
    if (matches(track, pad.templates[k])) return k + 1;
  }
  return -1;
}

int sequence_length(const State& state, Pad pad, const Kit& kit) {
  if (pad == Pad::chord) return kit.progressions[static_cast<int>(state.key.mode)].length;
  if (pad == Pad::pluck) return kit.pluck_sequence.length;
  return 1;
}

// The next melodic position continues from the last hit and wraps at the list
// length; the first hit is position 0 (§8.4, D-020, D-036).
uint8_t next_note(const Track& track, int length) {
  for (int i = track.step_count - 1; i >= 0; --i) {
    if (!is_rest(track.steps[i])) return static_cast<uint8_t>((track.steps[i].note + 1) % length);
  }
  return 0;
}

Tenths nudged(Tenths value, int detents) {
  const int moved = static_cast<int>(value) + detents;
  if (moved < 0) return 0;
  if (moved > kTenthsMax) return kTenthsMax;
  return static_cast<Tenths>(moved);
}

bool same_pattern(const Track& a, const Track& b) {
  if (a.alt != b.alt || a.speed != b.speed || a.step_count != b.step_count) return false;
  for (int i = 0; i < a.step_count; ++i) {
    if (a.steps[i] != b.steps[i]) return false;
  }
  return true;
}

// A dice press fills only empty tracks; it changes something when one of them
// would take a different pattern from the loop (D-038: no change, no undo level).
bool fill_changes_anything(const State& current, const State& loop) {
  for (int i = 0; i < kTrackCount; ++i) {
    if (is_empty(current.tracks[i]) && !same_pattern(current.tracks[i], loop.tracks[i])) return true;
  }
  return false;
}

bool replace_changes_anything(const State& current, const State& loop) {
  for (int i = 0; i < kTrackCount; ++i) {
    if (!same_pattern(current.tracks[i], loop.tracks[i])) return true;
  }
  return false;
}

}  // namespace

void tap(Section& section, Pad pad, const Kit& kit) {
  const State& current = section.state();
  const Track& track = track_of(current, pad);
  if (is_full(track)) return;
  const KitPad& kit_pad = pad_of(kit, pad);
  const int template_index = next_template(track, kit_pad);
  const uint8_t note = next_note(track, sequence_length(current, pad, kit));

  Track& live = track_of(section.push_edit(), pad);
  if (template_index >= 0) {
    const TapTemplate& tap_template = kit_pad.templates[template_index];
    live.step_count = tap_template.step_count;
    for (int i = 0; i < tap_template.step_count; ++i) live.steps[i] = tap_template.steps[i];
    return;
  }
  live.steps[live.step_count] = Step{1, note};
  live.step_count += 1;
}

void remove_last_step(Section& section, Pad pad) {
  if (is_empty(track_of(section.state(), pad))) return;
  track_of(section.push_edit(), pad).step_count -= 1;
}

void split(Section& section, Pad pad) {
  const Track& track = track_of(section.state(), pad);
  if (is_empty(track) || is_rest(track.steps[track.step_count - 1])) return;
  Track& live = track_of(section.push_edit(), pad);
  Step& last = live.steps[live.step_count - 1];
  last.hits = static_cast<uint8_t>(last.hits % kMaxHitsPerStep + 1);
}

void skip(Section& section, Pad pad) {
  if (is_full(track_of(section.state(), pad))) return;
  Track& live = track_of(section.push_edit(), pad);
  live.steps[live.step_count] = Step{0, 0};
  live.step_count += 1;
}

void swap(Section& section, Pad pad) {
  Track& live = track_of(section.push_edit(), pad);
  live.alt = static_cast<Alt>((static_cast<int>(live.alt) + 1) % kAltCount);
}

void adjust_speed(Section& section, Pad pad, int detents) {
  const int current = static_cast<int>(track_of(section.state(), pad).speed);
  int moved = current + detents;
  if (moved < 0) moved = 0;
  if (moved >= kSpeedLevels) moved = kSpeedLevels - 1;
  if (moved == current) return;
  track_of(section.push_edit(), pad).speed = static_cast<Speed>(moved);
}

void adjust_level(State& state, Pad pad, int detents) {
  Track& track = track_of(state, pad);
  track.level = nudged(track.level, detents);
}

void adjust_tone(State& state, Pad pad, int detents) {
  Track& track = track_of(state, pad);
  track.tone = nudged(track.tone, detents);
}

void adjust_send(State& state, Pad pad, int detents) {
  Track& track = track_of(state, pad);
  track.send = nudged(track.send, detents);
}

void adjust_track_chance(State& state, Pad pad, int detents) {
  Track& track = track_of(state, pad);
  track.chance = nudged(track.chance, detents);
}

void dice_fill_empty(Section& section, const Kit& kit, uint32_t roll) {
  if (kit.dice_loop_count == 0) return;
  const Decoded loop = decode(kit.dice_loops[roll % kit.dice_loop_count], kit);
  if (!loop.ok || !fill_changes_anything(section.state(), loop.state)) return;
  State& live = section.push_edit();
  for (int i = 0; i < kTrackCount; ++i) {
    if (is_empty(live.tracks[i])) copy_pattern(loop.state.tracks[i], live.tracks[i]);
  }
}

void dice_replace_all(Section& section, const Kit& kit, uint32_t roll) {
  if (kit.dice_loop_count == 0) return;
  const Decoded loop = decode(kit.dice_loops[roll % kit.dice_loop_count], kit);
  if (!loop.ok || !replace_changes_anything(section.state(), loop.state)) return;
  State& live = section.push_edit();
  for (int i = 0; i < kTrackCount; ++i) copy_pattern(loop.state.tracks[i], live.tracks[i]);
}

void load(Section& section, const State& state) {
  if (section.state() == state) return;  // the same loop again: nothing to undo (D-038)
  section.push_edit() = state;
}

}  // namespace engine
