#pragma once

#include "engine/limits.h"

// Fixed rates and sizes of the sound engine (PRD §7.4, §7.5, §12 rule 4). Nothing in
// sound/ allocates after init, so every buffer is sized from here.
namespace sound {

constexpr int kSampleRate = 48000;  // §7.4
constexpr int kBlockSize = 128;     // samples per render call (D-074)
constexpr int kVoiceCount = 16;     // §7.5 polyphony; T-12 measures it on hardware
constexpr int kTrackCount = engine::kTrackCount;

// Tempo range (§6.3). The delay locks to a dotted eighth: 3/16 of a cycle of 240 / bpm
// seconds, which is 45 / bpm seconds, longest at the slowest tempo (0.75 s at 60 bpm).
constexpr int kMinBpm = 60;
constexpr int kMaxBpm = 180;
constexpr int kDottedEighthSecondsTimesBpm = 45;
constexpr int kMaxDelayFrames = kSampleRate * kDottedEighthSecondsTimesBpm / kMinBpm;  // 36000

}  // namespace sound
