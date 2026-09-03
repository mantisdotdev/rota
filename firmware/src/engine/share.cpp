#include "engine/share.h"

#include <cstring>

namespace engine {

namespace {

constexpr const char* kSectionPrefix = "RT2:";
constexpr const char* kSongPrefix = "RT2S:";
constexpr char kFieldSeparator = ':';
constexpr char kTrackSeparator = '-';
constexpr char kModifierSeparator = ',';
constexpr char kLineageMark = '~';
constexpr char kSectionSeparator = ';';
constexpr char kArrangementMark = '/';
constexpr char kRestChar = '.';
constexpr char kFirstSectionLetter = 'A';
constexpr char kLastSectionLetter = 'D';
constexpr const char* kBase36 = "0123456789abcdefghijklmnopqrstuvwxyz";
constexpr int kNotesPerHitRow = 8;  // step char = (hits − 1) × 8 + note
constexpr int kMaxStepValue = kMaxHitsPerStep * kNotesPerHitRow;
constexpr int kModifierCount = 4;  // level, tone, send, chance
constexpr int kRootCount = 12;
constexpr int kMinBpm = 60;
constexpr int kMaxBpm = 180;
constexpr int kMaxSwing = 100;
constexpr int kMaxIntegerDigits = 8;  // enough for any field, small enough never to overflow

constexpr const char* kRootCodes[kRootCount] = {"c", "cs", "d", "ds", "e", "f", "fs", "g", "gs", "a", "as", "b"};
constexpr const char* kModeCodes[kModeCount] = {"m", "M", "dor", "pm", "pM"};
// Longest first when reading, so `dor`, `pm` and `pM` win over `m` and `M`.
constexpr Mode kModeReadOrder[kModeCount] = {Mode::dorian, Mode::pentatonic_minor, Mode::pentatonic_major,
                                             Mode::minor, Mode::major};
constexpr char kAltCodes[] = {'e', 'a', 'b', 'f'};
constexpr char kSpeedCodes[] = {'h', '1', 'd'};

// Characters that end a track's steps, or the junk after its modifier digits, in
// either kind of code.
constexpr const char* kStepStops = ",-:~;/";
constexpr const char* kTrackStops = "-:~;/";

constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_lower_or_digit(char c) { return is_lower(c) || is_digit(c); }
constexpr bool is_section_letter(char c) { return c >= kFirstSectionLetter && c <= kLastSectionLetter; }

int base36_value(char c) {
  if (is_digit(c)) return c - '0';
  if (is_lower(c)) return c - 'a' + 10;
  return -1;
}

// ---- writing -------------------------------------------------------------------

class Writer {
 public:
  Writer(char* buffer, int capacity) : buffer_(buffer), capacity_(capacity), length_(0) { buffer_[0] = '\0'; }

  void put(char c) {
    if (length_ + 1 >= capacity_) return;  // never for a valid state: the capacities cover the worst case
    buffer_[length_++] = c;
    buffer_[length_] = '\0';
  }

  void put(const char* text) {
    for (; *text != '\0'; ++text) put(*text);
  }

