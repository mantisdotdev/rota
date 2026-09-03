// Teensy storage: the board's own microSD slot (SDIO), FAT32 (BOM). Whole files
// only; paths are relative to the card's root.
#include <Arduino.h>
#include <SD.h>

#include "hal/hal.h"
#include "hal/teensy/teensy_internal.h"

namespace {

constexpr int kPathCapacity = 128;

bool card_ready_ = false;

// Creates every directory above the file, as write_file on the host does.
void make_parents(const char* path) {
  char parent[kPathCapacity];
  int length = 0;
  for (const char* c = path; *c != '\0' && length + 1 < kPathCapacity; ++c) {
    if (*c == '/' && length > 0) {
      parent[length] = '\0';
      if (!SD.exists(parent)) SD.mkdir(parent);
    }
    parent[length++] = *c;
  }
}

}  // namespace

namespace hal::teensy {

void storage_init() {
  card_ready_ = SD.begin(BUILTIN_SDCARD);
  if (!card_ready_) Serial.println("hal/teensy: no SD card");
}

}  // namespace hal::teensy

namespace hal {

FileRead read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size) {
  *size = 0;
  if (!card_ready_) return FileRead::missing;
  File file = SD.open(path, FILE_READ);
  if (!file) return FileRead::missing;
  const uint64_t length = file.size();  // compared before any narrowing: a size is never a verdict
  if (length > capacity) {
    file.close();
    return FileRead::unusable;
  }
  const int read = file.read(out, static_cast<size_t>(length));
  file.close();
  if (read < 0 || static_cast<uint64_t>(read) != length) return FileRead::unusable;
  *size = static_cast<uint32_t>(length);
  return FileRead::ok;
}

bool write_file(const char* path, const uint8_t* data, uint32_t size) {
  if (!card_ready_) return false;
  make_parents(path);
  File file = SD.open(path, FILE_WRITE_BEGIN);  // truncates
  if (!file) return false;
  const size_t written = file.write(data, size);
  file.close();
  return written == size;
}

}  // namespace hal
