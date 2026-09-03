// Following an external clock (PRD §11, D-112, D-117..D-122): spec/scenarios.md
// T-104..T-114. The fake's now_us is set to us_of(frame) before every callback, so a
// pulse stamped at us_of(F) is placed at frame F (less the output buffer) — the same
// inverse deadline_of uses. The two-device 3 ms figure (T-19) is bench-only; here the
// follower's arithmetic is checked against a leader the test clocks itself.
#include <algorithm>
#include <vector>

#include "app_support.h"
#include "engine/kits/lofi.h"

using namespace app_support;

namespace {

int64_t midi_tick_frames(int bpm) { return kSecond * 60 / bpm / app::kMidiPulsesPerBeat; }
int64_t sync_pulse_frames(int bpm) { return kSecond * 60 / bpm / app::kSyncPulsesPerBeat; }
int64_t beat_frames(int bpm) { return kSecond * 60 / bpm; }

// A leader the test clocks: it emits a pulse whenever the world reaches the next grid
// point, advancing the world one block at a time so the app drains and folds it as it
// would a real wire.
struct Leader {
  World& w;
  hal::ClockPort port;
  int64_t per;
  int64_t next;

  Leader(World& world, hal::ClockPort p, int bpm)
      : w(world), port(p), per(p == hal::ClockPort::midi ? midi_tick_frames(bpm) : sync_pulse_frames(bpm)), next(0) {}

  void start() {  // MIDI Start marks the leader's cycle 0; sync carries none
    next = w.frames;
    if (port == hal::ClockPort::midi) hal_fake::push_clock_in(port, hal::ClockPulse::start, us_of(w.frames));
  }
  void run_to(int64_t target) {
    while (w.frames < target) {
      if (w.frames >= next) {
        hal_fake::push_clock_in(port, hal::ClockPulse::tick, us_of(next));  // stamped at the grid, not the block
        next += per;
      }
      w.run_for(kBlock);
    }
  }
  void stop_byte() { hal_fake::push_clock_in(port, hal::ClockPulse::stop, us_of(w.frames)); }
};

std::vector<int64_t> hits(const World& w, Pad pad) {
  std::vector<int64_t> out;
  for (const app::Fired& f : w.fired) {
    if (f.event.track == pad && !f.audition) out.push_back(f.sample);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The typical spacing between consecutive hits, ignoring the first (the count-in beat).
int64_t typical_gap(const std::vector<int64_t>& xs) {
  std::vector<int64_t> gaps;
  for (size_t i = 1; i < xs.size(); ++i) gaps.push_back(xs[i] - xs[i - 1]);
  std::sort(gaps.begin(), gaps.end());
  return gaps.empty() ? 0 : gaps[gaps.size() / 2];  // median: robust to the odd wrapped gap
}

bool near(int64_t a, int64_t b, int64_t tol) { return (a > b ? a - b : b - a) <= tol; }

}  // namespace

TEST_CASE("T-104 A device follows a MIDI leader's tempo and comes in on its cycle") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);  // the section's own tempo is 100 bpm
  REQUIRE(w.state(0).bpm == 100);

  Leader lead(w, hal::ClockPort::midi, 120);
  lead.start();
  const int64_t start_frame = w.frames;
  lead.run_to(w.frames + 2 * beat_frames(120));  // a couple of beats to lock

  CHECK(app::clock_following());
  CHECK(near(app::clock_measured_bpm(), 120, 1));

  w.press(hal::Button::play);  // play while following: the count-in waits for the leader's cycle
  lead.run_to(w.frames + 5 * 4 * beat_frames(120));
  w.collect();

  const std::vector<int64_t> kicks = hits(w, Pad::kick);
  REQUIRE(kicks.size() >= 8);
  // The beat is the leader's 120 (24000 frames), not the section's 100 (28800).
  CHECK(near(typical_gap(kicks), beat_frames(120), 200));
  // The kick's own bpm field never changed: the wire owns tempo, not the pattern (D-112).
  CHECK(w.state(0).bpm == 100);
  // Phase lock: every kick, heard one output buffer after its sample, sits on the
  // leader's beat grid laid from its Start (D-112). The first kick is the count-in's
  // beat, on the leader's cycle downbeat.
  const int64_t beat = beat_frames(120);
  const int64_t cycle = 4 * beat;
  auto phase_mod = [&](int64_t k, int64_t m) { return ((k + hal::kAudioBlockFrames - start_frame) % m + m) % m; };
  for (int64_t k : kicks) {
    const int64_t phase = phase_mod(k, beat);
    CHECK((phase < 600 || phase > beat - 600));
  }
  const int64_t first = phase_mod(kicks.front(), cycle);
  CHECK((first < 600 || first > cycle - 600));
}

