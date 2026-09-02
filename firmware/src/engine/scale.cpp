#include "engine/scale.h"

namespace engine {

namespace {

// Intervals in semitones above the root, in Mode order (Appendix B).
constexpr Scale kScales[kModeCount] = {
    {7, {0, 2, 3, 5, 7, 8, 10}},  // natural minor
    {7, {0, 2, 4, 5, 7, 9, 11}},  // major
    {7, {0, 2, 3, 5, 7, 9, 10}},  // dorian
    {5, {0, 3, 5, 7, 10}},        // pentatonic minor
    {5, {0, 2, 4, 7, 9}},         // pentatonic major
};

}  // namespace

const Scale& scale_of(Mode mode) { return kScales[static_cast<int>(mode)]; }

int degree_semitones(Mode mode, int degree) {
  const Scale& scale = scale_of(mode);
  const int octaves = degree / scale.length;
  const int index = degree % scale.length;
  return scale.semitones[index] + kSemitonesPerOctave * octaves;
}

uint8_t pitch_of_degree(Key key, uint8_t octave, int degree) {
  const int pitch = kSemitonesPerOctave * (octave + 1) + key.root + degree_semitones(key.mode, degree);
  return static_cast<uint8_t>(pitch);
}

bool in_scale(Key key, uint8_t midi) {
  const int above_root = (midi - key.root + kSemitonesPerOctave) % kSemitonesPerOctave;
  const Scale& scale = scale_of(key.mode);
  for (int i = 0; i < scale.length; ++i) {
    if (scale.semitones[i] == above_root) return true;
  }
  return false;
}

ChordPitches chord_pitches(Key key, uint8_t octave, int degree) {
  return ChordPitches{{pitch_of_degree(key, octave, degree), pitch_of_degree(key, octave, degree + 2),
                       pitch_of_degree(key, octave, degree + 4)}};
}

namespace {

constexpr int kLetterCount = 7;
constexpr char kLetters[kLetterCount] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
constexpr int kNaturalSemitones[kLetterCount] = {0, 2, 4, 5, 7, 9, 11};
// The order letters enter a key signature: sharps F C G D A E B, flats B E A D G C F.
constexpr int kSharpOrder[kLetterCount] = {3, 0, 4, 1, 5, 2, 6};
constexpr int kFlatOrder[kLetterCount] = {6, 2, 5, 1, 4, 0, 3};
constexpr int kFifthSemitones = 7;
constexpr int kRelativeMajorOffset = 3;  // a minor key's relative major lies a minor third up
constexpr int kWholeToneBelow = 10;      // dorian is spelled by the major key a whole tone below
constexpr int kMinorThird = 3;
constexpr int kPerfectFifth = 7;
constexpr char kSharpMark = '#';
constexpr char kFlatMark = 'b';
constexpr char kMinorMark = 'm';
constexpr char kLowercaseOffset = 'a' - 'A';
constexpr int kMidiOctaveOffset = 1;  // C4 = 60, so the MIDI octave is midi / 12 − 1

struct Signature {
  int sharps;
  int flats;  // one of the two is always 0
};

// The major key whose signature spells `key` (D-032).
int signature_major_root(Key key) {
  switch (key.mode) {
    case Mode::minor:
    case Mode::pentatonic_minor:
      return (key.root + kRelativeMajorOffset) % kSemitonesPerOctave;
    case Mode::major:
    case Mode::pentatonic_major:
      return key.root;
    case Mode::dorian:
      return (key.root + kWholeToneBelow) % kSemitonesPerOctave;
  }
  return key.root;
}

// A major key n fifths above C carries n sharps, n fifths below C n flats. The
// pitch class n fifths up is 7n mod 12, so a tonic p sits 7p mod 12 fifths up.
// Fewer accidentals win and a tie goes to flats (D-032).
Signature signature_of(Key key) {
  const int major = signature_major_root(key);
  const int sharps = (major * kFifthSemitones) % kSemitonesPerOctave;
  const int flats = (kSemitonesPerOctave - sharps) % kSemitonesPerOctave;
  if (sharps < flats) return Signature{sharps, 0};
  return Signature{0, flats};
}

struct Spelling {
  int letter;      // index into kLetters
  int accidental;  // −1 flat, 0 natural, +1 sharp
};

Spelling spell(int pitch_class, Signature signature) {
  int accidental_of[kLetterCount] = {};
  for (int i = 0; i < signature.sharps; ++i) accidental_of[kSharpOrder[i]] = 1;
  for (int i = 0; i < signature.flats; ++i) accidental_of[kFlatOrder[i]] = -1;
  for (int letter = 0; letter < kLetterCount; ++letter) {
    const int spelled =
        (kNaturalSemitones[letter] + accidental_of[letter] + kSemitonesPerOctave) % kSemitonesPerOctave;
    if (spelled == pitch_class) return Spelling{letter, accidental_of[letter]};
  }
  // Outside the signature's scale, which no key-locked pitch is (§8.4): lean the
  // signature's way. Every pitch class is a natural or one accidental away.
  const int lean = signature.flats > 0 ? -1 : 1;
  for (int letter = 0; letter < kLetterCount; ++letter) {
    const int spelled = (kNaturalSemitones[letter] + lean + kSemitonesPerOctave) % kSemitonesPerOctave;
    if (spelled == pitch_class) return Spelling{letter, lean};
  }
  return Spelling{0, 0};
}

int put_accidental(char* out, int accidental) {
  if (accidental > 0) {
    out[0] = kSharpMark;
    return 1;
  }
  if (accidental < 0) {
    out[0] = kFlatMark;
    return 1;
  }
  return 0;
}

}  // namespace

ChordName chord_name(Key key, int degree) {
  ChordName name{};
  const ChordPitches chord = chord_pitches(key, 0, degree);
  const int third = (chord.tones[1] - chord.tones[0]) % kSemitonesPerOctave;
  const int fifth = (chord.tones[2] - chord.tones[0]) % kSemitonesPerOctave;
  const bool minor_triad = third == kMinorThird && fifth == kPerfectFifth;
  const Spelling spelling = spell(chord.tones[0] % kSemitonesPerOctave, signature_of(key));
  int length = 0;
  name.text[length++] = kLetters[spelling.letter];
  length += put_accidental(name.text + length, spelling.accidental);
  if (minor_triad) name.text[length++] = kMinorMark;
  name.text[length] = '\0';
  return name;
}

PitchName pitch_name(Key key, uint8_t midi) {
  PitchName name{};
  const Spelling spelling = spell(midi % kSemitonesPerOctave, signature_of(key));
  int length = 0;
  name.text[length++] = static_cast<char>(kLetters[spelling.letter] + kLowercaseOffset);
  length += put_accidental(name.text + length, spelling.accidental);
  const int octave = midi / kSemitonesPerOctave - kMidiOctaveOffset;  // −1 to 9
  if (octave < 0) {
    name.text[length++] = '-';
    name.text[length++] = static_cast<char>('0' - octave);
  } else {
    name.text[length++] = static_cast<char>('0' + octave);
  }
  name.text[length] = '\0';
  return name;
}

}  // namespace engine
