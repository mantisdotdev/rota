// render: share code → WAV (PRD §12, D-082).
//   ./build/render "<share code>" <cycles> out/<name>.wav
// Renders a section or song code for that many cycles with the lofi kit, whose
// samples are read from spec/kits/lofi/, into a 16-bit 48 kHz stereo WAV, and prints
// the duration and the peak level.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/kits/lofi.h"
#include "render/offline.h"
#include "sound/limits.h"

namespace {

constexpr int kExpectedArgCount = 4;  // program, share code, cycles, output path
constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kChannels = 2;
constexpr int kBitsPerSample = 16;
constexpr float kInt16FullScale = 32768.0f;

void put_u32(std::ofstream& out, uint32_t value) {
  const char bytes[4] = {static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff),
                         static_cast<char>((value >> 16) & 0xff), static_cast<char>((value >> 24) & 0xff)};
  out.write(bytes, 4);
}

void put_u16(std::ofstream& out, uint16_t value) {
  const char bytes[2] = {static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff)};
  out.write(bytes, 2);
}

bool write_wav(const std::string& path, const render::Rendered& rendered, std::string& error) {
  const std::filesystem::path parent = std::filesystem::path(path).parent_path();
  std::error_code ignored;
  if (!parent.empty()) std::filesystem::create_directories(parent, ignored);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = path + ": cannot write";
    return false;
  }
  const uint32_t data_bytes = static_cast<uint32_t>(rendered.interleaved.size() * 2);
  const uint32_t block_align = kChannels * kBitsPerSample / 8;
  out.write("RIFF", 4);
  put_u32(out, 36 + data_bytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  put_u32(out, 16);
  put_u16(out, 1);  // PCM
  put_u16(out, kChannels);
  put_u32(out, sound::kSampleRate);
  put_u32(out, sound::kSampleRate * block_align);
  put_u16(out, static_cast<uint16_t>(block_align));
  put_u16(out, kBitsPerSample);
  out.write("data", 4);
  put_u32(out, data_bytes);
  for (int16_t sample : rendered.interleaved) put_u16(out, static_cast<uint16_t>(sample));
  if (!out) {
    error = path + ": write failed";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != kExpectedArgCount) {
    std::fprintf(stderr, "usage: render \"<share code>\" <cycles> out/<name>.wav\n");
    return kExitUsage;
  }
  char* end = nullptr;
  const long cycles = std::strtol(argv[2], &end, 10);
  if (end == argv[2] || *end != '\0' || cycles < 1 || cycles > render::kMaxCycles) {
    std::fprintf(stderr, "render: cycles must be 1 to %d, got %s\n", render::kMaxCycles, argv[2]);
    return kExitUsage;
  }
  const engine::Kit& kit = engine::kits::kLofi;
  std::string error;
  render::KitSamples samples;
  if (!render::load_kit_samples(std::string(ROTA_KITS_DIR) + "/" + kit.id, kit, samples, error)) {
    std::fprintf(stderr, "render: %s\n", error.c_str());
    return kExitFailed;
  }
  render::Rendered rendered;
  if (!render::render_code(argv[1], static_cast<int>(cycles), render::kDefaultSeed, kit, samples.bank(), rendered,
                           error)) {
    std::fprintf(stderr, "render: %s\n", error.c_str());
    return kExitFailed;
  }
  if (!rendered.note.empty()) std::fprintf(stderr, "render: %s\n", rendered.note.c_str());
  const std::string path = argv[3];
  if (!write_wav(path, rendered, error)) {
    std::fprintf(stderr, "render: %s\n", error.c_str());
    return kExitFailed;
  }
  const double seconds = static_cast<double>(rendered.frames()) / sound::kSampleRate;
  const int peak = rendered.peak();
  if (peak == 0) {
    std::printf("render: wrote %s: %.2f s, silent\n", path.c_str(), seconds);
  } else {
    const double peak_dbfs = 20.0 * std::log10(static_cast<double>(peak) / kInt16FullScale);
    std::printf("render: wrote %s: %.2f s, peak %.2f dBFS\n", path.c_str(), seconds, peak_dbfs);
  }
  return kExitOk;
}
