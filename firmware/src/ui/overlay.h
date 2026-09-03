#pragma once

#include <cstdint>

#include "ui/draw.h"
#include "ui/font.h"

// The rows every view shares (§9.1, §8.3, §8.5, Appendix D "Overlays"): the top row
// holds the view's state, bpm or title at the left and the armed button in the
// accent; the bottom row is the message row, a knob's value for a second over the
// status for 1.8 s over the footer; during the tutorial the prompt rows sit above
// the message row on a backing. A view lays out between content_bottom() and
// kContentTop, so nothing transient is ever drawn over a view's content (D-098,
// D-091 revisited).
namespace ui {

constexpr int kTopRow = kMargin;                             // 6
constexpr int kContentTop = kTopRow + kLineHeight;           // 20: where a view's rows may start
constexpr int kBottomRow = kHeight - kMargin - kLineHeight;  // 220: the message row's y
constexpr int kOverlayGap = 2;
constexpr int kMaxPromptLines = 3;
constexpr int kPromptPad = 2;
constexpr int kBpmColumns = 3;
constexpr int kArmedAfterBpm = kMargin + (kBpmColumns + 1) * kGlyphAdvance;  // 54: the ring's marker follows the bpm

// The lowest y a view may draw to while `prompt_lines` show: above the message row,
// or above the prompt box.
constexpr int content_bottom(int prompt_lines) {
  return prompt_lines == 0 ? kBottomRow - kOverlayGap
                           : kBottomRow - kOverlayGap - (prompt_lines * kLineHeight + 2 * kPromptPad) - kOverlayGap;
}
constexpr int kContentBottom = content_bottom(0);                // 218
constexpr int kContentBottomMin = content_bottom(kMaxPromptLines);  // 170: what a static layout must fit above

struct Overlay {
  const char* status;         // or nullptr
  const char* knob;           // "filter 0.9", or nullptr; takes the message row over the status
  const char* footer;         // shown while neither is, in the legend colour
  const char* armed;          // "split", "swap", "skip", or nullptr
  int armed_x;                // where the marker starts in the top row
  const char* const* prompt;  // tutorial lines, or nullptr
  int prompt_count;           // at most kMaxPromptLines
};

void draw_overlay(uint16_t* framebuffer, const Overlay& overlay);

}  // namespace ui
