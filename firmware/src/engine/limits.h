#pragma once

// Fixed sizes from PRD §6 and §12 rule 4. Firmware allocates nothing after init,
// so every container in the engine is sized by these.
namespace engine {

constexpr int kTrackCount = 8;         // one track per pad (§6.1)
constexpr int kMaxStepsPerTrack = 16;  // §6.1, D-013
constexpr int kMaxHitsPerStep = 4;     // sub-hits within one step (§6.1)
constexpr int kSectionCount = 4;       // A–D (§6.8)
constexpr int kSongSlotCount = 8;      // one song per pad, always saved (§6.8, D-030)
constexpr int kUndoDepth = 60;         // per section (§8.2, T-13)
constexpr int kMaxArrangementLength = 64;  // letters A–D per song (§6.8, D-025)
constexpr int kMaxNoteSequenceLength = 8;  // entries in a progression or pluck sequence; positions 0–7 (§10.1, D-020)
constexpr int kMaxDiceLoops = 4;           // starting loops per kit (§8.2, D-028)

}  // namespace engine
