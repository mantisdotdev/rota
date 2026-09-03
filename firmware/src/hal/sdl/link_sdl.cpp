// The jam link for the simulator (PRD §12 rule 5, §11, D-114). A desk machine has no
// MIDI DIN, so two simulators share the MIDI cable over a UDP pair on localhost: start
// each with ROTA_LINK=<my port>:<their port>, crossed, and one can follow the other's
// clock and pass a loop across, so the reference platform can rehearse a jam before the
// board exists. With no ROTA_LINK there is no port and the simulator behaves exactly as
// it did before there was a wire.
//
// This models the MIDI cable only: the four System Real Time bytes and our SysEx, both
// ways, metered at 31250 baud so a transfer feels the ~79 ms a share code costs on the
// wire. It does not model the Pocket Operator's sync jack — there is no PO on a desk — so
// send_clock_out on the sync port is accepted and dropped, and no sync pulse ever arrives.
// What loopback UDP cannot rehearse is a framing error, a baud mismatch or cable noise, so
// io/'s recovery path is reached only through the test fake (tests/hal_fake.cpp).
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hal/hal.h"
#include "hal/sdl/sdl_internal.h"

namespace {

bool open_ = false;
int sock_ = -1;
sockaddr_in peer_{};
uint64_t next_send_us_ = 0;  // the wire is free again at this microsecond (31250 baud)

bool real_time_pulse(uint8_t byte, hal::ClockPulse& pulse) {
  switch (byte) {
    case 0xF8: pulse = hal::ClockPulse::tick; return true;
    case 0xFA: pulse = hal::ClockPulse::start; return true;
    case 0xFB: pulse = hal::ClockPulse::resume; return true;
    case 0xFC: pulse = hal::ClockPulse::stop; return true;
    default: return false;
  }
}

uint8_t status_of(hal::ClockPulse pulse) {
  switch (pulse) {
    case hal::ClockPulse::tick: return 0xF8;
    case hal::ClockPulse::start: return 0xFA;
    case hal::ClockPulse::resume: return 0xFB;
    case hal::ClockPulse::stop: return 0xFC;
  }
  return 0xF8;
}

// The clock pulses read this pass and the wire bytes for io/, each a ring one deep enough
// for a jam message and a little of the wire behind it; the newest is dropped when full.
constexpr int kClockRing = 2 * hal::kClockInCapacity + 1;
hal::ClockIn clock_ring_[kClockRing];
int clock_head_ = 0;
int clock_tail_ = 0;

void push_clock(hal::ClockPort port, hal::ClockPulse pulse, uint64_t time_us) {
  const int next = (clock_tail_ + 1) % kClockRing;
  if (next == clock_head_) return;
  clock_ring_[clock_tail_] = hal::ClockIn{port, pulse, time_us};
  clock_tail_ = next;
}

constexpr int kMidiRing = hal::kMidiInputCapacity + 1;
uint8_t midi_ring_[kMidiRing];
int midi_head_ = 0;
int midi_tail_ = 0;

void push_midi(uint8_t byte) {
  const int next = (midi_tail_ + 1) % kMidiRing;
  if (next == midi_head_) return;
  midi_ring_[midi_tail_] = byte;
  midi_tail_ = next;
}

struct Pending {
  bool armed;
  hal::ClockPulse pulse;
  uint64_t at_us;
};
Pending midi_out_{false, hal::ClockPulse::tick, 0};

void send_byte(uint8_t byte) {
  sendto(sock_, &byte, 1, 0, reinterpret_cast<sockaddr*>(&peer_), sizeof peer_);
}

// The wire carries one byte every byte time; a caller offering faster is refused and
// tries again, so a clock pulse and a pattern share the 31250 baud a real cable has.
bool wire_free(uint64_t now) { return now >= next_send_us_; }
void wire_took(uint64_t now) { next_send_us_ = now + hal::kMidiByteUs; }

int parse_ports(const char* spec, int& bind_port, int& peer_port) {
  return std::sscanf(spec, "%d:%d", &bind_port, &peer_port) == 2 ? 0 : -1;
}

}  // namespace

