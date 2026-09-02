#include "ui/song.h"

#include <cstdio>

#include "ui/color.h"
#include "ui/font.h"

namespace ui {

namespace {

constexpr int kGroup = 4;           // letters per group, groups per row
constexpr int kGroupColumns = 5;    // four letters and a space
constexpr int kFilledPercent = 30;  // a filled tile's fill, of the text colour
constexpr int kTitleCapacity = 12;
const char* const kEmpty = "empty";

constexpr uint16_t kBackground = rgb565(kScreenBackground);
constexpr uint16_t kText = rgb565(kScreenText);
constexpr uint16_t kAccent565 = rgb565(kAccent);
constexpr uint16_t kLegend565 = rgb565(kLegend);
constexpr uint16_t kFilled = rgb565(blend(kScreenBackground, kScreenText, kFilledPercent));

// A letter's place: groups of four, four groups a row (§9.6).
void letter_at(int index, int& x, int& y) {
  const int group = index / kGroup;
  x = kMargin + ((group % kGroup) * kGroupColumns + index % kGroup) * kGlyphAdvance;
  y = kSongLettersTop + (group / kGroup) * kLineHeight;
}

void draw_letters(Canvas& canvas, const SongModel& model) {
  if (model.length == 0) {
    draw_text(canvas, kMargin, kSongLettersTop, kEmpty, kLegend565);
    return;
  }
  char letter[2] = {'\0', '\0'};
  for (int i = 0; i < model.length; ++i) {
    int x = 0;
    int y = 0;
    letter_at(i, x, y);
    letter[0] = model.letters[i];
    draw_text(canvas, x, y, letter, i == model.playing ? kAccent565 : kText);
  }
}

// Eight tiles in the pads' row: an outline when empty, filled when the song holds
// anything, the accent for the song being played and edited.
void draw_tiles(Canvas& canvas, const SongModel& model) {
  char digit[2] = {'\0', '\0'};
  for (int i = 0; i < engine::kSongSlotCount; ++i) {
    const int x = kMargin + i * (kSongTileSize + kSongTileGap);
    const bool current = i + 1 == model.song;
    if (current) {
      fill_rect(canvas, x, kSongTileTop, kSongTileSize, kSongTileSize, kAccent565);
    } else if (model.filled[i]) {
      fill_rect(canvas, x, kSongTileTop, kSongTileSize, kSongTileSize, kFilled);
    } else {
      rect_outline(canvas, x, kSongTileTop, kSongTileSize, kSongTileSize, kLegend565);
    }
    digit[0] = static_cast<char>('1' + i);
    draw_text(canvas, x + (kSongTileSize - kGlyphWidth * kFontScale) / 2, kSongTileTop + (kSongTileSize - kLineHeight) / 2,
              digit, current ? kBackground : kText);
  }
}

}  // namespace

const char* const kSongHint[kSongHintLines] = {"A-D add", "undo removes", "hold dice clears", "pads pick a song"};

void draw_song_view(uint16_t* framebuffer, const SongModel& model) {
  Canvas canvas{framebuffer};
  fill(canvas, kBackground);
  char title[kTitleCapacity];
  std::snprintf(title, sizeof title, "song %d", model.song);
  draw_text(canvas, kMargin, kSongTitleTop, title, kText);
  draw_letters(canvas, model);
  draw_tiles(canvas, model);
  if (!model.show_hint) return;
  for (int i = 0; i < kSongHintLines; ++i) {
    draw_text(canvas, kMargin, kSongHintTop + i * kLineHeight, kSongHint[i], kLegend565);
  }
}

}  // namespace ui
