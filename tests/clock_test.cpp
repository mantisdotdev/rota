// The clock on the wire (PRD §7.6, §11): spec/scenarios.md T-102, T-103.
//
// The fake's clock is the audio clock — the harness sets now_us from the frame count
// before every callback — so a pulse due on a frame has one right microsecond and the
// test can name it. What it cannot check is a real UART or a real pin; that is T-118,
// on the bench.
#include <string>
#include <vector>

#include "app_support.h"
#include "engine/kits/lofi.h"
#include "ui/settings.h"

using namespace app_support;

namespace {

// A pulse due on `frame` is armed for the microsecond that frame reaches the output,
// which is one platform buffer later than the block that rendered it: our own pulse
// has to coincide with our own sound (D-114).
uint64_t heard_at(int64_t frame) { return us_of(frame + hal::audio_buffer_frames()); }

// The conversion floors twice — the anchor's own microsecond, then the offset from it
// — so a deadline may sit a microsecond either side of the exact answer.
constexpr uint64_t kRounding = 2;

bool near_us(uint64_t got, uint64_t want) {
  return got > want ? got - want <= kRounding : want - got <= kRounding;
}

std::vector<uint64_t> deadlines(hal::ClockPort port, hal::ClockPulse pulse) {
  std::vector<uint64_t> out;
  for (const hal_fake::ClockOut& sent : hal_fake::clock_out()) {
    if (sent.port == port && sent.pulse == pulse) out.push_back(sent.at_us);
  }
  return out;
}

int count_of(hal::ClockPort port, hal::ClockPulse pulse) { return static_cast<int>(deadlines(port, pulse).size()); }

// Every pulse on `port` due before `until`, so the ones armed a lookahead past the
// end of the run are not counted against it. The boundary is nudged by the rounding,
// since a pulse due exactly on `until` belongs to what comes after.
int ticks_before(hal::ClockPort port, int64_t until) {
  int count = 0;
  for (const uint64_t at_us : deadlines(port, hal::ClockPulse::tick)) {
    if (at_us + kRounding < heard_at(until)) count += 1;
  }
  return count;
}

// The grid point nearest `at_us`, as the microsecond it would be heard at. Reading a
// microsecond back as a frame is coarse — one microsecond is 48 frames — but grid
// points are thousands of frames apart, so which one is meant is never in doubt, and
// the comparison itself is then exact.
uint64_t nearest_grid(uint64_t at_us, int64_t origin, int64_t step) {
  const int64_t frame =
      static_cast<int64_t>(at_us) * sound::kSampleRate / 1000000 - hal::audio_buffer_frames();
  const int64_t index = (frame - origin + step / 2) / step;
  return heard_at(origin + index * step);
}

// Every pulse from `from` onwards sits on the grid `origin` and `step` describe, and
// no two of them are closer together than the grid: a backlog is never fired off as a
// burst, however long the wire was busy.
void check_on_grid(const std::vector<uint64_t>& pulses, size_t from, int64_t origin, int64_t step) {
  for (size_t i = from; i < pulses.size(); ++i) {
    CHECK(near_us(pulses[i], nearest_grid(pulses[i], origin, step)));
    if (i > from) CHECK(pulses[i] - pulses[i - 1] >= us_of(step) - kRounding);
  }
}

void open_settings(World& w) {
  w.button_down(hal::Button::undo);
  w.run_for(kSecond / 10);
  w.button_down(hal::Button::show);
  w.run_for(kSecond / 2);
  w.button_up(hal::Button::show);
  w.button_up(hal::Button::undo);
  REQUIRE(w.model().view == app::View::settings);
}

// Puts the cursor on `row` and turns the filter knob, which is what sets a row (D-096).
void set_row(World& w, ui::SettingsRow row, bool on) {
  w.turn(hal::Encoder::speed, static_cast<int>(row) - w.model().settings_cursor);
  REQUIRE(w.model().settings_cursor == static_cast<int>(row));
  w.turn(hal::Encoder::filter, on ? 1 : -1);
}

}  // namespace