namespace hal::sdl {

void link_init() {
  const char* spec = std::getenv("ROTA_LINK");
  if (spec == nullptr) {
    std::puts("hal/sdl: no jam link (set ROTA_LINK=<my port>:<their port> to link two simulators)");
    return;
  }
  int bind_port = 0;
  int peer_port = 0;
  if (parse_ports(spec, bind_port, peer_port) != 0) {
    std::printf("hal/sdl: ROTA_LINK=%s is not <my port>:<their port>; no jam link\n", spec);
    return;
  }
  sock_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_ < 0) {
    std::puts("hal/sdl: could not open the jam link socket");
    return;
  }
  sockaddr_in me{};
  me.sin_family = AF_INET;
  me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  me.sin_port = htons(static_cast<uint16_t>(bind_port));
  if (bind(sock_, reinterpret_cast<sockaddr*>(&me), sizeof me) != 0) {
    std::printf("hal/sdl: could not bind the jam link to port %d\n", bind_port);
    close(sock_);
    sock_ = -1;
    return;
  }
  fcntl(sock_, F_SETFL, O_NONBLOCK);  // read never blocks the main loop
  peer_.sin_family = AF_INET;
  peer_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer_.sin_port = htons(static_cast<uint16_t>(peer_port));
  open_ = true;
  std::printf("hal/sdl: jam link on 127.0.0.1:%d -> :%d\n", bind_port, peer_port);
  std::fflush(stdout);
}

// From hal::poll(), every main-loop pass.
void link_poll() {
  if (!open_) return;
  const uint64_t now = hal::now_us();

  // Read everything waiting into one buffer, then spread the stamps back one byte time
  // each — the bytes arrived 320 us apart, and stamping them alike would feed the
  // follower a zero interval.
  uint8_t buffer[hal::kMidiInputCapacity];
  int count = 0;
  while (count < static_cast<int>(sizeof buffer)) {
    uint8_t byte = 0;
    const ssize_t got = recv(sock_, &byte, 1, 0);
    if (got <= 0) break;
    buffer[count++] = byte;
  }
  for (int i = 0; i < count; ++i) {
    const uint64_t stamp = now - static_cast<uint64_t>(count - 1 - i) * hal::kMidiByteUs;
    hal::ClockPulse pulse;
    if (real_time_pulse(buffer[i], pulse)) {
      push_clock(hal::ClockPort::midi, pulse, stamp);
    } else {
      push_midi(buffer[i]);
    }
  }

  if (midi_out_.armed && static_cast<int64_t>(now - midi_out_.at_us) >= 0 && wire_free(now)) {
    send_byte(status_of(midi_out_.pulse));
    wire_took(now);
    midi_out_.armed = false;
  }
}

}  // namespace hal::sdl

namespace hal {

int read_clock_in(ClockIn* out, int capacity) {
  int count = 0;
  while (count < capacity && clock_head_ != clock_tail_) {
    out[count++] = clock_ring_[clock_head_];
    clock_head_ = (clock_head_ + 1) % kClockRing;
  }
  return count;
}

bool send_clock_out(ClockPort port, ClockPulse pulse, uint64_t at_us) {
  if (port == ClockPort::sync) return true;  // no PO on a desk: accept and drop the sync pulse
  if (!open_) return true;                   // no link: the clock thinks it sent, and nothing goes out
  if (midi_out_.armed) return false;
  midi_out_ = Pending{true, pulse, at_us};
  return true;
}

int midi_read(uint8_t* out, int capacity) {
  int count = 0;
  while (count < capacity && midi_head_ != midi_tail_) {
    out[count++] = midi_ring_[midi_head_];
    midi_head_ = (midi_head_ + 1) % kMidiRing;
  }
  return count;
}

int midi_send(const uint8_t* bytes, int count) {
  if (!open_ || count <= 0) return 0;
  const uint64_t now = now_us();
  if (!wire_free(now)) return 0;  // the wire is busy; the caller offers again next pass
  send_byte(bytes[0]);
  wire_took(now);
  return 1;
}

bool midi_port_open() { return open_; }

}  // namespace hal
