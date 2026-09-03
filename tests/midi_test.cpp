// The jam link's SysEx (PRD §11, D-111, D-114): spec/scenarios.md T-111, T-112.
// io::MidiPort is a pure parser and formatter — no app state, no clock — so these
// drive it directly with byte arrays.
#include <cstdint>
#include <vector>

#include "engine_support.h"
#include "io/midi.h"
#include "io/share.h"

using namespace support;

namespace {

std::vector<uint8_t> loop_bytes(const engine::State& state) {
  uint8_t out[io::kMessageCapacity];
  const int n = io::format_loop(state, lofi(), out);
  return std::vector<uint8_t>(out, out + n);
}

std::vector<uint8_t> track_bytes(const engine::State& state, int pad) {
  uint8_t out[io::kMessageCapacity];
  const int n = io::format_track(state, lofi(), pad, out);
  return std::vector<uint8_t>(out, out + n);
}

// Feeds every byte; returns true if one of them completed a well-formed message, and
// leaves that message in `out`. A parser that refuses never returns true.
bool feed_all(io::MidiPort& port, const std::vector<uint8_t>& bytes, io::Received& out) {
  bool got = false;
  for (const uint8_t b : bytes) {
    if (port.feed(b, lofi(), out)) got = true;
  }
  return got;
}

engine::State beat(const char* code) { return engine::decode(code, lofi()).state; }

// Two states carry the same loop when their share codes with their own ids match:
// the id is a hash of the bare code, so equal patterns give equal codes (D-105).
std::string same_loop(const engine::State& state) { return io::shared_code(state, lofi()).text; }

const char* kEmpty = "RT2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1";
const char* kClassic = "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1";
const char* kWorst =
    "RT2:lofi:180:10:10:10:100:csdor:fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-"
    "fhoooooooooooooooo,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-"
    "fhoooooooooooooooo,7959~zzzzzz";

}  // namespace

TEST_CASE("T-111 A loop and a track go out as SysEx and read back as the same loop") {
  io::MidiPort port;
  io::Received got;

  SUBCASE("the envelope is F0 7D R T 01, the type and pad, the code, F7") {
    const std::vector<uint8_t> bytes = loop_bytes(beat(kClassic));
    REQUIRE(bytes.size() >= 8);
    CHECK(bytes[0] == io::kSysExStart);
    CHECK(bytes[1] == io::kManufacturer);
    CHECK(bytes[2] == io::kTagR);
    CHECK(bytes[3] == io::kTagT);
    CHECK(bytes[4] == io::kVersion);
    CHECK(bytes[5] == io::kTypeLoop);
    CHECK(bytes[6] == 0);  // a loop names pad 0
    CHECK(bytes.back() == io::kSysExEnd);
    // Every payload byte has its top bit clear, so none can be mistaken for a status
    // byte and nothing is escaped (§10, D-114).
    for (size_t i = io::kHeaderBytes + 1; i + 1 < bytes.size(); ++i) CHECK(bytes[i] < 0x80);
  }

  SUBCASE("a loop round-trips: empty, the classic beat, and the worst case G-14") {
    for (const char* code : {kEmpty, kClassic, kWorst}) {
      port.reset();
      const engine::State state = beat(code);
      REQUIRE(feed_all(port, loop_bytes(state), got));
      CHECK_FALSE(got.track);
      CHECK(got.pad == 0);
      REQUIRE(got.decoded.ok);
      CHECK(same_loop(got.decoded.state) == same_loop(state));
    }
  }

  SUBCASE("G-JAM-01 the empty loop is byte-identical to spec/jam-link.md") {
    const uint8_t golden[] = {
        0xF0, 0x7D, 0x52, 0x54, 0x01, 0x4C, 0x00, 0x52, 0x54, 0x32, 0x3A, 0x6C, 0x6F, 0x66, 0x69, 0x3A,
        0x31, 0x30, 0x30, 0x3A, 0x31, 0x30, 0x3A, 0x32, 0x3A, 0x30, 0x3A, 0x31, 0x35, 0x3A, 0x63, 0x6D,
        0x3A, 0x65, 0x31, 0x2D, 0x65, 0x31, 0x2D, 0x65, 0x31, 0x2D, 0x65, 0x31, 0x2D, 0x65, 0x31, 0x2D,
        0x65, 0x31, 0x2D, 0x65, 0x31, 0x2D, 0x65, 0x31, 0x7E, 0x61, 0x76, 0x30, 0x73, 0x39, 0x65, 0xF7};
    const std::vector<uint8_t> bytes = loop_bytes(beat(kEmpty));
    REQUIRE(bytes.size() == sizeof golden);
    for (size_t i = 0; i < bytes.size(); ++i) CHECK(bytes[i] == golden[i]);
  }

  SUBCASE("a track carries the pad it was sent from and the whole loop behind it") {
    const engine::State state = beat(kClassic);
    const std::vector<uint8_t> bytes = track_bytes(state, 4);
    CHECK(bytes[5] == io::kTypeTrack);
    CHECK(bytes[6] == 4);
    REQUIRE(feed_all(port, bytes, got));
    CHECK(got.track);
    CHECK(got.pad == 4);
    CHECK(same_loop(got.decoded.state) == same_loop(state));
  }

  SUBCASE("the id the code carries is the loop's own, so a receiver can keep it as lineage") {
    const engine::State state = beat(kClassic);
    REQUIRE(feed_all(port, loop_bytes(state), got));
    // shared_code appends the bare code's own id after `~`; decode reads it as lineage.
    CHECK(got.decoded.state.lineage[0] != '\0');
    CHECK(std::string(got.decoded.state.lineage) == std::string(io::shared_code(state, lofi()).text).substr(
                                                        std::string(io::shared_code(state, lofi()).text).find('~') + 1));
  }
}

