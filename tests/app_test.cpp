// The app under the scripted input harness (tests/app_support.h): the scheduler,
// the input grammar and the audio path together, on a fake HAL with a fake clock.
// spec/scenarios.md T-05, T-07, T-17, T-18, T-39, T-40, T-78, T-79, T-80, T-81, T-82, T-83, T-84,
// T-95, T-96.
#include <memory>
#include <string>

#include "app_support.h"
#include "engine/edits.h"
#include "engine/kits/lofi.h"

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
  CHECK(w.knob() == "filter 0.9");

  w.pad_down(Pad::hat);
  w.turn(Encoder::volume, -1);
  w.pad_up(Pad::hat);
  CHECK(engine::track_of(w.state(0), Pad::hat).level == 7);
  CHECK(engine::track_of(w.state(1), Pad::hat).level == 7);
  CHECK(w.knob() == "hat level 0.7");
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
  w.run_until(w.cycle_start(4));  // from the very next cycle boundary A plays on live
  CHECK(w.model().playing == 0);
  CHECK(w.times_in_cycle(Pad::kick, 2) == "0");
  CHECK(w.times_in_cycle(Pad::kick, 3) == "0");

  w.press(Button::play);  // in the song view with nothing to play
  CHECK(w.status() == "song is empty");
  CHECK(w.model().transport);
}

TEST_CASE("T-82 Stopping inside the lookahead drops the hits already handed over") {
  World w;
  w.tap(Pad::kick, 4);
  w.tap(Pad::bass);  // a synth voice at 0 of every cycle, sustaining two seconds (D-075): the tail to keep
  w.play();
  // Two blocks (5.3 ms) before the kick at 1/4, which the 10 ms lookahead has already handed over.
  w.run_until(w.at(1, engine::Fraction{1, 4}) - 2 * kBlock);
  w.press(Button::play);
  CHECK_FALSE(w.model().transport);
  w.run_for(8 * kBlock);
  CHECK(w.times_in_cycle(Pad::kick, 1) == "0");  // the 1/4 hit never sounded
  CHECK(w.times_in_cycle(Pad::bass, 1) == "0");  // the bass struck 0.6 s before the stop, at this cycle's start
  CHECK(w.last_peak > 0.01f);                     // and it is still sounding after the stop

  w.play();  // a fresh cycle whose hits all fire
  w.run_until(w.cycle_start(1));
  CHECK(w.times_in_cycle(Pad::kick, 0) == "0 1/4 1/2 3/4");
}

TEST_CASE("T-83 The audio clock carries on past the 32-bit block count") {
  hal_fake::reset();
  auto engine = std::make_unique<sound::Engine>();
  auto audio = std::make_unique<app::AudioPath>();
  const sound::SampleBank silent{};
  audio->init(*engine, engine::kits::kLofi, silent);
  const uint64_t wrap = uint64_t{1} << 32;  // 132 days of blocks at 48 kHz
  audio->reset(wrap - 2);
  float left[kBlock];
  float right[kBlock];
  int64_t last = audio->position();
  CHECK(last == static_cast<int64_t>((wrap - 2) * static_cast<uint64_t>(kBlock)));
  for (int i = 0; i < 4; ++i) {
    audio->render(left, right);
    const int64_t now = audio->position();
    CHECK(now == last + kBlock);  // never backwards, across the wrap
    last = now;
  }
  CHECK(last == static_cast<int64_t>((wrap + 2) * static_cast<uint64_t>(kBlock)));

  SUBCASE("the scheduler starts a cycle before the wrap and lands the next cycle's kick after it") {
    auto model = std::make_unique<app::Model>(engine::kits::kLofi);
    engine::tap(model->sections[0], Pad::kick, engine::kits::kLofi);  // a kick at 0
    model->transport = true;
    app::Scheduler scheduler(engine::kits::kLofi);
    scheduler.set_seed(42);
    audio->reset(wrap - 4);
    scheduler.start(*model, *audio);  // the first beat two blocks on: cycle 0 starts at wrap - 2 blocks
    const int64_t cycle_start = static_cast<int64_t>((wrap - 2) * static_cast<uint64_t>(kBlock));
    std::vector<int64_t> kicks;
    engine::Fraction playhead{0, 1};
    int64_t timer_at = audio->position();
    while (audio->position() < cycle_start + kCycleFrames + 2 * kBlock) {
      if (timer_at <= audio->position()) {
        scheduler.tick(*model, *audio);
        timer_at += 96;  // 2 ms
      }
      audio->render(left, right);
      const engine::Fraction now = scheduler.playhead(audio->position());
      if (audio->position() < cycle_start + kCycleFrames) {  // inside cycle 0, wrap included
        CHECK(now >= playhead);  // the ring's playhead never runs backwards, not even at a beat
        playhead = now;
      }
      app::Fired hit;
      while (audio->fired.pop(hit)) {
        if (hit.event.track == Pad::kick) kicks.push_back(hit.sample);
      }
    }
    CHECK(kicks == std::vector<int64_t>{cycle_start, cycle_start + kCycleFrames});  // the second lies past 2^32 blocks
  }
}

