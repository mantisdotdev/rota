#include "sound/sidechain.h"

#include <cmath>

#include "sound/limits.h"

namespace sound {

Sidechain::Sidechain() : on_(false), floor_(1.0), recovery_(1.0), gain_(1.0) {}

void Sidechain::set(const engine::Sidechain& settings) {
  on_ = settings.on && settings.duck_db > 0 && settings.release_ms > 0;
  gain_ = 1.0;
  if (!on_) {
    floor_ = 1.0;
    recovery_ = 1.0;
    return;
  }
  const double duck_db = settings.duck_db;
  const double release_frames = static_cast<double>(settings.release_ms) * kSampleRate / 1000.0;
  floor_ = std::pow(10.0, -duck_db / 20.0);
  // Linear in dB: the same ratio every sample, reaching 0 dB exactly at the release time.
  recovery_ = std::pow(10.0, duck_db / (20.0 * release_frames));
}

void Sidechain::duck() {
  if (on_) gain_ = floor_;
}

}  // namespace sound
