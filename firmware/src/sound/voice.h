#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "sound/limits.h"

namespace sound {

// One kit sample: 16-bit 48 kHz mono (§12 rule 6), owned by whoever loaded it (io/ on
// the device, the tool on the host). frame_count 0 is silence: a synth pad, or a
// sample the kit could not provide.
struct Sample {
  const int16_t* frames;
  int frame_count;
};

struct SampleBank {
  Sample samples[kTrackCount];  // by pad
};

// A sample cut short, at its decay point or when the next hit on the pad takes over,
// fades over this long so nothing clicks; one played to its end is left alone (D-075).
constexpr int kSampleFadeFrames = kSampleRate * 5 / 1000;

// Plays a kit sample with the pad's pitch (semitones, by resampling), start (fraction
// of the sample) and decay (fraction of what follows the start), Appendix A.
class SampleVoice {
 public:
  SampleVoice();
  void start(const Sample& sample, const engine::KitPad& pad, float gain);
  void release();
  bool active() const { return active_; }
  // What is left to hear, for voice stealing: the gain times the part not yet played.
  float loudness() const;
  // Adds the voice into out[from, to).
  void render(float* out, int from, int to);

 private:
  static constexpr int kFractionBits = 32;  // 32.32 fixed-point frame positions: exact, no drift

  const int16_t* frames_;
  int frame_count_;
  uint64_t position_;
  uint64_t rate_;
  uint64_t end_;
  uint64_t started_at_;
  float gain_;
  bool fades_;  // the end is a cut, so the last kSampleFadeFrames before it fade
  bool active_;
};

}  // namespace sound
