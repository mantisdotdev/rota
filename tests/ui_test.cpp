// The views under the scripted harness (tests/app_support.h): every §9 view drawn
// into the fake HAL's framebuffer and read back with the screen's own font, the
// lights, and the first-run tutorial. spec/scenarios.md T-22, T-45, T-57, T-58,
// T-85, T-86, T-87, T-88, T-89, T-90, T-91, T-92.
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app_support.h"
#include "engine/edits.h"
#include "engine/kits/lofi.h"
#include "engine/share.h"
#include "engine_support.h"
#include "ui/color.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/leds.h"
#include "ui/overlay.h"
#include "ui/settings.h"
#include "ui/share.h"
#include "ui/song.h"
#include "ui/text.h"

using namespace app_support;

namespace {

constexpr uint16_t kText = ui::rgb565(ui::kScreenText);
constexpr uint16_t kAccent = ui::rgb565(ui::kAccent);
constexpr uint16_t kLegend = ui::rgb565(ui::kLegend);
constexpr uint16_t kBackground = ui::rgb565(ui::kScreenBackground);
constexpr uint16_t kMarker = 0xFFFF;

const char* const kClassicBeat = "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1";  // G-04

Button section(char letter) { return static_cast<Button>(static_cast<int>(Button::section_a) + (letter - 'A')); }

const uint16_t* screen() { return hal_fake::framebuffer(); }

// The lit pixels of `text` as the screen's font draws it, relative to its origin.
struct Stamp {
  std::vector<std::pair<int, int>> lit;
  int width;
};

Stamp stamp_of(const char* text) {
  std::vector<uint16_t> pixels(static_cast<size_t>(ui::kWidth) * ui::kHeight, 0);
  ui::Canvas canvas{pixels.data()};
  ui::draw_text(canvas, 0, 0, text, kMarker);
  Stamp stamp{{}, ui::text_width(text)};
  for (int y = 0; y < ui::kLineHeight; ++y) {
    for (int x = 0; x < stamp.width; ++x) {
      if (pixels[static_cast<size_t>(y) * ui::kWidth + x] == kMarker) stamp.lit.emplace_back(x, y);
    }
  }
  return stamp;
}

bool stamp_matches(const uint16_t* fb, const Stamp& stamp, int x, int y, uint16_t colour) {
  for (const auto& [dx, dy] : stamp.lit) {
    const int px = x + dx;
    const int py = y + dy;
    if (px < 0 || py < 0 || px >= ui::kWidth || py >= ui::kHeight) return false;
    if (fb[py * ui::kWidth + px] != colour) return false;
  }
  return true;
}

// `text` in `colour` with its origin exactly at (x, y).
bool text_at(const uint16_t* fb, int x, int y, const char* text, uint16_t colour) {
  const Stamp stamp = stamp_of(text);
  return !stamp.lit.empty() && stamp_matches(fb, stamp, x, y, colour);
}

// `text` in `colour` anywhere on the screen.
bool has_text(const uint16_t* fb, const char* text, uint16_t colour = kText) {
  const Stamp stamp = stamp_of(text);
  if (stamp.lit.empty()) return false;
  for (int y = 0; y + ui::kLineHeight <= ui::kHeight; ++y) {
    for (int x = 0; x + stamp.width <= ui::kWidth; ++x) {
      if (stamp_matches(fb, stamp, x, y, colour)) return true;
    }
  }
  return false;
}

int right_aligned(const char* text) { return ui::kWidth - ui::kMargin - ui::text_width(text); }

// Nothing but background on screen row `y`: the overlay's rows and the gaps round
// them hold no view content (Appendix D "Overlays").
bool background_row(const uint16_t* fb, int y) {
  for (int x = 0; x < ui::kWidth; ++x) {
    if (fb[y * ui::kWidth + x] != kBackground) return false;
  }
  return true;
}

bool same(ui::Rgb a, ui::Rgb b) { return a.red == b.red && a.green == b.green && a.blue == b.blue; }
ui::Rgb pad_led(int pad) {
  const hal_fake::Led led = hal_fake::led(pad);
  return ui::Rgb{led.red, led.green, led.blue};
}
ui::Rgb button_led(Button button) {
  const hal_fake::Led led = hal_fake::button_led(static_cast<int>(button));
  return ui::Rgb{led.red, led.green, led.blue};
}
constexpr ui::Rgb kOff{0, 0, 0};
constexpr ui::Rgb kButtonRest = ui::blend(kOff, ui::kScreenText, 6);
constexpr ui::Rgb kHalfAccent = ui::blend(kOff, ui::kAccent, 40);

// Holding undo and show together opens the settings (§9.4), whichever went down first.
void open_settings(World& w, Button first, Button second) {
  w.button_down(first);
  w.run_for(kSecond / 10);
  w.button_down(second);
  w.run_for(kSecond / 2);
  w.button_up(second);
  w.button_up(first);
  REQUIRE(w.model().view == app::View::settings);
}

// ---- A small QR reader, independent of the encoder: the modules come off the
// screen, the format information is checked against its own BCH remainder, the
// data codewords are unmasked, read in placement order, de-interleaved with the
// standard's block table (versions 1–10) and parsed as one byte-mode segment.

struct Grid {
  int size;
  std::vector<uint8_t> dark;
  bool at(int x, int y) const { return dark[static_cast<size_t>(y) * size + x] != 0; }
};

Grid grid_from_screen(const uint16_t* fb) {
  int side = 0;
  while (ui::kShareLeft + side < ui::kWidth && fb[ui::kShareTop * ui::kWidth + ui::kShareLeft + side] == kText) ++side;
  Grid grid{side / ui::kQrModulePx - 2 * ui::kQrQuietModules, {}};
  REQUIRE(grid.size > 0);
  grid.dark.assign(static_cast<size_t>(grid.size) * grid.size, 0);
  for (int y = 0; y < grid.size; ++y) {
    for (int x = 0; x < grid.size; ++x) {
      const int px = ui::kShareLeft + (ui::kQrQuietModules + x) * ui::kQrModulePx + 1;
      const int py = ui::kShareTop + (ui::kQrQuietModules + y) * ui::kQrModulePx + 1;
      grid.dark[static_cast<size_t>(y) * grid.size + x] = fb[py * ui::kWidth + px] == kBackground ? 1 : 0;
    }
  }
  return grid;
}

int alignment_positions(int version, int out[7]) {
  if (version == 1) return 0;
  const int count = version / 7 + 2;
  const int step = version == 32 ? 26 : (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
  int position = version * 4 + 10;
  for (int i = count - 1; i >= 1; --i, position -= step) out[i] = position;
  out[0] = 6;
  return count;
}

std::vector<uint8_t> function_modules(int size, int version) {
  std::vector<uint8_t> function(static_cast<size_t>(size) * size, 0);
  auto mark = [&](int x, int y) {
    if (x >= 0 && y >= 0 && x < size && y < size) function[static_cast<size_t>(y) * size + x] = 1;
  };
  for (int i = 0; i < size; ++i) {
    mark(6, i);
    mark(i, 6);
  }
  for (int dy = -4; dy <= 4; ++dy) {
    for (int dx = -4; dx <= 4; ++dx) {
      mark(3 + dx, 3 + dy);
      mark(size - 4 + dx, 3 + dy);
      mark(3 + dx, size - 4 + dy);
    }
  }
  int positions[7];
  const int count = alignment_positions(version, positions);
  for (int i = 0; i < count; ++i) {
    for (int j = 0; j < count; ++j) {
      if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0)) continue;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) mark(positions[i] + dx, positions[j] + dy);
      }
    }
  }
  for (int i = 0; i < 9; ++i) {
    mark(8, i);
    mark(i, 8);
  }
  for (int i = 0; i < 8; ++i) {
    mark(size - 1 - i, 8);
    mark(8, size - 1 - i);
  }
  if (version >= 7) {
    for (int i = 0; i < 6; ++i) {
      for (int j = 0; j < 3; ++j) {
        mark(size - 11 + j, i);
        mark(i, size - 11 + j);
      }
    }
  }
  return function;
}

