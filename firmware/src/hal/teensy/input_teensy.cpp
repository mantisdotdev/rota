// Teensy input and pad LEDs: two NeoTrellis boards over I2C (pads, then the eleven
// buttons on the second board's keys, D-045) and five encoders on interrupt pins
// (D-053, D-054). Polled from hal::poll(); the seesaw keeps a FIFO of edges so a
// press between polls is not lost.
#include <Adafruit_NeoTrellis.h>
#include <Arduino.h>
#include <Encoder.h>
#include <Wire.h>

#include "hal/hal.h"
#include "hal/teensy/pins.h"
#include "hal/teensy/teensy_internal.h"

namespace {

constexpr uint32_t kI2cClockHz = 400000;
constexpr int kInputCapacity = 64;
constexpr int kKeysPerBoard = NEO_TRELLIS_NUM_KEYS;
constexpr int kKeypadReadSlack = 2;  // Adafruit's own read() asks for count + 2: a seesaw firmware quirk
constexpr int kCountsPerDetent = 4;  // PEC11R: 24 pulses for 24 detents, one quadrature cycle each
constexpr uint32_t kDebounceMs = 20;
constexpr int kNoButton = -1;

// Pads are the top two rows of the first board, kick to rim in reading order.
// Buttons on the second board: row 0 split swap skip undo, row 1 dice show play,
// row 2 A B C D (keys 7 and 12–15 spare).
constexpr uint8_t kButtonKeys[hal::kButtonCount] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11};

Adafruit_NeoTrellis pads_(hal::pins::kPadsAddress);
Adafruit_NeoTrellis buttons_(hal::pins::kButtonsAddress);
bool pads_ready_ = false;
bool buttons_ready_ = false;

Encoder encoders_[hal::kEncoderCount] = {
    Encoder(hal::pins::kEncoders[0].a, hal::pins::kEncoders[0].b), Encoder(hal::pins::kEncoders[1].a, hal::pins::kEncoders[1].b),
    Encoder(hal::pins::kEncoders[2].a, hal::pins::kEncoders[2].b), Encoder(hal::pins::kEncoders[3].a, hal::pins::kEncoders[3].b),
    Encoder(hal::pins::kEncoders[4].a, hal::pins::kEncoders[4].b),
};
long encoder_counts_[hal::kEncoderCount];

struct Push {
  bool pressed;
  uint32_t changed_ms;
};
Push pushes_[hal::kEncoderCount];

hal::InputEvent input_[kInputCapacity];
int input_count_ = 0;

void push(hal::InputKind kind, int index, int detents) {
  if (input_count_ >= kInputCapacity) return;
  input_[input_count_++] = hal::InputEvent{kind, static_cast<uint8_t>(index), static_cast<int8_t>(detents), hal::now_us()};
}

int button_of_key(int key) {
  for (int i = 0; i < hal::kButtonCount; ++i) {
    if (kButtonKeys[i] == key) return i;
  }
  return kNoButton;
}

void activate(Adafruit_NeoTrellis& board) {
  for (int key = 0; key < kKeysPerBoard; ++key) {
    board.activateKey(key, SEESAW_KEYPAD_EDGE_RISING);
    board.activateKey(key, SEESAW_KEYPAD_EDGE_FALLING);
  }
}

void read_keys(Adafruit_NeoTrellis& board, bool is_pads) {
  int count = board.getKeypadCount();
  if (count <= 0) return;
  delayMicroseconds(500);  // as Adafruit's read() does before the FIFO read
  keyEventRaw raw[kKeysPerBoard + kKeypadReadSlack];
  if (count > kKeysPerBoard) count = kKeysPerBoard;
  if (!board.readKeypad(raw, static_cast<uint8_t>(count + kKeypadReadSlack))) return;
  for (int i = 0; i < count; ++i) {
    const int key = NEO_TRELLIS_SEESAW_KEY(raw[i].bit.NUM);
    const bool down = raw[i].bit.EDGE == SEESAW_KEYPAD_EDGE_RISING;
    if (!down && raw[i].bit.EDGE != SEESAW_KEYPAD_EDGE_FALLING) continue;
    if (key < 0 || key >= kKeysPerBoard) continue;
    if (is_pads) {
      if (key < hal::kPadCount) push(down ? hal::InputKind::pad_down : hal::InputKind::pad_up, key, 0);
      continue;
    }
    const int button = button_of_key(key);
    if (button != kNoButton) push(down ? hal::InputKind::button_down : hal::InputKind::button_up, button, 0);
  }
}

void read_encoders() {
  for (int i = 0; i < hal::kEncoderCount; ++i) {
    const long now = encoders_[i].read();
    const long steps = (now - encoder_counts_[i]) / kCountsPerDetent;
    if (steps != 0) {
      encoder_counts_[i] += steps * kCountsPerDetent;
      push(hal::InputKind::encoder_turn, i, static_cast<int>(steps));
    }
    const bool pressed = digitalRead(hal::pins::kEncoders[i].push) == LOW;
    Push& state = pushes_[i];
    if (pressed != state.pressed && millis() - state.changed_ms >= kDebounceMs) {
      state.pressed = pressed;
      state.changed_ms = millis();
      push(pressed ? hal::InputKind::encoder_down : hal::InputKind::encoder_up, i, 0);
    }
  }
}

}  // namespace

namespace hal::teensy {

void input_init() {
  Wire.begin();
  Wire.setClock(kI2cClockHz);
  pads_ready_ = pads_.begin(pins::kPadsAddress);
  buttons_ready_ = buttons_.begin(pins::kButtonsAddress);
  if (!pads_ready_) Serial.println("hal/teensy: the pad board did not answer");
  if (!buttons_ready_) Serial.println("hal/teensy: the button board did not answer");
  if (pads_ready_) activate(pads_);
  if (buttons_ready_) activate(buttons_);
  for (int i = 0; i < kEncoderCount; ++i) {
    pinMode(pins::kEncoders[i].push, INPUT_PULLUP);
    encoder_counts_[i] = encoders_[i].read();
    pushes_[i] = Push{false, millis()};
  }
}

void input_read() {
  if (pads_ready_) read_keys(pads_, true);
  if (buttons_ready_) read_keys(buttons_, false);
  read_encoders();
}

}  // namespace hal::teensy

namespace hal {

int read_input(InputEvent* out, int capacity) {
  const int count = input_count_ < capacity ? input_count_ : capacity;
  for (int i = 0; i < count; ++i) out[i] = input_[i];
  for (int i = count; i < input_count_; ++i) input_[i - count] = input_[i];
  input_count_ -= count;
  return count;
}

void set_led(int pad, uint8_t red, uint8_t green, uint8_t blue) {
  if (!pads_ready_ || pad < 0 || pad >= kPadCount) return;
  pads_.pixels.setPixelColor(static_cast<uint16_t>(pad), red, green, blue);
}

void show_leds() {
  if (pads_ready_) pads_.pixels.show();
}

}  // namespace hal
