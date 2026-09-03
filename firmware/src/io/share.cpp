#include "io/share.h"

#include <cstdint>
#include <cstring>

namespace io {

namespace {

constexpr char kLineageMark = '~';
constexpr const char* kBase36 = "0123456789abcdefghijklmnopqrstuvwxyz";
constexpr uint32_t kBase = 36;
constexpr uint32_t kFnvOffset = 2166136261u;  // FNV-1a, 32 bit
constexpr uint32_t kFnvPrime = 16777619u;

// The id is a hash of the loop's own code rather than a draw from the PRNG, so the
// same loop always has the same id, an unchanged loop re-shared keeps the id its
// parent gave it, and nothing has to be kept anywhere between power cycles (D-105).
// Six base36 characters hold 2.2 billion values, which the hash folds into.
void write_id(const char* code, char* out) {
  uint32_t hash = kFnvOffset;
  for (const char* c = code; *c != '\0'; ++c) hash = (hash ^ static_cast<uint8_t>(*c)) * kFnvPrime;
  for (int i = engine::kLineageLength - 1; i >= 0; --i) {
    out[i] = kBase36[hash % kBase];
    hash /= kBase;
  }
  out[engine::kLineageLength] = '\0';
}

}  // namespace

engine::SectionCode shared_code(const engine::State& state, const engine::Kit& kit) {
  engine::State bare = state;  // the loop's own code names no parent
  bare.lineage[0] = '\0';
  engine::SectionCode code = engine::encode(bare, kit);
  char id[engine::kLineageLength + 1];
  write_id(code.text, id);
  const int length = static_cast<int>(std::strlen(code.text));
  // The worst case is 238 characters (share-format §6), so the id always fits; a kit
  // id longer than the grammar allows would be the only way here, and it cannot be.
  if (length + 1 + engine::kLineageLength >= engine::kSectionCodeCapacity) return code;
  code.text[length] = kLineageMark;
  std::memcpy(code.text + length + 1, id, engine::kLineageLength + 1);
  return code;
}

}  // namespace io
