#pragma once

#include <cstdint>

// EVT wiring (hardware/WIRING.md, D-089). Every number here comes from the Teensy
// 4.1 pinout card, the audio shield's page and the library docs, and none has been
// checked on a board: treat all of them as unverified until the bring-up runbook
// says otherwise (CLAUDE.md, Landmines). WIRING.md is the table people read; keep
// the two in step.
namespace hal::pins {

// Audio shield Rev D (SGTL5000) on the Teensy 4.1 header rows: I2S on 7, 8, 20, 21,
// 23 and I2C on 18, 19 are wired by the shield itself; nothing here drives them.
constexpr uint8_t kCodecI2cAddress = 0x0A;

// Display, QDtech MSP2834 (ILI9341) on SPI1, the bus D-047 chose; DC on a hardware
// chip-select pin of SPI1 for the driver's fast path.
constexpr uint8_t kDisplayCs = 36;
constexpr uint8_t kDisplayDc = 38;
constexpr uint8_t kDisplaySck = 27;
constexpr uint8_t kDisplayMosi = 26;
constexpr uint8_t kDisplayMiso = 39;
constexpr uint8_t kDisplayReset = 37;
constexpr uint8_t kDisplayBacklight = 33;  // PWM; off until after boot (D-052)

// NeoTrellis boards on Wire (18 SDA, 19 SCL), with the codec: pads at the default
// address, buttons with jumper A0 soldered (D-045).
constexpr uint8_t kPadsAddress = 0x2E;
constexpr uint8_t kButtonsAddress = 0x2F;

// Bourns PEC11R encoders (D-053, D-054): A and B on interrupt-capable pins, the
// push switch to ground with the internal pull-up.
struct EncoderPins {
  uint8_t a;
  uint8_t b;
  uint8_t push;
};
constexpr EncoderPins kEncoders[5] = {
    {2, 3, 4},     // speed
    {5, 9, 14},    // filter
    {16, 17, 22},  // fx
    {24, 25, 28},  // chance
    {29, 30, 31},  // volume
};

// Power sensing, both analog (D-065, D-072): the cell through two 100 kΩ resistors,
// and the headphone jack's tip switch through a pull-up.
constexpr uint8_t kBatterySense = 40;    // A16
constexpr uint8_t kHeadphoneSense = 41;  // A17

// Reserved for io/ (not driven here): MIDI on Serial1 (0 RX, 1 TX); sync in 32,
// sync out 34. Routed to the audio shield's unused sockets and left alone: 6, 10,
// 11, 12, 13, 15.

}  // namespace hal::pins
