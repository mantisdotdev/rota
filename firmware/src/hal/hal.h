#pragma once

#include <cstdint>

// Hardware abstraction layer (PRD §12). One interface, one implementation per
// platform: hal/teensy/ for the device, hal/sdl/ for the host simulator (D-016).
// Nothing in engine/ or sound/ may include this header (§12 rule 1).
namespace hal {

// Brings the platform up. Call once, before anything else.
void init();

// Monotonic milliseconds since init.
uint32_t now_ms();

// Services platform events. Returns false when the host asks to quit (window
// closed or Escape pressed); the device never asks.
bool poll();

// Pushes the frame to the screen and paces the main loop.
void present();

}  // namespace hal