TEST_CASE("T-84 Hold split and a pad: the roll retriggers at every 1/16, handed over in sample order") {
  World w;
  w.tap(Pad::kick, 15);  // hits 7680 frames apart: the second lies 480 frames after the 1/16 point at 7200
  w.button_down(Button::split);
  w.run_for(kSecond / 2);  // past the hold threshold: the roll is on
  w.pad_down(Pad::hat);
  w.play();
  w.run_until(w.cycle_start(2));
  CHECK(w.times_in_cycle(Pad::hat, 1) ==
        "0 1/16 1/8 3/16 1/4 5/16 3/8 7/16 1/2 9/16 5/8 11/16 3/4 13/16 7/8 15/16");
  CHECK(w.times_in_cycle(Pad::kick, 1) == "0 1/15 2/15 1/5 4/15 1/3 2/5 7/15 8/15 3/5 2/3 11/15 4/5 13/15 14/15");

  // The timer stalls across the 1/16 point at 7200; when it wakes, the kick at 7680 is
  // due in the same window. The late roll hit must sound in the very next block, not
  // behind the kick handed over after it.
  const int64_t grid = w.cycle_start(2) + 7200;
  const int64_t kick = grid + 480;
  w.run_until(grid - 8 * kBlock);
  const int64_t tick = w.skip_timer_until(grid + 32);
  const int64_t expected = (tick + kBlock - 1) / kBlock * kBlock;  // the block after the tick
  REQUIRE(expected < kick);
  w.run_until(kick + 3 * kBlock);
  std::vector<int64_t> late_roll;
  for (const app::Fired& hit : w.fired) {
    if (hit.event.track == Pad::hat && hit.sample >= grid && hit.sample <= kick) late_roll.push_back(hit.sample);
  }
  CHECK(late_roll == std::vector<int64_t>{expected});
  std::vector<int64_t> kicks;
  for (const app::Fired& hit : w.fired) {
    if (hit.event.track == Pad::kick && hit.sample >= grid && hit.sample <= kick) kicks.push_back(hit.sample);
  }
  CHECK(kicks == std::vector<int64_t>{kick});

  w.button_up(Button::split);  // the roll ends with the hold
  w.run_until(w.cycle_start(4));
  CHECK(w.times_in_cycle(Pad::hat, 3) == "");
}

TEST_CASE("T-95 Hold play, then four taps in rhythm set the bpm") {
  World w;
  w.tap(Pad::kick, 4);
  w.hold(Button::play);
  CHECK(w.status() == "tap 4 times in rhythm");
  CHECK_FALSE(w.model().transport);  // the hold does not start the loop

  const char* const countdown[] = {"3 more", "2 more", "1 more"};
  for (int i = 0; i < 3; ++i) {
    w.press(Button::play);
    CHECK(w.status() == countdown[i]);
    w.run_for(kSecond / 2);  // half a second between taps: two beats a second
  }
  w.press(Button::play);
  CHECK(w.status() == "120 bpm");
  CHECK(w.state(0).bpm == 120);
  CHECK_FALSE(w.model().transport);  // a tap is never a play

  SUBCASE("a fifth press plays, the mode having ended with the fourth tap") {
    w.press(Button::play);
    CHECK(w.model().transport);
  }

  SUBCASE("the mode times out five seconds after the last tap and changes nothing") {
    w.hold(Button::play);
    w.press(Button::play);
    w.run_for(4 * kSecond);
    CHECK(w.status() == "3 more");  // still waiting
    w.run_for(2 * kSecond);
    CHECK(w.status() == "tempo unchanged");
    CHECK(w.state(0).bpm == 120);
  }

  SUBCASE("taps slower than the range clamp to 60 bpm") {
    w.hold(Button::play);
    for (int i = 0; i < 3; ++i) {
      w.press(Button::play);
      w.run_for(3 * kSecond / 2);  // a beat and a half apart: 40 bpm, and no gap reaches the timeout
    }
    w.press(Button::play);
    CHECK(w.status() == "60 bpm");
    CHECK(w.state(0).bpm == 60);
  }

  SUBCASE("taps faster than the range clamp to 180 bpm") {
    w.hold(Button::play);
    for (int i = 0; i < 4; ++i) w.press(Button::play);  // all inside one microsecond of the clock
    CHECK(w.status() == "180 bpm");
    CHECK(w.state(0).bpm == 180);
  }

  SUBCASE("the tempo reaches the section waiting to play as a knob would") {
    w.play();
    w.press(section('B'));  // B copies A and waits for the boundary
    w.hold(Button::play);
    for (int i = 0; i < 3; ++i) {
      w.press(Button::play);
      w.run_for(kSecond / 4);  // a quarter second between taps: 240 bpm, clamped
    }
    w.press(Button::play);
    CHECK(w.state(0).bpm == 180);
    CHECK(w.state(1).bpm == 180);
    CHECK(w.model().transport);  // the taps did not stop the loop
  }
}

