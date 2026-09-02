#include "ui/draw.h"

#include <cmath>
#include <cstdlib>

namespace ui {

void fill(Canvas& canvas, uint16_t colour) {
  for (int i = 0; i < kWidth * kHeight; ++i) canvas.pixels[i] = colour;
}

void horizontal_line(Canvas& canvas, int x0, int x1, int y, uint16_t colour) {
  if (y < 0 || y >= kHeight) return;
  if (x0 > x1) {
    const int swap = x0;
    x0 = x1;
    x1 = swap;
  }
  if (x0 < 0) x0 = 0;
  if (x1 >= kWidth) x1 = kWidth - 1;
  uint16_t* row = canvas.pixels + y * kWidth;
  for (int x = x0; x <= x1; ++x) row[x] = colour;
}

void fill_circle(Canvas& canvas, int cx, int cy, int radius, uint16_t colour) {
  if (radius < 0) return;
  for (int dy = -radius; dy <= radius; ++dy) {
    const int dx = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    horizontal_line(canvas, cx - dx, cx + dx, cy + dy, colour);
  }
}

void line(Canvas& canvas, int x0, int y0, int x1, int y1, uint16_t colour) {
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int step_y = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    canvas.set(x0, y0, colour);
    if (x0 == x1 && y0 == y1) return;
    const int doubled = 2 * error;
    if (doubled >= dy) {
      error += dy;
      x0 += step_x;
    }
    if (doubled <= dx) {
      error += dx;
      y0 += step_y;
    }
  }
}

void fill_rect(Canvas& canvas, int x, int y, int width, int height, uint16_t colour) {
  for (int row = y; row < y + height; ++row) horizontal_line(canvas, x, x + width - 1, row, colour);
}

}  // namespace ui
