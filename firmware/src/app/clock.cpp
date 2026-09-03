#include "app/clock.h"

#include "sound/limits.h"

namespace app {

namespace {
constexpr int kSecondsPerMinute = 60;
}  // namespace

// One beat is 60 / bpm seconds, rounded to a frame; a cycle is four of them (§6.1).
int Clock::beat_frames(int bpm) const { return (sound::kSampleRate * kSecondsPerMinute + bpm / 2) / bpm; }

}  // namespace app