bool mask_inverts(int mask, int x, int y) {
  switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}

int raw_data_modules(int version) {
  int result = (16 * version + 128) * version + 64;
  if (version >= 2) {
    const int count = version / 7 + 2;
    result -= (25 * count - 10) * count - 55;
    if (version >= 7) result -= 36;
  }
  return result;
}

// The QR standard's tables for versions 1–10, rows L, M, Q, H.
const int kEccPerBlock[4][11] = {{-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18},
                                 {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26},
                                 {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24},
                                 {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28}};
const int kBlocks[4][11] = {{-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4},
                            {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5},
                            {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8},
                            {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8}};

std::string decode_qr(const Grid& grid) {
  const int size = grid.size;
  const int version = (size - 17) / 4;
  REQUIRE(version >= 1);
  REQUIRE(version <= 10);
  int format = 0;
  for (int i = 0; i < 8; ++i) format |= (grid.at(size - 1 - i, 8) ? 1 : 0) << i;
  for (int i = 8; i < 15; ++i) format |= (grid.at(8, size - 15 + i) ? 1 : 0) << i;
  format ^= 0x5412;
  const int data_bits = format >> 10;
  int remainder = data_bits;
  for (int i = 0; i < 10; ++i) remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
  REQUIRE((format & 0x3FF) == remainder);  // the format information is intact
  const int mask = data_bits & 7;
  const int level_bits = data_bits >> 3;  // L 1, M 0, Q 3, H 2
  const int level = level_bits == 1 ? 0 : level_bits == 0 ? 1 : level_bits == 3 ? 2 : 3;

  const std::vector<uint8_t> function = function_modules(size, version);
  std::vector<uint8_t> bits;
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vertical = 0; vertical < size; ++vertical) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? size - 1 - vertical : vertical;
        if (function[static_cast<size_t>(y) * size + x] != 0) continue;
        bits.push_back((grid.at(x, y) != mask_inverts(mask, x, y)) ? 1 : 0);
      }
    }
  }
  const int raw_codewords = raw_data_modules(version) / 8;
  REQUIRE(static_cast<int>(bits.size()) >= raw_codewords * 8);
  std::vector<uint8_t> interleaved(static_cast<size_t>(raw_codewords), 0);
  for (int i = 0; i < raw_codewords * 8; ++i) {
    interleaved[static_cast<size_t>(i / 8)] = static_cast<uint8_t>((interleaved[static_cast<size_t>(i / 8)] << 1) | bits[static_cast<size_t>(i)]);
  }
  const int blocks = kBlocks[level][version];
  const int ecc_per_block = kEccPerBlock[level][version];
  const int short_blocks = blocks - raw_codewords % blocks;
  const int short_length = raw_codewords / blocks - ecc_per_block;
  std::vector<uint8_t> data;
  for (int block = 0; block < blocks; ++block) {
    const int length = short_length + (block < short_blocks ? 0 : 1);
    for (int j = 0, k = block; j < length; ++j, k += blocks) {
      if (j == short_length) k -= short_blocks;
      data.push_back(interleaved[static_cast<size_t>(k)]);
    }
  }
  size_t bit = 0;
  auto take = [&](int count) {
    int value = 0;
    for (int i = 0; i < count; ++i, ++bit) value = (value << 1) | ((data[bit / 8] >> (7 - bit % 8)) & 1);
    return value;
  };
  REQUIRE(take(4) == 4);  // byte mode
  const int count = take(version >= 10 ? 16 : 8);
  std::string text;
  for (int i = 0; i < count; ++i) text += static_cast<char>(take(8));
  return text;
}

}  // namespace

