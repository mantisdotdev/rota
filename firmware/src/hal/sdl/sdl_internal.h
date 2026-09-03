#pragma once

#include <cstdint>

// What the SDL HAL's files share with each other and nobody else.
namespace hal::sdl {

// Writes the framebuffer as a PNG under ROTA_SCREENS_DIR, numbered per run (D-100).
void save_screenshot(const uint16_t* framebuffer);

// The jam link over UDP (§11, §12 rule 5): ROTA_LINK=<my port>:<their port> links two
// simulators; with none there is no port. link_poll runs from hal::poll().
void link_init();
void link_poll();

}  // namespace hal::sdl
