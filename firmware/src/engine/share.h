#pragma once

#include "engine/kit.h"
#include "engine/limits.h"
#include "engine/state.h"

// PB2 share codes (PRD §10, spec/share-format.md). The code is the contract
// between web, device and tests: decode → encode of a canonical code is
// byte-identical, and unknown future fields are ignored (T-15, T-16).
namespace engine {

struct SectionCode {
  char text[kSectionCodeCapacity];  // NUL-terminated
};

struct SongCode {
  char text[kSongCodeCapacity];  // NUL-terminated
};

// Sections A–D plus the arrangement (§6.8, D-025). Section states carry no
// lineage of their own; the song has at most one.
struct Song {
  State sections[kSectionCount];
  uint8_t arrangement_length;  // 1–64
  char arrangement[kMaxArrangementLength];  // letters A–D, one cycle each
  char lineage[kLineageLength + 1];
};

// Result of a load. A code naming a kit the caller lacks loads with `kit`, says
// so through kit_substituted and requested_kit, and encodes kit's id from then
// on (D-026). ok == false means the code did not load and state is unspecified.
struct Decoded {
  bool ok;
  bool kit_substituted;
  char requested_kit[kKitIdLength + 1];
  State state;
};

struct DecodedSong {
  bool ok;
  bool kit_substituted;
  char requested_kit[kKitIdLength + 1];
  Song song;
};

Decoded decode(const char* code, const Kit& kit);
SectionCode encode(const State& state, const Kit& kit);

DecodedSong decode_song(const char* code, const Kit& kit);
SongCode encode_song(const Song& song, const Kit& kit);

}  // namespace engine
