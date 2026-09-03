// The scripted input harness (spec/scenarios.md T-17, T-18, T-39, T-40, T-78, T-79):
// the app under the fake HAL, run frame by frame. The audio clock is the master:
// the world advances in 128-frame blocks, the scheduler's timer fires every 2 ms
// of that clock, and every input carries the time it was "pressed".
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "allocation_counter.h"
#include "app/app.h"
#include "app/scheduler.h"
#include "doctest/doctest.h"
#include "engine/fraction.h"
#include "engine/state.h"
#include "hal_fake.h"
#include "sound/limits.h"

namespace app_support {

using engine::Pad;
using hal::Button;
using hal::Encoder;

constexpr int64_t kBlock = sound::kBlockSize;
constexpr int64_t kCycleFrames = 115200;  // 240 / 100 bpm seconds at 48 kHz
constexpr int64_t kBeatFrames = kCycleFrames / 4;
constexpr int64_t kSecond = sound::kSampleRate;

inline uint64_t us_of(int64_t frames) { return static_cast<uint64_t>(frames) * 1000000 / sound::kSampleRate; }

struct World {
  int64_t frames = 0;        // audio frames rendered
  int64_t timer_at = 0;      // the frame the timer next fires on
  int64_t timer_frames = 0;  // its period in frames
  int64_t origin = 0;        // where the transport last started: cycle 0, fraction 0
  std::vector<app::Fired> fired;
  uint32_t seen = 0;
  uint64_t audio_allocations = 0;
  uint64_t timer_allocations = 0;
  float last_peak = 0.0f;  // the loudest sample of the last block rendered

  // The tutorial has run unless a test asks for a first boot (§8.5, T-22).
  explicit World(bool first_run = false) {
    hal_fake::reset();
    if (!first_run) hal::write_file(app::kTutorialDoneFile, &app::kTutorialRan, 1);
    const sound::SampleBank silent{};
    app::init(silent);
    timer_frames = static_cast<int64_t>(hal_fake::timer_period_us()) * sound::kSampleRate / 1000000;
    REQUIRE(timer_frames > 0);  // a period under 21 us would never advance the world
    REQUIRE(hal_fake::audio_callback() != nullptr);
    REQUIRE(hal_fake::timer_callback() != nullptr);
  }

  void collect() {
    const app::FiredLog& log = app::fired_log();
    while (seen < log.total) fired.push_back(log.at(seen++));
  }

  // Runs the world up to `target` frames, interleaving the timer and the audio blocks.
  void run_until(int64_t target) {
    float left[kBlock];
    float right[kBlock];
    while (frames < target) {
      if (timer_at <= frames) {
        hal_fake::set_time_us(us_of(timer_at));
        const uint64_t before = allocation_counter::count();
        hal_fake::timer_callback()();
        timer_allocations += allocation_counter::count() - before;
        timer_at += timer_frames;
      } else {
        hal_fake::set_time_us(us_of(frames));
        const uint64_t before = allocation_counter::count();
        hal_fake::audio_callback()(left, right);
        audio_allocations += allocation_counter::count() - before;
        last_peak = 0.0f;
        for (int i = 0; i < kBlock; ++i) last_peak = std::max(last_peak, std::max(std::fabs(left[i]), std::fabs(right[i])));
        frames += kBlock;
      }
      app::tick();
      collect();
    }
  }

  void run_for(int64_t count) { run_until(frames + count); }

  // At least one frame is drawn: 20 ms of world time at 60 fps (§7.3).
  void frame() { run_for(kSecond / 50); }

  // A stalled timer thread: no tick until the first period boundary at or after
  // `frame`. Returns when that tick will fire.
  int64_t skip_timer_until(int64_t frame) {
    timer_at = (frame + timer_frames - 1) / timer_frames * timer_frames;
    return timer_at;
  }

  void input(hal::InputKind kind, int index, int detents = 0) {
    hal_fake::set_time_us(us_of(frames));
    hal_fake::push(hal::InputEvent{kind, static_cast<uint8_t>(index), static_cast<int8_t>(detents), us_of(frames)});
    app::tick();
    collect();
  }

  void pad_down(Pad pad) { input(hal::InputKind::pad_down, engine::index_of(pad)); }
  void pad_up(Pad pad) { input(hal::InputKind::pad_up, engine::index_of(pad)); }
  void tap(Pad pad, int times = 1) {
    for (int i = 0; i < times; ++i) {
      pad_down(pad);
      pad_up(pad);
    }
  }
  void button_down(Button button) { input(hal::InputKind::button_down, static_cast<int>(button)); }
  void button_up(Button button) { input(hal::InputKind::button_up, static_cast<int>(button)); }
  void press(Button button) {
    button_down(button);
    button_up(button);
  }
  // Holds past the 300 ms threshold so the hold meaning fires (D-085).
  void hold(Button button) {
    button_down(button);
    run_for(kSecond / 2);
    button_up(button);
  }
  void turn(Encoder encoder, int detents) { input(hal::InputKind::encoder_turn, static_cast<int>(encoder), detents); }

  void play() {
    origin = frames + static_cast<int64_t>(app::kStartDelayBlocks) * kBlock;
    press(Button::play);
  }

  int64_t cycle_start(int cycle) const { return origin + cycle * kCycleFrames; }
  int64_t at(int cycle, engine::Fraction fraction) const {
    return cycle_start(cycle) + static_cast<int64_t>(fraction.num) * kCycleFrames / fraction.den;
  }

  // Pattern hits on `pad` inside `cycle`, as fractions of it: "0 1/4 1/2 3/4".
  std::string times_in_cycle(Pad pad, int cycle, bool auditions = false) const {
    std::string out;
    for (const app::Fired& hit : fired) {
      if (hit.event.track != pad || hit.audition != auditions) continue;
      const int64_t within = hit.sample - cycle_start(cycle);
      if (within < 0 || within >= kCycleFrames) continue;
      const engine::Fraction f = engine::reduced(within, kCycleFrames);
      if (!out.empty()) out += " ";
      out += f.den == 1 ? std::to_string(f.num) : std::to_string(f.num) + "/" + std::to_string(f.den);
    }
    return out;
  }

  std::vector<int64_t> audition_samples(Pad pad) const {
    std::vector<int64_t> out;
    for (const app::Fired& hit : fired) {
      if (hit.event.track == pad && hit.audition) out.push_back(hit.sample);
    }
    return out;
  }

  const engine::State& state(int section) const { return app::model().sections[section].state(); }
  const app::Model& model() const { return app::model(); }
  std::string status() const { return app::model().status.text; }
  std::string knob() const { return app::model().knob.text; }
};

}  // namespace app_support
