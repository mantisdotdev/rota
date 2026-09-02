#pragma once

#include <cstdint>

// What the SDL HAL's files share with each other and nobody else.
namespace hal::sdl {

// Writes the framebuffer as a PNG under ROTA_SCREENS_DIR, numbered per run (D-100).
void save_screenshot(const uint16_t* framebuffer);

}  // namespace hal::sdl
