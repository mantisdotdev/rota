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
constexpr int kMaxTapTemplates = 4;        // smart-default taps a kit may define per pad; lofi's kick uses four (§6.6)
constexpr int kModeCount = 5;              // minor, major, dorian, pentatonic minor, pentatonic major (Appendix B)
constexpr int kKitIdLength = 12;           // kit id, 1–12 characters (share-format §2)
constexpr int kPadNameLength = 12;         // what a pad is called, as the text view and the status line say it
constexpr int kPadSourceLength = 24;       // a sample pad's wav file, or a synth pad's preset name
constexpr int kLineageLength = 6;          // base36 id of the parent loop (§10.1)

// Speed 2 plays the step list twice per cycle, so hits and ghost slots both double.
constexpr int kMaxEventsPerTrack = kMaxStepsPerTrack * kMaxHitsPerStep * 2 + kMaxStepsPerTrack * 2;
constexpr int kMaxEventsPerCycle = kTrackCount * kMaxEventsPerTrack;

// Share-code buffers, NUL included. A section code is at most 238 characters
// (share-format §6); a song is four section bodies of at most 227, three `;`,
// `RT2S:`, `/`, 64 arrangement letters and a 7-character lineage: 988 (§5).
constexpr int kSectionCodeCapacity = 256;
constexpr int kSongCodeCapacity = 1024;

// The longest code the decoder will look at, NUL not counted: the canonical worst
// case and as much again for the fields a later version may add, which a decoder
// skips (T-16). Every path that carries a code in from outside — a card, a SysEx
// payload, USB text, the player's paste box — is bounded here rather than at each
// of them (T-62, D-106).
constexpr int kMaxSectionCodeInput = 2 * kSectionCodeCapacity;
constexpr int kMaxSongCodeInput = 2 * kSongCodeCapacity;

}  // namespace engine
