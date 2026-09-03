#include "io/store.h"

#include <cstdio>
#include <cstring>

#include "hal/hal.h"

namespace io {

namespace {

// A song file is four section codes and the arrangement, one per line (D-104):
//
//   RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1
//   ... B, C, D ...
//   AABABBCD~k9z2ab
//
// The last line is empty when the song has no arrangement, which is why a song
// file is not an RT2S code: that grammar has no empty arrangement (share-format §5).
constexpr int kSongLines = engine::kSectionCount + 1;
constexpr uint32_t kSongFileCapacity = engine::kSectionCodeCapacity * engine::kSectionCount +
                                       engine::kMaxArrangementLength + engine::kLineageLength + kSongLines + 1;
constexpr uint32_t kSettingsFileCapacity = 256;
constexpr int kPathCapacity = 20;
constexpr char kNewline = '\n';
constexpr char kLineageMark = '~';
constexpr char kKeySeparator = '=';
constexpr char kFirstSectionLetter = 'A';

// One staging buffer for the file being read or written. io/ runs on the main loop
// only, and firmware allocates nothing after init (§12 rule 4).
char file_[kSongFileCapacity];

constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_lower_or_digit(char c) { return is_lower(c) || is_digit(c); }
constexpr bool is_section_letter(char c) { return c >= kFirstSectionLetter && c < kFirstSectionLetter + engine::kSectionCount; }

void song_path(int slot, char* out) { std::snprintf(out, kPathCapacity, "songs/%d.txt", slot); }

void refuse(const char* path, const char* why) {
  char line[kPathCapacity + 48];
  std::snprintf(line, sizeof line, "io: %s %s", path, why);
  hal::log(line);
}

// Reads a whole file into file_ and terminates it. False when it is missing, empty
// or bigger than the buffer, which for a file this firmware wrote cannot happen.
bool read_file(const char* path, uint32_t capacity) {
  uint32_t size = 0;
  if (!hal::read_file(path, reinterpret_cast<uint8_t*>(file_), capacity - 1, &size)) return false;
  file_[size] = '\0';
  return size > 0;
}

// Splits file_ into NUL-terminated lines in place. Returns how many, or -1 when
// there are more than `capacity`; the empty piece after a trailing newline is not a line.
int split_lines(char** lines, int capacity) {
  int count = 0;
  char* start = file_;
  for (char* c = file_;; ++c) {
    if (*c != kNewline && *c != '\0') continue;
    const bool end = *c == '\0';
    if (end && c == start) break;  // the file ended with its last newline
    if (count == capacity) return -1;
    *c = '\0';
    lines[count++] = start;
    start = c + 1;
    if (end) break;
  }
  return count;
}

// `AABABBCD`, then an optional `~` and six base36 characters, as a song code ends
// (share-format §5). An empty line is a song with no arrangement yet.
bool read_arrangement(const char* line, engine::Song& song) {
  int length = 0;
  const char* c = line;
  for (; is_section_letter(*c); ++c) {
    if (length >= engine::kMaxArrangementLength) return false;
    song.arrangement[length++] = *c;
  }
  song.arrangement_length = static_cast<uint8_t>(length);
  song.lineage[0] = '\0';
  if (*c == '\0') return true;
  if (*c != kLineageMark) return false;
  ++c;
  for (int i = 0; i < engine::kLineageLength; ++i) {
    if (!is_lower_or_digit(c[i])) return false;
    song.lineage[i] = c[i];
  }
  song.lineage[engine::kLineageLength] = '\0';
  return c[engine::kLineageLength] == '\0';
}

// Appends to file_ at `length`, which the caller must have kept inside the buffer.
uint32_t append(uint32_t length, const char* text) {
  const uint32_t size = static_cast<uint32_t>(std::strlen(text));
  std::memcpy(file_ + length, text, size);
  return length + size;
}

uint32_t append(uint32_t length, char c) {
  file_[length] = c;
  return length + 1;
}

// A settings line is `key=value`; the value is an integer, and a flag is 0 or 1.
// Anything else — an unknown key, junk, a value outside the row's range — is left
// as it is, so a card written by another firmware still loads what it shares.
void read_setting(char* line, Settings& settings) {
  char* separator = std::strchr(line, kKeySeparator);
  if (separator == nullptr) return;
  *separator = '\0';
  const char* key = line;
  const char* text = separator + 1;
  int value = 0;
  int digits = 0;
  for (const char* c = text; is_digit(*c); ++c) {
    value = value * 10 + (*c - '0');
    if (++digits > 3) return;
  }
  if (digits == 0 || text[digits] != '\0') return;
  const bool flag = value <= 1;
  if (std::strcmp(key, "song") == 0 && value >= kFirstSlot && value <= kLastSlot) settings.song = value;
  if (std::strcmp(key, "brightness") == 0 && value <= 100) settings.brightness = value;
  if (std::strcmp(key, "sleep") == 0) settings.sleep_minutes = value;
  if (std::strcmp(key, "midi-in") == 0 && flag) settings.midi_clock_in = value == 1;
  if (std::strcmp(key, "midi-out") == 0 && flag) settings.midi_clock_out = value == 1;
  if (std::strcmp(key, "sync-in") == 0 && flag) settings.sync_in = value == 1;
  if (std::strcmp(key, "sync-out") == 0 && flag) settings.sync_out = value == 1;
}

}  // namespace

bool operator==(const Settings& a, const Settings& b) {
  return a.song == b.song && a.brightness == b.brightness && a.sleep_minutes == b.sleep_minutes &&
         a.midi_clock_in == b.midi_clock_in && a.midi_clock_out == b.midi_clock_out && a.sync_in == b.sync_in &&
         a.sync_out == b.sync_out;
}

bool load_song(int slot, const engine::Kit& kit, engine::Song& song) {
  char path[kPathCapacity];
  song_path(slot, path);
  if (!read_file(path, kSongFileCapacity)) return false;
  char* lines[kSongLines];
  if (split_lines(lines, kSongLines) != kSongLines) {
    refuse(path, "is not four sections and an arrangement");
    return false;
  }
  for (int s = 0; s < engine::kSectionCount; ++s) {
    const engine::Decoded decoded = engine::decode(lines[s], kit);
    if (!decoded.ok) {
      refuse(path, "holds a code that did not load");
      return false;
    }
    song.sections[s] = decoded.state;
  }
  if (!read_arrangement(lines[engine::kSectionCount], song)) {
    refuse(path, "holds an arrangement that did not load");
    return false;
  }
  return true;
}

bool save_song(int slot, const engine::Kit& kit, const engine::Song& song) {
  uint32_t length = 0;
  for (int s = 0; s < engine::kSectionCount; ++s) {
    const engine::SectionCode code = engine::encode(song.sections[s], kit);
    length = append(length, code.text);
    length = append(length, kNewline);
  }
  for (int i = 0; i < song.arrangement_length; ++i) length = append(length, song.arrangement[i]);
  if (song.lineage[0] != '\0') {
    length = append(length, kLineageMark);
    length = append(length, song.lineage);
  }
  length = append(length, kNewline);
  char path[kPathCapacity];
  song_path(slot, path);
  return hal::write_file(path, reinterpret_cast<const uint8_t*>(file_), length);
}

bool load_settings(Settings& settings) {
  settings = kDefaultSettings;
  if (!read_file("settings.txt", kSettingsFileCapacity)) return false;
  char* lines[kSettingsFileCapacity / 4];
  const int count = split_lines(lines, static_cast<int>(kSettingsFileCapacity / 4));
  if (count < 0) return false;
  for (int i = 0; i < count; ++i) read_setting(lines[i], settings);
  return true;
}

bool save_settings(const Settings& settings) {
  const int length = std::snprintf(file_, kSettingsFileCapacity,
                                   "song=%d\nbrightness=%d\nsleep=%d\nmidi-in=%d\nmidi-out=%d\nsync-in=%d\nsync-out=%d\n",
                                   settings.song, settings.brightness, settings.sleep_minutes, settings.midi_clock_in,
                                   settings.midi_clock_out, settings.sync_in, settings.sync_out);
  if (length <= 0) return false;
  return hal::write_file("settings.txt", reinterpret_cast<const uint8_t*>(file_), static_cast<uint32_t>(length));
}

}  // namespace io
