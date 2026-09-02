// tools/render behind its command line: T-66, T-75, T-76, and the limiter's ceiling
// on every golden code (spec/scenarios.md).
#include <cmath>
#include <cstring>
#include <string>

#include "engine/share.h"
#include "render/offline.h"
#include "sound_support.h"

using namespace sound_support;

namespace {

const char* const kBeat = "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1";              // G-04, T-05
const char* const kBeatChance1 = "RT2:lofi:100:10:2:10:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1";      // T-05 at chance 1
const char* const kBeatBassChord = "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e1-e1";  // lofi dice loop 1
const char* const kKickThenNothing =
    "RT2S:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1;lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1;"
    "lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1;lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1/AB";
const char* const kGoldens[] = {
    "RT2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e1-e1.0-e1-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e1-e1-e108-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e10-b1.0-e1-a1.0-e1-e1-e1-f10",
    "RT2:lofi:100:10:2:0:15:cm:e1-e1-ed00-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e1-e1-eh0000-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e100.-e1-e1-e1-e1-e1-e1-e1",
    "RT2:lofi:100:10:2:0:15:cm:e1-e1.0.0,6a1a-e1-e1-e1-e1-e1-e1",
    "RT2:lofi:96:10:3:2:15:cm:e108-e1.0.0-e10000-e1.0,7a9a-e1-e10123-e1-e1",
    "RT2:lofi:90:10:2:0:15:am:e10-e1-e1-e1-e100-e10123-e1-e1",
    "RT2:lofi:128:6:5:3:0:fsdor:e10000-e1.0.0-e10000-e1.0.0-e1-e1-e1-e1~k9z2ab",
    "RT2:lofi:180:10:10:10:100:csdor:fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-"
    "fhoooooooooooooooo,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-"
    "fhoooooooooooooooo,7959~zzzzzz",
    "RT2S:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e1-e1;"
    "lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e10123-e1;"
    "lofi:100:10:2:0:15:cm:e10-e1.0-e100000-e1.0-e10-e101-e10123-e1;"
    "lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1/AABABBCD~k9z2ab",
};

constexpr int kFramesPerCycleAt100 = 115200;
constexpr int kPeakAtMinusOneDb = 29204;  // floor(10^(-1/20) × 32767)

const render::KitSamples& kit_samples() {
  static render::KitSamples samples;
  static bool loaded = false;
  if (!loaded) {
    std::string error;
    REQUIRE_MESSAGE(render::load_kit_samples(std::string(ROTA_KITS_DIR) + "/lofi", lofi(), samples, error), error);
    loaded = true;
  }
  return samples;
}

render::Rendered rendered(const char* code, int cycles, uint32_t seed = render::kDefaultSeed) {
  render::Rendered out;
  std::string error;
  REQUIRE_MESSAGE(render::render_code(code, cycles, seed, lofi(), kit_samples().bank(), out, error), error);
  return out;
}

float rms_of_cycle(const render::Rendered& out, int cycle) {
  double sum = 0.0;
  const size_t from = static_cast<size_t>(cycle) * kFramesPerCycleAt100 * 2;
  const size_t to = from + static_cast<size_t>(kFramesPerCycleAt100) * 2;
  for (size_t i = from; i < to; ++i) sum += static_cast<double>(out.interleaved[i]) * out.interleaved[i];
  return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)) / 32768.0);
}

}  // namespace

TEST_CASE("T-66 The same code renders bit-identically across runs") {
  CHECK(rendered(kBeat, 2).interleaved == rendered(kBeat, 2).interleaved);
  CHECK(rendered(kBeatBassChord, 2).interleaved == rendered(kBeatBassChord, 2).interleaved);
  SUBCASE("chance 1 with the same seed too, and another seed rolls other dice") {
    CHECK(rendered(kBeatChance1, 4).interleaved == rendered(kBeatChance1, 4).interleaved);
    CHECK(rendered(kBeatChance1, 4).interleaved != rendered(kBeatChance1, 4, 43).interleaved);
  }
}

