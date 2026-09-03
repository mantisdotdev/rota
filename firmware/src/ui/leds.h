#pragma once

#include "engine/state.h"
#include "ui/color.h"

// The lights (§7.2, §8.2, §8.6, D-099): a pad glows in its track's colour, faintly
// with no steps, more with steps, fully for a moment after a hit; a backlit button
// rests at a faint warm glow and lights in the accent for its state. Pure: the app
// hands the result to the HAL.
namespace ui {

// hal::Button's order: split, swap, skip, undo, dice, show, play, A, B, C, D.
// ui/ may not include hal/ (§12 rule 1), so app/ asserts these against hal::Button.
constexpr int kButtonCount = 11;
constexpr int kSplit = 0;
constexpr int kSwap = 1;
constexpr int kShow = 5;
constexpr int kPlay = 6;
constexpr int kSectionA = 7;
constexpr int kNoButton = -1;

struct LedModel {
  const engine::State* state;
  bool hit[engine::kTrackCount];  // fired within the last 100 ms
  int armed;                      // a button index, or kNoButton
  bool roll;                      // split held
  bool showing;                   // a view other than the ring
  bool transport;
  bool tap_tempo;                 // play's hold is waiting for the four taps (§8.2, D-102)
  int current_section;
  int playing_section;
};

struct Leds {
  Rgb pads[engine::kTrackCount];
  Rgb buttons[kButtonCount];
};

void light(const LedModel& model, Leds& out);

}  // namespace ui