TEST_CASE("T-85 The ring's corners, the message row's status for 1.8 s and knob value for 1 s, the armed marker") {
  World w;
  w.tap(Pad::kick, 2);  // a dot at six o'clock, nearest the message row
  w.frame();
  const uint16_t* fb = screen();
  CHECK(text_at(fb, ui::kMargin, ui::kTopRow, "100", kText));                     // bpm, top-left
  CHECK(text_at(fb, right_aligned("A 1 87%"), ui::kTopRow, "A 1 87%", kText));  // section, song, battery (the fake reports 87 %)
  CHECK(text_at(fb, ui::kMargin, ui::kBottomRow, "2 kicks, spread evenly", kText));  // status in the message row
  CHECK(background_row(fb, ui::kBottomRow - 1));  // the ring stops above the message row
  CHECK(background_row(fb, ui::kBottomRow - 2));

  w.turn(Encoder::filter, -1);
  w.frame();
  CHECK(text_at(fb, ui::kMargin, ui::kBottomRow, "filter 0.9", kText));  // the knob's value takes the row
  CHECK_FALSE(has_text(fb, "2 kicks, spread evenly"));
  w.run_for(kSecond * 9 / 10);
  CHECK(text_at(fb, ui::kMargin, ui::kBottomRow, "filter 0.9", kText));  // 0.9 s: still there
  w.run_for(kSecond / 5);
  CHECK_FALSE(has_text(fb, "filter 0.9"));                                          // 1.1 s: gone
  CHECK(text_at(fb, ui::kMargin, ui::kBottomRow, "2 kicks, spread evenly", kText));  // and the status is back
  w.run_for(kSecond * 8 / 10);
  CHECK_FALSE(has_text(fb, "2 kicks, spread evenly"));  // 1.9 s: gone

  w.press(Button::split);
  w.frame();
  CHECK(text_at(fb, ui::kArmedAfterBpm, ui::kTopRow, "split", kAccent));  // armed: the top row, after the bpm
  w.press(Button::show);
  w.frame();
  CHECK(text_at(fb, right_aligned("split"), ui::kTopRow, "split", kAccent));  // elsewhere at the row's right
}

TEST_CASE("T-86 The text view: one line per track in the mini-notation, wrapped to the screen") {
  World w;
  w.press(Button::show);
  w.frame();
  const uint16_t* fb = screen();
  CHECK(has_text(fb, "nothing yet"));

  w.tap(Pad::kick, 3);
  w.tap(Pad::snare, 2);
  w.tap(Pad::hat);
  w.press(Button::split);
  w.tap(Pad::hat);  // hh hh*2
  w.tap(Pad::clap, 2);
  w.press(Button::swap);
  w.tap(Pad::clap);
  w.press(Button::swap);
  w.tap(Pad::clap);  // alt b
  w.tap(Pad::chord, 4);
  w.tap(Pad::bass, 2);
  ui::TextLines lines;
  ui::text_lines(w.state(0), engine::kits::kLofi, lines);
  REQUIRE(lines.count == 7);
  CHECK(std::string(lines.text[0]) == "kick   bd bd bd");
  CHECK(std::string(lines.text[1]) == "snare  ~ sd ~ sd");
  CHECK(std::string(lines.text[2]) == "hat    hh hh*2");
  CHECK(std::string(lines.text[3]) == "clap   <~ cp ~ cp>");
  CHECK(std::string(lines.text[4]) == "       (alt: b)");  // the note wraps: 25 columns
  CHECK(lines.track[4] == ui::kNoTrack);
  CHECK(std::string(lines.text[5]) == "bass   bass bass");
  CHECK(std::string(lines.text[6]) == "chord  Cm Ab Eb Bb");
  w.frame();
  CHECK(text_at(fb, ui::kMargin, ui::kTextTop, "kick", ui::rgb565(ui::kTrackRgb[0])));  // the name in the track's colour
  CHECK(text_at(fb, ui::kMargin + ui::kTextNameColumns * ui::kGlyphAdvance, ui::kTextTop, "bd bd bd", kText));
  CHECK(text_at(fb, ui::kMargin, ui::kTextTop + 6 * ui::kLineHeight, "chord", ui::rgb565(ui::kTrackRgb[5])));
  CHECK(has_text(fb, "Cm Ab Eb Bb"));
  CHECK(has_text(fb, "(alt: b)"));

  SUBCASE("speed wraps the steps in brackets") {
    w.pad_down(Pad::hat);
    w.turn(Encoder::speed, 1);
    w.pad_up(Pad::hat);
    ui::text_lines(w.state(0), engine::kits::kLofi, lines);
    CHECK(std::string(lines.text[2]) == "hat    [hh hh*2]*2");
    w.pad_down(Pad::hat);
    w.turn(Encoder::speed, -2);
    w.pad_up(Pad::hat);
    ui::text_lines(w.state(0), engine::kits::kLofi, lines);
    CHECK(std::string(lines.text[2]) == "hat    [hh hh*2]/2");
  }
  SUBCASE("a long track wraps at the notation column") {
    w.tap(Pad::kick, 13);  // sixteen steps
    ui::text_lines(w.state(0), engine::kits::kLofi, lines);
    CHECK(std::string(lines.text[0]) == "kick   bd bd bd bd bd bd");
    CHECK(std::string(lines.text[1]) == "       bd bd bd bd bd bd");
    CHECK(std::string(lines.text[2]) == "       bd bd bd bd");
    CHECK(lines.track[1] == ui::kNoTrack);
    CHECK(lines.track[3] == engine::index_of(Pad::snare));
  }
  SUBCASE("more lines than the screen holds end in ...") {
    for (int pad = 0; pad < engine::kTrackCount; ++pad) w.tap(engine::pad_at(pad), 16);
    ui::text_lines(w.state(0), engine::kits::kLofi, lines);
    CHECK(ui::kTextMaxLines == 14);
    CHECK(lines.count == ui::kTextMaxLines);
    CHECK(std::string(lines.text[ui::kTextMaxLines - 1]) == "...");
    w.frame();
    CHECK(text_at(fb, ui::kMargin, ui::kTextTop + (ui::kTextMaxLines - 1) * ui::kLineHeight, "...", kText));
    CHECK(background_row(fb, ui::kBottomRow - 1));
    ui::text_lines(w.state(0), engine::kits::kLofi, lines, 10);  // fewer rows while the prompt shows
    CHECK(lines.count == 10);
    CHECK(std::string(lines.text[9]) == "...");
  }
}

