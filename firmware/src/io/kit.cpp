#include "io/kit.h"

#include <cstdio>
#include <cstring>

#include "engine/share.h"
#include "hal/hal.h"
#include "io/lines.h"
#include "sound/limits.h"

namespace io {

namespace {

constexpr int kPathCapacity = 64;
// The file is four dice loops at a section code's full length, eight pads with their
// templates, and a line each for the rest.
constexpr uint32_t kKitFileCapacity = 4096;
constexpr int kMaxKitLines = 96;
constexpr int kMaxFields = 8;  // a pad line, the longest
constexpr const char* kKitPrefix = "RTK1";
constexpr const char* kKitFileName = "kit.txt";
char kit_file_[kKitFileCapacity];
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


// ---- the kit itself -------------------------------------------------------------

namespace {

// One line's `key=value`, split where the caller's grammar says. Returns how many
// fields the value held, or -1 when the line is not this key or has too many.
int fields_of(char* line, const char* key, char** fields, int capacity) {
  const size_t length = std::strlen(key);
  if (std::strncmp(line, key, length) != 0 || line[length] != '=') return -1;
  char* at = line + length + 1;
  int count = 0;
  for (;;) {
    if (count == capacity) return -1;
    fields[count++] = at;
    char* comma = std::strchr(at, ',');
    if (comma == nullptr) return count;
    *comma = '\0';
    at = comma + 1;
  }
}

// A whole number in `text`, 0 to `most`. False on anything else, so nothing a card
// holds becomes a value the rest of the firmware would not have produced.
bool number(const char* text, int most, int& out) {
  int value = 0;
  int digits = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10 + (*c - '0');
    if (++digits > 5 || value > most) return false;
  }
  out = value;
  return digits > 0;
}

// A sample's file name, and nothing that could name a file outside the kit's folder:
// letters, digits, and the three punctuation marks a file name needs.
bool is_file_name(const char* text) {
  if (*text == '\0' || *text == '.') return false;  // no hidden files, and no `..`
  for (const char* c = text; *c != '\0'; ++c) {
    const bool ordinary = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '-' || *c == '_';
    if (!ordinary) return false;
  }
  return true;
}

bool copy_word(const char* from, char* into, int capacity) {
  const size_t length = std::strlen(from);
  if (length == 0 || length >= static_cast<size_t>(capacity)) return false;
  std::memcpy(into, from, length + 1);
  return true;
}

bool read_degrees(char** fields, int count, engine::DegreeList& list) {
  if (count < 1 || count > engine::kMaxNoteSequenceLength) return false;
  for (int i = 0; i < count; ++i) {
    int degree = 0;
    if (!number(fields[i], 255, degree)) return false;
    list.degrees[i] = static_cast<uint8_t>(degree);
  }
  list.length = static_cast<uint8_t>(count);
  return true;
}

bool read_pad(char** fields, int count, engine::KitPad& pad) {
  if (count != kMaxFields) return false;
  int pitch = 0;
  int start = 0;
  int decay = 0;
  int octave = 0;
  int send = 0;
  const bool sample = std::strcmp(fields[1], "sample") == 0;
  if (!sample && std::strcmp(fields[1], "synth") != 0) return false;
  if (!copy_word(fields[0], pad.name, sizeof pad.name)) return false;
  // The source becomes half of a path, so it is a file name or it is nothing.
  if (!is_file_name(fields[2]) || !copy_word(fields[2], pad.source, sizeof pad.source)) return false;
  if (!number(fields[3], 24, pitch) || !number(fields[4], 100, start) || !number(fields[5], 100, decay) ||
      !number(fields[6], 9, octave) || !number(fields[7], engine::kTenthsMax, send)) {
    return false;
  }
  pad.voice = sample ? engine::Voice::sample : engine::Voice::synth;
  pad.pitch_semitones = static_cast<int8_t>(pitch);
  pad.start = static_cast<float>(start) / 100.0f;  // hundredths on the card, a fraction here (D-109)
  pad.decay = static_cast<float>(decay) / 100.0f;
  pad.octave = static_cast<uint8_t>(octave);
  pad.send = static_cast<engine::Tenths>(send);
  pad.template_count = 0;
  return true;
}

bool read_template(const char* steps, engine::KitPad& pad) {
  if (pad.template_count >= engine::kMaxTapTemplates) return false;
  engine::TapTemplate& into = pad.templates[pad.template_count];
  int count = 0;
  for (const char* c = steps; *c != '\0'; ++c) {
    if (count >= engine::kMaxStepsPerTrack) return false;
    if (!engine::read_step(*c, into.steps[count++])) return false;
  }
  if (count == 0) return false;
  into.step_count = static_cast<uint8_t>(count);
  pad.template_count += 1;
  return true;
}

// Every line of the file, in the order kit_builder.py writes them: the progressions
// in mode order, the pads in the order share-format §2 fixes, and each pad's
// templates under it.
bool read_kit(char** lines, int count, engine::Kit& kit) {
  if (count < 1 || std::strcmp(lines[0], kKitPrefix) != 0) return false;
  int progressions = 0;
  int pads = 0;
  int dice = 0;
  bool have_pluck = false;
  bool have_id = false;
  bool have_swing = false;
  bool have_filter = false;
  bool have_fx = false;
  bool have_sidechain = false;
  // A field said twice is a file that says two things: which one the kit meant is not
  // a question this firmware gets to answer, so it refuses the file instead.
  char* fields[kMaxFields];
  for (int i = 1; i < count; ++i) {
    char* line = lines[i];
    int got = fields_of(line, "id", fields, 1);
    if (got == 1) {
      if (have_id || !is_kit_id(fields[0]) || !copy_word(fields[0], kit.id, sizeof kit.id)) return false;
      have_id = true;
      continue;
    }
    int value = 0;
    got = fields_of(line, "swing", fields, 1);
    if (got == 1) {
      if (have_swing || !number(fields[0], 100, value)) return false;
      kit.swing_hundredths = static_cast<uint8_t>(value);
      have_swing = true;
      continue;
    }
    got = fields_of(line, "filter", fields, 1);
    if (got == 1) {
      if (have_filter || !number(fields[0], engine::kTenthsMax, value)) return false;
      kit.filter = static_cast<engine::Tenths>(value);
      have_filter = true;
      continue;
    }
    got = fields_of(line, "fx", fields, 1);
    if (got == 1) {
      if (have_fx || !number(fields[0], engine::kTenthsMax, value)) return false;
      kit.fx = static_cast<engine::Tenths>(value);
      have_fx = true;
      continue;
    }
    got = fields_of(line, "sidechain", fields, 3);
    if (got == 3) {
      int on = 0;
      int duck = 0;
      int release = 0;
      if (have_sidechain || !number(fields[0], 1, on) || !number(fields[1], 24, duck) ||
          !number(fields[2], 5000, release)) {
        return false;
      }
      kit.sidechain = engine::Sidechain{on == 1, static_cast<uint8_t>(duck), static_cast<uint16_t>(release)};
      have_sidechain = true;
      continue;
    }
    got = fields_of(line, "progression", fields, engine::kMaxNoteSequenceLength);
    if (got > 0) {
      if (progressions >= engine::kModeCount || !read_degrees(fields, got, kit.progressions[progressions])) return false;
      progressions += 1;
      continue;
    }
    got = fields_of(line, "pluck", fields, engine::kMaxNoteSequenceLength);
    if (got > 0) {
      if (have_pluck || !read_degrees(fields, got, kit.pluck_sequence)) return false;
      have_pluck = true;
      continue;
    }
    got = fields_of(line, "dice", fields, 1);
    if (got == 1) {
      if (dice >= engine::kMaxDiceLoops || !copy_word(fields[0], kit.dice_loops[dice], engine::kSectionCodeCapacity)) {
        return false;
      }
      dice += 1;
      continue;
    }
    got = fields_of(line, "pad", fields, kMaxFields);
    if (got > 0) {
      if (pads >= engine::kTrackCount || !read_pad(fields, got, kit.pads[pads])) return false;
      pads += 1;
      continue;
    }
    got = fields_of(line, "template", fields, 1);
    if (got == 1) {
      if (pads == 0 || !read_template(fields[0], kit.pads[pads - 1])) return false;
      continue;
    }
    return false;  // a line this firmware does not know: a kit is not a place to guess
  }
  kit.dice_loop_count = static_cast<uint8_t>(dice);
  // Every field, not just the ones with a count: an absent line would otherwise leave
  // the zero `engine::Kit{}` put there, and a kit with no swing is not this kit.
  return have_id && have_pluck && have_swing && have_filter && have_fx && have_sidechain &&
         progressions == engine::kModeCount && pads == engine::kTrackCount && dice > 0;
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

bool is_kit_id(const char* text) {
  const size_t length = std::strlen(text);
  if (length == 0 || length > engine::kKitIdLength) return false;
  for (const char* c = text; *c != '\0'; ++c) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) return false;
  }
  return true;
}

bool load_kit(const char* id, engine::Kit& kit) {
  if (!is_kit_id(id)) {
    hal::log("io: that is not a kit id, so no kit was looked for");
    return false;
  }
  char path[kPathCapacity];
  std::snprintf(path, sizeof path, "kits/%s/%s", id, kKitFileName);
  uint32_t size = 0;
  if (hal::read_file(path, reinterpret_cast<uint8_t*>(kit_file_), kKitFileCapacity - 1, &size) != hal::FileRead::ok ||
      size == 0) {
    refuse(path, "is not a kit this device can read");
    return false;
  }
  kit_file_[size] = '\0';
  char* lines[kMaxKitLines];
  const int count = split_lines(kit_file_, lines, kMaxKitLines);
  kit = engine::Kit{};
  if (count < 0 || !read_kit(lines, count, kit)) {
    refuse(path, "does not say what a kit is");
    return false;
  }
  // Its samples are looked for by its own id, so a kit that calls itself something
  // else than the folder it sits in would send us hunting in another folder.
  if (std::strcmp(kit.id, id) != 0) {
    refuse(path, "calls itself a different kit than the folder it is in");
    return false;
  }
  return true;
}

}  // namespace io
