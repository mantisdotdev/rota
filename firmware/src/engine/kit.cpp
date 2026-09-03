#include "engine/kit.h"

#include <cstring>

namespace engine {

namespace {

bool same(const DegreeList& a, const DegreeList& b) {
  return a.length == b.length && std::memcmp(a.degrees, b.degrees, a.length) == 0;
}

bool same(const TapTemplate& a, const TapTemplate& b) {
  if (a.step_count != b.step_count) return false;
  for (int i = 0; i < a.step_count; ++i) {
    if (a.steps[i] != b.steps[i]) return false;
  }
  return true;
}

bool same(const KitPad& a, const KitPad& b) {
  if (std::strcmp(a.name, b.name) != 0 || a.voice != b.voice || std::strcmp(a.source, b.source) != 0) return false;
  if (a.pitch_semitones != b.pitch_semitones || a.start != b.start || a.decay != b.decay) return false;
  if (a.octave != b.octave || a.send != b.send || a.template_count != b.template_count) return false;
  for (int i = 0; i < a.template_count; ++i) {
    if (!same(a.templates[i], b.templates[i])) return false;
  }
  return true;
}

}  // namespace

bool operator==(const Kit& a, const Kit& b) {
  if (std::strcmp(a.id, b.id) != 0) return false;
  for (int i = 0; i < kTrackCount; ++i) {
    if (!same(a.pads[i], b.pads[i])) return false;
  }
  for (int i = 0; i < kModeCount; ++i) {
    if (!same(a.progressions[i], b.progressions[i])) return false;
  }
  if (!same(a.pluck_sequence, b.pluck_sequence)) return false;
  if (a.dice_loop_count != b.dice_loop_count) return false;
  for (int i = 0; i < a.dice_loop_count; ++i) {
    if (std::strcmp(a.dice_loops[i], b.dice_loops[i]) != 0) return false;
  }
  return a.swing_hundredths == b.swing_hundredths && a.filter == b.filter && a.fx == b.fx &&
         a.sidechain.on == b.sidechain.on && a.sidechain.duck_db == b.sidechain.duck_db &&
         a.sidechain.release_ms == b.sidechain.release_ms;
}

}  // namespace engine
