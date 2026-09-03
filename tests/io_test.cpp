// What the card keeps (spec/scenarios.md T-56, T-89, T-97, T-98, T-99): the song files and
// the settings file io/ writes, and the app keeping them as the player plays.
#include <cstring>
#include <string>
#include <vector>

#include "app_support.h"
#include "engine_support.h"
#include "io/store.h"
#include "ui/settings.h"

using namespace app_support;

namespace {

const engine::Kit& kit() { return engine::kits::kLofi; }

Button section_button(char letter) {
  return static_cast<Button>(static_cast<int>(Button::section_a) + (letter - 'A'));
}

std::string file(const char* path) {
  uint8_t buffer[4096];
  uint32_t size = 0;
  if (!hal::read_file(path, buffer, sizeof buffer, &size)) return "";
  return std::string(reinterpret_cast<const char*>(buffer), size);
}

void put(const char* path, const std::string& text) {
  REQUIRE(hal::write_file(path, reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size())));
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> lines;
  std::string line;
  for (const char c : text) {
    if (c != '\n') {
      line += c;
      continue;
    }
    lines.push_back(line);
    line.clear();
  }
  return lines;
}

bool logged(const std::string& fragment) {
  for (const std::string& line : hal_fake::log()) {
    if (line.find(fragment) != std::string::npos) return true;
  }
  return false;
}

// Four sections that differ, an arrangement, and a lineage on B as a section loaded
// from a code carries one (§10.2).
engine::Song made_song() {
  engine::Song song{};
  for (int i = 0; i < engine::kSectionCount; ++i) {
    engine::Section section = support::fresh_section();
    support::taps(section, engine::pad_at(i), i + 1);
    song.sections[i] = section.state();
  }
  song.sections[1].bpm = 105;
  std::strcpy(song.sections[1].lineage, "k9z2ab");
  song.arrangement_length = 4;
  std::memcpy(song.arrangement, "AABD", 4);
  return song;
}

}  // namespace

TEST_CASE("T-97 A song file keeps the four sections, their lineage and the arrangement; junk does not load") {
  hal_fake::reset();
  const engine::Song song = made_song();
  REQUIRE(io::save_song(3, kit(), song));
  engine::Song back{};
  REQUIRE(io::load_song(3, kit(), back));
  CHECK(back == song);
  CHECK(std::string(back.sections[1].lineage) == "k9z2ab");  // no RT2S code carries a section's own lineage
  CHECK(back.sections[1].bpm == 105);

  const std::vector<std::string> written = lines_of(file("songs/3.txt"));
  REQUIRE(written.size() == 5);
  for (int i = 0; i < engine::kSectionCount; ++i) CHECK(written[i].rfind("RT2:", 0) == 0);
  CHECK(written[4] == "AABD");

  engine::Song without = song;  // an empty arrangement, which no RT2S code can express
  without.arrangement_length = 0;
  REQUIRE(io::save_song(3, kit(), without));
  REQUIRE(io::load_song(3, kit(), back));
  CHECK(back == without);
  CHECK(lines_of(file("songs/3.txt")).size() == 5);

  std::string junk;  // the third section is not a code
  for (int i = 0; i < 5; ++i) junk += (i == 2 ? std::string("not a code") : written[i]) + "\n";
  put("songs/4.txt", junk);
  CHECK_FALSE(io::load_song(4, kit(), back));
  CHECK(file("songs/4.txt") == junk);  // nothing on the card was changed
  CHECK(logged("songs/4.txt"));

  put("songs/5.txt", written[0] + "\n");  // one line, not five
  CHECK_FALSE(io::load_song(5, kit(), back));

  hal_fake::refuse_writes(true);
  CHECK_FALSE(io::save_song(6, kit(), song));
  CHECK(file("songs/6.txt").empty());
}

TEST_CASE("T-98 The settings file keeps the rows and the open song, and ignores what it cannot read") {
  hal_fake::reset();
  const io::Settings written{5, 40, 0, false, true, false, true};
  REQUIRE(io::save_settings(written));
  io::Settings back{};
  REQUIRE(io::load_settings(back));
  CHECK(back == written);

  put("settings.txt", "song=9\nbrightness=40\nnonsense\ncolour=blue\nsleep=30\nmidi-in=7\n");
  REQUIRE(io::load_settings(back));
  CHECK(back.song == io::kDefaultSettings.song);                     // 9 is not a slot
  CHECK(back.brightness == 40);
  CHECK(back.sleep_minutes == 30);
  CHECK(back.midi_clock_in == io::kDefaultSettings.midi_clock_in);   // 7 is not a flag
  CHECK(back.sync_out == io::kDefaultSettings.sync_out);             // the row is not in the file

  hal_fake::reset();
  CHECK_FALSE(io::load_settings(back));  // no file at all
  CHECK(back == io::kDefaultSettings);
}

