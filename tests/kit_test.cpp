// T-61: the generated lofi header carries PRD Appendix A exactly (D-027), so the
// engine tests run against the real kit data rather than a fixture.
#include <cstring>

#include "engine_support.h"

using namespace support;

TEST_CASE("T-61 Generate the lofi kit header from spec/kits/lofi/kit.json") {
  const Kit& kit = lofi();
  CHECK(std::strcmp(kit.id, "lofi") == 0);

  const char* names[] = {"kick", "snare", "hat", "clap", "bass", "chord", "pluck", "rim"};
  const Tenths sends[] = {1, 1, 1, 1, 0, 4, 3, 1};
  const int template_counts[] = {4, 2, 2, 2, 1, 1, 1, 1};
  for (int i = 0; i < kTrackCount; ++i) {
    CAPTURE(names[i]);
    CHECK(std::strcmp(kit.pads[i].name, names[i]) == 0);
    CHECK(kit.pads[i].send == sends[i]);
    CHECK(kit.pads[i].template_count == template_counts[i]);
  }
  CHECK(pad_of(kit, Pad::kick).voice == Voice::sample);
  CHECK(std::strcmp(pad_of(kit, Pad::kick).source, "kick.wav") == 0);
  CHECK(pad_of(kit, Pad::bass).voice == Voice::synth);
  CHECK(std::strcmp(pad_of(kit, Pad::bass).source, "sub-saw") == 0);
  CHECK(pad_of(kit, Pad::bass).octave == 2);
  CHECK(std::strcmp(pad_of(kit, Pad::chord).source, "warm-poly") == 0);
  CHECK(pad_of(kit, Pad::chord).octave == 4);
  CHECK(std::strcmp(pad_of(kit, Pad::pluck).source, "keys") == 0);
  CHECK(pad_of(kit, Pad::pluck).octave == 5);

  const TapTemplate& snare_once = pad_of(kit, Pad::snare).templates[0];
  CHECK(snare_once.step_count == 2);
  CHECK(snare_once.steps[0] == Step{0, 0});
  CHECK(snare_once.steps[1] == Step{1, 0});
  const TapTemplate& hat_twice = pad_of(kit, Pad::hat).templates[1];
  CHECK(hat_twice.step_count == 4);
  for (int i = 0; i < 4; ++i) CHECK(hat_twice.steps[i] == Step{1, 0});

  const uint8_t expected[kModeCount][4] = {{0, 5, 2, 6}, {0, 4, 5, 3}, {0, 3, 6, 3}, {0, 4, 1, 2}, {0, 4, 1, 3}};
  for (int mode = 0; mode < kModeCount; ++mode) {
    CAPTURE(mode);
    CHECK(kit.progressions[mode].length == 4);
    for (int i = 0; i < 4; ++i) CHECK(kit.progressions[mode].degrees[i] == expected[mode][i]);
  }
  const uint8_t pluck[] = {0, 2, 4, 7, 9, 7, 4, 2};
  CHECK(kit.pluck_sequence.length == 8);
  for (int i = 0; i < 8; ++i) CHECK(kit.pluck_sequence.degrees[i] == pluck[i]);

  CHECK(kit.dice_loop_count == 3);
  for (int i = 0; i < kit.dice_loop_count; ++i) {
    CAPTURE(i);
    CHECK(decode(kit.dice_loops[i], kit).ok);
  }
  CHECK(kit.swing_hundredths == 15);
  CHECK(kit.filter == 10);
  CHECK(kit.fx == 2);
  CHECK(kit.sidechain.on);
  CHECK(kit.sidechain.duck_db == 5);
  CHECK(kit.sidechain.release_ms == 120);
}
