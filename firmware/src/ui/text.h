#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/state.h"
#include "ui/overlay.h"

// The text view (§9.2, D-094): one line per track with steps, in a mini-notation
// that reads like Strudel, wrapped to the screen's 25 columns with the pad name
// in the first seven. text_lines() is the formatting on its own, so a test can
// read it as text; draw_text_view() draws it, the name in the track's colour.
namespace ui {

constexpr int kTextColumns = 25;     // 25 × 12 px = 300 px between the margins
constexpr int kTextNameColumns = 7;  // "snare  ": the longest pad name and two spaces
constexpr int kTextTop = kContentTop;
constexpr int kTextMaxLines = (kContentBottom - kTextTop) / kLineHeight;  // 14 rows between the overlay's rows
constexpr int kNoTrack = -1;

struct TextLines {
  int count;
  char text[kTextMaxLines][kTextColumns + 1];
  int8_t track[kTextMaxLines];  // the pad a line starts, or kNoTrack for a continuation
};

// At most `max_lines` rows (kTextMaxLines or fewer while the prompt rows show).
void text_lines(const engine::State& state, const engine::Kit& kit, TextLines& out, int max_lines = kTextMaxLines);
void draw_text_view(uint16_t* framebuffer, const engine::State& state, const engine::Kit& kit, int bottom);

}  // namespace ui
