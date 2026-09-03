// The reference renders in spec/screens/ (D-101), written by the tests that already
// check the state each one shows, so a screen and its picture cannot drift.
//
// Nothing is written unless ROTA_SCREENS names a directory, so a plain ./build/tests
// touches no files; tools/screens.py turns what lands there into the PNGs the repo
// keeps, and CI regenerates and diffs them the way it does the kits' headers.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "doctest/doctest.h"
#include "hal/hal.h"
#include "hal_fake.h"

namespace screens {

// The framebuffer as it stands, expanded to 8 bits a channel exactly as
// hal/sdl/screenshot_sdl.cpp expands it, so a saved simulator screen and a
// generated one are the same picture.
inline void capture(const char* name) {
  const char* directory = std::getenv("ROTA_SCREENS");
  if (directory == nullptr) return;
  const std::string path = std::string(directory) + "/" + name + ".raw";
  std::FILE* file = std::fopen(path.c_str(), "wb");
  REQUIRE_MESSAGE(file != nullptr, "cannot write " << path);
  const uint16_t* framebuffer = hal_fake::framebuffer();
  for (int i = 0; i < hal::kScreenWidth * hal::kScreenHeight; ++i) {
    const uint16_t pixel = framebuffer[i];
    const uint8_t red = static_cast<uint8_t>((pixel >> 11) & 0x1F);
    const uint8_t green = static_cast<uint8_t>((pixel >> 5) & 0x3F);
    const uint8_t blue = static_cast<uint8_t>(pixel & 0x1F);
    const uint8_t rgb[3] = {static_cast<uint8_t>((red << 3) | (red >> 2)),
                            static_cast<uint8_t>((green << 2) | (green >> 4)),
                            static_cast<uint8_t>((blue << 3) | (blue >> 2))};
    REQUIRE(std::fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb);
  }
  std::fclose(file);
}

}  // namespace screens