TEST_CASE("T-56 A pad in the song view picks a song, an empty slot copies this one, and both survive a power cycle") {
  World w;
  w.tap(Pad::kick, 4);
  w.press(Button::show);
  w.press(Button::show);
  REQUIRE(w.model().view == app::View::song);
  w.press(section_button('A'));
  w.press(section_button('B'));  // arrangement AB
  w.frame();

  w.tap(Pad::snare);  // pad 2: an empty song becomes a copy of this one (§9.6)
  CHECK(w.status() == "song 2");
  w.frame();
  CHECK(w.model().settings.song == 2);
  CHECK(engine::track_of(w.state(0), Pad::kick).step_count == 4);  // the copy carries the sections
  CHECK(w.model().arrangement.length == 2);                        // and the arrangement

  w.press(Button::show);  // the ring, to edit section B
  w.press(section_button('B'));
  REQUIRE(w.model().current == 1);
  w.tap(Pad::snare, 2);
  w.press(Button::show);
  w.press(Button::show);
  w.tap(Pad::kick);  // pad 1: back to song 1
  w.frame();
  CHECK(w.model().settings.song == 1);
  CHECK(engine::is_empty(engine::track_of(w.state(1), Pad::snare)));  // the edit landed in song 2 only
  CHECK(engine::track_of(w.state(0), Pad::kick).step_count == 4);
  CHECK(w.model().arrangement.length == 2);

  w.reboot();  // nothing was saved by hand
  CHECK(w.model().settings.song == 1);
  CHECK(engine::track_of(w.state(0), Pad::kick).step_count == 4);
  CHECK(w.model().arrangement.length == 2);
  CHECK(w.model().song_filled[1]);  // the song view's tiles know song 2 is there
  CHECK_FALSE(w.model().song_filled[2]);

  w.press(Button::show);
  w.press(Button::show);
  w.tap(Pad::snare);  // song 2 again
  w.frame();
  CHECK(w.model().settings.song == 2);
  CHECK(engine::track_of(w.state(1), Pad::snare).step_count == 4);  // `~ sd ~ sd`
}

TEST_CASE("T-99 The card takes the song a second after the last change, and a refused write waits its second") {
  World w;
  const int start = hal_fake::writes();
  w.tap(Pad::kick);
  w.frame();
  CHECK(hal_fake::writes() == start);  // the second has not passed
  w.run_for(kSecond + kSecond / 10);
  CHECK(hal_fake::writes() == start + 1);  // one tap, one write
  w.run_for(2 * kSecond);
  CHECK(hal_fake::writes() == start + 1);  // and nothing more while nothing changes

  hal_fake::refuse_writes(true);
  w.tap(Pad::snare);
  w.run_for(kSecond + kSecond / 10);
  const int refused = hal_fake::writes();
  CHECK(refused == start + 2);  // one attempt, not one a frame
  w.run_for(kSecond / 2);
  CHECK(hal_fake::writes() == refused);  // still waiting out the second
  w.run_for(kSecond);
  CHECK(hal_fake::writes() == refused + 1);

  hal_fake::refuse_writes(false);
  w.run_for(kSecond + kSecond / 10);
  w.reboot();
  CHECK(engine::track_of(w.state(0), Pad::snare).step_count == 2);  // `~ sd`
}

TEST_CASE("T-89 A factory reset empties the card too, so the reset survives the power cycle") {
  World w;
  w.tap(Pad::kick, 4);
  w.turn(Encoder::chance, 3);
  w.run_for(kSecond + kSecond / 10);  // the card has the loop and the chance
  REQUIRE(engine::track_of(w.state(0), Pad::kick).step_count == 4);

  w.button_down(Button::undo);  // undo and show held together open settings (§9.4)
  w.run_for(kSecond / 10);
  w.button_down(Button::show);
  w.run_for(kSecond / 2);
  w.button_up(Button::show);
  w.button_up(Button::undo);
  REQUIRE(w.model().view == app::View::settings);
  w.turn(Encoder::speed, -1);  // the last row, wrapping backwards
  REQUIRE(w.model().settings_cursor == static_cast<int>(ui::SettingsRow::factory_reset));
  w.hold(Button::play);
  REQUIRE(w.status() == "reset");
  w.frame();

  w.reboot();
  CHECK(engine::is_empty(engine::track_of(w.state(0), Pad::kick)));
  CHECK(w.state(0).chance == engine::make_state(kit()).chance);
  CHECK(w.model().settings.song == 1);
  for (const bool filled : w.model().song_filled) CHECK_FALSE(filled);
}
