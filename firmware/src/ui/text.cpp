#include "ui/text.h"

#include <cstdio>
#include <cstring>

#include "engine/scale.h"
#include "ui/color.h"
#include "ui/draw.h"
#include "ui/font.h"

namespace ui {

namespace {

constexpr int kTokenCapacity = 16;                        // "<[eb5*4]/2>" is 11
constexpr int kMaxTokens = engine::kMaxStepsPerTrack + 1;  // the steps and the alt note
const char* const kEllipsis = "...";
const char* const kNothing = "nothing yet";

// Strudel's names for the drum sounds and `bass` for the bass, whose pitch follows
// the chord (§9.2); chord and pluck steps are named by pitch.
const char* const kSoundToken[engine::kTrackCount] = {"bd", "sd", "hh", "cp", "bass", nullptr, nullptr, "rim"};
const char* const kAltNote[] = {"", "(alt: a)", "(alt: b)", "(alt: fourth)"};

constexpr uint16_t kBackground = rgb565(kScreenBackground);
constexpr uint16_t kText = rgb565(kScreenText);

struct Token {
  char text[kTokenCapacity];
};

struct Notation {
  Token tokens[kMaxTokens];
  int count;
};

void put(Token& token, const char* text) {
  const size_t length = std::strlen(token.text);
  std::snprintf(token.text + length, sizeof token.text - length, "%s", text);
}

void prefix(Token& token, char mark) {
  const size_t length = std::strlen(token.text);
  if (length + 1 >= sizeof token.text) return;
  std::memmove(token.text + 1, token.text, length + 1);
  token.text[0] = mark;
}

void step_token(const engine::State& state, const engine::Kit& kit, engine::Pad pad, engine::Step step, Token& out) {
  out.text[0] = '\0';
  if (engine::is_rest(step)) {
    put(out, "~");
    return;
  }
  switch (pad) {
    case engine::Pad::chord:
      put(out, engine::chord_name(state.key, engine::chord_degree(kit, state.key.mode, step.note)).text);
      break;
    case engine::Pad::pluck: {
      const uint8_t midi = engine::pitch_of_degree(state.key, engine::pad_of(kit, pad).octave,
                                                   engine::pluck_degree(kit, step.note));
      put(out, engine::pitch_name(state.key, midi).text);
      break;
    }
    default:
      put(out, kSoundToken[engine::index_of(pad)]);
      break;
  }
  if (step.hits > 1) {
    char split[4];
    std::snprintf(split, sizeof split, "*%d", step.hits);
    put(out, split);
  }
}

// The steps, then speed as `[...]*2` or `[...]/2` round them, then alternation
// as `<...>` with its note after (D-094).
void notation_of(const engine::State& state, const engine::Kit& kit, engine::Pad pad, Notation& out) {
  const engine::Track& track = engine::track_of(state, pad);
  out.count = 0;
  for (int i = 0; i < track.step_count; ++i) step_token(state, kit, pad, track.steps[i], out.tokens[out.count++]);
  Token& first = out.tokens[0];
  Token& last = out.tokens[out.count - 1];
  if (track.speed != engine::Speed::one) {
    prefix(first, '[');
    put(last, track.speed == engine::Speed::two ? "]*2" : "]/2");
  }
  if (track.alt != engine::Alt::every) {
    prefix(first, '<');
    put(last, ">");
    Token& note = out.tokens[out.count++];
    note.text[0] = '\0';
    put(note, kAltNote[static_cast<int>(track.alt)]);
  }
}

// Starts a line: the pad's name padded to the notation column, or spaces for a
// continuation. False when the screen is full.
bool begin_line(TextLines& out, int track, const char* name, int max_lines) {
  if (out.count >= max_lines) return false;
  char* line = out.text[out.count];
  std::memset(line, ' ', kTextNameColumns);
  line[kTextNameColumns] = '\0';
  if (name != nullptr) {  // kits are an open format read from the card (§12 rule 6): the name is data
    size_t length = std::strlen(name);
    if (length > kTextNameColumns - 1) length = kTextNameColumns - 1;  // one space always separates
    std::memcpy(line, name, length);
  }
  out.track[out.count] = static_cast<int8_t>(track);
  out.count += 1;
  return true;
}

}  // namespace

void text_lines(const engine::State& state, const engine::Kit& kit, TextLines& out, int max_lines) {
  if (max_lines > kTextMaxLines) max_lines = kTextMaxLines;
  if (max_lines < 1) max_lines = 1;
  out.count = 0;
  bool more = false;
  for (int pad = 0; pad < engine::kTrackCount && !more; ++pad) {
    if (engine::is_empty(state.tracks[pad])) continue;
    Notation notation;
    notation_of(state, kit, engine::pad_at(pad), notation);
    if (!begin_line(out, pad, engine::pad_of(kit, engine::pad_at(pad)).name, max_lines)) {
      more = true;
      break;
    }
    int column = kTextNameColumns;
    for (int t = 0; t < notation.count; ++t) {
      const char* token = notation.tokens[t].text;
      const int length = static_cast<int>(std::strlen(token));
      const bool first_on_line = column == kTextNameColumns;
      if (column + (first_on_line ? 0 : 1) + length > kTextColumns) {
        if (!begin_line(out, kNoTrack, nullptr, max_lines)) {
          more = true;
          break;
        }
        column = kTextNameColumns;
      }
      char* line = out.text[out.count - 1];
      if (column != kTextNameColumns) line[column++] = ' ';
      std::memcpy(line + column, token, static_cast<size_t>(length));
      column += length;
      line[column] = '\0';
    }
  }
  if (more) {  // the screen holds no more: the last row says so
    std::snprintf(out.text[max_lines - 1], kTextColumns + 1, "%s", kEllipsis);
    out.track[max_lines - 1] = kNoTrack;
  }
  if (out.count == 0) {
    std::snprintf(out.text[0], kTextColumns + 1, "%s", kNothing);
    out.track[0] = kNoTrack;
    out.count = 1;
  }
}

void draw_text_view(uint16_t* framebuffer, const engine::State& state, const engine::Kit& kit, int bottom) {
  Canvas canvas{framebuffer};
  fill(canvas, kBackground);
  TextLines lines;
  text_lines(state, kit, lines, (bottom - kTextTop) / kLineHeight);
  for (int i = 0; i < lines.count; ++i) {
    const int y = kTextTop + i * kLineHeight;
    if (lines.track[i] == kNoTrack) {
      draw_text(canvas, kMargin, y, lines.text[i], kText);
      continue;
    }
    char name[kTextNameColumns + 1];
    std::memcpy(name, lines.text[i], kTextNameColumns);
    name[kTextNameColumns] = '\0';
    draw_text(canvas, kMargin, y, name, rgb565(kTrackRgb[lines.track[i]]));
    draw_text(canvas, kMargin + kTextNameColumns * kGlyphAdvance, y, lines.text[i] + kTextNameColumns, kText);
  }
}

}  // namespace ui
