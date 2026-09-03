#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/share.h"
#include "engine/state.h"

// The jam link on the wire (PRD §11, D-111, D-114). Two devices trade patterns as
// MIDI System Exclusive, and the payload is an ordinary RT2 share code — so there is
// no second grammar to keep and no RT3 to version. This file is the envelope around
// that code and nothing else: no clock, no transport, no app state. The clock bytes
// that share the wire never reach here; the HAL lifts them (D-114).
//
// The envelope is
//   F0  7D  'R' 'T'  01  <type>  <pad>  <RT2 code…>  F7
// 0x7D is the SysEx id MIDI reserves for non-commercial and educational use (C-06);
// 'R' 'T' mark the message as ours, so another maker's 0x7D message is dropped rather
// than parsed; 01 is the protocol version, so a later firmware can change its mind by
// refusing an older one. <type> is 'L' for a whole loop or 'T' for one track, and
// <pad> is the pad a track was sent from (0 for a loop). The code carries the loop's
// own id, as the share view's does (§10.2, D-105); the receiver decides whether to
// keep it as lineage (a loop does, a track does not — that is app/'s call, §11).
//
// Every code byte is one of `A–Z a–z 0–9 : . , - ~ ; /` (§10), so its top bit is
// clear and none can be mistaken for a status byte: nothing is escaped, and the only
// bytes with the top bit set are the envelope's own F0 and F7. A byte the wire drops
// in the middle re-syncs on the next F0.
namespace io {

constexpr uint8_t kSysExStart = 0xF0;
constexpr uint8_t kSysExEnd = 0xF7;
constexpr uint8_t kManufacturer = 0x7D;
constexpr uint8_t kTagR = 'R';
constexpr uint8_t kTagT = 'T';
constexpr uint8_t kVersion = 1;
constexpr uint8_t kTypeLoop = 'L';
constexpr uint8_t kTypeTrack = 'T';
constexpr int kHeaderBytes = 6;  // manufacturer, R, T, version, type, pad

// The most an outgoing message is: the envelope around a section code, which is at
// most kSectionCodeCapacity including the NUL the code carries and this does not.
constexpr int kMessageCapacity = kHeaderBytes + engine::kSectionCodeCapacity + 1;  // +1 for F7

// What arrived, once a whole well-formed message has. `track` false is a loop, whose
// pad is 0; true is one track, on `pad` 0–7. `decoded` is the payload, always ok.
struct Received {
  bool track;
  int pad;
  engine::Decoded decoded;
};

// Writes the SysEx for the whole loop, or for one track, into `out` (which must hold
// kMessageCapacity), and returns the byte count. The payload is `state` as a share
// code with its own id (io::shared_code); a track message differs only in the two
// header bytes, so both spell the whole loop and the receiver takes what it asked for.
int format_loop(const engine::State& state, const engine::Kit& kit, uint8_t* out);
int format_track(const engine::State& state, const engine::Kit& kit, int pad, uint8_t* out);

// The receiver: a byte at a time off hal::midi_read, so a message can arrive across
// as many reads as the wire's pace spreads it over. Feed each byte; the call returns
// true when `byte` completed a well-formed message, whose contents are then in `out`.
// A message with a foreign id, a missing tag, the wrong version, an out-of-range pad,
// a payload that is not a code, or one longer than the engine accepts (D-106) returns
// false and changes nothing; the parser re-syncs on the next F0 either way.
class MidiPort {
 public:
  MidiPort() { reset(); }
  void reset();
  bool feed(uint8_t byte, const engine::Kit& kit, Received& out);

 private:
  // The engine caps a section code at kMaxSectionCodeInput (D-106); one past that,
  // plus the NUL the decoder reads, is all this holds, so the engine is the one place
  // that refuses an oversize code and the read here stays bounded.
  static constexpr int kPayloadCapacity = engine::kMaxSectionCodeInput + 2;

  bool in_message_;
  bool overflow_;
  int length_;  // bytes gathered after F0, header included
  uint8_t buffer_[kHeaderBytes + kPayloadCapacity];
};

}  // namespace io