TEST_CASE("T-112 A malformed message changes nothing and the parser re-syncs on the next F0") {
  io::MidiPort port;
  io::Received got;
  const engine::State state = beat(kClassic);

  auto refused = [&](std::vector<uint8_t> bytes) {
    port.reset();
    const bool got_one = feed_all(port, bytes, got);
    CHECK_FALSE(got_one);
    // Whatever was refused, a good message straight after still arrives.
    CHECK(feed_all(port, loop_bytes(state), got));
  };

  SUBCASE("a foreign manufacturer id") {
    std::vector<uint8_t> b = loop_bytes(state);
    b[1] = 0x43;  // not 0x7D
    refused(b);
  }
  SUBCASE("our id but not our tag") {
    std::vector<uint8_t> b = loop_bytes(state);
    b[2] = 'X';  // the 'R' is gone
    refused(b);
  }
  SUBCASE("a protocol version this firmware does not know") {
    std::vector<uint8_t> b = loop_bytes(state);
    b[4] = 2;
    refused(b);
  }
  SUBCASE("a payload that is not a code") {
    std::vector<uint8_t> b = {io::kSysExStart, io::kManufacturer, io::kTagR, io::kTagT,
                              io::kVersion,    io::kTypeLoop,     0,        'n',
                              'o',             'p',               'e',      io::kSysExEnd};
    refused(b);
  }
  SUBCASE("a song code where a section code belongs") {
    std::vector<uint8_t> b = {io::kSysExStart, io::kManufacturer, io::kTagR, io::kTagT, io::kVersion, io::kTypeLoop, 0};
    for (const char* c = "RT2S:lofi"; *c; ++c) b.push_back(static_cast<uint8_t>(*c));
    b.push_back(io::kSysExEnd);
    refused(b);
  }
  SUBCASE("a payload longer than the engine accepts (D-106)") {
    std::vector<uint8_t> b = {io::kSysExStart, io::kManufacturer, io::kTagR, io::kTagT, io::kVersion, io::kTypeLoop, 0};
    for (int i = 0; i < 600; ++i) b.push_back('e');  // past kMaxSectionCodeInput
    b.push_back(io::kSysExEnd);
    refused(b);
  }
  SUBCASE("a loop that names a pad, or a track whose pad is not a track") {
    std::vector<uint8_t> loop = loop_bytes(state);
    loop[6] = 5;  // a loop must be pad 0
    refused(loop);
    std::vector<uint8_t> track = track_bytes(state, 0);
    track[6] = 8;  // there is no track 8
    refused(track);
  }
  SUBCASE("a message cut short, then a whole one") {
    std::vector<uint8_t> truncated = loop_bytes(state);
    truncated.pop_back();  // no F7
    refused(truncated);  // the next F0 in loop_bytes discards the truncated one
  }
  SUBCASE("a control byte in the payload is refused, not truncated to its valid prefix") {
    std::vector<uint8_t> b = loop_bytes(state);  // a complete valid message
    b.insert(b.end() - 1, 'X');   // trailing garbage, before F7
    b.insert(b.end() - 2, 0x00);  // a NUL after the complete code: it would end the decoder's C string
    refused(b);  // without the guard, decode would accept the prefix and ignore the X
  }

  SUBCASE("a status byte scattered through a good message does not break it") {
    std::vector<uint8_t> b = loop_bytes(state);
    b.insert(b.begin() + io::kHeaderBytes + 3, 0xF8);  // a clock byte the HAL would normally have lifted
    REQUIRE(feed_all(port, b, got));
    CHECK(same_loop(got.decoded.state) == same_loop(state));
  }
}
