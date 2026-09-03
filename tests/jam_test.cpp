// The jam link's gestures and arrivals (PRD §11, D-111, D-105): spec/scenarios.md
// T-113..T-116. app/ is one set of statics, so a send is checked by reading the bytes
// back off the fake wire and a receive by pushing a formatted message onto it; the
// two-device latency of T-19 is a bench measurement (T-118).
#include <vector>

#include "app_support.h"
#include "engine/kits/lofi.h"
#include "io/midi.h"
#include "io/share.h"

using namespace app_support;

namespace {

// Hold show until the share view opens, and leave show down for the send gesture.
void open_share_holding_show(World& w) {
  w.button_down(hal::Button::show);
  w.run_for(kSecond / 2);  // past the hold threshold: the share view opens
  REQUIRE(w.model().view == app::View::share);
}

std::vector<uint8_t> sent() {
  const std::vector<uint8_t>& v = hal_fake::midi_sent();
  return std::vector<uint8_t>(v.begin(), v.end());
}

// The one message the bytes on the wire decode to, through a fresh receiver.
bool decode_sent(const std::vector<uint8_t>& bytes, io::Received& out) {
  io::MidiPort port;
  bool got = false;
  for (uint8_t b : bytes)
    if (port.feed(b, app::kit(), out)) got = true;
  return got;
}

const char* kSenderLoop = "RT2:lofi:120:10:2:0:15:am:e10000-e1.0.0-e10108-e1-e1-e1-e1-e1";

// The pattern a jam copies: steps, alternation and speed (§11, T-114). Compared as the
// share-code spelling of the steps plus alt and speed, so a mismatch reads plainly.
bool same_pattern(const engine::Track& a, const engine::Track& b) {
  if (a.step_count != b.step_count || a.alt != b.alt || a.speed != b.speed) return false;
  for (int i = 0; i < a.step_count; ++i) {
    if (a.steps[i].hits != b.steps[i].hits || a.steps[i].note != b.steps[i].note) return false;
  }
  return true;
}

// The mix a jam never touches: level, tone, send, chance and the mute (§11, T-114).
bool same_mix(const engine::Track& a, const engine::Track& b) {
  return a.level == b.level && a.tone == b.tone && a.send == b.send && a.chance == b.chance && a.mute == b.mute;
}

}  // namespace

TEST_CASE("T-113 Hold show and press a pad or dice sends, and does not play the pad") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  w.tap(Pad::snare, 2);
  const int snare_steps = engine::track_of(w.state(0), Pad::snare).step_count;

  SUBCASE("a pad sends its track and neither sounds nor adds a hit") {
    open_share_holding_show(w);
    w.tap(Pad::snare);  // hold show is still down: this is a send, not a tap
    w.run_for(kSecond / 10);
    w.button_up(hal::Button::show);

    // The snare pad did not gain a step and did not mute; only a message went out.
    CHECK(engine::track_of(w.state(0), Pad::snare).step_count == snare_steps);
    io::Received got;
    REQUIRE(decode_sent(sent(), got));
    CHECK(got.track);
    CHECK(got.pad == engine::index_of(Pad::snare));
    CHECK(w.status() == "sent a track");
  }

  SUBCASE("dice sends the whole loop and neither fills nor clears") {
    open_share_holding_show(w);
    w.press(hal::Button::dice);  // show still down: send the loop
    w.run_for(kSecond / 10);
    w.button_up(hal::Button::show);
    io::Received got;
    REQUIRE(decode_sent(sent(), got));
    CHECK_FALSE(got.track);
    CHECK(w.status() == "sent the loop");
  }

  SUBCASE("with no port the gesture is harmless") {
    hal_fake::set_midi_port_open(false);
    open_share_holding_show(w);
    w.tap(Pad::snare);
    w.run_for(kSecond / 10);
    CHECK(sent().empty());
    CHECK(w.status() == "no jam link");
  }

  SUBCASE("a second gesture while one is still going out is refused") {
    hal_fake::choke_midi(true);  // the first message cannot drain
    open_share_holding_show(w);
    w.tap(Pad::snare);
    w.run_for(kSecond / 20);
    w.tap(Pad::kick);  // a second send while the first is stuck
    w.run_for(kSecond / 20);
    CHECK(w.status() == "still sending");
  }
}