TEST_CASE("T-57 Text view after chord x4 in C minor and in C pentatonic minor") {
  World w;
  w.tap(Pad::chord, 4);
  w.press(Button::show);
  w.frame();
  const uint16_t* fb = screen();
  CHECK(has_text(fb, "chord  Cm Ab Eb Bb", ui::rgb565(ui::kTrackRgb[5])) == false);  // the name is coloured, the names are not
  CHECK(has_text(fb, "Cm Ab Eb Bb"));

  open_settings(w, Button::undo, Button::show);
  w.turn(Encoder::speed, 1);   // the scale row
  w.turn(Encoder::filter, 3);  // minor -> major -> dorian -> pentatonic minor
  CHECK(w.state(0).key.mode == engine::Mode::pentatonic_minor);
  w.press(Button::show);  // back to the ring
  w.press(Button::show);  // the text view
  REQUIRE(w.model().view == app::View::text);
  w.frame();
  CHECK(has_text(fb, "C Bb Eb F"));
  CHECK_FALSE(has_text(fb, "Cm"));
}

TEST_CASE("T-58 Text view chord and pluck names in cm, em, csm, dsm, gsdor") {
  struct Case {
    engine::Key key;
    const char* chords;
    const char* plucks;
  };
  const Case cases[] = {
      {{0, engine::Mode::minor}, "chord  Cm Ab Eb Bb", "pluck  c5 eb5 g5"},
      {{4, engine::Mode::minor}, "chord  Em C G D", "pluck  e5 g5 b5"},
      {{1, engine::Mode::minor}, "chord  C#m A E B", "pluck  c#5 e5 g#5"},
      {{3, engine::Mode::minor}, "chord  Ebm Cb Gb Db", "pluck  eb5 gb5 bb5"},
      {{8, engine::Mode::dorian}, "chord  Abm Db Gb Db", "pluck  ab5 cb6 eb6"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.chords);
    engine::Section section = support::fresh_section();
    support::taps(section, Pad::chord, 4);
    support::taps(section, Pad::pluck, 3);
    section.state().key = c.key;
    ui::TextLines lines;
    ui::text_lines(section.state(), support::lofi(), lines);
    REQUIRE(lines.count == 2);
    CHECK(std::string(lines.text[0]) == c.chords);
    CHECK(std::string(lines.text[1]) == c.plucks);
    CHECK(support::code_of(section.state()).find("#") == std::string::npos);  // codes keep `s`
  }
}

