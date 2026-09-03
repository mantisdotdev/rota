#pragma once

#include "engine/kit.h"
#include "sound/voice.h"

// A kit, off the card (PRD §7.5, §12 rule 6, D-108, D-109). A kit lives in
// `kits/<id>/` on the microSD: `kit.txt` says what the pads are and how they behave,
// and the WAVs beside it are the sounds. tools/kit_builder.py writes both from the
// kit.json a kit is authored in.
namespace io {

// Reads `kits/<id>/kit.txt` into `kit`. False when there is no such kit or the file
// does not say what a kit is, which is logged; `kit` is then unspecified and the
// caller keeps whatever it had.
bool load_kit(const char* id, engine::Kit& kit);

// Reads every sample pad's WAV into the memory hal::sample_memory() gives, and fills
// `bank` with what was read. A pad whose file is missing or is not a sample this
// firmware can play is left silent and logged, so one bad file costs one sound rather
// than the whole kit. False when the board has nowhere to put samples at all.
bool load_samples(const engine::Kit& kit, sound::SampleBank& bank);

}  // namespace io
