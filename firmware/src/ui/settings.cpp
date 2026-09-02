#include "ui/settings.h"

#include <cstdio>
#include <cstring>

#include "engine/scale.h"
#include "ui/color.h"
#include "ui/font.h"

namespace ui {

namespace {

constexpr int kValueCapacity = 20;
constexpr uint8_t kMiddleC = 60;  // the octave does not matter: the name drops it

const char* const kLabel[kSettingsRowCount] = {"key",      "scale",   "swing",          "kit",     "brightness",
                                               "sleep",    "midi clock in", "midi clock out", "sync in", "sync out",
                                               "firmware", "run tutorial",  "factory reset"};
const char* const kModeName[engine::kModeCount] = {"minor", "major", "dorian", "pentatonic minor", "pentatonic major"};
const char* const kOn = "on";
const char* const kOff = "off";
const char* const kNever = "off";
const char* const kPlay = "play";
const char* const kHoldPlay = "hold play";
const char* const kTitle = "settings";

constexpr uint16_t kBackground = rgb565(kScreenBackground);
constexpr uint16_t kText = rgb565(kScreenText);
constexpr uint16_t kAccent565 = rgb565(kAccent);
constexpr uint16_t kLegend565 = rgb565(kLegend);

// The root spelled by the key signature (D-032), without the octave: `c#`, `eb`.
void root_name(engine::Key key, char* out, int capacity) {
  const engine::PitchName name = engine::pitch_name(key, static_cast<uint8_t>(kMiddleC + key.root));
  int length = 0;
  for (const char* c = name.text; *c != '\0' && length + 1 < capacity; ++c) {
    if (*c >= '0' && *c <= '9') break;
    out[length++] = *c;
  }
  out[length] = '\0';
}

}  // namespace

void settings_value(const SettingsModel& model, SettingsRow row, char* out, int capacity) {
  switch (row) {
    case SettingsRow::key:
      root_name(model.key, out, capacity);
      return;
    case SettingsRow::scale:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", kModeName[static_cast<int>(model.key.mode)]);
      return;
    case SettingsRow::swing:
      std::snprintf(out, static_cast<size_t>(capacity), "%d.%02d", model.swing / 100, model.swing % 100);
      return;
    case SettingsRow::kit:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", model.kit);
      return;
    case SettingsRow::brightness:
      std::snprintf(out, static_cast<size_t>(capacity), "%d%%", model.brightness);
      return;
    case SettingsRow::sleep:
      if (model.sleep_minutes == 0) {
        std::snprintf(out, static_cast<size_t>(capacity), "%s", kNever);
      } else {
        std::snprintf(out, static_cast<size_t>(capacity), "%d min", model.sleep_minutes);
      }
      return;
    case SettingsRow::midi_clock_in:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", model.midi_clock_in ? kOn : kOff);
      return;
    case SettingsRow::midi_clock_out:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", model.midi_clock_out ? kOn : kOff);
      return;
    case SettingsRow::sync_in:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", model.sync_in ? kOn : kOff);
      return;
    case SettingsRow::sync_out:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", model.sync_out ? kOn : kOff);
      return;
    case SettingsRow::firmware:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", model.firmware);
      return;
    case SettingsRow::run_tutorial:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", kPlay);
      return;
    case SettingsRow::factory_reset:
      std::snprintf(out, static_cast<size_t>(capacity), "%s", kHoldPlay);
      return;
  }
  out[0] = '\0';
}

void draw_settings_view(uint16_t* framebuffer, const SettingsModel& model) {
  Canvas canvas{framebuffer};
  fill(canvas, kBackground);
  draw_text(canvas, kMargin, kSettingsTitleTop, kTitle, kText);
  char value[kValueCapacity];
  for (int i = 0; i < kSettingsRowCount; ++i) {
    const int y = kSettingsRowsTop + i * kLineHeight;
    const bool selected = i == model.cursor;
    settings_value(model, static_cast<SettingsRow>(i), value, kValueCapacity);
    draw_text(canvas, kMargin, y, kLabel[i], selected ? kAccent565 : kLegend565);
    draw_text(canvas, kWidth - kMargin - text_width(value), y, value, selected ? kAccent565 : kText);
  }
}

}  // namespace ui
