#pragma once

#include <cstdint>

#include "engine/fraction.h"
#include "engine/kit.h"
#include "engine/limits.h"
#include "engine/state.h"

namespace engine {

// One scheduled hit or ghost (§6.4). Pitches are resolved here, not in sound/ (D-041).
struct Event {
  Pad track;
  Fraction time;           // within the cycle, [0, 1)
  uint8_t note;            // MIDI pitch: the bass root, the pluck note, the chord root; 0 on drum tracks
  uint8_t chord_upper[2];  // chord track only: the pitches of degrees d+2 and d+4 (D-021); 0 elsewhere
  float velocity;
  uint8_t sub_index;       // which sub-hit of its step; 0 for a ghost
  bool is_ghost;
};

struct EventList {
  int count;
  Event items[kMaxEventsPerCycle];
};

// The engine's one output (§6.4): every event of one cycle, sorted by time. Chance
// and humanize draw from a stream fixed by (seed, cycle_index), so the same call
// always answers the same (D-034); the caller keeps one seed per session.
void events(const State& state, const Kit& kit, uint32_t cycle_index, uint32_t seed, EventList& out);

// p = global chance × track chance (§6.5).
float effective_chance(const State& state, Pad pad);

// Velocity (§6.4): a hit lands uniformly in 0.9–1.0 (base 0.95 with ±5% humanize);
// sub-hits after the first are × 0.8 and ghosts × 0.55 of their humanized value.
constexpr float kMinHitVelocity = 0.9f;
constexpr float kMaxHitVelocity = 1.0f;
constexpr float kSubHitScale = 0.8f;
constexpr float kGhostScale = 0.55f;

// Chance (§6.5): a hit drops with probability 0.6p; a drum step gains a ghost
// with probability 0.3p; chords are never dropped when p < 0.5.
constexpr float kDropShare = 0.6f;
constexpr float kGhostShare = 0.3f;
constexpr float kChordDropThreshold = 0.5f;

// Swing (§6.3): events on odd eighth positions move later by swing × 1/24.
constexpr int kEighthsPerCycle = 8;
constexpr int kSwingDenominator = 24;

}  // namespace engine