TEST_CASE("T-102 The beat goes out on both ports while playing, and each row can stop it") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  REQUIRE(w.state(0).bpm == 100);
  w.play();
  w.run_until(w.cycle_start(2));

  // 24 to the beat and four beats to a cycle is 96, which is the number that lets a
  // follower find the cycle and not just the beat (D-112); the sync jack carries the
  // Pocket Operator's two.
  const int64_t beat = kBeatFrames;
  CHECK(ticks_before(hal::ClockPort::midi, w.cycle_start(2)) == 2 * 96);
  CHECK(ticks_before(hal::ClockPort::sync, w.cycle_start(2)) == 2 * 8);

  // Every pulse on its own frame, and the last of the two cycles exactly two cycles
  // on: the spacing is derived from the beat each time, so nothing accumulates.
  const std::vector<uint64_t> midi = deadlines(hal::ClockPort::midi, hal::ClockPulse::tick);
  const std::vector<uint64_t> sync = deadlines(hal::ClockPort::sync, hal::ClockPulse::tick);
  REQUIRE(midi.size() >= 192);
  REQUIRE(sync.size() >= 16);
  for (int i = 0; i < 192; ++i) {
    CHECK(near_us(midi[static_cast<size_t>(i)], heard_at(w.origin + static_cast<int64_t>(i) * beat / 24)));
  }
  for (int i = 0; i < 16; ++i) {
    CHECK(near_us(sync[static_cast<size_t>(i)], heard_at(w.origin + static_cast<int64_t>(i) * beat / 2)));
  }

  // Start is one byte early so the tick it belongs to lands on the beat: two bytes
  // cannot leave a 31250 baud wire at once.
  REQUIRE(count_of(hal::ClockPort::midi, hal::ClockPulse::start) == 1);
  CHECK(deadlines(hal::ClockPort::midi, hal::ClockPulse::start)[0] == midi[0] - hal::kMidiByteUs);
  CHECK(count_of(hal::ClockPort::sync, hal::ClockPulse::start) == 0);  // the sync wire has no room for a transport

  // Stop leaves at once, because the hits already handed over stop sounding at once
  // (T-82), and Continue is never sent at all: a Rota stop always rewinds.
  CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::stop) == 0);
  w.press(hal::Button::play);
  w.frame();
  CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::stop) == 1);
  CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::resume) == 0);

  SUBCASE("a row switched off stops its own port and leaves the other one alone") {
    open_settings(w);
    set_row(w, ui::SettingsRow::midi_clock_out, false);
    w.press(hal::Button::show);
    const int midi_before = count_of(hal::ClockPort::midi, hal::ClockPulse::tick);
    const int sync_before = count_of(hal::ClockPort::sync, hal::ClockPulse::tick);
    w.play();
    w.run_until(w.cycle_start(1));
    CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::tick) == midi_before);
    CHECK(count_of(hal::ClockPort::sync, hal::ClockPulse::tick) > sync_before);
    // No Start either: a Start byte with no clock behind it would tell a listener to
    // run on a tempo it will never be given.
    CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::start) == 1);
  }

  SUBCASE("a row switched on mid-play lands in phase instead of firing a burst") {
    open_settings(w);
    set_row(w, ui::SettingsRow::sync_out, false);
    w.press(hal::Button::show);
    w.play();
    w.run_until(w.cycle_start(1));
    const int quiet = count_of(hal::ClockPort::sync, hal::ClockPulse::tick);
    open_settings(w);
    set_row(w, ui::SettingsRow::sync_out, true);
    w.press(hal::Button::show);
    w.run_for(2 * kBeatFrames);
    const std::vector<uint64_t> sync = deadlines(hal::ClockPort::sync, hal::ClockPulse::tick);
    REQUIRE(static_cast<int>(sync.size()) > quiet);
    // The count kept moving while the row was off, so what comes back is the grid the
    // port would have been on all along — not a burst timed from the moment it
    // returned, and not a pulse dropped because it was already due.
    check_on_grid(sync, static_cast<size_t>(quiet), w.origin, kBeatFrames / app::kSyncPulsesPerBeat);
  }
}

