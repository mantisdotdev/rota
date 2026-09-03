#include "app/jam.h"

#include <cstring>

#include "hal/hal.h"
#include "io/share.h"

namespace app {

namespace {

// The pattern of a track — its steps and how they play — without the mix. A jam sends
// what an undo would move (D-035): steps, alternation and speed travel; level, tone,
// send, chance and the transient mute stay the receiver's (§11, T-114).
void copy_pattern(engine::Track& dst, const engine::Track& src) {
  dst.alt = src.alt;
  dst.speed = src.speed;
  dst.step_count = src.step_count;
  for (int i = 0; i < engine::kMaxStepsPerTrack; ++i) dst.steps[i] = src.steps[i];
}

}  // namespace

void Jam::step(Model& model, const engine::Kit& kit, uint64_t now_us, const uint8_t* bytes, int count) {
  if (model.jam_request.pending) begin_send(model, kit, now_us);
  for (int i = 0; i < count; ++i) {
    io::Received msg;
    if (in_.feed(bytes[i], kit, msg)) apply(model, msg, now_us);
  }
}

void Jam::begin_send(Model& model, const engine::Kit& kit, uint64_t now_us) {
  const JamRequest req = model.jam_request;
  model.jam_request.pending = false;
  if (!hal::midi_port_open()) {  // no wire in this build, and no cable a jack could feel
    say(model.status, now_us, kStatusUs, "no jam link");
    return;
  }
  if (sending()) {  // one message at a time, so a held-show roll across the pads cannot queue eight
    say(model.status, now_us, kStatusUs, "still sending");
    return;
  }
  const engine::State& state = model.sections[model.current].state();
  out_len_ = req.track ? io::format_track(state, kit, req.pad, out_) : io::format_loop(state, kit, out_);
  out_at_ = 0;
  say(model.status, now_us, kStatusUs, req.track ? "sent a track" : "sent the loop");
}

void Jam::apply(Model& model, const io::Received& msg, uint64_t at_us) {
  engine::State& live = model.sections[model.current].push_edit();  // one undoable level (§6.7)
  const engine::State& in = msg.decoded.state;
  if (msg.track) {
    copy_pattern(live.tracks[msg.pad], in.tracks[msg.pad]);
    say(model.status, at_us, kStatusUs, "got a track");
  } else {
    for (int t = 0; t < engine::kTrackCount; ++t) copy_pattern(live.tracks[t], in.tracks[t]);
    std::memcpy(live.lineage, in.lineage, sizeof live.lineage);  // a whole loop keeps the sender's id (T-115, D-105)
    say(model.status, at_us, kStatusUs, "got a loop");
  }
}

void Jam::pump_out() {
  while (sending()) {
    const int took = hal::midi_send(out_ + out_at_, out_len_ - out_at_);
    if (took <= 0) return;  // the wire is full or absent: the rest waits for the next pass
    out_at_ += took;
  }
}

}  // namespace app
