#pragma once

#include <cstdint>

#include "engine/state.h"
#include "ui/draw.h"
#include "ui/overlay.h"

// The settings view (§9.4, D-096): the rows of §9.4 with their values, the
// selected row in the accent. Which row is selected and what a turn does to it is
// the app's (app/controller.cpp); the row order here is the contract between them.
namespace ui {

enum class SettingsRow : uint8_t {
  key,
  scale,
  swing,
  kit,
  brightness,
  sleep,
  midi_clock_in,
  midi_clock_out,
  sync_in,
  sync_out,
  firmware,
  run_tutorial,
  factory_reset,
};
constexpr int kSettingsRowCount = 13;
constexpr int kSettingsTitleTop = kTopRow;
constexpr int kSettingsRowsTop = kContentTop;
static_assert(kSettingsRowsTop + kSettingsRowCount * kLineHeight <= kContentBottom, "the rows must end above the message row");
constexpr const char* kSettingsHint = "speed picks, filter sets";  // the footer while nothing else shows

struct SettingsModel {
  engine::Key key;
  uint8_t swing;  // hundredths
  const char* kit;
  int brightness;     // percent
  int sleep_minutes;  // 0 = never
  bool midi_clock_in;
  bool midi_clock_out;
  bool sync_in;
  bool sync_out;
  const char* firmware;
  int cursor;  // the selected row
};

// The row's value as the view prints it, for the app's status lines and the tests.
void settings_value(const SettingsModel& model, SettingsRow row, char* out, int capacity);

void draw_settings_view(uint16_t* framebuffer, const SettingsModel& model);

}  // namespace ui
