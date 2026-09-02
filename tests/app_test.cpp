// The app under the scripted input harness (tests/app_support.h): the scheduler,
// the input grammar and the audio path together, on a fake HAL with a fake clock.
// spec/scenarios.md T-05, T-07, T-17, T-18, T-39, T-40, T-78, T-79, T-80, T-81.
#include <string>

#include "app_support.h"

using namespace app_support;

namespace {

std::string steps_of(const engine::State& state, Pad pad) {
  static const char* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
  const engine::Track& track = engine::track_of(state, pad);
  std::string out;
  for (int i = 0; i < track.step_count; ++i) {
    const engine::Step s = track.steps[i];
    out += engine::is_rest(s) ? '.' : kDigits[(s.hits - 1) * 8 + s.note];
  }
  return out;
}

Button section(char letter) { return static_cast<Button>(static_cast<int>(Button::section_a) + (letter - 'A')); }

}  // namespace

TEST_CASE("T-05 Kick x4, snare x2, hat x2, played by the scheduler at exact samples") {
  World w;
  w.tap(Pad::kick, 4);
  w.tap(Pad::snare, 2);
  w.tap(Pad::hat, 2);
  w.play();
  w.run_until(w.cycle_start(2));
  CHECK(w.audition_samples(Pad::kick).size() == 4);  // every press sounded, in the block after it
  for (int cycle = 0; cycle < 2; ++cycle) {
    CHECK(w.times_in_cycle(Pad::kick, cycle) == "0 1/4 1/2 3/4");
    CHECK(w.times_in_cycle(Pad::snare, cycle) == "1/4 3/4");
    CHECK(w.times_in_cycle(Pad::hat, cycle) == "0 1/4 1/2 3/4");
  }
  CHECK(w.status() == "4 hats, spread evenly");
}

TEST_CASE("T-07 Split pressed with no pad tap for 5 s") {
  World w;
  w.press(Button::split);
  w.run_for(5 * kSecond + 10 * kBlock);
  w.tap(Pad::kick);  // disarmed: a plain tap
  CHECK(steps_of(w.state(0), Pad::kick) == "0");
  CHECK(w.status() == "one kick");

  w.press(Button::split);
  w.tap(Pad::snare);  // armed, but the snare has no steps
  CHECK(w.status() == "add a hit first");
  CHECK(steps_of(w.state(0), Pad::snare) == "");

  w.press(Button::split);
  w.tap(Pad::kick);  // armed and the kick has a hit: it splits
  CHECK(steps_of(w.state(0), Pad::kick) == "8");
  CHECK(w.status() == "kick x2");
}

TEST_CASE("T-17 Switch to empty section B") {
  World w;
  w.tap(Pad::kick, 4);
  w.play();
  w.run_until(w.at(0, engine::Fraction{3, 10}));
  w.press(section('B'));
  CHECK(w.model().current == 1);
  CHECK(w.model().playing == 0);
  CHECK(w.model().pending_section == 1);
  CHECK(w.state(1) == w.state(0));  // B equals A
  CHECK(w.status() == "copied into B");

  // The scheduler moves at the cycle boundary; it looks at most the lookahead ahead of it.
  w.run_until(w.cycle_start(1) - 8 * kBlock);
  CHECK(w.model().playing == 0);
  w.run_until(w.cycle_start(1) + kBlock);
  CHECK(w.model().playing == 1);
  CHECK(w.model().current == 1);
  CHECK(w.model().pending_section == app::kNoSection);

  w.run_until(w.cycle_start(2));
  CHECK(w.times_in_cycle(Pad::kick, 0) == "0 1/4 1/2 3/4");
  CHECK(w.times_in_cycle(Pad::kick, 1) == "0 1/4 1/2 3/4");  // no gap across the switch
}

TEST_CASE("T-18 Edit at cycle fraction 0.30") {
  World w;
  w.tap(Pad::kick, 4);
  w.play();
  w.run_until(w.at(1, engine::Fraction{3, 10}));
  const int64_t pressed = w.frames;
  w.tap(Pad::snare);  // ~ sd: a hit at 1/2
  w.run_until(w.cycle_start(3));

  const std::vector<int64_t> auditions = w.audition_samples(Pad::snare);
  REQUIRE(auditions.size() == 1);
  CHECK(auditions[0] == pressed);  // sounded in the very next block
  CHECK(w.times_in_cycle(Pad::snare, 1) == "1/2");  // landed at 0.50 of the same cycle
  CHECK(w.times_in_cycle(Pad::snare, 2) == "1/2");

  // A hit that would have fallen before the beat waits for the next cycle.
  w.run_until(w.at(3, engine::Fraction{3, 10}));
  w.tap(Pad::hat, 2);  // hh hh hh hh: 0 1/4 1/2 3/4
  w.run_until(w.cycle_start(5));
  CHECK(w.times_in_cycle(Pad::hat, 3) == "1/2 3/4");
  CHECK(w.times_in_cycle(Pad::hat, 4) == "0 1/4 1/2 3/4");
}

