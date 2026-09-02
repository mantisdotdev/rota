#include "sound/reverb.h"

#include "sound/dsp.h"

namespace sound {

Reverb::Reverb() : combs_{}, allpasses_{}, pool_{} {
  int offset = 0;
  for (int channel = 0; channel < kReverbChannels; ++channel) {
    const int spread = channel == 0 ? 0 : kReverbSpread;
    for (int c = 0; c < kReverbCombCount; ++c) {
      combs_[channel][c] = Line{offset, kReverbCombFrames[c] + spread, 0, 0.0f};
      offset += kReverbCombFrames[c] + kReverbSpread;
    }
    for (int a = 0; a < kReverbAllpassCount; ++a) {
      allpasses_[channel][a] = Line{offset, kReverbAllpassFrames[a] + spread, 0, 0.0f};
      offset += kReverbAllpassFrames[a] + kReverbSpread;
    }
  }
}

float Reverb::comb(Line& line, float input) {
  float& slot = pool_[line.offset + line.index];
  const float output = slot;
  line.store = output * (1.0f - kDamping) + line.store * kDamping;
  slot = input + line.store * kCombFeedback;
  if (++line.index >= line.length) line.index = 0;
  return output;
}

float Reverb::allpass(Line& line, float input) {
  float& slot = pool_[line.offset + line.index];
  const float delayed = slot;
  slot = input + delayed * kAllpassFeedback;
  if (++line.index >= line.length) line.index = 0;
  return delayed - input;
}

void Reverb::process(const float* in, float* left, float* right, int count) {
  float* outputs[kReverbChannels] = {left, right};
  for (int i = 0; i < count; ++i) {
    const float input = in[i] + kAntiDenormal;
    for (int channel = 0; channel < kReverbChannels; ++channel) {
      float sum = 0.0f;
      for (int c = 0; c < kReverbCombCount; ++c) sum += comb(combs_[channel][c], input);
      for (int a = 0; a < kReverbAllpassCount; ++a) sum = allpass(allpasses_[channel][a], sum);
      outputs[channel][i] += sum * kWet;
    }
  }
}

}  // namespace sound
