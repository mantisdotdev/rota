#include "sound/dsp.h"

namespace sound {

SineTable::SineTable() {
  for (int i = 0; i <= kSize; ++i) {
    table_[i] = std::sin(kTwoPi * static_cast<float>(i) / static_cast<float>(kSize));
  }
}

const SineTable kSine;

}  // namespace sound