TEST_CASE("T-103 The pulse spacing follows the tempo, and a refused pulse is not turned into a burst") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);

  SUBCASE("at 60 bpm and at 180 bpm the sync pulses stay inside a Pocket Operator's window") {
    w.turn(hal::Encoder::speed, -100);  // clamped at the bottom of §6.3's range
    REQUIRE(w.state(0).bpm == 60);
    w.play();
    w.run_until(w.origin + 2 * kSecond);
    const std::vector<uint64_t> slow = deadlines(hal::ClockPort::sync, hal::ClockPulse::tick);
    REQUIRE(slow.size() >= 3);
    const uint64_t slow_step = slow[1] - slow[0];
    CHECK(near_us(slow_step, 500000));  // 2 PPQN at 60 bpm: half a second
    CHECK(near_us(slow[2] - slow[1], slow_step));
    const std::vector<uint64_t> slow_midi = deadlines(hal::ClockPort::midi, hal::ClockPulse::tick);
    REQUIRE(slow_midi.size() >= 2);
    CHECK(near_us(slow_midi[1] - slow_midi[0], 41666));  // 24 PPQN at 60 bpm

    w.press(hal::Button::play);
    w.frame();
    w.turn(hal::Encoder::speed, 120);  // one bpm a detent, clamped at the top of the range
    REQUIRE(w.state(0).bpm == 180);
    const int before = count_of(hal::ClockPort::sync, hal::ClockPulse::tick);
    w.play();
    w.run_until(w.origin + kSecond);
    const std::vector<uint64_t> fast = deadlines(hal::ClockPort::sync, hal::ClockPulse::tick);
    REQUIRE(static_cast<int>(fast.size()) >= before + 3);
    const uint64_t fast_step = fast[static_cast<size_t>(before) + 1] - fast[static_cast<size_t>(before)];
    CHECK(near_us(fast_step, 166666));  // 2 PPQN at 180 bpm
    // Both ends of §6.3's tempo range sit inside the 12.5 ms to 1.5 s a Pocket
    // Operator's input accepts (BOM, PO-16 guide), so the jack works across it.
    CHECK(fast_step > 12500);
    CHECK(slow_step < 1500000);
  }

  SUBCASE("an overslept timer sends the pulses it missed rather than dropping them") {
    w.play();
    w.run_for(kBeatFrames / 4);
    const int before = count_of(hal::ClockPort::midi, hal::ClockPulse::tick);
    const int64_t resumes = w.skip_timer_until(w.frames + kBeatFrames / 2);  // no tick for half a beat
    w.run_until(resumes + kBlock);
    w.run_for(kBlock);
    // The pulses of that half beat are on the wire with the deadlines they were due,
    // late rather than lost: a lost pulse is a phase error nothing afterwards can
    // correct, since 24 PPQN carries a tempo and no downbeat.
    CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::tick) > before + 8);
    const std::vector<uint64_t> midi = deadlines(hal::ClockPort::midi, hal::ClockPulse::tick);
    for (size_t i = 1; i < midi.size(); ++i) {
      CHECK(near_us(midi[i] - midi[i - 1], us_of(kBeatFrames / 24)));  // still one grid, still no drift
    }
  }

  SUBCASE("a port that still has a pulse armed is offered the next one on the next tick") {
    w.play();
    w.run_for(kBeatFrames / 4);
    const int before = count_of(hal::ClockPort::midi, hal::ClockPulse::tick);
    REQUIRE(before > 0);
    hal_fake::refuse_clock_out(true);
    w.run_for(kBeatFrames / 2);
    CHECK(count_of(hal::ClockPort::midi, hal::ClockPulse::tick) == before);  // nothing was taken
    hal_fake::refuse_clock_out(false);
    w.run_for(2 * kBeatFrames);
    const std::vector<uint64_t> midi = deadlines(hal::ClockPort::midi, hal::ClockPulse::tick);
    REQUIRE(static_cast<int>(midi.size()) > before);
    // What the wire refused is offered again and lands back on the same grid, one
    // pulse at a time: a beat's worth of refusals never becomes a burst of 24.
    check_on_grid(midi, static_cast<size_t>(before), w.origin, kBeatFrames / app::kMidiPulsesPerBeat);
  }
}
