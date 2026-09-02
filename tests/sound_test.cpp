// The sound engine stage by stage: T-63–T-65, T-67–T-74, T-77 (spec/scenarios.md).
#include <cmath>

#include "sound/delay.h"
#include "sound/dynamics.h"
#include "sound/filter.h"
#include "sound/reverb.h"
#include "sound/sidechain.h"
#include "sound_support.h"

using namespace sound_support;
using engine::Pad;

namespace {

constexpr float kC2Hz = 65.41f;
constexpr float kC4Hz = 261.63f;
constexpr float kEb4Hz = 311.13f;
constexpr float kG4Hz = 392.00f;
constexpr float kCs4Hz = 277.18f;
constexpr float kF4Hz = 349.23f;
constexpr float kC5Hz = 523.25f;
constexpr int kOneSecondFrames = kSampleRate;
constexpr int kTenMsFrames = kSampleRate / 100;

// 1 kHz at −26 dBFS: quiet enough that the compressor, clipper and limiter stay linear,
// so what the per-track stages do can be read straight off the output.
constexpr float kQuietAmplitude = 0.05f;
constexpr float kTestHz = 1000.0f;

std::vector<float> render_synth(const Preset& preset, const uint8_t (&pitches)[kMaxTones], int tones, int frames) {
  SynthVoice voice;
  voice.start(preset, pitches, tones, 1.0f);
  std::vector<float> out(static_cast<size_t>(frames), 0.0f);
  for (int at = 0; at < frames; at += kBlockSize) voice.render(out.data() + at, 0, std::min(kBlockSize, frames - at));
  return out;
}

bool no_subnormal(const std::vector<float>& v) {
  for (float x : v) {
    if (std::fpclassify(x) == FP_SUBNORMAL) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("T-63 A sine at +6 dBFS through the limiter never exceeds -1 dBFS") {
  Limiter limiter;
  CHECK(limiter.ceiling() == doctest::Approx(0.89125f));
  const std::vector<float> loud = sine(kTestHz, 2.0f, kOneSecondFrames);
  const std::vector<float> out = through_stereo(limiter, loud);
  CHECK(peak_of(out) <= limiter.ceiling() + 1e-6f);
  CHECK(peak_of(out, kSampleRate / 2) > limiter.ceiling() * 0.98f);  // it limits, it does not just attenuate

  SUBCASE("the gain moves smoothly, so the sine keeps its shape") {
    float largest_step = 0.0f;
    float previous = 0.0f;
    bool have_previous = false;
    for (size_t i = Limiter::kLookaheadFrames + kTenMsFrames; i < out.size(); ++i) {
      const float in = loud[i - Limiter::kLookaheadFrames];
      if (std::fabs(in) < 1.0f) {
        have_previous = false;
        continue;
      }
      const float gain = out[i] / in;
      if (have_previous) largest_step = std::max(largest_step, std::fabs(gain - previous));
      previous = gain;
      have_previous = true;
    }
    CHECK(largest_step < 0.02f);
  }

  SUBCASE("a signal under the ceiling passes unchanged, one millisecond later") {
    Limiter transparent;
    const std::vector<float> quiet = sine(kTestHz, 0.5f, 4800);
    const std::vector<float> passed = through_stereo(transparent, quiet);
    for (size_t i = 0; i + Limiter::kLookaheadFrames < passed.size(); ++i) {
      REQUIRE(passed[i + Limiter::kLookaheadFrames] == doctest::Approx(quiet[i]).epsilon(1e-5));
    }
  }
}

TEST_CASE("T-64 A kick event ducks bass, chord and pluck by the kit's amount with its release") {
  SUBCASE("the envelope: -5 dB at once, back to 0 dB linearly over 120 ms") {
    Sidechain sidechain;
    sidechain.set(lofi().sidechain);
    CHECK(sidechain.next() == 1.0f);
    sidechain.duck();
    CHECK(db(sidechain.next()) == doctest::Approx(-5.0).epsilon(0.01));
    for (int i = 1; i < 2880; ++i) sidechain.next();
    CHECK(db(sidechain.next()) == doctest::Approx(-2.5).epsilon(0.02));  // 60 ms in
    for (int i = 2881; i < 5760; ++i) sidechain.next();
    CHECK(std::fabs(db(sidechain.next())) < 0.01);  // 120 ms in
    CHECK(sidechain.next() == 1.0f);
  }

  SUBCASE("off in the kit: no duck") {
    Sidechain sidechain;
    sidechain.set(engine::Sidechain{false, 5, 120});
    sidechain.duck();
    CHECK(sidechain.next() == 1.0f);
  }

  SUBCASE("in the engine: a steady tone on the chord pad drops after a kick, one on the rim pad does not") {
    const engine::Kit kit = kit_with_sample_chord();
    TestBank bank;
    bank.set(Pad::chord, sine16(kTestHz, kQuietAmplitude, 2 * kOneSecondFrames));
    bank.set(Pad::rim, sine16(kTestHz, kQuietAmplitude, 2 * kOneSecondFrames));
    const int kick_block = blocks_of_seconds(1.0f);
    const size_t kick_at = static_cast<size_t>(kick_block) * kBlockSize + Limiter::kLookaheadFrames;

    Params dry = default_params(kit);
    dry.fx = 0.0f;  // no tail from before the kick in the window after it

    Harness chord;
    chord.init(kit, bank.bank());
    chord.engine->set_params(dry);
    chord.render(kick_block + 40, {cue(0, 0, hit(Pad::chord)), cue(kick_block, 0, hit(Pad::kick))});
    const float chord_before = rms_of(chord.left, kick_at - kTenMsFrames, kick_at);
    const float chord_after = rms_of(chord.left, kick_at, kick_at + kTenMsFrames);
    CHECK(db(chord_after / chord_before) == doctest::Approx(-4.79).epsilon(0.06));  // the mean of the first 10 ms

    Harness rim;
    rim.init(kit, bank.bank());
    rim.engine->set_params(dry);
    rim.render(kick_block + 40, {cue(0, 0, hit(Pad::rim)), cue(kick_block, 0, hit(Pad::kick))});
    const float rim_before = rms_of(rim.left, kick_at - kTenMsFrames, kick_at);
    const float rim_after = rms_of(rim.left, kick_at, kick_at + kTenMsFrames);
    CHECK(std::fabs(db(rim_after / rim_before)) < 0.05);
  }
}

TEST_CASE("T-65 Delay time equals the dotted eighth at the given bpm") {
  Delay delay;
  delay.set_tempo(100.0f);
  CHECK(delay.length() == 21600);
  delay.set_tempo(120.0f);
  CHECK(delay.length() == 18000);
  delay.set_tempo(180.0f);
  CHECK(delay.length() == 12000);
  delay.set_tempo(60.0f);
  CHECK(delay.length() == 36000);
  CHECK(delay.length() == kMaxDelayFrames);

  SUBCASE("an impulse comes back exactly one dotted eighth later") {
    Delay fresh;
    fresh.set_tempo(100.0f);
    const int frames = 22000;
    std::vector<float> in(static_cast<size_t>(frames), 0.0f);
    std::vector<float> left(static_cast<size_t>(frames), 0.0f);
    std::vector<float> right(static_cast<size_t>(frames), 0.0f);
    in[0] = 1.0f;
    for (int at = 0; at < frames; at += kBlockSize) {
      fresh.process(in.data() + at, left.data() + at, right.data() + at, std::min(kBlockSize, frames - at));
    }
    int first = -1;
    for (int i = 0; i < frames; ++i) {
      if (std::fabs(left[i]) > 1e-9f) {
        first = i;
        break;
      }
    }
    CHECK(first == 21600);
    CHECK(left[21600] == doctest::Approx(1.0f));
    CHECK(right[21600] == doctest::Approx(1.0f));
  }

  SUBCASE("a sweep of one bpm per block never jumps: each crossfade finishes before the next starts") {
    // 100 Hz: one bpm at 100 bpm moves the read point 216 frames, nearly half a period,
    // so a restarted fade would jump by almost the whole amplitude.
    Delay swept;
    swept.set_tempo(100.0f);
    const int fill = 188 * kBlockSize;  // half a second, in whole blocks
    const int sweep_blocks = 40;
    const std::vector<float> in = sine(100.0f, 1.0f, fill + sweep_blocks * kBlockSize);
    std::vector<float> left(in.size(), 0.0f);
    std::vector<float> right(in.size(), 0.0f);
    for (int at = 0; at < fill; at += kBlockSize) {
      swept.process(in.data() + at, left.data() + at, right.data() + at, kBlockSize);
    }
    for (int b = 0; b < sweep_blocks; ++b) {
      swept.set_tempo(100.0f + static_cast<float>(b));
      const int at = fill + b * kBlockSize;
      swept.process(in.data() + at, left.data() + at, right.data() + at, kBlockSize);
    }
    float largest_step = 0.0f;
    for (size_t i = static_cast<size_t>(fill) + 1; i < left.size(); ++i) {
      largest_step = std::max(largest_step, std::fabs(left[i] - left[i - 1]));
    }
    CHECK(largest_step < 0.1f);  // a 100 Hz sine moves at most 0.04 per sample even with the feedback
    CHECK(swept.length() == 15540);  // heading to 139 bpm: 45 / 139 s
  }

  SUBCASE("bpm outside 60-180 is clamped, so there is no division by zero") {
    Delay clamped;
    clamped.set_tempo(0.0f);
    CHECK(clamped.length() == 36000);
    clamped.set_tempo(1000.0f);
    CHECK(clamped.length() == 12000);
  }
}

TEST_CASE("T-67 A sample voice plays the kit's sample with its pitch, start and decay") {
  std::vector<int16_t> ramp(480);
  for (int i = 0; i < 480; ++i) ramp[static_cast<size_t>(i)] = static_cast<int16_t>(i * 60);
  const Sample sample{ramp.data(), 480};
  engine::KitPad pad = engine::pad_of(lofi(), Pad::kick);
  auto play = [&](const engine::KitPad& settings, int frames) {
    SampleVoice voice;
    voice.start(sample, settings, 1.0f);
    std::vector<float> out(static_cast<size_t>(frames), 0.0f);
    for (int at = 0; at < frames; at += kBlockSize) voice.render(out.data() + at, 0, std::min(kBlockSize, frames - at));
    return std::make_pair(out, voice.active());
  };
  auto value = [&](int frame) { return static_cast<float>(ramp[static_cast<size_t>(frame)]) / 32768.0f; };

  SUBCASE("pitch 0, start 0, decay 1: the sample as it is, then silence") {
    const auto [out, active] = play(pad, 1024);
    for (int i = 0; i < 480; ++i) REQUIRE(out[static_cast<size_t>(i)] == doctest::Approx(value(i)));
    for (int i = 480; i < 1024; ++i) REQUIRE(out[static_cast<size_t>(i)] == 0.0f);
    CHECK_FALSE(active);
  }
  SUBCASE("pitch +12 reads every other frame") {
    pad.pitch_semitones = 12;
    const auto [out, active] = play(pad, 512);
    for (int i = 0; i < 240; ++i) REQUIRE(out[static_cast<size_t>(i)] == doctest::Approx(value(2 * i)));
    CHECK(out[240] == 0.0f);
    CHECK_FALSE(active);
  }
  SUBCASE("start 0.5 begins half way in") {
    pad.start = 0.5f;
    const auto [out, active] = play(pad, 512);
    CHECK(out[0] == doctest::Approx(value(240)));
    CHECK(out[239] == doctest::Approx(value(479)));
    CHECK(out[240] == 0.0f);
    CHECK_FALSE(active);
  }
  SUBCASE("decay 0.5 plays half and fades the last 5 ms of it") {
    pad.decay = 0.5f;
    const auto [out, active] = play(pad, 512);
    CHECK(out[0] == doctest::Approx(value(0)));
    CHECK(out[100] == doctest::Approx(value(100) * (140.0f / 240.0f)));
    CHECK(out[239] == doctest::Approx(value(239) * (1.0f / 240.0f)));
    CHECK(out[240] == 0.0f);
    CHECK_FALSE(active);
  }
  SUBCASE("release fades the voice out within 5 ms") {
    SampleVoice voice;
    voice.start(sample, pad, 1.0f);
    std::vector<float> out(512, 0.0f);
    voice.render(out.data(), 0, 100);
    voice.release();
    voice.render(out.data(), 100, 100 + kSampleFadeFrames);
    CHECK_FALSE(voice.active());
    CHECK(out[100] == doctest::Approx(value(100)));
    CHECK(out[100 + kSampleFadeFrames - 1] < value(100 + kSampleFadeFrames - 1) * 0.01f);
  }
  SUBCASE("the gain passed in scales the output; the engine sets it to the velocity squared") {
    SampleVoice voice;
    voice.start(sample, pad, 0.25f);
    std::vector<float> out(128, 0.0f);
    voice.render(out.data(), 0, 128);
    CHECK(out[100] == doctest::Approx(value(100) * 0.25f));
  }
}

TEST_CASE("T-68 The lofi presets resolve and a synth voice sounds the event's MIDI pitch") {
  REQUIRE(preset_named("sub-saw") != nullptr);
  REQUIRE(preset_named("warm-poly") != nullptr);
  REQUIRE(preset_named("keys") != nullptr);
  CHECK(preset_named("nope") == nullptr);
  for (int i = 0; i < kTrackCount; ++i) {
    const engine::KitPad& pad = lofi().pads[i];
    if (pad.voice == engine::Voice::synth) CHECK(preset_named(pad.source) != nullptr);
  }

  SUBCASE("sub-saw at MIDI 36 is loudest at C2") {
    const uint8_t pitches[kMaxTones] = {36, 0, 0};
    const std::vector<float> out = render_synth(*preset_named("sub-saw"), pitches, 1, kOneSecondFrames);
    CHECK(loudest_frequency(out, 30.0f, 200.0f, 0.5f) == doctest::Approx(kC2Hz).epsilon(0.02));
  }
  SUBCASE("keys at MIDI 72 is loudest at C5") {
    const uint8_t pitches[kMaxTones] = {72, 0, 0};
    const std::vector<float> out = render_synth(*preset_named("keys"), pitches, 1, kOneSecondFrames);
    CHECK(loudest_frequency(out, 300.0f, 800.0f, 1.0f) == doctest::Approx(kC5Hz).epsilon(0.01));
  }
  SUBCASE("warm-poly sounds all three tones of a C minor chord") {
    const uint8_t pitches[kMaxTones] = {60, 63, 67};
    const std::vector<float> out = render_synth(*preset_named("warm-poly"), pitches, 3, kOneSecondFrames);
    const float outside = std::max(magnitude_at(out, kCs4Hz), magnitude_at(out, kF4Hz));
    CHECK(magnitude_at(out, kC4Hz) > 4.0f * outside);
    CHECK(magnitude_at(out, kEb4Hz) > 4.0f * outside);
    CHECK(magnitude_at(out, kG4Hz) > 4.0f * outside);
  }
  SUBCASE("a voice ends on its own") {
    SynthVoice voice;
    const uint8_t pitches[kMaxTones] = {72, 0, 0};
    voice.start(*preset_named("keys"), pitches, 1, 1.0f);
    std::vector<float> out(static_cast<size_t>(3 * kOneSecondFrames), 0.0f);
    for (size_t at = 0; at < out.size(); at += kBlockSize) voice.render(out.data() + at, 0, kBlockSize);
    CHECK_FALSE(voice.active());
  }
}

TEST_CASE("T-69 A track's level, tone and send shape its output") {
  const engine::Kit kit = kit_with_sample_chord();
  const int chord_index = engine::index_of(Pad::chord);
  const size_t settled = static_cast<size_t>(kSampleRate) / 2;
  const size_t end = static_cast<size_t>(kSampleRate) * 9 / 10;
  auto steady = [&](Params params) {
    TestBank bank;
    bank.set(Pad::chord, sine16(kTestHz, kQuietAmplitude, 2 * kOneSecondFrames));
    Harness harness;
    harness.init(kit, bank.bank());
    harness.engine->set_params(params);
    harness.render(blocks_of_seconds(1.0f), {cue(0, 0, hit(Pad::chord))});
    return rms_of(harness.left, settled, end);
  };
  const Params defaults = default_params(kit);
  CHECK(defaults.filter == 1.0f);
  CHECK(defaults.fx == doctest::Approx(0.2f));
  CHECK(defaults.tracks[chord_index].send == doctest::Approx(0.4f));
  CHECK(defaults.tracks[chord_index].level == doctest::Approx(0.8f));
  const float at_default = steady(defaults);
  REQUIRE(at_default > 0.0f);

  SUBCASE("level is linear: 0.4 is half of 0.8") {
    Params half = defaults;
    half.tracks[chord_index].level = 0.4f;
    CHECK(steady(half) / at_default == doctest::Approx(0.5f).epsilon(0.02));
  }
  SUBCASE("tone 0 is a 220 Hz low-pass: 1 kHz loses about 26 dB") {
    Params dark = defaults;
    dark.tracks[chord_index].tone = 0.0f;
    CHECK(db(steady(dark) / at_default) == doctest::Approx(-26.3).epsilon(0.06));
  }
  SUBCASE("send feeds the delay and reverb, and fx 0 silences them") {
    auto echo = [&](float send, float fx) {
      TestBank bank;
      bank.set(Pad::chord, sine16(kTestHz, kQuietAmplitude, kSampleRate / 10));
      Harness harness;
      harness.init(kit, bank.bank());
      Params params = defaults;
      params.tracks[chord_index].send = send;
      params.fx = fx;
      harness.engine->set_params(params);
      const int settle = 2;  // the sends ramp from the kit's defaults over the first block
      harness.render(settle + blocks_of_seconds(0.6f), {cue(settle, 0, hit(Pad::chord))});
      const size_t at = settle * kBlockSize + 21600 + Limiter::kLookaheadFrames;  // the dotted eighth at 100 bpm
      return rms_of(harness.left, at, at + kSampleRate / 10);
    };
    CHECK(echo(1.0f, 1.0f) > 1e-4f);
    CHECK(echo(1.0f, 0.0f) < 1e-6f);
    CHECK(echo(0.0f, 1.0f) < 1e-6f);
  }
}

TEST_CASE("T-70 The filter knob is a master low-pass: 220 Hz at 0, open at 1") {
  CHECK(cutoff_of_knob(0.0f) == doctest::Approx(220.0f));
  CHECK(cutoff_of_knob(1.0f) == doctest::Approx(20000.0f));
  CHECK(cutoff_of_knob(0.5f) == doctest::Approx(std::sqrt(220.0f * 20000.0f)));  // exponential: the middle is the geometric mean

  auto steady = [&](float filter) {
    TestBank bank;
    bank.set(Pad::rim, sine16(kTestHz, kQuietAmplitude, 2 * kOneSecondFrames));
    Harness harness;
    harness.init(lofi(), bank.bank());
    Params params = default_params(lofi());
    params.filter = filter;
    harness.engine->set_params(params);
    harness.render(blocks_of_seconds(1.0f), {cue(0, 0, hit(Pad::rim))});
    return rms_of(harness.left, kSampleRate / 2, kSampleRate * 9 / 10);
  };
  const float open = steady(1.0f);
  REQUIRE(open > 0.0f);
  CHECK(db(steady(0.0f) / open) == doctest::Approx(-26.3).epsilon(0.06));
}

TEST_CASE("T-71 The glue compressor holds about 4:1 above its threshold") {
  SUBCASE("a 0 dBFS sine, 12 dB over the threshold, comes out 9 dB down plus the makeup") {
    Compressor compressor;
    const std::vector<float> out = through_stereo(compressor, sine(kTestHz, 1.0f, 2 * kOneSecondFrames));
    const float expected = gain_of_db(Compressor::kMakeupDb - 12.0f * (1.0f - 1.0f / Compressor::kRatio));
    CHECK(std::fabs(db(peak_of(out, kOneSecondFrames) / expected)) < 1.5f);
    CHECK(compressor.reduction_db() == doctest::Approx(9.0f).epsilon(0.2));
  }
  SUBCASE("a sine well under the threshold only gets the makeup gain") {
    Compressor compressor;
    const std::vector<float> out = through_stereo(compressor, sine(kTestHz, kQuietAmplitude, kOneSecondFrames));
    CHECK(peak_of(out, kSampleRate / 2) == doctest::Approx(kQuietAmplitude * gain_of_db(Compressor::kMakeupDb)).epsilon(0.01));
    CHECK(compressor.reduction_db() == 0.0f);
  }
}

TEST_CASE("T-72 The soft clipper is linear to -6 dBFS and never exceeds full scale") {
  CHECK(SoftClipper::shape(0.3f) == 0.3f);
  CHECK(SoftClipper::shape(-0.5f) == -0.5f);
  CHECK(SoftClipper::shape(0.5001f) == doctest::Approx(0.5001f).epsilon(1e-3));
  CHECK(SoftClipper::shape(2.0f) < 1.0f);
  CHECK(SoftClipper::shape(10.0f) <= 1.0f);
  CHECK(SoftClipper::shape(-10.0f) >= -1.0f);
  float previous = -1.0f;
  for (float x = -3.0f; x <= 3.0f; x += 0.01f) {
    const float y = SoftClipper::shape(x);
    REQUIRE(y > previous);
    previous = y;
  }
}

TEST_CASE("T-73 Nothing in the chain decays into a denormal") {
  SUBCASE("the whole engine after a hit and ten seconds of silence") {
    TestBank bank;
    bank.set(Pad::kick, sine16(60.0f, 0.9f, kSampleRate / 5));
    Harness harness;
    harness.init(lofi(), bank.bank());
    Params params = default_params(lofi());
    params.fx = 1.0f;
    harness.engine->set_params(params);
    harness.render(blocks_of_seconds(12.0f),
                   {cue(0, 0, hit(Pad::kick)), cue(0, 0, hit(Pad::chord, 1.0f, 60, 63, 67)), cue(0, 0, hit(Pad::pluck, 1.0f, 72))});
    CHECK(no_subnormal(harness.left));
    CHECK(no_subnormal(harness.right));
    CHECK(peak_of(harness.left, harness.left.size() - kSampleRate) < 1e-6f);
    CHECK(harness.engine->active_voices() == 0);
  }
  SUBCASE("a filter flushes to exactly zero") {
    Svf filter;
    filter.set(500.0f, kButterworthQ);
    std::vector<float> in(static_cast<size_t>(kSampleRate), 0.0f);
    in[0] = 1.0f;
    for (size_t at = 0; at < in.size(); at += kBlockSize) {
      filter.process(in.data() + at, kBlockSize);
      filter.flush();
    }
    CHECK(no_subnormal(in));
    CHECK(in.back() == 0.0f);
  }
  SUBCASE("the delay, the reverb and the compressor after an impulse") {
    Delay delay;
    Reverb reverb;
    Compressor compressor;
    std::vector<float> in(static_cast<size_t>(5 * kSampleRate), 0.0f);
    std::vector<float> left(in.size(), 0.0f);
    std::vector<float> right(in.size(), 0.0f);
    in[0] = 1.0f;
    for (size_t at = 0; at < in.size(); at += kBlockSize) {
      delay.process(in.data() + at, left.data() + at, right.data() + at, kBlockSize);
      reverb.process(in.data() + at, left.data() + at, right.data() + at, kBlockSize);
    }
    CHECK(no_subnormal(left));
    CHECK(no_subnormal(right));
    std::vector<float> burst(static_cast<size_t>(12 * kSampleRate), 0.0f);
    for (size_t i = 0; i < 4800; ++i) burst[i] = 1.0f;
    CHECK(no_subnormal(through_stereo(compressor, burst)));
  }
}

TEST_CASE("T-74 Sixteen voices sound at once and the seventeenth steals the quietest") {
  TestBank bank;
  Harness harness;
  harness.init(lofi(), bank.bank());
  std::vector<Cue> plucks;
  for (int i = 0; i < kVoiceCount; ++i) plucks.push_back(cue(0, i, hit(Pad::pluck, 1.0f, static_cast<uint8_t>(60 + i))));
  harness.render(1, plucks);
  CHECK(harness.engine->active_voices() == kVoiceCount);

  Harness more;
  more.init(lofi(), bank.bank());
  plucks.push_back(cue(1, 0, hit(Pad::pluck, 1.0f, 80)));
  more.render(2, plucks);
  CHECK(more.engine->active_voices() == kVoiceCount);

  SUBCASE("a monophonic pad keeps one voice: the second bass note releases the first") {
    Harness bass;
    bass.init(lofi(), bank.bank());
    bass.render(blocks_of_seconds(1.0f), {cue(0, 0, hit(Pad::bass, 1.0f, 36)), cue(2, 0, hit(Pad::bass, 1.0f, 39))});
    CHECK(bass.engine->active_voices() == 1);
  }
}

TEST_CASE("T-77 An impulse into the reverb leaves a tail that decays and ends") {
  Reverb reverb;
  std::vector<float> in(static_cast<size_t>(3 * kSampleRate), 0.0f);
  std::vector<float> left(in.size(), 0.0f);
  std::vector<float> right(in.size(), 0.0f);
  in[0] = 1.0f;
  for (size_t at = 0; at < in.size(); at += kBlockSize) {
    reverb.process(in.data() + at, left.data() + at, right.data() + at, kBlockSize);
  }
  const float early = rms_of(left, 0, kSampleRate / 10);
  const float later = rms_of(left, kSampleRate / 2, kSampleRate * 6 / 10);
  const float end = rms_of(left, 2 * kSampleRate, 3 * kSampleRate);
  CHECK(early > 0.0f);
  CHECK(later < early);
  CHECK(later > 0.0f);
  CHECK(db(end / early) < -60.0f);
  CHECK(left != right);  // the spread makes the channels differ
}