TEST_CASE("T-107 When the leader goes quiet the loop keeps its tempo and the sections adopt it") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  Leader lead(w, hal::ClockPort::midi, 120);
  lead.start();
  lead.run_to(w.frames + 2 * beat_frames(120));
  w.press(hal::Button::play);
  lead.run_to(w.frames + 3 * 4 * beat_frames(120));
  REQUIRE(app::clock_following());

  // The leader stops. For a second and a half a gap changes nothing.
  w.run_for(kSecond);
  CHECK(app::clock_following());  // still following, tempo held
  w.run_for(kSecond);                  // now past 1.5 s of silence
  CHECK_FALSE(app::clock_following());

  // The sections took the measured tempo as if a knob had set it, and the loop plays on.
  CHECK(w.state(0).bpm == 120);
  CHECK(w.status() == "ext off, 120 bpm");
  w.collect();
  const size_t before = hits(w, Pad::kick).size();
  w.run_for(4 * beat_frames(120));
  CHECK(hits(w, Pad::kick).size() > before);  // no glitch, no stop (T-20)

  SUBCASE("a MIDI Stop ends the follow at once, before the quiet timeout") {
    World w2;
    hal_fake::set_midi_port_open(true);
    w2.tap(Pad::kick, 4);
    Leader lead2(w2, hal::ClockPort::midi, 120);
    lead2.start();
    lead2.run_to(w2.frames + 2 * beat_frames(120));
    w2.press(hal::Button::play);
    lead2.run_to(w2.frames + 2 * 4 * beat_frames(120));
    REQUIRE(app::clock_following());
    lead2.stop_byte();
    w2.run_for(kSecond / 4);  // well under the 1.5 s quiet timeout
    CHECK_FALSE(app::clock_following());
    CHECK(w2.state(0).bpm == 120);  // the last measured tempo is adopted
  }
}

TEST_CASE("T-105 A device follows the sync jack when no MIDI is present") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  Leader lead(w, hal::ClockPort::sync, 100);
  lead.start();  // sync sends no Start; begins its grid on the first pulse
  lead.run_to(w.frames + 3 * beat_frames(100));  // sync is coarse: a few beats to lock

  CHECK(app::clock_following());
  CHECK(near(app::clock_measured_bpm(), 100, 3));  // 2 PPQN is coarse, so a little slack

  w.press(hal::Button::play);
  lead.run_to(w.frames + 5 * 4 * beat_frames(100));
  w.collect();
  const std::vector<int64_t> kicks = hits(w, Pad::kick);
  REQUIRE(kicks.size() >= 8);
  CHECK(near(typical_gap(kicks), beat_frames(100), 400));
}

TEST_CASE("T-106 MIDI wins over sync whenever MIDI is alive") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  Leader midi(w, hal::ClockPort::midi, 120);
  Leader sync(w, hal::ClockPort::sync, 90);
  midi.start();
  sync.start();
  // Clock both for two beats; they interleave on the two rings.
  const int64_t until = w.frames + 2 * beat_frames(120);
  while (w.frames < until) {
    if (w.frames >= midi.next) { hal_fake::push_clock_in(hal::ClockPort::midi, hal::ClockPulse::tick, us_of(midi.next)); midi.next += midi.per; }
    if (w.frames >= sync.next) { hal_fake::push_clock_in(hal::ClockPort::sync, hal::ClockPulse::tick, us_of(sync.next)); sync.next += sync.per; }
    w.run_for(kBlock);
  }
  CHECK(app::clock_following());
  CHECK(near(app::clock_measured_bpm(), 120, 2));  // MIDI's tempo, not sync's 90
}

TEST_CASE("T-108 A garbage-fast wire cannot drive the beat below the range or hang the loop") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  Leader lead(w, hal::ClockPort::midi, 400);  // far above §6.3's 180 ceiling
  lead.start();
  lead.run_to(w.frames + 2 * beat_frames(180));
  w.press(hal::Button::play);
  lead.run_to(w.frames + 5 * 4 * beat_frames(180));  // the run completing at all proves push_window did not spin
  w.collect();
  const std::vector<int64_t> kicks = hits(w, Pad::kick);
  REQUIRE(kicks.size() >= 8);
  CHECK(typical_gap(kicks) >= app::kMinBeatFrames - 50);  // clamped at 16000, never faster
}

TEST_CASE("T-109 A pulse stamped before the newest anchor maps behind it, not far ahead") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  Leader lead(w, hal::ClockPort::midi, 120);
  lead.start();
  lead.run_to(w.frames + 2 * beat_frames(120));
  w.press(hal::Button::play);
  lead.run_to(w.frames + 2 * 4 * beat_frames(120));
  REQUIRE(app::clock_following());

  // A tick whose stamp is a millisecond old — as a real ISR stamp read a pass later is.
  hal_fake::push_clock_in(hal::ClockPort::midi, hal::ClockPulse::tick, us_of(w.frames) - 1000);
  lead.run_to(w.frames + 4 * beat_frames(120));
  w.collect();
  // No wrap-to-1e13 blow-up: the loop is still following at a sane tempo.
  CHECK(app::clock_following());
  CHECK(near(app::clock_measured_bpm(), 120, 2));
}
