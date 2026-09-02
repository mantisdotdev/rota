#include "render/offline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>

#include "engine/events.h"
#include "engine/share.h"
#include "engine/state.h"
#include "sound/engine.h"

namespace render {

namespace {

constexpr int kMaxSampleFrames = sound::kSampleRate * 2;  // D-081
constexpr int kInt16Max = 32767;
constexpr float kTenths = 10.0f;
constexpr int kSecondsPerCycleTimesBpm = 240;  // §6.1
const char* const kSectionPrefix = "RT2:";
const char* const kSongPrefix = "RT2S:";
const char* const kDidNotLoad = "that code did not load";  // Appendix D

uint32_t little_u32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t little_u16(const unsigned char* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

int frames_of_cycle(int bpm) {
  return static_cast<int>(std::lround(static_cast<double>(sound::kSampleRate) * kSecondsPerCycleTimesBpm / bpm));
}

sound::Params params_of(const engine::State& state, const engine::Kit& kit) {
  sound::Params params = sound::default_params(kit);
  params.bpm = static_cast<float>(state.bpm);
  params.filter = static_cast<float>(state.filter) / kTenths;
  params.fx = static_cast<float>(state.fx) / kTenths;
  params.master = 1.0f;
  for (int i = 0; i < engine::kTrackCount; ++i) {
    const engine::Track& track = state.tracks[i];
    params.tracks[i].level = static_cast<float>(track.level) / kTenths;
    params.tracks[i].tone = static_cast<float>(track.tone) / kTenths;
    params.tracks[i].send = static_cast<float>(track.send) / kTenths;
  }
  return params;
}

int16_t to_int16(float sample) {
  const float clamped = std::min(std::max(sample, -1.0f), 1.0f);
  // Truncation toward zero: a peak at the limiter's ceiling never rounds above it.
  return static_cast<int16_t>(clamped * static_cast<float>(kInt16Max));
}

struct Scheduled {
  int64_t frame;
  engine::Event event;
};

struct CycleSpan {
  int state;
  int64_t start;
  int frames;
};

}  // namespace

sound::SampleBank KitSamples::bank() const {
  sound::SampleBank bank{};
  for (int i = 0; i < engine::kTrackCount; ++i) {
    bank.samples[i].frames = frames[i].empty() ? nullptr : frames[i].data();
    bank.samples[i].frame_count = static_cast<int>(frames[i].size());
  }
  return bank;
}

int Rendered::peak() const {
  int peak = 0;
  for (int16_t sample : interleaved) peak = std::max(peak, std::abs(static_cast<int>(sample)));
  return peak;
}

bool read_wav(const std::string& path, std::vector<int16_t>& frames, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = path + ": cannot open";
    return false;
  }
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    error = path + ": not a RIFF WAVE file";
    return false;
  }
  bool has_format = false;
  size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    const unsigned char* chunk = bytes.data() + offset;
    const uint32_t size = little_u32(chunk + 4);
    const size_t body = offset + 8;
    if (body + size > bytes.size()) {
      error = path + ": truncated chunk";
      return false;
    }
    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      if (size < 16) {
        error = path + ": fmt chunk too short";
        return false;
      }
      const uint16_t format = little_u16(chunk + 8);
      const uint16_t channels = little_u16(chunk + 10);
      const uint32_t rate = little_u32(chunk + 12);
      const uint16_t bits = little_u16(chunk + 22);
      if (format != 1 || channels != 1 || rate != static_cast<uint32_t>(sound::kSampleRate) || bits != 16) {
        error = path + ": needs 16-bit 48 kHz mono PCM";
        return false;
      }
      has_format = true;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      if (!has_format) {
        error = path + ": data before fmt";
        return false;
      }
      const size_t count = size / 2;
      if (count == 0 || count > static_cast<size_t>(kMaxSampleFrames)) {
        error = path + ": a sample is 1 to " + std::to_string(kMaxSampleFrames) + " frames";
        return false;
      }
      frames.resize(count);
      for (size_t i = 0; i < count; ++i) {
        frames[i] = static_cast<int16_t>(little_u16(bytes.data() + body + i * 2));
      }
      return true;
    }
    offset = body + size + (size & 1u);
  }
  error = path + ": no data chunk";
  return false;
}

