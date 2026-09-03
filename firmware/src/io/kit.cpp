#include "io/kit.h"

#include <cstdio>
#include <cstring>

#include "hal/hal.h"
#include "sound/limits.h"

namespace io {

namespace {

constexpr int kPathCapacity = 64;
constexpr uint32_t kRiffHeaderBytes = 12;
constexpr uint32_t kChunkHeaderBytes = 8;
constexpr uint32_t kFormatChunkBytes = 16;  // the least a fmt chunk may hold
constexpr uint16_t kPcm = 1;
constexpr uint16_t kMono = 1;
constexpr uint16_t kBitsPerSample = 16;

uint16_t little_u16(const uint8_t* at) { return static_cast<uint16_t>(at[0] | (at[1] << 8)); }

uint32_t little_u32(const uint8_t* at) {
  return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) | (static_cast<uint32_t>(at[2]) << 16) |
         (static_cast<uint32_t>(at[3]) << 24);
}

void refuse(const char* path, const char* why) {
  char line[kPathCapacity + 48];
  std::snprintf(line, sizeof line, "io: %s %s", path, why);
  hal::log(line);
}

// Finds the PCM inside a RIFF WAVE already in memory: on success `offset` and
// `frames` say where the samples are and how many. A card is a boundary, so every
// field is checked and nothing is trusted to be there (D-081, T-77).
bool find_pcm(const uint8_t* bytes, uint32_t size, const char* path, uint32_t& offset, uint32_t& frames) {
  if (size < kRiffHeaderBytes || std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0) {
    refuse(path, "is not a RIFF WAVE file");
    return false;
  }
  bool has_format = false;
  uint32_t at = kRiffHeaderBytes;
  while (at + kChunkHeaderBytes <= size) {
    const uint8_t* chunk = bytes + at;
    const uint32_t length = little_u32(chunk + 4);
    const uint32_t body = at + kChunkHeaderBytes;
    if (length > size - body) {  // subtraction, not addition: a huge length must not wrap
      refuse(path, "has a chunk that runs past the end of the file");
      return false;
    }
    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      if (length < kFormatChunkBytes) {
        refuse(path, "has a fmt chunk too short to read");
        return false;
      }
      if (little_u16(chunk + 8) != kPcm || little_u16(chunk + 10) != kMono ||
          little_u32(chunk + 12) != static_cast<uint32_t>(sound::kSampleRate) ||
          little_u16(chunk + 22) != kBitsPerSample) {
        refuse(path, "is not 16-bit 48 kHz mono PCM");
        return false;
      }
      has_format = true;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      if (!has_format) {
        refuse(path, "has its samples before it says what they are");
        return false;
      }
      frames = length / 2;
      if (frames == 0 || frames > hal::kMaxSampleFramesPerPad) {
        refuse(path, "is empty or longer than the two seconds a sample may be");
        return false;
      }
      offset = body;
      return true;
    }
    at = body + length + (length & 1);  // chunks are padded to an even length
  }
  refuse(path, "has no samples in it");
  return false;
}

}  // namespace

bool load_samples(const engine::Kit& kit, sound::SampleBank& bank) {
  bank = sound::SampleBank{};
  uint32_t capacity = 0;
  int16_t* memory = hal::sample_memory(&capacity);
  if (memory == nullptr || capacity == 0) {
    hal::log("io: no memory for samples, so the sample pads are silent");
    return false;
  }

  uint32_t used = 0;
  for (int i = 0; i < engine::kTrackCount; ++i) {
    const engine::KitPad& pad = kit.pads[i];
    if (pad.voice != engine::Voice::sample) continue;
    char path[kPathCapacity];
    std::snprintf(path, sizeof path, "kits/%s/%s", kit.id, pad.source);

    // The file is read straight into the room left in the arena and parsed where it
    // lands, so no second buffer the size of a sample is ever needed; the samples are
    // then moved down over the header they came with.
    int16_t* into = memory + used;
    uint32_t size = 0;
    if (hal::read_file(path, reinterpret_cast<uint8_t*>(into), (capacity - used) * sizeof(int16_t), &size) !=
        hal::FileRead::ok) {
      refuse(path, "did not come off the card, so its pad is silent");
      continue;
    }
    uint32_t offset = 0;
    uint32_t frames = 0;
    if (!find_pcm(reinterpret_cast<const uint8_t*>(into), size, path, offset, frames)) continue;
    std::memmove(into, reinterpret_cast<const uint8_t*>(into) + offset, frames * sizeof(int16_t));
    bank.samples[i] = sound::Sample{into, static_cast<int>(frames)};
    used += frames;
  }
  return true;
}

}  // namespace io
