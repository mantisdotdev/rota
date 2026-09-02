#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/section.h"
#include "engine/state.h"

// Edits (§6.7, §8.2) on a section. Each one that changes the pattern records one
// undo level; a call that changes nothing (tapping a full track, splitting a track
// with no hit) records nothing (D-038). Commit timing is app/'s job: these apply at once,
// and the scheduler calls them on the beat (D-003).
namespace engine {

// Adds a hit following the kit's smart defaults (§6.6, D-033). Adds nothing when
// the track is full (D-024); the caller reads is_full() for the status line.
void tap(Section& section, Pad pad, const Kit& kit);

// Hold pad + undo: removes the track's last step, hit or rest (§8.2).
void remove_last_step(Section& section, Pad pad);

// The last step's hits cycle 1 → 2 → 3 → 4 → 1 (§8.2). No hit to split: no change (D-042).
void split(Section& section, Pad pad);

// Appends a rest (§8.2).
void skip(Section& section, Pad pad);

// Alternation cycles every → a → b → fourth → every (§8.2).
void swap(Section& section, Pad pad);

// Hold pad + speed knob: 0.5 → 1 → 2 by detents, clamped (§6.2, §8.1).
void adjust_speed(Section& section, Pad pad, int detents);

// Hold pad + volume / filter / fx / chance: one detent is 0.1, clamped to 0–1
// (§6.2, §8.1, D-012). Performance controls, not undoable.
void adjust_level(State& state, Pad pad, int detents);
void adjust_tone(State& state, Pad pad, int detents);
void adjust_send(State& state, Pad pad, int detents);
void adjust_track_chance(State& state, Pad pad, int detents);

// Dice (§8.2, D-028): `roll` is a draw from the app's seeded PRNG; the loop is
// roll mod the kit's loop count. Press fills only the empty tracks; hold replaces
// all eight. Only the loops' tracks are used, never their globals.
void dice_fill_empty(Section& section, const Kit& kit, uint32_t roll);
void dice_replace_all(Section& section, const Kit& kit, uint32_t roll);

// Replaces the section's state, as when a code is loaded (with undo).
void load(Section& section, const State& state);

}  // namespace engine