TEST_CASE("T-95 A hold of play skips the tutorial instead of opening tap tempo") {
  World w(true);
  REQUIRE(w.model().tutorial.active);
  w.hold(Button::play);
  CHECK(w.status() == "tutorial skipped");
  CHECK_FALSE(w.model().tutorial.active);
  CHECK_FALSE(w.model().transport);
}

TEST_CASE("T-96 Hold two section buttons: their contents swap") {
  World w;
  w.tap(Pad::kick, 4);    // A: four kicks
  w.press(section('B'));  // B copies A
  w.tap(Pad::snare, 2);   // B: kicks and snares
  w.turn(Encoder::speed, 5);  // and its own tempo
  w.press(section('A'));
  REQUIRE(w.state(0).bpm == 100);
  REQUIRE(w.state(1).bpm == 105);
  const int levels_a = w.model().sections[0].undo_levels();
  const int levels_b = w.model().sections[1].undo_levels();

  w.button_down(section('A'));
  w.run_for(kSecond / 10);
  w.button_down(section('B'));
  w.run_for(kSecond / 2);  // A's hold fires with B down
  w.button_up(section('B'));
  w.button_up(section('A'));

  CHECK(w.status() == "swapped A and B");
  CHECK(steps_of(w.state(0), Pad::snare) == ".0.0");  // A plays what B held
  CHECK(steps_of(w.state(1), Pad::snare) == "");      // and B what A held
  CHECK(w.model().current == 0);                      // neither release switched section
  CHECK(w.model().sections[0].undo_levels() == levels_a + 1);  // one undoable load each
  CHECK(w.model().sections[1].undo_levels() == levels_b + 1);
  CHECK(w.state(0).bpm == 105);  // the tempo is part of a section, so it travels too
  CHECK(w.state(1).bpm == 100);

  w.press(Button::undo);
  CHECK(steps_of(w.state(0), Pad::snare) == "");  // A's own loop is back
  CHECK(w.state(0).bpm == 105);                   // at the tempo it has now: undo never moves a knob (D-035)
  w.press(section('B'));
  w.press(Button::undo);
  CHECK(steps_of(w.state(1), Pad::snare) == ".0.0");  // and B's, on its own undo

  SUBCASE("two sections without steps have nothing to swap") {
    w.button_down(section('C'));
    w.run_for(kSecond / 10);
    w.button_down(section('D'));
    w.run_for(kSecond / 2);
    w.button_up(section('D'));
    w.button_up(section('C'));
    CHECK(w.status() == "nothing to swap");
    CHECK(w.model().sections[2].undo_levels() == 0);
    CHECK(w.model().sections[3].undo_levels() == 0);
    CHECK(w.model().current == 1);  // the swap left the player on B; a hold never switches
  }

  SUBCASE("a third button held joins no swap: the pair is spent until it is released") {
    const std::string kept = steps_of(w.state(2), Pad::kick);
    w.button_down(section('A'));
    w.run_for(kSecond / 10);
    w.button_down(section('B'));
    w.button_down(section('C'));
    w.run_for(kSecond / 2);
    w.button_up(section('C'));
    w.button_up(section('B'));
    w.button_up(section('A'));
    CHECK(w.status() == "swapped A and B");
    CHECK(steps_of(w.state(2), Pad::kick) == kept);
  }
}
