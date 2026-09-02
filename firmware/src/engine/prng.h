#pragma once

#include <cstdint>

namespace engine {

// The engine's only source of randomness (§6.4, CLAUDE.md Landmines): a seeded
// xorshift32. Chance and humanize draw from it; nothing in engine/ calls rand().
class Prng {
 public:
  explicit Prng(uint32_t seed) : state_(mix(seed)) {
    if (state_ == 0) state_ = kGoldenRatio;  // xorshift never leaves zero
  }

  // The stream for one cycle: the same seed and cycle index always roll the same
  // dice, whatever was asked before (D-034).
  static Prng for_cycle(uint32_t seed, uint32_t cycle_index) { return Prng(mix(seed) ^ mix(cycle_index + 1)); }

  uint32_t next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
  }

  // Uniform in [0, 1) with 24 bits, exact in a float.
  float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

 private:
  static constexpr uint32_t kGoldenRatio = 0x9E3779B9u;

  // splitmix32 finaliser: spreads nearby seeds (42, 43) and cycle indices apart.
  static constexpr uint32_t mix(uint32_t x) {
    x += kGoldenRatio;
    x ^= x >> 16;
    x *= 0x21F0AAADu;
    x ^= x >> 15;
    x *= 0x735A2D97u;
    x ^= x >> 15;
    return x;
  }

  uint32_t state_;
};

}  // namespace engine
