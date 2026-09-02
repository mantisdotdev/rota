// Tapping, templates, split and skip: spec/scenarios.md T-01–T-07, T-14, T-32, T-33, T-38.
#include "engine_support.h"

using namespace support;

TEST_CASE("T-01 Tap kick once") {
  Section section = fresh_section();
  tap(section, Pad::kick, lofi());
  CHECK(steps_text(section.state(), Pad::kick) == "0");
  CHECK(hit_times(events_of(section.state()), Pad::kick) == "0");
}

TEST_CASE("T-02 Tap kick three times") {
  Section section = fresh_section();
  taps(section, Pad::kick, 3);
  CHECK(steps_text(section.state(), Pad::kick) == "000");
  CHECK(track_of(section.state(), Pad::kick).step_count == 3);  // three dividers on the ring
  CHECK(hit_times(events_of(section.state()), Pad::kick) == "0 1/3 2/3");
}

TEST_CASE("T-03 Tap snare once (default kit)") {
  Section section = fresh_section();
  tap(section, Pad::snare, lofi());
  CHECK(steps_text(section.state(), Pad::snare) == ".0");
  CHECK(hit_times(events_of(section.state()), Pad::snare) == "1/2");
}

TEST_CASE("T-04 Tap snare twice") {
  Section section = fresh_section();
  taps(section, Pad::snare, 2);
  CHECK(steps_text(section.state(), Pad::snare) == ".0.0");
  CHECK(hit_times(events_of(section.state()), Pad::snare) == "1/4 3/4");
}

TEST_CASE("T-05 Kick x4, snare x2, hat x2") {
  Section section = fresh_section();
  taps(section, Pad::kick, 4);
  taps(section, Pad::snare, 2);
  tap(section, Pad::hat, lofi());
  CHECK(steps_text(section.state(), Pad::hat) == "00");
  tap(section, Pad::hat, lofi());
  CHECK(steps_text(section.state(), Pad::hat) == "0000");
  const EventList list = events_of(section.state());
  CHECK(hit_times(list, Pad::kick) == "0 1/4 1/2 3/4");
  CHECK(hit_times(list, Pad::snare) == "1/4 3/4");
  CHECK(hit_times(list, Pad::hat) == "0 1/4 1/2 3/4");
}

TEST_CASE("T-06 Hat x1 (template hh hh) then split on hat") {
  Section section = fresh_section();
  tap(section, Pad::hat, lofi());
  split(section, Pad::hat);
  CHECK(steps_text(section.state(), Pad::hat) == "08");
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 1/2 3/4");
}

TEST_CASE("T-07 Split pressed with no pad tap for 5 s") {
  // Engine half only: split on a pad with no steps changes nothing and records no
  // undo level. The 5 s disarm and the "add a hit first" status are app/ and ui/.
  Section section = fresh_section();
  const State before = section.state();
  split(section, Pad::kick);
  CHECK(section.state() == before);
  CHECK(section.undo_levels() == 0);
}

TEST_CASE("T-14 Skip on kick with bd bd") {
  Section section = fresh_section();
  taps(section, Pad::kick, 2);
  skip(section, Pad::kick);
  CHECK(steps_text(section.state(), Pad::kick) == "00.");
  CHECK(hit_times(events_of(section.state()), Pad::kick) == "0 1/3");
}

TEST_CASE("T-32 Hat x1, x2, x3") {
  Section section = fresh_section();
  tap(section, Pad::hat, lofi());
  CHECK(steps_text(section.state(), Pad::hat) == "00");
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 1/2");
  tap(section, Pad::hat, lofi());
  CHECK(steps_text(section.state(), Pad::hat) == "0000");
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 1/4 1/2 3/4");
  tap(section, Pad::hat, lofi());
  CHECK(steps_text(section.state(), Pad::hat) == "00000");
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 1/5 2/5 3/5 4/5");
}

TEST_CASE("T-33 Clap x1, x2, x3") {
  Section section = fresh_section();
  tap(section, Pad::clap, lofi());
  CHECK(steps_text(section.state(), Pad::clap) == ".0");
  CHECK(hit_times(events_of(section.state()), Pad::clap) == "1/2");
  tap(section, Pad::clap, lofi());
  CHECK(steps_text(section.state(), Pad::clap) == ".0.0");
  CHECK(hit_times(events_of(section.state()), Pad::clap) == "1/4 3/4");
  tap(section, Pad::clap, lofi());
  CHECK(steps_text(section.state(), Pad::clap) == ".0.00");
  CHECK(hit_times(events_of(section.state()), Pad::clap) == "1/5 3/5 4/5");
}

TEST_CASE("T-38 Kick with 16 steps, tap kick again") {
  Section section = fresh_section();
  taps(section, Pad::kick, 16);
  REQUIRE(track_of(section.state(), Pad::kick).step_count == 16);
  CHECK(is_full(track_of(section.state(), Pad::kick)));  // the app reads this for "kick is full"
  const std::string before = hit_times(events_of(section.state()), Pad::kick);
  const int levels = section.undo_levels();
  tap(section, Pad::kick, lofi());
  CHECK(track_of(section.state(), Pad::kick).step_count == 16);
  CHECK(hit_times(events_of(section.state()), Pad::kick) == before);
  CHECK(section.undo_levels() == levels);  // nothing to undo: the tap added nothing
}
