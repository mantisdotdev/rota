#include "ui/ring.h"

#include <cmath>
#include <cstdio>

#include "engine/limits.h"
#include "ui/color.h"
#include "ui/draw.h"
#include "ui/font.h"

namespace ui {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr int kCentreX = kWidth / 2;
constexpr int kCentreY = kHeight / 2;
constexpr int kOuterRadius = 110;
constexpr int kBandWidth = 10;
constexpr int kInnerRadius = kOuterRadius - kBandWidth * engine::kTrackCount;  // 30: the hole
constexpr int kHitRadius = 3;
constexpr int kSubHitRadius = 2;
constexpr int kGhostRadius = 1;
constexpr int kSwell = 3;  // how far a flash grows at its start
constexpr int kPlayheadOverhang = 4;
constexpr int kMargin = 6;
constexpr int kInactivePercent = 30;   // §9.1: alternations that do not play this cycle
constexpr int kDividerPercent = 30;
constexpr int kBandEvenPercent = 3;
constexpr int kBandOddPercent = 7;
constexpr int kBatteryTextCapacity = 16;

constexpr uint16_t kBackground = rgb565(kScreenBackground);
constexpr uint16_t kText = rgb565(kScreenText);
constexpr uint16_t kAccent565 = rgb565(kAccent);
constexpr uint16_t kLegend565 = rgb565(kLegend);
constexpr uint16_t kBandEven = rgb565(blend(kScreenBackground, kScreenText, kBandEvenPercent));
constexpr uint16_t kBandOdd = rgb565(blend(kScreenBackground, kScreenText, kBandOddPercent));

struct Point {
  int x;
  int y;
};

// Twelve o'clock is fraction 0; the ring runs clockwise.
Point on_ring(float fraction, float radius) {
  const float angle = fraction * kTwoPi;
  return Point{kCentreX + static_cast<int>(std::lround(radius * std::sin(angle))),
               kCentreY - static_cast<int>(std::lround(radius * std::cos(angle)))};
}

int band_outer(int track) { return kOuterRadius - track * kBandWidth; }
int band_middle(int track) { return band_outer(track) - kBandWidth / 2; }

// §6.2: every cycle, even cycles, odd cycles, cycle index mod 4 == 3.
bool plays_this_cycle(engine::Alt alt, uint32_t cycle) {
  switch (alt) {
    case engine::Alt::every:
      return true;
    case engine::Alt::a:
      return cycle % 2 == 0;
    case engine::Alt::b:
      return cycle % 2 == 1;
    case engine::Alt::fourth:
      return cycle % 4 == 3;
  }
  return true;
}

void draw_bands(Canvas& canvas) {
  for (int track = 0; track < engine::kTrackCount; ++track) {
    fill_circle(canvas, kCentreX, kCentreY, band_outer(track), track % 2 == 0 ? kBandEven : kBandOdd);
  }
  fill_circle(canvas, kCentreX, kCentreY, kInnerRadius, kBackground);
}

void draw_divider(Canvas& canvas, int track, float fraction, uint16_t colour) {
  const Point inner = on_ring(fraction, static_cast<float>(band_outer(track) - kBandWidth + 1));
  const Point outer = on_ring(fraction, static_cast<float>(band_outer(track) - 1));
  line(canvas, inner.x, inner.y, outer.x, outer.y, colour);
}

void draw_dot(Canvas& canvas, int track, float fraction, int radius, uint16_t colour) {
  const Point at = on_ring(fraction, static_cast<float>(band_middle(track)));
  fill_circle(canvas, at.x, at.y, radius, colour);
}

// The steps of one track as they lie on this cycle (§6.1, §6.2): the whole list
// once at speed 1, twice at speed 2, and one half of it at speed 0.5, even cycles
// the first half (D-022).
void draw_track(Canvas& canvas, const engine::Track& track, int index, uint32_t cycle) {
  if (engine::is_empty(track)) return;
  const bool active = plays_this_cycle(track.alt, cycle) && !track.mute;
  const Rgb colour = kTrackRgb[index];
  const uint16_t dot = rgb565(active ? colour : blend(kScreenBackground, colour, kInactivePercent));
  const uint16_t divider = rgb565(blend(kScreenBackground, colour, active ? kDividerPercent : kDividerPercent / 2));
  const int passes = track.speed == engine::Speed::two ? 2 : 1;
  const float stretch = track.speed == engine::Speed::half ? 2.0f : 1.0f;
  const float offset = track.speed == engine::Speed::half && cycle % 2 == 1 ? 1.0f : 0.0f;
  const float step_width = stretch / (static_cast<float>(track.step_count) * static_cast<float>(passes));
  for (int pass = 0; pass < passes; ++pass) {
    for (int i = 0; i < track.step_count; ++i) {
      const float start = (static_cast<float>(pass) / static_cast<float>(passes)) +
                          static_cast<float>(i) * step_width - offset;
      if (start < 0.0f || start >= 1.0f) continue;
      draw_divider(canvas, index, start, divider);
      const engine::Step step = track.steps[i];
      if (engine::is_rest(step)) continue;
      for (int k = 0; k < step.hits; ++k) {
        const float at = start + static_cast<float>(k) * step_width / static_cast<float>(step.hits);
        draw_dot(canvas, index, at, k == 0 ? kHitRadius : kSubHitRadius, dot);
      }
    }
  }
}

// A hit that just fired swells and brightens, then settles back (§9.1).
void draw_flash(Canvas& canvas, const Flash& flash) {
  const int track = engine::index_of(flash.pad);
  const float remaining = 1.0f - flash.age;
  const int base = flash.is_ghost ? kGhostRadius : flash.sub_index == 0 ? kHitRadius : kSubHitRadius;
  const int radius = base + static_cast<int>(std::lround(kSwell * remaining));
  const Rgb colour = blend(kTrackRgb[track], kWhite, static_cast<int>(60.0f * remaining));
  draw_dot(canvas, track, static_cast<float>(engine::to_double(flash.time)), radius, rgb565(colour));
}

void draw_playhead(Canvas& canvas, float fraction) {
  const Point inner = on_ring(fraction, static_cast<float>(kInnerRadius - 2));
  const Point outer = on_ring(fraction, static_cast<float>(kOuterRadius + kPlayheadOverhang));
  line(canvas, inner.x, inner.y, outer.x, outer.y, kAccent565);
  fill_circle(canvas, outer.x, outer.y, 2, kAccent565);
}

void draw_corners(Canvas& canvas, const RingModel& model) {
  char text[kBatteryTextCapacity];
  std::snprintf(text, sizeof text, "%d", model.bpm);
  draw_text(canvas, kMargin, kMargin, text, kText);
  std::snprintf(text, sizeof text, "%c %d %d%%", model.section, model.song, model.battery);
  draw_text(canvas, kWidth - kMargin - text_width(text), kMargin, text, kText);
  const int bottom = kHeight - kMargin - kLineHeight;
  if (model.status != nullptr) {
    draw_text(canvas, kMargin, bottom, model.status, kText);
  } else if (model.footer != nullptr) {
    draw_text(canvas, kMargin, bottom, model.footer, kLegend565);
  }
  if (model.armed != nullptr) {
    draw_text(canvas, kWidth - kMargin - text_width(model.armed), bottom, model.armed, kAccent565);
  }
}

}  // namespace

void draw_ring(uint16_t* framebuffer, const RingModel& model) {
  Canvas canvas{framebuffer};
  fill(canvas, kBackground);
  draw_bands(canvas);
  for (int track = 0; track < engine::kTrackCount; ++track) {
    draw_track(canvas, model.state->tracks[track], track, model.cycle_index);
  }
  for (int i = 0; i < model.flash_count; ++i) draw_flash(canvas, model.flashes[i]);
  draw_playhead(canvas, static_cast<float>(engine::to_double(model.playhead)));
  draw_corners(canvas, model);
}

}  // namespace ui