TEST_CASE("T-87 The song view: number, letters in fours, the playing letter, tiles and the hint") {
  World w;
  w.tap(Pad::kick);
  w.press(Button::show);
  w.press(Button::show);
  REQUIRE(w.model().view == app::View::song);
  w.frame();
  const uint16_t* fb = screen();
  CHECK(text_at(fb, ui::kMargin, ui::kSongTitleTop, "song 1", kText));
  CHECK(text_at(fb, ui::kMargin, ui::kSongLettersTop, "empty", kLegend));
  for (int i = 0; i < ui::kSongHintLines; ++i) {
    CAPTURE(i);
    CHECK(text_at(fb, ui::kMargin, ui::kSongHintTop + i * ui::kLineHeight, ui::kSongHint[i], kLegend));
  }
  // Tile 1 is the song being played and edited: the accent with its digit cut out; tile 2 is empty: an outline.
  const int tile1 = ui::kMargin;
  const int tile2 = ui::kMargin + ui::kSongTileSize + ui::kSongTileGap;
  CHECK(fb[(ui::kSongTileTop + 2) * ui::kWidth + tile1 + 2] == kAccent);
  CHECK(text_at(fb, tile1 + (ui::kSongTileSize - ui::kGlyphWidth * ui::kFontScale) / 2,
                ui::kSongTileTop + (ui::kSongTileSize - ui::kLineHeight) / 2, "1", kBackground));
  CHECK(fb[ui::kSongTileTop * ui::kWidth + tile2 + 2] == kLegend);
  CHECK(fb[(ui::kSongTileTop + 2) * ui::kWidth + tile2 + 2] == kBackground);

  for (const char letter : std::string("AABAB")) w.press(section(letter));
  w.frame();
  CHECK(text_at(fb, ui::kMargin, ui::kSongLettersTop, "AABA B", kText));  // grouped in fours
  CHECK_FALSE(has_text(fb, ui::kSongHint[0], kLegend));                     // the hint has done its job (T-92)
  w.play();
  w.run_until(w.at(1, engine::Fraction{1, 2}));
  REQUIRE(w.model().song_position == 1);
  CHECK(text_at(fb, ui::kMargin + ui::kGlyphAdvance, ui::kSongLettersTop, "A", kAccent));  // the playing letter
  CHECK(text_at(fb, ui::kMargin, ui::kSongLettersTop, "A", kText));

  for (int i = 5; i < engine::kMaxArrangementLength; ++i) w.press(section('A'));
  w.press(section('A'));
  CHECK(w.status() == "song is full");
  w.frame();
  CHECK(text_at(fb, ui::kMargin, ui::kSongLettersTop + 3 * ui::kLineHeight, "AAAA AAAA AAAA AAAA", kText));
}

TEST_CASE("T-92 The song view's hint shows until a letter is added or a song picked, then not again this power cycle") {
  World w;
  w.press(Button::show);
  w.press(Button::show);
  w.frame();
  const uint16_t* fb = screen();
  CHECK(text_at(fb, ui::kMargin, ui::kSongHintTop, ui::kSongHint[0], kLegend));
  w.press(Button::show);  // away and back: still nothing added
  w.press(Button::show);
  w.press(Button::show);
  w.frame();
  CHECK(text_at(fb, ui::kMargin, ui::kSongHintTop, ui::kSongHint[0], kLegend));
  for (int i = 0; i < ui::kSongHintLines; ++i) CHECK(background_row(fb, ui::kSongHintTop + ui::kSongHintLines * ui::kLineHeight + i));

  w.press(section('B'));
  w.frame();
  CHECK_FALSE(has_text(fb, ui::kSongHint[0], kLegend));
  w.press(Button::show);
  w.press(Button::show);
  w.press(Button::show);
  w.frame();
  CHECK_FALSE(has_text(fb, ui::kSongHint[3], kLegend));  // gone for the rest of the power cycle

  World fresh;  // a new power cycle: the hint again, until a pad picks a song
  fresh.press(Button::show);
  fresh.press(Button::show);
  fresh.frame();
  CHECK(text_at(fb, ui::kMargin, ui::kSongHintTop + 3 * ui::kLineHeight, ui::kSongHint[3], kLegend));
  fresh.tap(Pad::snare);
  fresh.frame();
  CHECK_FALSE(has_text(fb, ui::kSongHint[3], kLegend));
}

TEST_CASE("T-88 The share view: the QR at 3 px per module, the code round it, back with show") {
  World w;
  w.tap(Pad::kick, 4);
  w.tap(Pad::snare, 2);
  w.tap(Pad::hat, 2);  // G-04
  w.hold(Button::show);
  REQUIRE(w.model().view == app::View::share);
  w.frame();
  const uint16_t* fb = screen();
  const Grid grid = grid_from_screen(fb);
  CHECK(grid.size == 37);  // version 5: 83 bytes of URL
  const int side = (grid.size + 2 * ui::kQrQuietModules) * ui::kQrModulePx;
  CHECK(ui::kShareTop == ui::kContentTop);                                         // below the top row
  CHECK(fb[ui::kShareTop * ui::kWidth + ui::kShareLeft] == kText);                  // the quiet zone is light
  CHECK(fb[(ui::kShareTop + side) * ui::kWidth + ui::kShareLeft] == kBackground);  // and ends where the square does
  const int beside = ui::kShareLeft + side + ui::kShareTextGap;                     // a glyph's width from the square
  CHECK(background_row(fb, ui::kShareTop + 3) == false);
  for (int x = ui::kShareLeft + side; x < beside; ++x) CHECK(fb[(ui::kShareTop + 3) * ui::kWidth + x] == kBackground);
  // Rows break only after a `:` or a `-`: 13 columns beside the QR.
  CHECK(text_at(fb, beside, ui::kShareTop, "RT2:lofi:100:", kText));
  CHECK(text_at(fb, beside, ui::kShareTop + ui::kLineHeight, "10:2:0:15:cm:", kText));
  CHECK(text_at(fb, beside, ui::kShareTop + 2 * ui::kLineHeight, "e10000-", kText));
  CHECK(text_at(fb, beside + 7 * ui::kGlyphAdvance, ui::kShareTop + 2 * ui::kLineHeight, "e1.0.0-", kText) == false);
  CHECK(text_at(fb, beside, ui::kShareTop + 3 * ui::kLineHeight, "e1.0.0-", kText));
  CHECK(text_at(fb, beside, ui::kShareTop + 4 * ui::kLineHeight, "e10000-e1-e1-", kText));
  CHECK(text_at(fb, beside, ui::kShareTop + 5 * ui::kLineHeight, "e1-e1-e1", kText));
  CHECK(text_at(fb, ui::kShareLeft, ui::kShareTop + side + ui::kShareCodeGap, ui::kScanHint, kLegend));  // what it is for
  CHECK(decode_qr(grid) == std::string(ui::kPlayerUrlPrefix) + kClassicBeat);  // T-45 on the screen

  w.tap(Pad::rim);  // the view stays live: the code and its QR follow the edit
  w.frame();
  CHECK(decode_qr(grid_from_screen(fb)) == std::string(ui::kPlayerUrlPrefix) + "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e10");

  w.press(Button::show);
  CHECK(w.model().view == app::View::ring);
}

