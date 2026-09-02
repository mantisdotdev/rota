// Host simulator entry point (PRD §12). The loop is the same as firmware/src/main.cpp;
// only the HAL underneath differs. Deliberately includes no SDL header: hal/sdl/ owns SDL.
// The kit's samples are read from spec/kits/ through the render tool's loader, which
// is host-only code; on the device io/ will read them from the card.
#include <cstdio>
#include <string>

#include "app/app.h"
#include "engine/kits/lofi.h"
#include "hal/hal.h"
#include "render/offline.h"

int main() {
  hal::init();
  const engine::Kit& kit = engine::kits::kLofi;
  render::KitSamples samples;
  std::string error;
  if (!render::load_kit_samples(std::string(ROTA_KITS_DIR) + "/" + kit.id, kit, samples, error)) {
    std::fprintf(stderr, "simulator: %s\n", error.c_str());
    return 1;
  }
  app::init(samples.bank());
  std::puts("simulator: 1-8 pads; s w k z d e space = split swap skip undo dice show play; a b c shift+d sections;");
  std::puts("simulator: up/down or the wheel turn the selected knob, left/right pick it, - = volume; Escape quits");
  std::fflush(stdout);
  while (hal::poll()) app::tick();
  return 0;
}
