#include "app/audition.h"

#include "engine/edits.h"
#include "engine/scale.h"

namespace app {

namespace {

constexpr float kAuditionVelocity = 1.0f;

uint8_t bass_pitch(const engine::State& state, const engine::KitPad& pad, uint8_t chord_root_midi) {
  if (chord_root_midi == 0) return engine::pitch_of_degree(state.key, pad.octave, 0);
  // The root's pitch class in the bass octave, as events() resolves it (§6.4).
  return static_cast<uint8_t>(engine::kSemitonesPerOctave * (pad.octave + 1) +
                              chord_root_midi % engine::kSemitonesPerOctave);
}

}  // namespace

engine::Event audition(const engine::State& state, const engine::Kit& kit, engine::Pad pad, uint8_t chord_root_midi,
                       engine::Fraction at) {
  engine::Event event{};
  event.track = pad;
  event.time = at;
  event.velocity = kAuditionVelocity;
  event.sub_index = 0;
  event.is_ghost = false;
  const engine::KitPad& kit_pad = engine::pad_of(kit, pad);
  const uint8_t position = engine::next_note_position(state, pad, kit);
  switch (pad) {
    case engine::Pad::chord: {
      const engine::ChordPitches chord =
          engine::chord_pitches(state.key, kit_pad.octave, engine::chord_degree(kit, state.key.mode, position));
      event.note = chord.tones[0];
      event.chord_upper[0] = chord.tones[1];
      event.chord_upper[1] = chord.tones[2];
      break;
    }
    case engine::Pad::pluck:
      event.note = engine::pitch_of_degree(state.key, kit_pad.octave, engine::pluck_degree(kit, position));
      break;
    case engine::Pad::bass:
      event.note = bass_pitch(state, kit_pad, chord_root_midi);
      break;
    default:
      break;
  }
  return event;
}

}  // namespace app
