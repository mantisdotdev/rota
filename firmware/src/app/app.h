#pragma once

#include <cstdint>

// The portable top layer (PRD §12): scheduler, input grammar, sections and songs
// will live here. Both entry points (firmware/src/main.cpp and host/main.cpp) drive
// it the same way; that is what makes the host simulator the reference behaviour.
namespace app {

void init();
void tick(uint32_t now_ms);

}  // namespace app
