#pragma once

#include <cstdint>

#include "app/model.h"
#include "engine/kit.h"
#include "io/midi.h"

// The jam link's app side (PRD §11, D-111), the counterpart to app/card.cpp. The
// show-held gesture records what to send in model.jam_request; this carries it out,
// exactly as the song view records a pick and the card carries it out. One message
// goes out at a time. A message that arrives lands as a single undoable edit on the
// section being edited (§6.7, D-003), moving no knob (D-035).
namespace app {

class Jam {
 public:
  // From app::tick under hal::lock(): starts the send the gesture asked for, if any,
  // and applies every message the incoming bytes complete. `bytes` are what
  // hal::midi_read handed the main loop this pass; `count` is bounded by the caller.
  void step(Model& model, const engine::Kit& kit, uint64_t now_us, const uint8_t* bytes, int count);

  // From app::tick outside the lock: puts the outgoing message on the wire as fast as
  // hal::midi_send will take it, which on the device is one byte a UART slot so a clock
  // pulse never waits long behind a pattern (D-114). What the wire refuses is offered
  // again next pass, so the code arrives once, in order.
  void pump_out();

 private:
  void begin_send(Model& model, const engine::Kit& kit, uint64_t now_us);
  void apply(Model& model, const io::Received& msg, uint64_t at_us);
  bool sending() const { return out_at_ < out_len_; }

  io::MidiPort in_;
  uint8_t out_[io::kMessageCapacity];
  int out_len_ = 0;
  int out_at_ = 0;
};

}  // namespace app
