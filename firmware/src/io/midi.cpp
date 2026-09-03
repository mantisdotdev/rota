#include "io/midi.h"

#include "io/share.h"

namespace io {

namespace {

int format(uint8_t type, int pad, const engine::State& state, const engine::Kit& kit, uint8_t* out) {
  const engine::SectionCode code = shared_code(state, kit);
  int i = 0;
  out[i++] = kSysExStart;
  out[i++] = kManufacturer;
  out[i++] = kTagR;
  out[i++] = kTagT;
  out[i++] = kVersion;
  out[i++] = type;
  out[i++] = static_cast<uint8_t>(pad);
  for (const char* c = code.text; *c != '\0'; ++c) out[i++] = static_cast<uint8_t>(*c);
  out[i++] = kSysExEnd;
  return i;
}

}  // namespace

int format_loop(const engine::State& state, const engine::Kit& kit, uint8_t* out) {
  return format(kTypeLoop, 0, state, kit, out);
}

int format_track(const engine::State& state, const engine::Kit& kit, int pad, uint8_t* out) {
  return format(kTypeTrack, pad, state, kit, out);
}

void MidiPort::reset() {
  in_message_ = false;
  overflow_ = false;
  length_ = 0;
}

bool MidiPort::feed(uint8_t byte, const engine::Kit& kit, Received& out) {
  if (byte == kSysExStart) {  // a new message begins, and discards any half-read one
    in_message_ = true;
    overflow_ = false;
    length_ = 0;
    return false;
  }
  if (!in_message_) return false;

  if (byte != kSysExEnd) {
    if (byte >= 0x80) return false;  // a status byte in the middle is not payload; skip it, stay in the message
    if (length_ < static_cast<int>(sizeof buffer_) - 1) {
      buffer_[length_++] = byte;  // room kept for the NUL the decoder reads
    } else {
      overflow_ = true;  // a code longer than the engine takes; keep counting to F7, then drop
    }
    return false;
  }

  // F7: the message is whole. Whatever the outcome, the next byte starts fresh.
  in_message_ = false;
  if (overflow_ || length_ < kHeaderBytes) return false;
  if (buffer_[0] != kManufacturer || buffer_[1] != kTagR || buffer_[2] != kTagT || buffer_[3] != kVersion) return false;
  const uint8_t type = buffer_[4];
  const int pad = buffer_[5];
  if (type == kTypeLoop) {
    if (pad != 0) return false;
  } else if (type == kTypeTrack) {
    if (pad >= engine::kTrackCount) return false;  // a data byte reaches here 0–127; a track is 0–7
  } else {
    return false;
  }
  buffer_[length_] = '\0';
  const engine::Decoded decoded = engine::decode(reinterpret_cast<const char*>(buffer_ + kHeaderBytes), kit);
  if (!decoded.ok) return false;
  out.track = type == kTypeTrack;
  out.pad = pad;
  out.decoded = decoded;
  return true;
}

}  // namespace io
