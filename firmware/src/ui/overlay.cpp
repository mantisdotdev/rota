#include "ui/overlay.h"

#include "ui/color.h"

namespace ui {

namespace {

constexpr int kPromptBarWidth = 2;
constexpr int kPromptBarGap = 6;

constexpr uint16_t kBackground = rgb565(kScreenBackground);
constexpr uint16_t kText = rgb565(kScreenText);
constexpr uint16_t kAccent565 = rgb565(kAccent);
constexpr uint16_t kLegend565 = rgb565(kLegend);

// The prompt sits just above the message row on a backing, with an accent bar at
// its left so it reads as an instruction and not as something that happened.
void draw_prompt(Canvas& canvas, const char* const* lines, int count) {
  if (count > kMaxPromptLines) count = kMaxPromptLines;
  int width = 0;
  for (int i = 0; i < count; ++i) {
    const int line_width = text_width(lines[i]);
    if (line_width > width) width = line_width;
  }
  const int height = count * kLineHeight + 2 * kPromptPad;
  const int top = kBottomRow - kOverlayGap - height;
  const int text_left = kMargin + kPromptBarWidth + kPromptBarGap;
  fill_rect(canvas, kMargin, top, text_left - kMargin + width + kPromptPad, height, kBackground);
  fill_rect(canvas, kMargin, top, kPromptBarWidth, height, kAccent565);
  for (int i = 0; i < count; ++i) {
    draw_text(canvas, text_left, top + kPromptPad + i * kLineHeight, lines[i], kText);
  }
}

}  // namespace

void draw_overlay(uint16_t* framebuffer, const Overlay& overlay) {
  Canvas canvas{framebuffer};
  if (overlay.knob != nullptr) {
    draw_text(canvas, kMargin, kBottomRow, overlay.knob, kText);
  } else if (overlay.status != nullptr) {
    draw_text(canvas, kMargin, kBottomRow, overlay.status, kText);
  } else if (overlay.footer != nullptr) {
    draw_text(canvas, kMargin, kBottomRow, overlay.footer, kLegend565);
  }
  if (overlay.armed != nullptr) draw_text(canvas, overlay.armed_x, kTopRow, overlay.armed, kAccent565);
  if (overlay.prompt != nullptr && overlay.prompt_count > 0) draw_prompt(canvas, overlay.prompt, overlay.prompt_count);
}

}  // namespace ui