  // Canonical integers: no leading zeros, `0` for zero (share-format §4).
  void put_int(int value) {
    char digits[kMaxIntegerDigits];
    int count = 0;
    do {
      digits[count++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value > 0 && count < kMaxIntegerDigits);
    while (count > 0) put(digits[--count]);
  }

 private:
  char* buffer_;
  int capacity_;
  int length_;
};

void write_track(Writer& writer, const Track& track, const KitPad& pad) {
  writer.put(kAltCodes[static_cast<int>(track.alt)]);
  writer.put(kSpeedCodes[static_cast<int>(track.speed)]);
  for (int i = 0; i < track.step_count; ++i) {
    const Step step = track.steps[i];
    writer.put(is_rest(step) ? kRestChar : kBase36[(step.hits - 1) * kNotesPerHitRow + step.note]);
  }
  // The modifier group is omitted iff all four are at their defaults (share-format §3).
  const bool all_default = track.level == kDefaultLevel && track.tone == kDefaultTone &&
                           track.send == pad.send && track.chance == kDefaultTrackChance;
  if (all_default) return;
  writer.put(kModifierSeparator);
  writer.put(kBase36[track.level]);
  writer.put(kBase36[track.tone]);
  writer.put(kBase36[track.send]);
  writer.put(kBase36[track.chance]);
}

// Everything after the prefix and before the lineage: kit through tracks.
void write_body(Writer& writer, const State& state, const Kit& kit) {
  writer.put(state.kit);
  writer.put(kFieldSeparator);
  writer.put_int(state.bpm);
  writer.put(kFieldSeparator);
  writer.put_int(state.filter);
  writer.put(kFieldSeparator);
  writer.put_int(state.fx);
  writer.put(kFieldSeparator);
  writer.put_int(state.chance);
  writer.put(kFieldSeparator);
  writer.put_int(state.swing);
  writer.put(kFieldSeparator);
  writer.put(kRootCodes[state.key.root]);
  writer.put(kModeCodes[static_cast<int>(state.key.mode)]);
  writer.put(kFieldSeparator);
  for (int i = 0; i < kTrackCount; ++i) {
    if (i > 0) writer.put(kTrackSeparator);
    write_track(writer, state.tracks[i], kit.pads[i]);
  }
}

void write_lineage(Writer& writer, const char* lineage) {
  if (lineage[0] == '\0') return;
  writer.put(kLineageMark);
  writer.put(lineage);
}

// ---- reading -------------------------------------------------------------------

class Reader {
 public:
  explicit Reader(const char* text) : at_(text) {}

  char peek() const { return *at_; }
  bool at_end() const { return *at_ == '\0'; }

  void advance() {
    if (!at_end()) ++at_;
  }

  bool accept(char c) {
    if (*at_ != c) return false;
    ++at_;
    return true;
  }

  bool accept(const char* text) {
    const size_t length = std::strlen(text);
    if (std::strncmp(at_, text, length) != 0) return false;
    at_ += length;
    return true;
  }

  // Digits only, at least one; leading zeros load and re-encode canonically (D-039).
  bool read_int(int min, int max, int& out) {
    int value = 0;
    int digits = 0;
    while (is_digit(peek()) && digits < kMaxIntegerDigits) {
      value = value * 10 + (peek() - '0');
      advance();
      ++digits;
    }
    if (digits == 0 || value < min || value > max) return false;
    out = value;
    return true;
  }

  // Up to `capacity` characters accepted by `keep`, NUL-terminated; returns the count.
  int read_while(bool (*keep)(char), char* out, int capacity) {
    int count = 0;
    while (keep(peek()) && count < capacity) out[count++] = *at_++;
    out[count] = '\0';
    return count;
  }

  bool at_any_of(const char* stops) const { return at_end() || std::strchr(stops, peek()) != nullptr; }

  void skip_until(const char* stops) {
    while (!at_any_of(stops)) advance();
  }

