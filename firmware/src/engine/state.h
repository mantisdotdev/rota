#pragma once

#include <cstdint>

#include "engine/limits.h"

// The pattern model (PRD §6). Pure data: no platform headers, no heap, and all
// time as fractions of one cycle. Seconds, samples and bpm-to-time live in app/.
namespace engine {

// Pad order is track order everywhere: on the surface, on the ring and in the
// share code (share-format §2).
enum class Pad : uint8_t { kick, snare, hat, clap, bass, chord, pluck, rim };

constexpr int index_of(Pad pad) { return static_cast<int>(pad); }
constexpr Pad pad_at(int index) { return static_cast<Pad>(index); }

// Drum tracks gain ghost hits under chance; bass, chord and pluck never do (§6.5).
constexpr bool is_drum(Pad pad) { return pad != Pad::bass && pad != Pad::chord && pad != Pad::pluck; }

enum class Alt : uint8_t { every, a, b, fourth };  // §6.2: every cycle, even, odd, cycle mod 4 == 3
enum class Speed : uint8_t { half, one, two };     // §6.2: 0.5, 1, 2
enum class Mode : uint8_t { minor, major, dorian, pentatonic_minor, pentatonic_major };  // Appendix B

// A value in tenths: 0–10 stands for 0.0–1.0 (§6.2, D-014).
using Tenths = uint8_t;
constexpr Tenths kTenthsMax = 10;

struct Step {
  uint8_t hits;  // 0 = rest; 1–4 = sub-hits inside the step (§6.1)
  uint8_t note;  // 0–7: position in the kit's progression or note sequence (D-020); 0 on drums and bass
};

constexpr bool is_rest(Step step) { return step.hits == 0; }
constexpr bool operator==(Step a, Step b) { return a.hits == b.hits && a.note == b.note; }
constexpr bool operator!=(Step a, Step b) { return !(a == b); }

struct Track {
  Alt alt;
  Speed speed;
  uint8_t step_count;
  Step steps[kMaxStepsPerTrack];
  Tenths level;
  Tenths tone;
  Tenths send;
  Tenths chance;
  bool mute;  // transient: set while the pad is held, never encoded (§6.2)
};

bool operator==(const Track& a, const Track& b);
inline bool operator!=(const Track& a, const Track& b) { return !(a == b); }

struct Key {
  uint8_t root;  // semitones above C: c cs d ds e f fs g gs a as b = 0–11 (Appendix B)
  Mode mode;
};

constexpr bool operator==(Key a, Key b) { return a.root == b.root && a.mode == b.mode; }
constexpr bool operator!=(Key a, Key b) { return !(a == b); }

// One section's full state: eight tracks plus the global parameters (§6.3, §6.8).
struct State {
  char kit[kKitIdLength + 1];
  uint8_t bpm;
  Tenths filter;
  Tenths fx;
  Tenths chance;
  uint8_t swing;  // hundredths (D-011)
  Key key;
  Track tracks[kTrackCount];
  char lineage[kLineageLength + 1];  // id carried by the code this loop was loaded from; empty otherwise
};

bool operator==(const State& a, const State& b);
inline bool operator!=(const State& a, const State& b) { return !(a == b); }

constexpr uint8_t kDefaultBpm = 100;         // §6.3
constexpr Tenths kDefaultLevel = 8;          // §6.2
constexpr Tenths kDefaultTone = kTenthsMax;  // §6.2: open
constexpr Tenths kDefaultTrackChance = kTenthsMax;
constexpr Key kDefaultKey{0, Mode::minor};   // C minor (§6.3)

struct Kit;

// An empty loop with the kit's defaults: its swing, filter, fx and per-pad sends.
State make_state(const Kit& kit);

inline Track& track_of(State& state, Pad pad) { return state.tracks[index_of(pad)]; }
inline const Track& track_of(const State& state, Pad pad) { return state.tracks[index_of(pad)]; }

bool is_empty(const Track& track);
bool is_full(const Track& track);

}  // namespace engine
