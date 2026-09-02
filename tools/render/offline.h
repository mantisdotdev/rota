#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/kit.h"
#include "sound/voice.h"

// Share code → samples, the engine behind tools/render (PRD §12, D-082). A tool, so
// it may allocate and read files; nothing here runs on the device.
namespace render {

constexpr int kMaxCycles = 256;  // about ten minutes at 100 bpm; a mistyped count cannot fill memory
constexpr uint32_t kDefaultSeed = 42;

// The frames behind a sound::SampleBank, one per pad; synth pads stay empty.
struct KitSamples {
  std::vector<int16_t> frames[engine::kTrackCount];
  sound::SampleBank bank() const;
};

// Reads a 16-bit 48 kHz mono PCM WAV of at most two seconds (D-081). false with a
// reason otherwise.
bool read_wav(const std::string& path, std::vector<int16_t>& frames, std::string& error);

// Loads every sample pad's WAV from kit_dir. false with a reason on the first problem.
bool load_kit_samples(const std::string& kit_dir, const engine::Kit& kit, KitSamples& out, std::string& error);

struct Rendered {
  std::vector<int16_t> interleaved;  // stereo pairs at sound::kSampleRate
  std::string note;                  // what a device's status line would say, e.g. `no kit jazz, using lofi`
  int frames() const { return static_cast<int>(interleaved.size() / 2); }
  int peak() const;  // largest |sample|
};

// Renders `cycles` cycles of a section (RT2) or song (RT2S) code with `kit`. A song
// steps through its arrangement, one cycle per letter, and loops. The result is
// exactly cycles × 240 / bpm seconds; `seed` fixes chance and humanize (D-034).
// false when the code does not load or the count is out of range.
bool render_code(const char* code, int cycles, uint32_t seed, const engine::Kit& kit, const sound::SampleBank& bank,
                 Rendered& out, std::string& error);

}  // namespace render