TEST_CASE("T-39 Song with arrangement AABABBCD in song play mode") {
  World w;
  w.tap(Pad::kick);  // A: kick at 0
  w.press(section('B'));
  w.tap(Pad::snare);  // B: a copy of A plus ~ sd
  w.press(section('C'));
  w.tap(Pad::hat);  // C: plus hh hh
  w.press(section('D'));
  w.tap(Pad::clap);  // D: plus ~ cp
  w.press(Button::show);
  w.press(Button::show);
  CHECK(w.model().view == app::View::song);
  const std::string arrangement = "AABABBCD";
  for (char letter : arrangement) w.press(section(letter));
  CHECK(std::string(w.model().arrangement.letters, w.model().arrangement.length) == arrangement);
  w.play();  // in the song view: the song from the top

  const std::string order = arrangement + arrangement;
  for (int cycle = 0; cycle < static_cast<int>(order.size()); ++cycle) {
    w.run_until(w.at(cycle, engine::Fraction{1, 2}));
    CHECK(w.model().playing == order[static_cast<size_t>(cycle)] - 'A');
  }
  w.run_until(w.cycle_start(static_cast<int>(order.size())));
  for (int cycle = 0; cycle < static_cast<int>(order.size()); ++cycle) {
    const int playing = order[static_cast<size_t>(cycle)] - 'A';
    CAPTURE(cycle);
    CHECK(w.times_in_cycle(Pad::kick, cycle) == "0");
    CHECK(w.times_in_cycle(Pad::snare, cycle) == (playing >= 1 ? "1/2" : ""));
    CHECK(w.times_in_cycle(Pad::hat, cycle) == (playing >= 2 ? "0 1/2" : ""));  // lands on the boundary itself
    CHECK(w.times_in_cycle(Pad::clap, cycle) == (playing >= 3 ? "1/2" : ""));
  }
}

TEST_CASE("T-40 Sections A and B both non-empty; switch A -> B") {
  World w;
  w.tap(Pad::kick, 4);
  w.press(section('B'));  // stopped: switches at once, copying A
  CHECK(w.model().current == 1);
  CHECK(w.model().playing == 1);
  w.tap(Pad::snare, 2);  // B: kicks and snares at 1/4 3/4
  w.play();
  w.run_until(w.at(1, engine::Fraction{3, 10}));
  w.press(section('A'));
  CHECK(w.status() == "A next");
  w.run_until(w.cycle_start(3));

  CHECK(w.times_in_cycle(Pad::snare, 1) == "1/4 3/4");  // B plays out its cycle
  CHECK(w.times_in_cycle(Pad::snare, 2) == "");         // A from the boundary
  CHECK(w.times_in_cycle(Pad::kick, 2) == "0 1/4 1/2 3/4");
  CHECK(steps_of(w.state(0), Pad::snare) == "");    // A as it was
  CHECK(steps_of(w.state(1), Pad::snare) == ".0.0");  // nothing copied over B

  // Back to B: it plays as it was left.
  w.press(section('B'));
  w.run_until(w.cycle_start(5));
  CHECK(w.times_in_cycle(Pad::snare, 3) == "");
  CHECK(w.times_in_cycle(Pad::snare, 4) == "1/4 3/4");
}

TEST_CASE("T-78 A pad press reaches the audio callback in the next block and the latency is measured") {
  World w;
  w.run_until(10 * kBlock);
  // A press that waited a millisecond in the platform's queue before the main loop saw it.
  hal_fake::set_time_us(us_of(w.frames));
  hal_fake::push(hal::InputEvent{hal::InputKind::pad_down, 0, 0, us_of(w.frames) - 1000});
  app::tick();
  w.pad_up(Pad::kick);
  w.run_for(2 * kBlock);

  const std::vector<int64_t> auditions = w.audition_samples(Pad::kick);
  REQUIRE(auditions.size() == 1);
  CHECK(auditions[0] == 10 * kBlock);
  REQUIRE(!hal_fake::log().empty());
  const std::string line = hal_fake::log().back();
  CHECK(line.find("audition latency: press read to render pickup 1.0 ms measured") == 0);
  CHECK(line.find("128-frame output buffer adds 2.6 ms") != std::string::npos);

  SUBCASE("a release stamped a millisecond before its press, as a millisecond clock can do, is still a tap") {
    hal_fake::push(hal::InputEvent{hal::InputKind::pad_down, 1, 0, us_of(w.frames)});
    hal_fake::push(hal::InputEvent{hal::InputKind::pad_up, 1, 0, us_of(w.frames) - 1000});
    app::tick();
    CHECK(steps_of(w.state(0), Pad::snare) == ".0");
  }
}

