#pragma once

#include <cstdint>

#include "ui/draw.h"

// The screen's monospace type (Appendix D): a 5×7 pixel face drawn at twice its
// size, 14 px tall, which meets the 12 px minimum of §8.6. Lowercase letters,
// digits, the capitals of chord names, section letters and code prefixes (A–G,
// M, R, S, T), and the punctuation the views use.
namespace ui {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kFontScale = 2;
constexpr int kGlyphAdvance = (kGlyphWidth + 1) * kFontScale;  // 12 px per character
constexpr int kLineHeight = kGlyphHeight * kFontScale;          // 14 px

int text_width(const char* text);
void draw_text(Canvas& canvas, int x, int y, const char* text, uint16_t colour);

}  // namespace ui
