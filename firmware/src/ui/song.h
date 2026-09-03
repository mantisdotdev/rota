#pragma once

#include <cstdint>

#include "engine/limits.h"
#include "ui/draw.h"
#include "ui/overlay.h"

// The song view (§9.6, D-095): the song number, the arrangement in fours with the
// playing letter in the accent, eight song tiles laid out like the pads, and the
// hint, one item per row because the whole of it does not fit one row at 12 px,
// shown until the player has added a letter or picked a song. Laid out above the
// overlay's fullest reservation, so the tutorial's prompt never covers it.
namespace ui {

constexpr int kSongTitleTop = kTopRow;
constexpr int kSongLettersTop = kContentTop + 2;  // four rows of four groups of four: 64 letters
constexpr int kSongTileTop = 82;
constexpr int kSongTileSize = 26;
constexpr int kSongTileGap = 14;
constexpr int kSongHintTop = 112;
constexpr int kSongHintLines = 4;
constexpr int kNoLetter = -1;
static_assert(kSongHintTop + kSongHintLines * kLineHeight <= kContentBottomMin, "the song view must fit above the prompt rows");

extern const char* const kSongHint[kSongHintLines];

struct SongModel {
  int song;  // 1–8
  bool filled[engine::kSongSlotCount];
  const char* letters;  // the arrangement, A–D
  int length;
  int playing;  // index of the letter playing, or kNoLetter
  bool show_hint;
};

void draw_song_view(uint16_t* framebuffer, const SongModel& model);

}  // namespace ui