TEST_CASE("T-114 A received track lands as one undoable edit, patterns only") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::hat, 1);  // the receiver's own hat, one step
  const engine::Track before = engine::track_of(w.state(0), Pad::hat);
  const engine::Track before_kick = engine::track_of(w.state(0), Pad::kick);
  const uint8_t receiver_bpm = w.state(0).bpm;  // 100

  // A sender whose hat is a different pattern (two steps, a split), at 120 bpm in A minor.
  const engine::State sender = engine::decode(kSenderLoop, app::kit()).state;
  uint8_t msg[io::kMessageCapacity];
  const int n = io::format_track(sender, app::kit(), engine::index_of(Pad::hat), msg);
  hal_fake::push_midi(msg, n);
  w.run_for(kSecond / 10);

  // The hat's whole pattern became the sender's — steps, alternation and speed — while
  // its mix and every global stayed the receiver's (patterns travel, knobs do not).
  const engine::Track after = engine::track_of(w.state(0), Pad::hat);
  CHECK(same_pattern(after, engine::track_of(sender, Pad::hat)));
  CHECK_FALSE(same_pattern(after, before));  // it really changed
  CHECK(same_mix(after, before));            // level, tone, send, chance and mute untouched
  CHECK(w.state(0).bpm == receiver_bpm);  // not the sender's 120
  CHECK(w.state(0).key.root == engine::make_state(app::kit()).key.root);  // still C, not the sender's A
  // No other track moved: a track message touches only its pad.
  CHECK(same_pattern(engine::track_of(w.state(0), Pad::kick), before_kick));
  CHECK(w.status() == "got a track");

  // It was a single undoable edit: one undo brings the receiver's hat back whole.
  w.press(hal::Button::undo);
  CHECK(same_pattern(engine::track_of(w.state(0), Pad::hat), before));
  CHECK(same_mix(engine::track_of(w.state(0), Pad::hat), before));
}

TEST_CASE("T-115 A received whole loop brings every pattern and the sender's id, no knobs") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 2);
  const uint8_t receiver_bpm = w.state(0).bpm;

  engine::Track before[engine::kTrackCount];
  for (int t = 0; t < engine::kTrackCount; ++t) before[t] = w.state(0).tracks[t];

  const engine::State sender = engine::decode(kSenderLoop, app::kit()).state;
  uint8_t msg[io::kMessageCapacity];
  const int n = io::format_loop(sender, app::kit(), msg);
  hal_fake::push_midi(msg, n);
  w.run_for(kSecond / 10);

  // Every track's whole pattern is the sender's, its mix the receiver's; the tempo and
  // key stay the receiver's.
  for (int t = 0; t < engine::kTrackCount; ++t) {
    CHECK(same_pattern(w.state(0).tracks[t], sender.tracks[t]));
    CHECK(same_mix(w.state(0).tracks[t], before[t]));
  }
  CHECK(w.state(0).bpm == receiver_bpm);
  // The loop carries the sender's own id as its lineage, so the share view can say what
  // it is based on (T-59, D-105); a track message never does.
  const engine::SectionCode code = io::shared_code(sender, app::kit());
  const std::string text = code.text;
  const std::string id = text.substr(text.find('~') + 1);
  CHECK(std::string(w.state(0).lineage) == id);
  CHECK(w.status() == "got a loop");

  // One undo brings the prior whole loop back: it arrived as a single edit.
  w.press(hal::Button::undo);
  for (int t = 0; t < engine::kTrackCount; ++t) CHECK(same_pattern(w.state(0).tracks[t], before[t]));
}

TEST_CASE("T-116 A wire that refuses bytes takes the whole message once, in order") {
  World w;
  hal_fake::set_midi_port_open(true);
  w.tap(Pad::kick, 4);
  hal_fake::choke_midi(true);  // the wire takes nothing at first

  open_share_holding_show(w);
  w.press(hal::Button::dice);  // queue the whole loop
  w.run_for(kSecond / 10);
  CHECK(sent().empty());  // nothing left while the wire refused

  hal_fake::choke_midi(false);  // the wire frees up
  w.run_for(kSecond / 10);
  w.button_up(hal::Button::show);

  // What was refused is offered again, so the code arrives once, in order, whole.
  io::Received got;
  REQUIRE(decode_sent(sent(), got));
  CHECK_FALSE(got.track);
  CHECK(got.decoded.ok);
}