TEST_CASE("T-79 The audio callback and the scheduler allocate nothing under the busiest pattern") {
  World w;
  for (int pad = 0; pad < engine::kTrackCount; ++pad) {
    w.tap(engine::pad_at(pad), 4);
    for (int i = 0; i < 3; ++i) {  // the last step's hits 1 -> 2 -> 3 -> 4
      w.press(Button::split);
      w.tap(engine::pad_at(pad));
    }
  }
  w.turn(Encoder::chance, 10);
  w.turn(Encoder::fx, 8);
  CHECK(w.state(0).chance == 10);
  CHECK(w.state(0).fx == 10);
  w.play();
  w.run_until(w.cycle_start(2));
  CHECK(!w.times_in_cycle(Pad::kick, 1).empty());
  CHECK(w.audio_allocations == 0);
  CHECK(w.timer_allocations == 0);
}

TEST_CASE("T-80 A knob turned while a section switch is pending is heard at once") {
  World w;
  w.tap(Pad::kick, 4);
  w.play();
  w.run_until(w.at(0, engine::Fraction{3, 10}));
  w.press(section('B'));  // a copy of A; the switch waits for the cycle boundary
  REQUIRE(w.model().playing == 0);
  REQUIRE(w.model().current == 1);

  w.turn(Encoder::filter, -1);
  CHECK(w.state(0).filter == 9);  // A, still playing, changes now
  CHECK(w.state(1).filter == 9);  // B, waiting, carries the same value
  CHECK(w.status() == "filter 0.9");

  w.pad_down(Pad::hat);
  w.turn(Encoder::volume, -1);
  w.pad_up(Pad::hat);
  CHECK(engine::track_of(w.state(0), Pad::hat).level == 7);
  CHECK(engine::track_of(w.state(1), Pad::hat).level == 7);
  CHECK(w.status() == "hat level 0.7");
  CHECK(w.model().master_volume == app::kDefaultMasterVolume);  // the pad took the volume control

  w.run_until(w.cycle_start(1) + kBlock);
  CHECK(w.model().playing == 1);
  CHECK(w.state(1).filter == 9);  // nothing jumps when B starts

  SUBCASE("with no switch pending only the current section changes") {
    w.press(section('A'));
    w.run_until(w.cycle_start(2) + kBlock);
    REQUIRE(w.model().playing == 0);
    w.turn(Encoder::fx, 1);
    CHECK(w.state(0).fx == 3);
    CHECK(w.state(1).fx == 2);
  }
}

TEST_CASE("T-81 Emptying the arrangement under a playing song ends song play without a stumble") {
  World w;
  w.tap(Pad::kick);  // A: kick at 0; B stays empty
  w.press(Button::show);
  w.press(Button::show);
  for (char letter : std::string("AAB")) w.press(section(letter));
  w.play();
  w.run_until(w.at(1, engine::Fraction{1, 2}));
  REQUIRE(w.model().song_mode);

  SUBCASE("undo removes the letters one by one") {
    w.press(Button::undo);
    w.press(Button::undo);
    CHECK(w.model().arrangement.length == 1);
    CHECK(w.model().song_mode);
    w.press(Button::undo);  // the last letter goes
    CHECK(w.model().arrangement.length == 0);
    CHECK(w.status() == "song is empty");
  }
  SUBCASE("hold dice clears them at once") {
    w.hold(Button::dice);
    CHECK(w.model().arrangement.length == 0);
    CHECK(w.status() == "song cleared");
  }
  CHECK_FALSE(w.model().song_mode);
  CHECK(w.model().transport);
  w.run_until(w.cycle_start(4));  // two more cycle boundaries: A plays on live
  CHECK(w.model().playing == 0);
  CHECK(w.times_in_cycle(Pad::kick, 3) == "0");

  w.press(Button::play);  // in the song view with nothing to play
  CHECK(w.status() == "song is empty");
  CHECK(w.model().transport);
}
