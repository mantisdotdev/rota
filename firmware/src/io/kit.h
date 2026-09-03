#pragma once

#include "engine/kit.h"
#include "sound/voice.h"

// A kit's samples, off the card (PRD §7.5, §12 rule 6). The kit itself is still the
// one compiled in; this is the half that makes it audible on the device, where the
// WAVs are `kits/<kit id>/<the pad's source>` on the microSD.
namespace io {

// Reads every sample pad's WAV into the memory hal::sample_memory() gives, and fills
// `bank` with what was read. A pad whose file is missing or is not a sample this
// firmware can play is left silent and logged, so one bad file costs one sound rather
// than the whole kit. False when the board has nowhere to put samples at all.
bool load_samples(const engine::Kit& kit, sound::SampleBank& bank);

}  // namespace io