TEST_CASE("T-75 render writes exactly the cycles asked, steps a song through its arrangement, and refuses a bad code") {
  CHECK(rendered(kBeat, 2).frames() == 2 * kFramesPerCycleAt100);
  CHECK(rendered(kGoldens[10], 1).frames() == 120000);  // 96 bpm
  CHECK(rendered(kGoldens[13], 1).frames() == 64000);   // 180 bpm

  SUBCASE("a song plays its letters in order, one cycle each") {
    const render::Rendered song = rendered(kKickThenNothing, 4);
    REQUIRE(song.frames() == 4 * kFramesPerCycleAt100);
    const float loud = rms_of_cycle(song, 0);
    REQUIRE(loud > 0.0f);
    CHECK(db(rms_of_cycle(song, 1) / loud) < -15.0f);
    CHECK(rms_of_cycle(song, 2) == doctest::Approx(loud).epsilon(0.05));
    CHECK(db(rms_of_cycle(song, 3) / loud) < -15.0f);
  }
  SUBCASE("a code that does not load, or a count out of range, is refused with Appendix D's words") {
    render::Rendered out;
    std::string error;
    CHECK_FALSE(render::render_code("RT2:lofi:100:10:2:0:15:cm:e1-e1", 1, 42, lofi(), kit_samples().bank(), out, error));
    CHECK(error == "that code did not load");
    CHECK_FALSE(render::render_code("hello", 1, 42, lofi(), kit_samples().bank(), out, error));
    CHECK(error == "that code did not load");
    CHECK_FALSE(render::render_code(kBeat, 0, 42, lofi(), kit_samples().bank(), out, error));
    CHECK_FALSE(render::render_code(kBeat, render::kMaxCycles + 1, 42, lofi(), kit_samples().bank(), out, error));
  }
  SUBCASE("a code for a kit the tool lacks plays with lofi and says so") {
    const render::Rendered out = rendered("RT2:jazz:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1", 1);
    CHECK(out.note == "no kit jazz, using lofi");
    CHECK(out.peak() > 0);
  }
}

TEST_CASE("T-63 Every golden code renders under the limiter's ceiling") {
  for (const char* code : kGoldens) {
    CAPTURE(code);
    const render::Rendered out = rendered(code, 2);
    CHECK(out.peak() <= kPeakAtMinusOneDb);
  }
  const render::Rendered worst = rendered(kGoldens[13], 4);  // every G-14 track plays on cycle 3 only (`fourth`)
  CHECK(worst.peak() > kPeakAtMinusOneDb * 0.9);  // G-14 drives the limiter, it is not just quiet
}

TEST_CASE("T-76 The lofi kit folder holds its five samples as 16-bit 48 kHz mono WAVs") {
  const render::KitSamples& samples = kit_samples();
  for (int i = 0; i < kTrackCount; ++i) {
    const engine::KitPad& pad = lofi().pads[i];
    CAPTURE(pad.name);
    if (pad.voice == engine::Voice::sample) {
      CHECK(samples.frames[i].size() > 0);
      CHECK(samples.frames[i].size() <= static_cast<size_t>(2 * kSampleRate));
    } else {
      CHECK(samples.frames[i].empty());
    }
  }
  SUBCASE("the reader refuses what is not a kit sample") {
    std::vector<int16_t> frames;
    std::string error;
    CHECK_FALSE(render::read_wav(std::string(ROTA_KITS_DIR) + "/lofi/missing.wav", frames, error));
    CHECK(error.find("cannot open") != std::string::npos);
    CHECK_FALSE(render::read_wav(std::string(ROTA_KITS_DIR) + "/lofi/kit.json", frames, error));
    CHECK(error.find("not a RIFF WAVE file") != std::string::npos);
  }
}
