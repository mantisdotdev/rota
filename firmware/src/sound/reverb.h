#pragma once

namespace sound {

// Line lengths of the small reverb (D-079): Freeverb's first four combs and two
// allpasses scaled to 48 kHz; the right channel's lines are kReverbSpread longer.
constexpr int kReverbChannels = 2;
constexpr int kReverbCombCount = 4;
constexpr int kReverbAllpassCount = 2;
constexpr int kReverbSpread = 23;
constexpr int kReverbCombFrames[kReverbCombCount] = {1215, 1293, 1390, 1476};
constexpr int kReverbAllpassFrames[kReverbAllpassCount] = {605, 480};

constexpr int reverb_pool_size() {
  int frames = 0;
  for (int length : kReverbCombFrames) frames += length + kReverbSpread;
  for (int length : kReverbAllpassFrames) frames += length + kReverbSpread;
  return frames * kReverbChannels;
}

// A small reverb (§12 names a plate; D-079): four parallel damped combs into two
// allpasses per channel, mono in, stereo out.
class Reverb {
 public:
  Reverb();
  // Adds the wet signal to left and right.
  void process(const float* in, float* left, float* right, int count);

 private:
  static constexpr float kCombFeedback = 0.78f;
  static constexpr float kDamping = 0.35f;
  static constexpr float kAllpassFeedback = 0.5f;
  static constexpr float kWet = 0.06f;

  struct Line {
    int offset;
    int length;
    int index;
    float store;  // the comb's damping state
  };

  float comb(Line& line, float input);
  float allpass(Line& line, float input);

  Line combs_[kReverbChannels][kReverbCombCount];
  Line allpasses_[kReverbChannels][kReverbAllpassCount];
  float pool_[reverb_pool_size()];
};

}  // namespace sound