TEST_CASE("T-45 The QR of every golden section code decodes back to the player URL") {
  const char* const goldens[] = {
      "RT2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1",
      kClassicBeat,
      "RT2:lofi:96:10:3:2:15:cm:e108-e1.0.0-e10000-e1.0,7a9a-e1-e10123-e1-e1",
      "RT2:lofi:128:6:5:3:0:fsdor:e10000-e1.0.0-e10000-e1.0.0-e1-e1-e1-e1~k9z2ab",
      "RT2:lofi:180:10:10:10:100:csdor:fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-"
      "fhoooooooooooooooo,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-"
      "fhoooooooooooooooo,7959~zzzzzz",
  };
  hal_fake::reset();
  ui::QrCode qr;
  for (const char* code : goldens) {
    CAPTURE(code);
    REQUIRE(ui::encode_share_qr(code, qr));
    CHECK(qr.size() <= 57);
    ui::draw_share_view(hal::framebuffer(), ui::ShareModel{code, &qr}, ui::kContentBottom);
    CHECK(decode_qr(grid_from_screen(screen())) == std::string(ui::kPlayerUrlPrefix) + code);
  }
  // G-14, the worst case, is version 10: 57 modules, 195 px with the quiet zone;
  // its 24-character tracks fit no row beside a QR that wide, so the text ends in `...`.
  CHECK(qr.size() == 57);
  CHECK(has_text(screen(), "..."));
  CHECK(background_row(screen(), ui::kBottomRow - 1));
}

