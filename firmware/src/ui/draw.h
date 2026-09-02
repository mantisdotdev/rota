#pragma once

#include <cstdint>

// Pixel primitives on the 320×240 RGB565 framebuffer (§7.3). Integer geometry, no
// anti-aliasing, nothing off the edges is written.
namespace ui {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

struct Canvas {
  uint16_t* pixels;

  void set(int x, int y, uint16_t colour) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    pixels[y * kWidth + x] = colour;
  }
};

void fill(Canvas& canvas, uint16_t colour);
void horizontal_line(Canvas& canvas, int x0, int x1, int y, uint16_t colour);
void fill_circle(Canvas& canvas, int cx, int cy, int radius, uint16_t colour);
void line(Canvas& canvas, int x0, int y0, int x1, int y1, uint16_t colour);
void fill_rect(Canvas& canvas, int x, int y, int width, int height, uint16_t colour);

}  // namespace ui
