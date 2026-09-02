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

bool read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t* size) {
  std::FILE* file = std::fopen(full_path(path).c_str(), "rb");
  if (file == nullptr) return false;
  const size_t read = std::fread(out, 1, capacity, file);
  const bool more = std::fgetc(file) != EOF;  // larger than the buffer
  const bool failed = std::ferror(file) != 0;  // a read error, not the end
  std::fclose(file);
  if (more || failed) return false;
  *size = static_cast<uint32_t>(read);
  return true;
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
