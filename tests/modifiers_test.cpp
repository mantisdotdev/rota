// Alternation, speed, per-track modifiers and mute: T-08, T-09, T-25–T-27, T-29, T-30, T-35, T-49.
#include "engine_support.h"

using namespace support;

namespace {

// Cycle indices in [0, cycles) on which the pad fires at all: "0 2 4".
std::string cycles_with_events(const State& state, Pad pad, int cycles) {
  std::vector<std::string> parts;
  for (int cycle = 0; cycle < cycles; ++cycle) {
    if (!events_on(events_of(state, static_cast<uint32_t>(cycle)), pad).empty()) {
      parts.push_back(std::to_string(cycle));
    }
  }
  return join(parts);
}

}  // namespace

TEST_CASE("T-08 Swap on clap (a), swap again (b)") {
  Section section = fresh_section();
  tap(section, Pad::clap, lofi());
  swap(section, Pad::clap);
  CHECK(track_of(section.state(), Pad::clap).alt == Alt::a);
  CHECK(cycles_with_events(section.state(), Pad::clap, 6) == "0 2 4");
  swap(section, Pad::clap);
  CHECK(track_of(section.state(), Pad::clap).alt == Alt::b);
  CHECK(cycles_with_events(section.state(), Pad::clap, 6) == "1 3 5");
  swap(section, Pad::clap);
  CHECK(track_of(section.state(), Pad::clap).alt == Alt::fourth);
  CHECK(cycles_with_events(section.state(), Pad::clap, 12) == "3 7 11");
  swap(section, Pad::clap);
  CHECK(track_of(section.state(), Pad::clap).alt == Alt::every);
}

TEST_CASE("T-09 Hold hat + speed knob one detent up") {
  Section section = fresh_section();
  tap(section, Pad::hat, lofi());
  adjust_speed(section, Pad::hat, +1);
  CHECK(track_of(section.state(), Pad::hat).speed == Speed::two);
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 1/4 1/2 3/4");
  CHECK(track_code(section.state(), Pad::hat) == "ed00");
  adjust_speed(section, Pad::hat, +1);
  CHECK(track_of(section.state(), Pad::hat).speed == Speed::two);  // clamped at 2
}

TEST_CASE("T-25 Hold the snare pad (~ sd ~ sd) and press volume down twice") {
  // The audible level and the untouched master volume are sound/ and app/.
  Section section = fresh_section();
  taps(section, Pad::snare, 2);
  CHECK(track_of(section.state(), Pad::snare).level == 8);
  adjust_level(section.state(), Pad::snare, -1);
  adjust_level(section.state(), Pad::snare, -1);
  CHECK(track_of(section.state(), Pad::snare).level == 6);
  CHECK(track_code(section.state(), Pad::snare) == "e1.0.0,6a1a");
  const Decoded reloaded = decode(code_of(section.state()).c_str(), lofi());
  REQUIRE(reloaded.ok);
  CHECK(track_of(reloaded.state, Pad::snare).level == 6);
}

TEST_CASE("T-26 Hold the hat pad and turn the filter knob one detent down") {
  Section section = fresh_section();
  tap(section, Pad::hat, lofi());
  adjust_tone(section.state(), Pad::hat, -1);
  CHECK(track_of(section.state(), Pad::hat).tone == 9);
  CHECK(section.state().filter == 10);
  for (int i = 0; i < kTrackCount; ++i) {
    if (pad_at(i) != Pad::hat) CHECK(section.state().tracks[i].tone == 10);
  }
  CHECK(track_code(section.state(), Pad::hat) == "e100,891a");
}

TEST_CASE("T-27 Hold the clap pad and turn the fx knob one detent up") {
  Section section = fresh_section();
  tap(section, Pad::clap, lofi());
  adjust_send(section.state(), Pad::clap, +1);
  CHECK(track_of(section.state(), Pad::clap).send == 2);
  CHECK(section.state().fx == 2);
  CHECK(track_code(section.state(), Pad::clap) == "e1.0,8a2a");
}

TEST_CASE("T-29 Hat hh hh hh hh, then hold hat + speed knob one detent down") {
  Section section = fresh_section();
  taps(section, Pad::hat, 2);
  adjust_speed(section, Pad::hat, -1);
  CHECK(track_of(section.state(), Pad::hat).speed == Speed::half);
  CHECK(hit_times(events_of(section.state(), 0), Pad::hat) == "0 1/2");
  CHECK(hit_times(events_of(section.state(), 1), Pad::hat) == "0 1/2");
  CHECK(hit_times(events_of(section.state(), 2), Pad::hat) == "0 1/2");
  CHECK(track_code(section.state(), Pad::hat) == "eh0000");
  adjust_speed(section, Pad::hat, -1);
  CHECK(track_of(section.state(), Pad::hat).speed == Speed::half);  // clamped at 0.5
}

TEST_CASE("T-30 Rim x1, then swap three times (every -> a -> b -> fourth)") {
  // The 30% ring opacity is ui/.
  Section section = fresh_section();
  tap(section, Pad::rim, lofi());
  swap(section, Pad::rim);
  swap(section, Pad::rim);
  swap(section, Pad::rim);
  CHECK(track_of(section.state(), Pad::rim).alt == Alt::fourth);
  CHECK(cycles_with_events(section.state(), Pad::rim, 12) == "3 7 11");
  CHECK(hit_times(events_of(section.state(), 3), Pad::rim) == "0");
  CHECK(track_code(section.state(), Pad::rim) == "f10");
}

TEST_CASE("T-35 Hold the kick pad for two cycles, then release (mute)") {
  Section section = fresh_section();
  taps(section, Pad::kick, 4);
  taps(section, Pad::snare, 2);
  taps(section, Pad::hat, 2);
  const std::string code_before = code_of(section.state());
  const std::string cycle0 = events_text(events_of(section.state(), 0));
  const std::string cycle1 = events_text(events_of(section.state(), 1));

  track_of(section.state(), Pad::kick).mute = true;
  for (uint32_t cycle = 0; cycle < 2; ++cycle) {
    const EventList list = events_of(section.state(), cycle);
    CHECK(events_on(list, Pad::kick).empty());
    CHECK(hit_times(list, Pad::snare) == "1/4 3/4");
    CHECK(hit_times(list, Pad::hat) == "0 1/4 1/2 3/4");
  }
  CHECK(code_of(section.state()) == code_before);  // the share code never shows the mute

  track_of(section.state(), Pad::kick).mute = false;
  CHECK(events_text(events_of(section.state(), 0)) == cycle0);
  CHECK(events_text(events_of(section.state(), 1)) == cycle1);
}

TEST_CASE("T-49 Hat hh hh hh at speed 0.5") {
  Section section = fresh_section();
  taps(section, Pad::hat, 2);
  remove_last_step(section, Pad::hat);  // hold hat + undo: `hh hh hh hh` -> `hh hh hh`
  REQUIRE(steps_text(section.state(), Pad::hat) == "000");
  adjust_speed(section, Pad::hat, -1);
  CHECK(hit_times(events_of(section.state(), 0), Pad::hat) == "0 2/3");
  CHECK(hit_times(events_of(section.state(), 1), Pad::hat) == "1/3");
  CHECK(hit_times(events_of(section.state(), 2), Pad::hat) == "0 2/3");
}
