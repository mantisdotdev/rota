// Key, chords, bass, pluck and their names: T-31, T-41, T-43, T-44, T-47, T-48, T-57, T-58.
#include "engine_support.h"

using namespace support;

namespace {

constexpr uint8_t kC2 = 36;
constexpr uint8_t kEb2 = 39;

bool chord_in_scale(const EventList& list, Key key) {
  for (const Event& e : events_on(list, Pad::chord)) {
    if (!in_scale(key, e.note) || !in_scale(key, e.chord_upper[0]) || !in_scale(key, e.chord_upper[1])) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST_CASE("T-31 Chord x4 (Cm Ab Eb Bb), then bass x2") {
  Section section = fresh_section();
  taps(section, Pad::chord, 4);
  taps(section, Pad::bass, 2);
  const EventList list = events_of(section.state());
  CHECK(chord_root_classes(list) == "0 8 3 10");
  CHECK(hit_times(list, Pad::bass) == "0 1/2");
  CHECK(notes_on(list, Pad::bass) == std::to_string(kC2) + " " + std::to_string(kEb2));

  for (int i = 0; i < 4; ++i) remove_last_step(section, Pad::chord);
  REQUIRE(is_empty(track_of(section.state(), Pad::chord)));
  CHECK(notes_on(events_of(section.state()), Pad::bass) == std::to_string(kC2) + " " + std::to_string(kC2));
}

TEST_CASE("T-41 Chord x5") {
  Section section = fresh_section();
  taps(section, Pad::chord, 5);
  CHECK(steps_text(section.state(), Pad::chord) == "01230");
  CHECK(chord_root_classes(events_of(section.state())) == "0 8 3 10 0");
  CHECK(track_code(section.state(), Pad::chord) == "e101230");
}

TEST_CASE("T-43 Chord Cm Ab Eb Bb; change key from C minor to A minor in settings") {
  Section section = fresh_section();
  taps(section, Pad::chord, 4);
  taps(section, Pad::bass, 4);
  CHECK(chord_root_classes(events_of(section.state())) == "0 8 3 10");
  const std::string code_c_minor = code_of(section.state());

  section.state().key = Key{9, Mode::minor};
  const EventList list = events_of(section.state());
  CHECK(chord_root_classes(list) == "9 5 0 7");  // Am F C G
  std::vector<std::string> bass_classes;
  for (const Event& e : events_on(list, Pad::bass)) bass_classes.push_back(std::to_string(pitch_class(e.note)));
  CHECK(join(bass_classes) == "9 5 0 7");
  CHECK(track_code(section.state(), Pad::chord) == "e10123");

  const std::string code_a_minor = code_of(section.state());
  CHECK(split_on(code_c_minor, ':')[7] == "cm");
  CHECK(split_on(code_a_minor, ':')[7] == "am");
  std::vector<std::string> before = split_on(code_c_minor, ':');
  std::vector<std::string> after = split_on(code_a_minor, ':');
  before[7] = after[7] = "";
  CHECK(before == after);  // only the key field changed
}

TEST_CASE("T-44 Pluck x9 in C minor") {
  Section section = fresh_section();
  taps(section, Pad::pluck, 9);
  CHECK(steps_text(section.state(), Pad::pluck) == "012345670");
  const EventList list = events_of(section.state());
  // Degrees 0 2 4 7 9 7 4 2 in octave 5: C Eb G C' Eb' C' G Eb, then position 0 again.
  CHECK(notes_on(list, Pad::pluck) == "72 75 79 84 87 84 79 75 72");
  for (const Event& e : events_on(list, Pad::pluck)) CHECK(in_scale(section.state().key, e.note));
}

TEST_CASE("T-47 Load a code with chord note 5 on lofi, whose progression has four entries") {
  const char* code = "PB2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e15-e1-e1";
  const Decoded loaded = decode(code, lofi());
  REQUIRE(loaded.ok);
  CHECK(track_of(loaded.state, Pad::chord).steps[0].note == 5);
  CHECK(chord_root_classes(events_of(loaded.state)) == "8");  // position 5 mod 4 = 1: VI, Ab
  CHECK(code_of(loaded.state) == code);
}

TEST_CASE("T-48 Chord x4 with root C in each of the five modes") {
  struct Expected {
    Mode mode;
    const char* roots;
  };
  const Expected table[] = {
      {Mode::minor, "0 8 3 10"},             // C Ab Eb Bb
      {Mode::major, "0 7 9 5"},              // C G A F
      {Mode::dorian, "0 5 10 5"},            // C F Bb F
      {Mode::pentatonic_minor, "0 10 3 5"},  // C Bb Eb F
      {Mode::pentatonic_major, "0 9 2 7"},   // C A D G
  };
  for (const Expected& expected : table) {
    Section section = fresh_section();
    section.state().key = Key{0, expected.mode};
    taps(section, Pad::chord, 4);
    taps(section, Pad::bass, 4);
    const EventList list = events_of(section.state());
    CAPTURE(static_cast<int>(expected.mode));
    CHECK(chord_root_classes(list) == expected.roots);
    CHECK(chord_in_scale(list, section.state().key));
    std::vector<std::string> bass_classes;
    for (const Event& e : events_on(list, Pad::bass)) bass_classes.push_back(std::to_string(pitch_class(e.note)));
    CHECK(join(bass_classes) == expected.roots);  // the bass plays the root degree
  }
}

TEST_CASE("T-57 Text view after chord x4 in C minor and in C pentatonic minor") {
  // The `chord  ` line itself is ui/; the names are the engine's (D-031).
  Section section = fresh_section();
  taps(section, Pad::chord, 4);
  CHECK(chord_names(section.state()) == "Cm Ab Eb Bb");
  section.state().key = Key{0, Mode::pentatonic_minor};
  CHECK(chord_names(section.state()) == "C Bb Eb F");
}

TEST_CASE("T-58 Text view after chord x4 in keys cm, em, csm, dsm, gsdor, and pluck x3 in cm") {
  struct Expected {
    Key key;
    const char* code_key;
    const char* names;
  };
  const Expected table[] = {
      {Key{0, Mode::minor}, "cm", "Cm Ab Eb Bb"},
      {Key{4, Mode::minor}, "em", "Em C G D"},
      {Key{1, Mode::minor}, "csm", "C#m A E B"},      // C# minor, 4 sharps, beats Db minor, 8 flats
      {Key{3, Mode::minor}, "dsm", "Ebm Cb Gb Db"},   // 6 sharps against 6 flats: flats
      {Key{8, Mode::dorian}, "gsdor", "Abm Db Gb Db"},  // Ab dorian, the same tie
  };
  for (const Expected& expected : table) {
    CAPTURE(expected.code_key);
    Section section = fresh_section();
    section.state().key = expected.key;
    taps(section, Pad::chord, 4);
    CHECK(chord_names(section.state()) == expected.names);
    CHECK(split_on(code_of(section.state()), ':')[7] == expected.code_key);  // codes keep sharps
  }
  Section section = fresh_section();
  taps(section, Pad::pluck, 3);
  CHECK(pluck_names(section.state()) == "c5 eb5 g5");
}
