#pragma once

#include <cstdint>

// The palette of Appendix D, as RGB for the LEDs and RGB565 for the screen.
namespace ui {

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

constexpr uint16_t rgb565(Rgb c) {
  return static_cast<uint16_t>(((c.red >> 3) << 11) | ((c.green >> 2) << 5) | (c.blue >> 3));
}

// `percent` of `over` on top of `under`.
constexpr Rgb blend(Rgb under, Rgb over, int percent) {
  return Rgb{static_cast<uint8_t>((under.red * (100 - percent) + over.red * percent) / 100),
             static_cast<uint8_t>((under.green * (100 - percent) + over.green * percent) / 100),
             static_cast<uint8_t>((under.blue * (100 - percent) + over.blue * percent) / 100)};
}

constexpr Rgb kScreenBackground{0x15, 0x13, 0x0F};
constexpr Rgb kScreenText{0xF1, 0xE9, 0xD6};
constexpr Rgb kAccent{0xF2, 0x6B, 0x1D};
constexpr Rgb kLegend{0x6B, 0x66, 0x5C};
constexpr Rgb kWhite{0xFF, 0xFF, 0xFF};

// Kick to rim.
constexpr Rgb kTrackRgb[8] = {
    {0xF2, 0x6B, 0x1D}, {0xF5, 0xB3, 0x2B}, {0x8F, 0xD3, 0xB0}, {0xF0, 0xA3, 0xB8},
    {0x6C, 0x9B, 0xE8}, {0xB7, 0x9B, 0xEB}, {0x5F, 0xD6, 0xCC}, {0xE3, 0xDC, 0xC8},
};

}  // namespace ui