bool load_kit_samples(const std::string& kit_dir, const engine::Kit& kit, KitSamples& out, std::string& error) {
  for (int i = 0; i < engine::kTrackCount; ++i) {
    out.frames[i].clear();
    const engine::KitPad& pad = kit.pads[i];
    if (pad.voice != engine::Voice::sample) continue;
    if (!read_wav(kit_dir + "/" + pad.source, out.frames[i], error)) return false;
  }
  return true;
}

bool render_code(const char* code, int cycles, uint32_t seed, const engine::Kit& kit, const sound::SampleBank& bank,
                 Rendered& out, std::string& error) {
  if (cycles < 1 || cycles > kMaxCycles) {
    error = "cycles must be 1 to " + std::to_string(kMaxCycles);
    return false;
  }
  std::vector<engine::State> states;
  std::vector<int> order;  // which state each cycle plays
  bool substituted = false;
  std::string requested_kit;
  if (std::strncmp(code, kSongPrefix, std::strlen(kSongPrefix)) == 0) {
    const engine::DecodedSong decoded = engine::decode_song(code, kit);
    if (!decoded.ok) {
      error = kDidNotLoad;
      return false;
    }
    for (const engine::State& section : decoded.song.sections) states.push_back(section);
    for (int i = 0; i < decoded.song.arrangement_length; ++i) order.push_back(decoded.song.arrangement[i] - 'A');
    substituted = decoded.kit_substituted;
    requested_kit = decoded.requested_kit;
  } else if (std::strncmp(code, kSectionPrefix, std::strlen(kSectionPrefix)) == 0) {
    const engine::Decoded decoded = engine::decode(code, kit);
    if (!decoded.ok) {
      error = kDidNotLoad;
      return false;
    }
    states.push_back(decoded.state);
    order.push_back(0);
    substituted = decoded.kit_substituted;
    requested_kit = decoded.requested_kit;
  } else {
    error = kDidNotLoad;
    return false;
  }
  out.note = substituted ? "no kit " + requested_kit + ", using " + kit.id : "";

  // One span per cycle, then every event of every cycle at its absolute frame.
  std::vector<CycleSpan> spans;
  std::vector<Scheduled> scheduled;
  int64_t total = 0;
  for (int cycle = 0; cycle < cycles; ++cycle) {
    const int state_index = order[static_cast<size_t>(cycle) % order.size()];
    const engine::State& state = states[state_index];
    const int frames = frames_of_cycle(state.bpm);
    spans.push_back(CycleSpan{state_index, total, frames});
    engine::EventList list;
    engine::events(state, kit, static_cast<uint32_t>(cycle), seed, list);
    for (int i = 0; i < list.count; ++i) {
      const engine::Event& event = list.items[i];
      const int64_t within = (static_cast<int64_t>(event.time.num) * frames) / event.time.den;
      scheduled.push_back(Scheduled{total + within, event});
    }
    total += frames;
  }

  auto engine = std::make_unique<sound::Engine>();
  engine->init(kit, bank);
  out.interleaved.assign(static_cast<size_t>(total) * 2, 0);
  std::vector<sound::Trigger> triggers;
  size_t next = 0;
  size_t span = 0;
  sound::StereoBlock block;
  for (int64_t block_start = 0; block_start < total; block_start += sound::kBlockSize) {
    while (span + 1 < spans.size() && spans[span + 1].start <= block_start) ++span;
    engine->set_params(params_of(states[spans[span].state], kit));
    triggers.clear();
    while (next < scheduled.size() && scheduled[next].frame < block_start + sound::kBlockSize) {
      triggers.push_back(sound::Trigger{scheduled[next].event, static_cast<int>(scheduled[next].frame - block_start)});
      ++next;
    }
    engine->render(triggers.data(), static_cast<int>(triggers.size()), block);
    const int64_t count = std::min<int64_t>(sound::kBlockSize, total - block_start);
    for (int64_t k = 0; k < count; ++k) {
      out.interleaved[static_cast<size_t>((block_start + k) * 2)] = to_int16(block.left[k]);
      out.interleaved[static_cast<size_t>((block_start + k) * 2 + 1)] = to_int16(block.right[k]);
    }
  }
  return true;
}

}  // namespace render
