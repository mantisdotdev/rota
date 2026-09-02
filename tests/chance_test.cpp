// Chance, velocity and swing: T-10, T-11, T-28, T-36, T-37, T-42, T-50.
#include "engine_support.h"

using namespace support;

namespace {

constexpr int kStatisticsCycles = 100;

// T-11's fixture: every drum pad plus the chord, so drops and ghosts have enough
// samples for ±5%: 16 drum hits and 20 drum steps per cycle.
State chance_fixture() {
  Section section = fresh_section();
  taps(section, Pad::kick, 4);
  taps(section, Pad::snare, 2);
  taps(section, Pad::hat, 2);
  taps(section, Pad::clap, 2);
  taps(section, Pad::rim, 4);
  taps(section, Pad::chord, 4);
  return section.state();
}

struct Tally {
  int drum_hits = 0;
  int drum_ghosts = 0;
  int chord_hits = 0;
};

Tally tally(const State& state) {
  Tally t;
  for (int cycle = 0; cycle < kStatisticsCycles; ++cycle) {
    const EventList list = events_of(state, static_cast<uint32_t>(cycle));
    for (int i = 0; i < list.count; ++i) {
      const Event& e = list.items[i];
      if (e.track == Pad::chord) {
        t.chord_hits += e.is_ghost ? 0 : 1;
      } else if (is_drum(e.track)) {
        if (e.is_ghost) {
          t.drum_ghosts += 1;
        } else {
          t.drum_hits += 1;
        }
      }
    }
  }
  return t;
}

}  // namespace

TEST_CASE("T-10 Chance at 0 after chance at 1") {
  State state = chance_fixture();
  std::vector<std::string> authored;
  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    REQUIRE(events_of(state, cycle).count == 20);  // 16 drum hits and 4 chords per cycle
    authored.push_back(events_text(events_of(state, cycle)));
  }

  state.chance = 10;
  for (uint32_t cycle = 0; cycle < 4; ++cycle) events_of(state, cycle);  // played with chance at 1
  state.chance = 0;
  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    CHECK(events_text(events_of(state, cycle)) == authored[cycle]);
  }
}

TEST_CASE("T-11 Chance 1, seed 42, 100 cycles") {
  State state = chance_fixture();
  const Tally authored = tally(state);
  REQUIRE(authored.drum_hits == 16 * kStatisticsCycles);
  REQUIRE(authored.drum_ghosts == 0);
  REQUIRE(authored.chord_hits == 4 * kStatisticsCycles);

  state.chance = 10;
  const Tally played = tally(state);
  const double drop_rate = 1.0 - static_cast<double>(played.drum_hits) / authored.drum_hits;
  const double ghost_rate = static_cast<double>(played.drum_ghosts) / (20 * kStatisticsCycles);
  CHECK(drop_rate >= 0.55);
  CHECK(drop_rate <= 0.65);
  CHECK(ghost_rate >= 0.25);
  CHECK(ghost_rate <= 0.35);
  CHECK(played.chord_hits < authored.chord_hits);  // at p = 1 chords do drop

  state.chance = 4;  // p = 0.4 on the chord track: never dropped below 0.5
  CHECK(tally(state).chord_hits == authored.chord_hits);
}

TEST_CASE("T-28 Global chance 1; hold the kick pad and turn the chance knob one detent down") {
  Section section = fresh_section();
  tap(section, Pad::kick, lofi());
  section.state().chance = 10;
  adjust_track_chance(section.state(), Pad::kick, -1);
  CHECK(track_of(section.state(), Pad::kick).chance == 9);
  CHECK(effective_chance(section.state(), Pad::kick) == doctest::Approx(0.9));
  for (int i = 0; i < kTrackCount; ++i) {
    if (pad_at(i) != Pad::kick) CHECK(effective_chance(section.state(), pad_at(i)) == doctest::Approx(1.0));
  }
  CHECK(track_code(section.state(), Pad::kick) == "e10,8a19");
}

