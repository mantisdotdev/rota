// Host HAL: an SDL2 window stands in for the screen (PRD §12). Keyboard input and
// audio output arrive with the input grammar and sound layers.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>

#include "hal/hal.h"

namespace {

// PRD §7.3: 320×240 panel, scaled up so it is legible on a laptop.
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kWindowScale = 2;

// PRD Appendix D: screen background #15130F.
constexpr Uint8 kScreenBackgroundRed = 0x15;
constexpr Uint8 kScreenBackgroundGreen = 0x13;
constexpr Uint8 kScreenBackgroundBlue = 0x0F;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
bool quit_requested = false;

[[noreturn]] void fail(const char* what) {
  std::fprintf(stderr, "hal/sdl: %s failed: %s\n", what, SDL_GetError());
  std::exit(EXIT_FAILURE);
}

}  // namespace

namespace hal {

void init() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) fail("SDL_Init");
  std::atexit(SDL_Quit);
  window = SDL_CreateWindow("Rota", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            kScreenWidth * kWindowScale, kScreenHeight * kWindowScale,
                            SDL_WINDOW_SHOWN);
  if (window == nullptr) fail("SDL_CreateWindow");
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == nullptr) fail("SDL_CreateRenderer");
  SDL_RenderSetLogicalSize(renderer, kScreenWidth, kScreenHeight);
  std::printf("hal/sdl: window %dx%d (screen %dx%d, scale %d)\n",
              kScreenWidth * kWindowScale, kScreenHeight * kWindowScale, kScreenWidth,
              kScreenHeight, kWindowScale);
  std::fflush(stdout);
}

uint32_t now_ms() { return SDL_GetTicks(); }

bool poll() {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    const bool closed = event.type == SDL_QUIT;
    const bool escaped = event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE;
    if (closed || escaped) quit_requested = true;
  }
  return !quit_requested;
}

void present() {
  SDL_SetRenderDrawColor(renderer, kScreenBackgroundRed, kScreenBackgroundGreen,
                         kScreenBackgroundBlue, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);
}

}  // namespace hal
