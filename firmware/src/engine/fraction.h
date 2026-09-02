#pragma once

#include <cstdint>
#include <numeric>

namespace engine {

// A point in the cycle as an exact fraction in [0, 1), so 1/3 is 1/3 on the host,
// on the device and in a test (D-040). Denominators stay small: at most 16 steps × 4 hits
// × 2 (speed) combined with 2400 (swing hundredths over 24) is well inside int32.
struct Fraction {
  int32_t num;
  int32_t den;  // always > 0
};

constexpr Fraction reduced(int64_t num, int64_t den) {
  const int64_t divisor = std::gcd(num, den);
  return Fraction{static_cast<int32_t>(num / divisor), static_cast<int32_t>(den / divisor)};
}

constexpr Fraction operator+(Fraction a, Fraction b) {
  return reduced(int64_t{a.num} * b.den + int64_t{b.num} * a.den, int64_t{a.den} * b.den);
}

constexpr Fraction operator-(Fraction a, Fraction b) {
  return reduced(int64_t{a.num} * b.den - int64_t{b.num} * a.den, int64_t{a.den} * b.den);
}

constexpr bool operator==(Fraction a, Fraction b) { return int64_t{a.num} * b.den == int64_t{b.num} * a.den; }
constexpr bool operator!=(Fraction a, Fraction b) { return !(a == b); }
constexpr bool operator<(Fraction a, Fraction b) { return int64_t{a.num} * b.den < int64_t{b.num} * a.den; }
constexpr bool operator>(Fraction a, Fraction b) { return b < a; }
constexpr bool operator<=(Fraction a, Fraction b) { return !(b < a); }
constexpr bool operator>=(Fraction a, Fraction b) { return !(a < b); }

constexpr double to_double(Fraction f) { return static_cast<double>(f.num) / f.den; }

}  // namespace engine
