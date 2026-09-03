// Host storage: a directory stands in for the SD card. Its path is compiled in by
// host/CMakeLists.txt (ROTA_STORAGE_DIR, under out/, which git ignores).
#include <cstdio>
#include <filesystem>
#include <string>

#include "hal/hal.h"

namespace {

std::string full_path(const char* path) { return std::string(ROTA_STORAGE_DIR) + "/" + path; }

}  // namespace

namespace hal {

// The host has no PSRAM to speak of and no reason to care: an ordinary array.
int16_t* sample_memory(uint32_t* frames) {
  static int16_t memory[kSampleMemoryFrames];
  *frames = kSampleMemoryFrames;
  return memory;
}

FileRead read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size) {
  *size = 0;
  const std::filesystem::path target = full_path(path);
  std::error_code error;
  const std::uintmax_t on_disk = std::filesystem::file_size(target, error);
  // A file that cannot even be sized is still a file: only a path with nothing at it
  // is missing, or a pick would copy over what it could not read (T-97).
  if (error) return std::filesystem::exists(target, error) ? FileRead::unusable : FileRead::missing;
  if (on_disk > capacity) return FileRead::unusable;
  std::FILE* file = std::fopen(target.c_str(), "rb");
  if (file == nullptr) return FileRead::unusable;
  const size_t read = std::fread(out, 1, capacity, file);
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed || read != on_disk) return FileRead::unusable;
  *size = static_cast<uint32_t>(read);
  return FileRead::ok;
}

bool write_file(const char* path, const uint8_t* data, uint32_t size) {
  const std::filesystem::path target = full_path(path);
  std::error_code ignored;
  std::filesystem::create_directories(target.parent_path(), ignored);
  std::FILE* file = std::fopen(target.c_str(), "wb");
  if (file == nullptr) return false;
  const bool written = std::fwrite(data, 1, size, file) == size;
  return std::fclose(file) == 0 && written;
}

}  // namespace hal