TEST_CASE("T-89 Settings: hold undo + show, the rows, the knobs that pick and set, play that runs") {
  World w;
  w.tap(Pad::kick);
  open_settings(w, Button::show, Button::undo);
  CHECK(engine::track_of(w.state(0), Pad::kick).step_count == 1);  // undo's own meaning did not fire
  w.frame();
  const uint16_t* fb = screen();
  CHECK(text_at(fb, ui::kMargin, ui::kSettingsTitleTop, "settings", kText));
  struct Row {
    const char* label;
    const char* value;
  };
  const Row rows[ui::kSettingsRowCount] = {{"key", "c"},           {"scale", "minor"},      {"swing", "0.15"},
                                            {"kit", "lofi"},        {"brightness", "100%"},  {"sleep", "10 min"},
                                            {"midi clock in", "on"}, {"midi clock out", "on"}, {"sync in", "on"},
                                            {"sync out", "on"},     {"firmware", "0.1.0"},   {"run tutorial", "play"},
                                            {"factory reset", "hold play"}};
  for (int i = 0; i < ui::kSettingsRowCount; ++i) {
    CAPTURE(rows[i].label);
    const int y = ui::kSettingsRowsTop + i * ui::kLineHeight;
    const uint16_t label_colour = i == 0 ? kAccent : kLegend;
    const uint16_t value_colour = i == 0 ? kAccent : kText;
    CHECK(text_at(fb, ui::kMargin, y, rows[i].label, label_colour));
    CHECK(text_at(fb, right_aligned(rows[i].value), y, rows[i].value, value_colour));
  }
  CHECK(text_at(fb, ui::kMargin, ui::kBottomRow, "one kick", kText));  // the tap's status, until it ends
  w.run_for(2 * kSecond);
  CHECK(text_at(fb, ui::kMargin, ui::kBottomRow, ui::kSettingsHint, kLegend));  // then the hint

  w.turn(Encoder::filter, 1);  // the key row: C# minor beats Db minor (D-032)
  CHECK(w.state(0).key.root == 1);
  w.frame();
  CHECK(text_at(fb, right_aligned("c#"), ui::kSettingsRowsTop, "c#", kAccent));
  w.turn(Encoder::filter, 2);  // Eb minor: a tie goes to flats
  w.frame();
  CHECK(text_at(fb, right_aligned("eb"), ui::kSettingsRowsTop, "eb", kAccent));
  w.turn(Encoder::filter, -3);
  CHECK(w.state(0).key.root == 0);

  w.turn(Encoder::speed, 1);  // scale
  w.turn(Encoder::filter, 1);
  CHECK(w.state(0).key.mode == engine::Mode::major);
  w.frame();
  CHECK(text_at(fb, right_aligned("major"), ui::kSettingsRowsTop + ui::kLineHeight, "major", kAccent));
  w.turn(Encoder::filter, -1);

  w.turn(Encoder::speed, 1);  // swing, 0.05 a detent
  w.turn(Encoder::filter, 1);
  CHECK(w.state(0).swing == 20);
  w.frame();
  CHECK(has_text(fb, "0.20", kAccent));
  w.turn(Encoder::filter, -1);

  w.turn(Encoder::speed, 2);  // brightness, 10 % a detent, and the HAL sees it with the frame
  w.turn(Encoder::filter, -2);
  CHECK(w.model().settings.brightness == 80);
  w.frame();
  CHECK(has_text(fb, "80%", kAccent));
  CHECK(hal_fake::brightness() == 80);

  w.turn(Encoder::speed, 1);  // sleep: 10 -> 5 -> off
  w.turn(Encoder::filter, -1);
  CHECK(w.model().settings.sleep_minutes == 5);
  w.turn(Encoder::filter, -1);
  CHECK(w.model().settings.sleep_minutes == 0);
  w.frame();
  CHECK(has_text(fb, "off", kAccent));
  w.turn(Encoder::filter, 2);
  CHECK(w.model().settings.sleep_minutes == 10);

  w.turn(Encoder::speed, 1);  // midi clock in: down is off, up is on
  w.turn(Encoder::filter, -1);
  CHECK_FALSE(w.model().settings.midi_clock_in);
  w.frame();
  CHECK(has_text(fb, "off", kAccent));
  w.turn(Encoder::filter, 1);
  CHECK(w.model().settings.midi_clock_in);

  w.turn(Encoder::speed, 7);  // the cursor wraps
  CHECK(w.model().settings.cursor == 0);
  CHECK(w.state(0).bpm == 100);  // the speed knob picked rows, it did not set the tempo
  w.turn(Encoder::fx, 1);        // the other knobs are what they always are
  CHECK(w.state(0).fx == 3);
  CHECK(w.knob() == "fx 0.3");
  w.tap(Pad::snare);  // pads are inert in a menu
  CHECK(engine::is_empty(engine::track_of(w.state(0), Pad::snare)));

  w.press(Button::show);
  CHECK(w.model().view == app::View::ring);

  SUBCASE("play on the run tutorial row starts the tutorial on the ring") {
    open_settings(w, Button::undo, Button::show);
    w.turn(Encoder::speed, -2);
    CHECK(w.model().settings.cursor == static_cast<int>(ui::SettingsRow::run_tutorial));
    w.press(Button::play);
    CHECK(w.model().tutorial.active);
    CHECK(w.model().view == app::View::ring);
    w.frame();
    CHECK(has_text(fb, "tap the kick"));
  }
  SUBCASE("factory reset takes a hold of play and brings everything back to power-on") {
    w.press(section('B'));
    w.press(Button::show);
    w.press(Button::show);
    w.press(section('A'));
    w.press(Button::show);
    open_settings(w, Button::undo, Button::show);
    w.turn(Encoder::speed, -1);
    CHECK(w.model().settings.cursor == static_cast<int>(ui::SettingsRow::factory_reset));
    w.press(Button::play);
    CHECK(w.status() == "hold play to reset");
    CHECK(engine::track_of(w.state(1), Pad::kick).step_count == 1);
    w.hold(Button::play);
    CHECK(w.status() == "reset");
    CHECK(engine::is_empty(engine::track_of(w.state(0), Pad::kick)));
    CHECK(engine::is_empty(engine::track_of(w.state(1), Pad::kick)));
    CHECK(w.model().arrangement.length == 0);
    CHECK(w.model().current == 0);
    CHECK(w.model().view == app::View::ring);
    CHECK(w.model().tutorial.active);
  }
}

TEST_CASE("T-22 First-boot tutorial: the prompts in order, done within 45 s when followed, skippable with play") {
  World w(true);
  w.frame();
  REQUIRE(w.model().tutorial.active);
  const uint16_t* fb = screen();
  const int64_t start = w.frames;
  auto read_and_act = [&] { w.run_for(5 * kSecond); };  // a tester reads the prompt and does it

  CHECK(has_text(fb, "tap the kick"));
  read_and_act();
  w.tap(Pad::kick);
  w.frame();
  CHECK(has_text(fb, "tap it again"));
  CHECK(has_text(fb, "see it stretch?"));
  CHECK(has_text(fb, "one kick"));  // the message row still reports the edit
  CHECK(background_row(fb, ui::content_bottom(2) + 1));  // the ring ends above the two prompt rows
  read_and_act();
  w.tap(Pad::kick);
  w.frame();
  CHECK(has_text(fb, "tap the snare"));
  // Two kicks: a dot at six o'clock. The ring has shrunk to end above the prompt's box.
  CHECK(background_row(fb, ui::content_bottom(1) + 1));
  read_and_act();
  w.tap(Pad::snare);
  w.frame();
  CHECK(has_text(fb, "now turn chance"));
  read_and_act();
  w.turn(Encoder::chance, 1);
  w.frame();
  CHECK(has_text(fb, "hold show to share it"));
  read_and_act();
  w.hold(Button::show);
  REQUIRE(w.model().view == app::View::share);
  w.frame();
  CHECK(has_text(fb, "press show twice"));
  CHECK(has_text(fb, "and tap A A B A"));
  CHECK(has_text(fb, "hold A and press play"));
  read_and_act();
  w.press(Button::show);  // ring
  w.press(Button::show);  // text
  w.press(Button::show);  // song
  REQUIRE(w.model().view == app::View::song);
  w.frame();
  CHECK(has_text(fb, ui::kSongHint[3], kLegend));  // the hint sits above the prompt's box, nothing covered
  CHECK(background_row(fb, ui::content_bottom(3) + 1));
  for (const char letter : std::string("AABA")) w.press(section(letter));
  w.button_down(section('A'));
  w.run_for(kSecond / 2);
  w.press(Button::play);
  w.button_up(section('A'));
  CHECK_FALSE(w.model().tutorial.active);
  CHECK(w.status() == "that's a song");
  CHECK(w.model().song_mode);  // from a stop the song starts at once (D-086)
  CHECK(w.frames - start <= 45 * kSecond);
  w.frame();
  CHECK_FALSE(has_text(fb, "hold A and press play"));
  uint8_t flag = 0;
  uint32_t size = 0;
  CHECK(hal::read_file(app::kTutorialDoneFile, &flag, 1, &size));
  CHECK(flag == '1');
}

