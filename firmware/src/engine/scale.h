#pragma once

#include <cstdint>

#include "engine/state.h"

// Keys and scales (PRD Appendix B, D-021). Pitches are MIDI note numbers, C4 = 60.
namespace engine {

constexpr int kMaxScaleLength = 7;
constexpr int kChordToneCount = 3;
constexpr int kSemitonesPerOctave = 12;

struct Scale {
  uint8_t length;
  uint8_t semitones[kMaxScaleLength];
};

const Scale& scale_of(Mode mode);

// Degree d of an n-note mode is the (d mod n)-th interval raised by floor(d / n)
// octaves, so degree 7 of a seven-note mode is the root an octave up.
int degree_semitones(Mode mode, int degree);

// MIDI pitch of a scale degree in a MIDI octave: 12 × (octave + 1) + root + degree.
uint8_t pitch_of_degree(Key key, uint8_t octave, int degree);

bool in_scale(Key key, uint8_t midi);

// A chord on degree d: degrees d, d+2, d+4 (stacked thirds; quartal in the
// pentatonic modes). tones[0] is the root.
struct ChordPitches {
  uint8_t tones[kChordToneCount];
};

ChordPitches chord_pitches(Key key, uint8_t octave, int degree);

// Names for the text view and the web player (§9.2). Share codes never use them:
// roots there stay in sharps (D-018).
constexpr int kChordNameCapacity = 4;  // "C#m" and NUL
constexpr int kPitchNameCapacity = 6;  // "c#-1" and NUL

struct ChordName {
  char text[kChordNameCapacity];
};

struct PitchName {
  char text[kPitchNameCapacity];
};

// The chord on `degree`: its root spelled by the key signature, plus `m` when the
// chord is a minor triad (minor third and fifth above the root); every other
// chord, the quartal pentatonic ones included, shows the root alone (D-031).
ChordName chord_name(Key key, int degree);

// A pitch spelled by the key signature of `key` (D-032): seven-note modes use each
// letter once; pentatonic modes spell like their parent; dorian takes the major
// key a whole tone below; a root with two names takes the one with fewer
// accidentals, ties going to flats. Lowercase with the MIDI octave, C4 = 60: `eb5`.
PitchName pitch_name(Key key, uint8_t midi);

}  // namespace engine
