#pragma once

// What the Teensy HAL's files share with each other and nobody else: the init of
// each part, called from hal::init() in order, and the per-loop reads.
namespace hal::teensy {

void display_init();
void input_init();
void input_read();
void storage_init();
void power_init();
void link_init();  // the MIDI wire and the sync jack (§7.6, §11); unverified until bring-up
void link_poll();  // from hal::poll(): drains MIDI RX and emits due out pulses

}  // namespace hal::teensy
