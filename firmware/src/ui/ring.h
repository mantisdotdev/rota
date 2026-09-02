#pragma once

#include <cstdint>

#include "engine/fraction.h"
#include "engine/state.h"

// The ring view (PRD §9.1, D-091): eight concentric bands, kick outermost, with
// dividers at step boundaries, dots at hits, the playhead sweeping clockwise from
// twelve o'clock, hits that flash and swell for 250 ms, and the corners: bpm,
// section, song and battery, transient status text. Everything the view needs
// comes in the model; nothing here touches the HAL.
namespace ui {

// A hit that just fired, for the swell: `age` runs 0 (now) to 1 (250 ms ago).
struct Flash {
  engine::Pad pad;
  engine::Fraction time;
  uint8_t sub_index;
  bool is_ghost;
  float age;
};

constexpr int kMaxFlashes = 64;

struct RingModel {
  const engine::State* state;  // the section the player is editing
  uint32_t cycle_index;        // which alternations play this cycle (§6.2)
  engine::Fraction playhead;
  bool playing;
  int bpm;
  char section;
  int song;
  int battery;
  const char* status;  // transient, or nullptr
  const char* footer;  // shown while no status is, or nullptr
  const char* armed;   // "split", "swap", "skip" while armed, or nullptr
  const Flash* flashes;
  int flash_count;
};

void draw_ring(uint16_t* framebuffer, const RingModel& model);

}  // namespace ui
