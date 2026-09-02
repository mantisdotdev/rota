// Screenshots of the simulator's screen as PNG files, for looking at the views
// without the window (D-100). A minimal encoder: stored deflate blocks, so no zlib.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "hal/hal.h"
#include "hal/sdl/sdl_internal.h"

namespace {

constexpr int kStoredBlockMax = 65535;
constexpr uint32_t kAdlerModulus = 65521;
constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
int screenshot_count_ = 0;

uint32_t crc32_of(const uint8_t* data, size_t length) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    ready = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body) {
  put32(out, static_cast<uint32_t>(body.size()));
  std::vector<uint8_t> typed(type, type + 4);
  typed.insert(typed.end(), body.begin(), body.end());
  out.insert(out.end(), typed.begin(), typed.end());
  put32(out, crc32_of(typed.data(), typed.size()));
}

// zlib framing round stored (uncompressed) deflate blocks.
std::vector<uint8_t> zlib_stored(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out{0x78, 0x01};
  size_t at = 0;
  do {
    const size_t take = raw.size() - at < static_cast<size_t>(kStoredBlockMax) ? raw.size() - at : kStoredBlockMax;
    const bool final = at + take == raw.size();
    out.push_back(final ? 1 : 0);
    out.push_back(static_cast<uint8_t>(take & 0xFF));
    out.push_back(static_cast<uint8_t>(take >> 8));
    out.push_back(static_cast<uint8_t>(~take & 0xFF));
    out.push_back(static_cast<uint8_t>((~take >> 8) & 0xFF));
    out.insert(out.end(), raw.begin() + static_cast<long>(at), raw.begin() + static_cast<long>(at + take));
    at += take;
  } while (at < raw.size());
  uint32_t a = 1;
  uint32_t b = 0;
  for (const uint8_t byte : raw) {
    a = (a + byte) % kAdlerModulus;
    b = (b + a) % kAdlerModulus;
  }
  put32(out, (b << 16) | a);
  return out;
}

}  // namespace

namespace hal::sdl {

void save_screenshot(const uint16_t* framebuffer) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(kScreenHeight) * (1 + static_cast<size_t>(kScreenWidth) * 3));
  for (int y = 0; y < kScreenHeight; ++y) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < kScreenWidth; ++x) {
      const uint16_t pixel = framebuffer[y * kScreenWidth + x];
      const uint8_t red = static_cast<uint8_t>((pixel >> 11) & 0x1F);
      const uint8_t green = static_cast<uint8_t>((pixel >> 5) & 0x3F);
      const uint8_t blue = static_cast<uint8_t>(pixel & 0x1F);
      raw.push_back(static_cast<uint8_t>((red << 3) | (red >> 2)));
      raw.push_back(static_cast<uint8_t>((green << 2) | (green >> 4)));
      raw.push_back(static_cast<uint8_t>((blue << 3) | (blue >> 2)));
    }
  }
  std::vector<uint8_t> png(kSignature, kSignature + sizeof kSignature);
  std::vector<uint8_t> header;
  put32(header, static_cast<uint32_t>(kScreenWidth));
  put32(header, static_cast<uint32_t>(kScreenHeight));
  header.insert(header.end(), {8, 2, 0, 0, 0});  // 8 bits per sample, RGB, deflate, no filter, no interlace
  chunk(png, "IHDR", header);
  chunk(png, "IDAT", zlib_stored(raw));
  chunk(png, "IEND", {});

  const std::filesystem::path dir = ROTA_SCREENS_DIR;
  std::error_code ignored;
  std::filesystem::create_directories(dir, ignored);
  screenshot_count_ += 1;
  char name[32];
  std::snprintf(name, sizeof name, "screen-%02d.png", screenshot_count_);
  const std::filesystem::path target = dir / name;
  std::FILE* file = std::fopen(target.c_str(), "wb");
  if (file == nullptr) {
    std::printf("hal/sdl: could not write %s\n", target.c_str());
    return;
  }
  std::fwrite(png.data(), 1, png.size(), file);
  std::fclose(file);
  std::printf("hal/sdl: saved %s\n", target.c_str());
  std::fflush(stdout);
}

}  // namespace hal::sdl
