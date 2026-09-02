// Undo, redo and dice: T-13, T-34, T-53.
#include "engine_support.h"

using namespace support;

TEST_CASE("T-13 Undo after any edit") {
  Section section = fresh_section();
  taps(section, Pad::kick, 2);
  taps(section, Pad::snare, 2);
  tap(section, Pad::chord, lofi());

  SUBCASE("tap, including the template it applied") {
    const State before = section.state();
    tap(section, Pad::snare, lofi());
    REQUIRE(steps_text(section.state(), Pad::snare) == ".0.00");
    section.undo();
    CHECK(section.state() == before);
    tap(section, Pad::hat, lofi());
    REQUIRE(steps_text(section.state(), Pad::hat) == "00");
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("remove last step") {
    const State before = section.state();
    remove_last_step(section, Pad::kick);
    REQUIRE(steps_text(section.state(), Pad::kick) == "0");
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("split") {
    const State before = section.state();
    split(section, Pad::kick);
    REQUIRE(steps_text(section.state(), Pad::kick) == "08");
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("skip") {
    const State before = section.state();
    skip(section, Pad::kick);
    REQUIRE(steps_text(section.state(), Pad::kick) == "00.");
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("swap") {
    const State before = section.state();
    swap(section, Pad::snare);
    REQUIRE(track_of(section.state(), Pad::snare).alt == Alt::a);
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("speed") {
    const State before = section.state();
    adjust_speed(section, Pad::kick, +1);
    REQUIRE(track_of(section.state(), Pad::kick).speed == Speed::two);
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("dice") {
    const State before = section.state();
    dice_fill_empty(section, lofi(), 2);
    REQUIRE_FALSE(is_empty(track_of(section.state(), Pad::hat)));
    section.undo();
    CHECK(section.state() == before);
    dice_replace_all(section, lofi(), 0);
    REQUIRE(steps_text(section.state(), Pad::kick) == "0000");
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("load") {
    const State before = section.state();
    load(section, decode("RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1", lofi()).state);
    REQUIRE(steps_text(section.state(), Pad::kick) == "0");
    section.undo();
    CHECK(section.state() == before);
    // Loading the state already in place changes nothing, so it records nothing
    // and keeps the redo the undo just made available (D-038).
    const int levels = section.undo_levels();
    REQUIRE(section.redo_levels() == 1);
    const State same = section.state();
    load(section, same);
    CHECK(section.undo_levels() == levels);
    CHECK(section.redo_levels() == 1);
  }
  SUBCASE("redo brings the edit back") {
    tap(section, Pad::rim, lofi());
    const State edited = section.state();
    section.undo();
    section.redo();
    CHECK(section.state() == edited);
  }
}

TEST_CASE("T-34 61 edits in section A, then undo 61 times") {
  Section a = fresh_section();
  Section b = fresh_section();
  tap(b, Pad::hat, lofi());
  const State b_state = b.state();
  const int b_levels = b.undo_levels();

  std::vector<State> after_edit;  // after_edit[k] is the state after edit k + 1
  tap(a, Pad::clap, lofi());  // edit 1
  after_edit.push_back(a.state());
  for (int edit = 2; edit <= 61; ++edit) {
    swap(a, Pad::clap);  // edits 2–61: every swap changes the alternation
    after_edit.push_back(a.state());
  }
  REQUIRE(after_edit.size() == 61);
  CHECK(a.undo_levels() == kUndoDepth);

  for (int undo = 1; undo <= 60; ++undo) {
    a.undo();
    CHECK(a.state() == after_edit[61 - undo - 1]);
  }
  CHECK(a.state() == after_edit[0]);  // the state after edit 1
  a.undo();                           // the 61st changes nothing
  CHECK(a.state() == after_edit[0]);
  a.redo();  // hold undo: redoes edit 2
  CHECK(a.state() == after_edit[1]);

  CHECK(b.state() == b_state);
  CHECK(b.undo_levels() == b_levels);
  b.undo();
  CHECK(is_empty(track_of(b.state(), Pad::hat)));
}

TEST_CASE("T-53 Kick x4 and snare x2 set, other tracks empty; press dice, then hold dice") {
  Section section = fresh_section();
  taps(section, Pad::kick, 4);
  taps(section, Pad::snare, 2);
  const State before = section.state();

  SUBCASE("press: the empty tracks come from the picked loop, kick and snare stay") {
    dice_fill_empty(section, lofi(), 2);  // roll 2 picks lofi's third loop (roll mod 3)
    const State& state = section.state();
    CHECK(steps_text(state, Pad::kick) == "0000");
    CHECK(steps_text(state, Pad::snare) == ".0.0");
    CHECK(steps_text(state, Pad::hat) == "000000");
    CHECK(steps_text(state, Pad::clap) == ".0.0");
    CHECK(track_of(state, Pad::clap).alt == Alt::b);
    CHECK(steps_text(state, Pad::bass) == "0000");
    CHECK(steps_text(state, Pad::chord) == "0123");
    CHECK(steps_text(state, Pad::pluck) == "01");
    CHECK(steps_text(state, Pad::rim) == "0.");
    CHECK(state.bpm == before.bpm);  // the loop's globals are ignored
    section.undo();
    CHECK(section.state() == before);
  }
  SUBCASE("press with another roll picks another loop") {
    dice_fill_empty(section, lofi(), 0);  // the first loop
    CHECK(steps_text(section.state(), Pad::hat) == "0000");
    CHECK(steps_text(section.state(), Pad::bass) == "00");
    CHECK(is_empty(track_of(section.state(), Pad::pluck)));  // the first loop has no pluck
    section.undo();
    dice_fill_empty(section, lofi(), 1);  // the second loop
    CHECK(steps_text(section.state(), Pad::hat) == "00000");
    CHECK(steps_text(section.state(), Pad::pluck) == "0123");
    section.undo();
    dice_fill_empty(section, lofi(), 3);  // wraps to the first loop
    CHECK(steps_text(section.state(), Pad::hat) == "0000");
  }
  SUBCASE("hold that puts the same loop back records no undo level (D-038)") {
    dice_replace_all(section, lofi(), 1);
    const int levels = section.undo_levels();
    dice_replace_all(section, lofi(), 1);
    CHECK(section.undo_levels() == levels);
    CHECK(steps_text(section.state(), Pad::hat) == "00000");
  }
  SUBCASE("press with nothing to fill records no undo level (D-038)") {
    dice_fill_empty(section, lofi(), 0);  // the first loop leaves clap, pluck and rim empty
    const int levels = section.undo_levels();
    dice_fill_empty(section, lofi(), 0);  // the same loop again: those three stay empty
    CHECK(section.undo_levels() == levels);
    dice_fill_empty(section, lofi(), 2);  // the third loop fills them
    CHECK(section.undo_levels() == levels + 1);
    CHECK(steps_text(section.state(), Pad::rim) == "0.");
  }
  SUBCASE("hold: all eight tracks are replaced, undo restores") {
    dice_replace_all(section, lofi(), 1);  // the second loop
    const State& state = section.state();
    CHECK(steps_text(state, Pad::kick) == "0");
    CHECK(steps_text(state, Pad::snare) == ".0");
    CHECK(steps_text(state, Pad::hat) == "00000");
    CHECK(steps_text(state, Pad::clap) == ".0");
    CHECK(steps_text(state, Pad::bass) == "0");
    CHECK(steps_text(state, Pad::chord) == "01");
    CHECK(steps_text(state, Pad::pluck) == "0123");
    CHECK(is_empty(track_of(state, Pad::rim)));
    section.undo();
    CHECK(section.state() == before);
  }
}