TEST_CASE("T-36 Any pattern, chance 0, seed 42") {
  Section section = fresh_section();
  taps(section, Pad::kick, 2);
  split(section, Pad::kick);  // `bd bd*2`: a first sub-hit and a second one
  taps(section, Pad::snare, 2);
  taps(section, Pad::chord, 2);
  const State state = section.state();

  const EventList list = events_of(state, 0, kSeed);
  REQUIRE(list.count > 0);
  int later_sub_hits = 0;
  for (int i = 0; i < list.count; ++i) {
    const Event& e = list.items[i];
    CHECK_FALSE(e.is_ghost);
    if (e.sub_index == 0) {
      CHECK(e.velocity >= 0.9f);
      CHECK(e.velocity <= 1.0f);
    } else {
      later_sub_hits += 1;
      CHECK(e.velocity >= 0.9f * kSubHitScale);
      CHECK(e.velocity <= 1.0f * kSubHitScale);
    }
  }
  CHECK(later_sub_hits == 1);
  CHECK(events_text(events_of(state, 0, kSeed)) == events_text(list));  // same seed, same velocities

  State ghosted = state;
  ghosted.chance = 10;
  int ghosts = 0;
  for (uint32_t cycle = 0; cycle < 50; ++cycle) {
    const EventList played = events_of(ghosted, cycle, kSeed);
    for (int i = 0; i < played.count; ++i) {
      if (!played.items[i].is_ghost) continue;
      ghosts += 1;
      CHECK(played.items[i].velocity >= 0.9f * kGhostScale);
      CHECK(played.items[i].velocity <= 1.0f * kGhostScale);
    }
  }
  CHECK(ghosts > 0);
}

TEST_CASE("T-37 Hat x6 (eight steps, one per eighth), swing 0.15") {
  Section section = fresh_section();
  taps(section, Pad::hat, 6);
  REQUIRE(steps_text(section.state(), Pad::hat) == "00000000");
  REQUIRE(section.state().swing == 15);
  // Odd eighths move by 0.15 × 1/24 = 1/160: 1/8 -> 21/160, 3/8 -> 61/160, and so on.
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 21/160 1/4 61/160 1/2 101/160 3/4 141/160");
  section.state().swing = 0;
  CHECK(hit_times(events_of(section.state()), Pad::hat) == "0 1/8 1/4 3/8 1/2 5/8 3/4 7/8");
}

TEST_CASE("T-42 Kick bd bd, chance 1, seed 42, 100 cycles") {
  Section section = fresh_section();
  taps(section, Pad::kick, 2);
  taps(section, Pad::bass, 2);
  taps(section, Pad::chord, 2);
  taps(section, Pad::pluck, 2);
  State state = section.state();
  state.chance = 10;

  int ghosts = 0;
  for (uint32_t cycle = 0; cycle < kStatisticsCycles; ++cycle) {
    const EventList list = events_of(state, cycle, kSeed);
    for (int i = 0; i < list.count; ++i) {
      const Event& e = list.items[i];
      if (!e.is_ghost) continue;
      CHECK(e.track == Pad::kick);
      const bool at_midpoint = e.time == Fraction{1, 4} || e.time == Fraction{3, 4};
      CHECK(at_midpoint);
      CHECK(e.velocity >= 0.9f * kGhostScale);
      CHECK(e.velocity <= 1.0f * kGhostScale);
      ghosts += 1;
    }
  }
  CHECK(ghosts > 0);
}

TEST_CASE("T-50 Snare ~ sd, chance 1, seed 42, 100 cycles") {
  Section section = fresh_section();
  tap(section, Pad::snare, lofi());
  State state = section.state();
  state.chance = 10;

  int at_rest_midpoint = 0;
  int at_hit_midpoint = 0;
  for (uint32_t cycle = 0; cycle < kStatisticsCycles; ++cycle) {
    const EventList list = events_of(state, cycle, kSeed);
    for (int i = 0; i < list.count; ++i) {
      const Event& e = list.items[i];
      if (!e.is_ghost) continue;
      CHECK(e.track == Pad::snare);
      if (e.time == Fraction{1, 4}) {
        at_rest_midpoint += 1;
      } else if (e.time == Fraction{3, 4}) {
        at_hit_midpoint += 1;
      } else {
        FAIL("ghost at " << fraction_text(e.time));
      }
      CHECK(e.velocity >= 0.9f * kGhostScale);
      CHECK(e.velocity <= 1.0f * kGhostScale);
    }
  }
  CHECK(at_rest_midpoint > 0);
  CHECK(at_hit_midpoint > 0);
}
