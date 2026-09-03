#include "ui/leds.h"

namespace ui {

namespace {

constexpr int kEmptyPercent = 8;     // a pad with no steps: dim, still findable in the dark
constexpr int kRestingPercent = 35;  // a pad with steps
constexpr int kHitPercent = 100;
constexpr int kRestGlowPercent = 6;  // a backlit button doing nothing, of the text colour
constexpr int kHalfPercent = 40;     // the section still playing while another is edited

constexpr Rgb kOff{0, 0, 0};

constexpr Rgb scaled(Rgb colour, int percent) { return blend(kOff, colour, percent); }

// §7.2: split, swap, show, play and the sections are backlit; skip lights when
// armed, which the EVT's keypad can do (D-099). Undo and dice never light.
bool backlit(int button) { return button == kSplit || button == kSwap || button == kShow || button == kPlay || button >= kSectionA; }

}  // namespace

void light(const LedModel& model, Leds& out) {
  for (int pad = 0; pad < engine::kTrackCount; ++pad) {
    const int percent = model.hit[pad] ? kHitPercent : engine::is_empty(model.state->tracks[pad]) ? kEmptyPercent : kRestingPercent;
    out.pads[pad] = scaled(kTrackRgb[pad], percent);
  }
  for (int button = 0; button < kButtonCount; ++button) {
    out.buttons[button] = backlit(button) ? scaled(kScreenText, kRestGlowPercent) : kOff;
  }
  if (model.armed != kNoButton && model.armed < kButtonCount) out.buttons[model.armed] = kAccent;
  if (model.roll) out.buttons[kSplit] = kAccent;
  if (model.showing) out.buttons[kShow] = kAccent;
  if (model.transport) out.buttons[kPlay] = kAccent;
  if (model.playing_section != model.current_section) out.buttons[kSectionA + model.playing_section] = scaled(kAccent, kHalfPercent);
  out.buttons[kSectionA + model.current_section] = kAccent;
}

}  // namespace ui