TEST_CASE("T-22 Play skips the tutorial, the next boot does not run it, and steps wait for their own gesture") {
  World w(true);
  w.tap(Pad::snare);  // not the kick: step 1 still waits
  CHECK(w.model().tutorial.step == 0);
  w.run_for(2 * kSecond);
  w.press(Button::play);
  CHECK_FALSE(w.model().tutorial.active);
  CHECK(w.status() == "tutorial skipped");
  CHECK_FALSE(w.model().transport);  // the press was the skip, not play
  w.frame();
  CHECK_FALSE(has_text(screen(), "tap the kick"));

  const sound::SampleBank silent{};
  app::init(silent);  // the next boot reads the flag off the card
  CHECK_FALSE(app::model().tutorial.active);
}

TEST_CASE("T-90 Pad LEDs: dim with no steps, the track colour with steps, full for 100 ms after a hit") {
  World w;
  w.frame();
  const ui::Rgb kick = ui::kTrackRgb[0];
  const ui::Rgb snare = ui::kTrackRgb[1];
  CHECK(same(pad_led(0), ui::blend(kOff, kick, 8)));
  CHECK(same(pad_led(1), ui::blend(kOff, snare, 8)));
  w.tap(Pad::kick);
  w.frame();
  CHECK(same(pad_led(0), kick));  // the press itself lit it: the audition is a hit
  w.run_for(kSecond / 5);
  CHECK(same(pad_led(0), ui::blend(kOff, kick, 35)));
  CHECK(same(pad_led(1), ui::blend(kOff, snare, 8)));
  w.play();
  w.run_until(w.cycle_start(0) + kSecond / 20);  // 50 ms after the kick fired
  CHECK(same(pad_led(0), kick));
  CHECK(same(pad_led(1), ui::blend(kOff, snare, 8)));
  w.run_until(w.cycle_start(0) + kSecond / 2);  // 500 ms after: back to resting
  CHECK(same(pad_led(0), ui::blend(kOff, kick, 35)));
}

TEST_CASE("T-91 Button backlights: armed, the roll, show, play and the sections") {
  World w;
  w.frame();
  CHECK(same(button_led(Button::split), kButtonRest));
  CHECK(same(button_led(Button::swap), kButtonRest));
  CHECK(same(button_led(Button::show), kButtonRest));
  CHECK(same(button_led(Button::play), kButtonRest));
  CHECK(same(button_led(Button::skip), kOff));
  CHECK(same(button_led(Button::undo), kOff));
  CHECK(same(button_led(Button::dice), kOff));
  CHECK(same(button_led(Button::section_a), ui::kAccent));
  CHECK(same(button_led(Button::section_b), kButtonRest));

  w.press(Button::split);  // armed
  w.frame();
  CHECK(same(button_led(Button::split), ui::kAccent));
  w.press(Button::split);  // disarmed
  w.frame();
  CHECK(same(button_led(Button::split), kButtonRest));
  w.press(Button::skip);
  w.frame();
  CHECK(same(button_led(Button::skip), ui::kAccent));
  w.press(Button::skip);
  w.frame();
  CHECK(same(button_led(Button::skip), kOff));

  w.button_down(Button::split);  // the roll
  w.run_for(kSecond / 2);
  CHECK(w.model().roll);
  CHECK(same(button_led(Button::split), ui::kAccent));
  w.button_up(Button::split);
  w.frame();
  CHECK(same(button_led(Button::split), kButtonRest));

  w.press(Button::show);  // the text view
  w.frame();
  CHECK(same(button_led(Button::show), ui::kAccent));
  w.press(Button::show);
  w.press(Button::show);  // the ring again
  w.frame();
  CHECK(same(button_led(Button::show), kButtonRest));

  w.tap(Pad::kick, 4);
  w.play();
  w.frame();
  CHECK(same(button_led(Button::play), ui::kAccent));
  w.run_until(w.at(0, engine::Fraction{3, 10}));
  w.press(section('B'));  // edited now, played from the next cycle
  w.frame();
  CHECK(same(button_led(Button::section_b), ui::kAccent));
  CHECK(same(button_led(Button::section_a), kHalfAccent));
  w.run_until(w.cycle_start(1) + 2 * kBlock);
  w.frame();
  CHECK(same(button_led(Button::section_b), ui::kAccent));
  CHECK(same(button_led(Button::section_a), kButtonRest));
  w.press(Button::play);  // stop
  w.frame();
  CHECK(same(button_led(Button::play), kButtonRest));
}
