#pragma once

#include <cstdint>

#include "engine/limits.h"
#include "engine/state.h"

// A kit definition (PRD §12 rule 6, Appendix A). Kits are data: spec/kits/<id>.json
// is the source and tools/kit_builder.py generates engine/kits/<id>.h from it. The
// engine reads sends, templates, progressions, the pluck sequence, dice loops and
// the swing/filter/fx defaults; the voice fields are for sound/.
//
// Every string here is a fixed array rather than a pointer, so a kit read off the
// card owns its own words and is the same type as the one compiled in (D-109).
namespace engine {

enum class Voice : uint8_t { sample, synth };

// Steps a pad takes on its first taps (§6.6): tap k applies template k − 1.
struct TapTemplate {
  uint8_t step_count;
  Step steps[kMaxStepsPerTrack];
};

struct KitPad {
  char name[kPadNameLength + 1];
  Voice voice;
  char source[kPadSourceLength + 1];  // sample pads: the wav file; synth pads: the preset name
  int8_t pitch_semitones;  // sample pads
  float start;             // sample pads: start point as a fraction of the sample
  float decay;             // sample pads: 1.0 = full length
  uint8_t octave;          // synth pads: MIDI octave of the root, C4 = 60
  Tenths send;             // default per-track fx send (§6.2)
  uint8_t template_count;
  TapTemplate templates[kMaxTapTemplates];
};

// A chord progression or note sequence as scale degrees of the current mode
// (Appendix B, D-020, D-021). 1–8 entries; melodic steps store positions in it.
struct DegreeList {
  uint8_t length;
  uint8_t degrees[kMaxNoteSequenceLength];
};

// Each kick event ducks bass, chord and pluck (§12 sound spec); the kit sets the depth.
struct Sidechain {
  bool on;
  uint8_t duck_db;
  uint16_t release_ms;
};

struct Kit {
  char id[kKitIdLength + 1];
  KitPad pads[kTrackCount];
  DegreeList progressions[kModeCount];  // indexed by Mode
  DegreeList pluck_sequence;
  uint8_t dice_loop_count;
  char dice_loops[kMaxDiceLoops][kSectionCodeCapacity];  // share codes; only their tracks are used (D-028)
  uint8_t swing_hundredths;
  Tenths filter;
  Tenths fx;
  Sidechain sidechain;
};

inline const KitPad& pad_of(const Kit& kit, Pad pad) { return kit.pads[index_of(pad)]; }

// The scale degree a melodic step's position selects; a position past the end
// wraps (D-020, T-47).
inline int chord_degree(const Kit& kit, Mode mode, uint8_t position) {
  const DegreeList& progression = kit.progressions[static_cast<int>(mode)];
  return progression.degrees[position % progression.length];
}

inline int pluck_degree(const Kit& kit, uint8_t position) {
  return kit.pluck_sequence.degrees[position % kit.pluck_sequence.length];
}

}  // namespace engine