 private:
  const char* at_;
};

bool read_tenths(Reader& reader, int& out) {
  const int value = base36_value(reader.peek());
  if (value < 0 || value > kTenthsMax) return false;
  reader.advance();
  out = value;
  return true;
}

bool read_field_int(Reader& reader, int min, int max, int& out) {
  return reader.read_int(min, max, out) && reader.accept(kFieldSeparator);
}

bool read_key(Reader& reader, Key& key) {
  int root = -1;
  // Two-character roots (`cs`) must win over their one-character prefix (`c`).
  for (int pass = 2; pass >= 1 && root < 0; --pass) {
    for (int i = 0; i < kRootCount; ++i) {
      if (static_cast<int>(std::strlen(kRootCodes[i])) == pass && reader.accept(kRootCodes[i])) {
        root = i;
        break;
      }
    }
  }
  if (root < 0) return false;
  for (Mode mode : kModeReadOrder) {
    if (reader.accept(kModeCodes[static_cast<int>(mode)])) {
      key = Key{static_cast<uint8_t>(root), mode};
      return true;
    }
  }
  return false;
}

bool read_code_char(Reader& reader, const char* codes, int count, int& index) {
  for (int i = 0; i < count; ++i) {
    if (reader.peek() == codes[i]) {
      index = i;
      reader.advance();
      return true;
    }
  }
  return false;
}

bool read_track(Reader& reader, Track& track) {
  int alt = 0;
  int speed = 0;
  if (!read_code_char(reader, kAltCodes, sizeof kAltCodes, alt)) return false;
  if (!read_code_char(reader, kSpeedCodes, sizeof kSpeedCodes, speed)) return false;
  track.alt = static_cast<Alt>(alt);
  track.speed = static_cast<Speed>(speed);

  track.step_count = 0;
  while (!reader.at_any_of(kStepStops)) {
    if (track.step_count >= kMaxStepsPerTrack) return false;
    const char c = reader.peek();
    Step step{0, 0};
    if (c != kRestChar) {
      const int value = base36_value(c);
      if (value < 0 || value >= kMaxStepValue) return false;
      step = Step{static_cast<uint8_t>(value / kNotesPerHitRow + 1), static_cast<uint8_t>(value % kNotesPerHitRow)};
    }
    track.steps[track.step_count++] = step;
    reader.advance();
  }

  if (!reader.accept(kModifierSeparator)) return true;
  int values[kModifierCount];
  for (int& value : values) {
    if (!read_tenths(reader, value)) return false;
  }
  track.level = static_cast<Tenths>(values[0]);
  track.tone = static_cast<Tenths>(values[1]);
  track.send = static_cast<Tenths>(values[2]);
  track.chance = static_cast<Tenths>(values[3]);
  reader.skip_until(kTrackStops);  // anything after the four digits is a future extension (T-16)
  return true;
}

// Kit through tracks. `state` starts as make_state(kit), so an omitted modifier
// group keeps the kit's defaults and the state carries kit's id whatever the code
// named (D-026); the id as written lands in requested_kit.
bool read_body(Reader& reader, const Kit& kit, State& state, char* requested_kit) {
  if (reader.read_while(is_lower_or_digit, requested_kit, kKitIdLength) == 0) return false;
  if (!reader.accept(kFieldSeparator)) return false;
  int bpm = 0;
  int filter = 0;
  int fx = 0;
  int chance = 0;
  int swing = 0;
  if (!read_field_int(reader, kMinBpm, kMaxBpm, bpm)) return false;
  if (!read_field_int(reader, 0, kTenthsMax, filter)) return false;
  if (!read_field_int(reader, 0, kTenthsMax, fx)) return false;
  if (!read_field_int(reader, 0, kTenthsMax, chance)) return false;
  if (!read_field_int(reader, 0, kMaxSwing, swing)) return false;
  if (!read_key(reader, state.key)) return false;
  if (!reader.accept(kFieldSeparator)) return false;
  for (int i = 0; i < kTrackCount; ++i) {
    if (i > 0 && !reader.accept(kTrackSeparator)) return false;
    if (!read_track(reader, state.tracks[i])) return false;
  }
  std::strncpy(state.kit, kit.id, kKitIdLength);
  state.bpm = static_cast<uint8_t>(bpm);
  state.filter = static_cast<Tenths>(filter);
  state.fx = static_cast<Tenths>(fx);
  state.chance = static_cast<Tenths>(chance);
  state.swing = static_cast<uint8_t>(swing);
  return true;
}

// Extra `:`-separated fields after the tracks are a future extension: skipped (T-16).
void skip_unknown_fields(Reader& reader, const char* stops) {
  if (reader.accept(kFieldSeparator)) reader.skip_until(stops);
}

// Scans at most `limit` + 1 characters, so a code that is too long — or one whose
// terminator is missing altogether — costs a bounded read and no more (T-62).
bool within_limit(const char* code, int limit) {
  for (int i = 0; i <= limit; ++i) {
    if (code[i] == '\0') return true;
  }
  return false;
}

bool read_lineage(Reader& reader, char* lineage) {
  if (!reader.accept(kLineageMark)) {
    lineage[0] = '\0';
    return true;
  }
  return reader.read_while(is_lower_or_digit, lineage, kLineageLength) == kLineageLength;
}

}  // namespace

bool operator==(const Song& a, const Song& b) {
  if (a.arrangement_length != b.arrangement_length || std::strcmp(a.lineage, b.lineage) != 0) return false;
  if (std::memcmp(a.arrangement, b.arrangement, a.arrangement_length) != 0) return false;
  for (int s = 0; s < kSectionCount; ++s) {
    if (a.sections[s] != b.sections[s]) return false;
  }
  return true;
}

Decoded decode(const char* code, const Kit& kit) {
  Decoded result{};
  if (!within_limit(code, kMaxSectionCodeInput)) return result;
  Reader reader(code);
  if (!reader.accept(kSectionPrefix)) return result;
  result.state = make_state(kit);
  if (!read_body(reader, kit, result.state, result.requested_kit)) return result;
  skip_unknown_fields(reader, "~");
  if (!read_lineage(reader, result.state.lineage)) return result;
  if (!reader.at_end()) return result;
  result.kit_substituted = std::strcmp(result.requested_kit, kit.id) != 0;
  result.ok = true;
  return result;
}

SectionCode encode(const State& state, const Kit& kit) {
  SectionCode code;
  Writer writer(code.text, kSectionCodeCapacity);
  writer.put(kSectionPrefix);
  write_body(writer, state, kit);
  write_lineage(writer, state.lineage);
  return code;
}

DecodedSong decode_song(const char* code, const Kit& kit) {
  DecodedSong result{};
  if (!within_limit(code, kMaxSongCodeInput)) return result;
  Reader reader(code);
  if (!reader.accept(kSongPrefix)) return result;
  for (int s = 0; s < kSectionCount; ++s) {
    State& section = result.song.sections[s];
    section = make_state(kit);
    char section_kit[kKitIdLength + 1];
    if (!read_body(reader, kit, section, section_kit)) return result;
    if (s == 0) {
      std::strcpy(result.requested_kit, section_kit);
    } else if (std::strcmp(section_kit, result.requested_kit) != 0) {
      return result;  // all four sections name one kit (D-025)
    }
    skip_unknown_fields(reader, ";/~");
    const char expected = s + 1 < kSectionCount ? kSectionSeparator : kArrangementMark;
    if (!reader.accept(expected)) return result;
  }
  int length = 0;
  while (is_section_letter(reader.peek())) {
    if (length >= kMaxArrangementLength) return result;
    result.song.arrangement[length++] = reader.peek();
    reader.advance();
  }
  if (length == 0) return result;
  result.song.arrangement_length = static_cast<uint8_t>(length);
  if (!read_lineage(reader, result.song.lineage)) return result;
  if (!reader.at_end()) return result;
  result.kit_substituted = std::strcmp(result.requested_kit, kit.id) != 0;
  result.ok = true;
  return result;
}

SongCode encode_song(const Song& song, const Kit& kit) {
  SongCode code;
  Writer writer(code.text, kSongCodeCapacity);
  writer.put(kSongPrefix);
  for (int s = 0; s < kSectionCount; ++s) {
    if (s > 0) writer.put(kSectionSeparator);
    write_body(writer, song.sections[s], kit);
  }
  writer.put(kArrangementMark);
  for (int i = 0; i < song.arrangement_length; ++i) writer.put(song.arrangement[i]);
  write_lineage(writer, song.lineage);
  return code;
}

}  // namespace engine
