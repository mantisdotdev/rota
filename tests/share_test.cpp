// Share codes: T-15, T-16, T-45, T-51, T-52, T-62 and every golden code in spec/share-format.md §7.
#include "engine_support.h"

using namespace support;

namespace {

struct Golden {
  const char* id;
  const char* code;
};

const Golden kGoldens[] = {
    {"G-01", "RT2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1"},
    {"G-02", "RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1"},
    {"G-03", "RT2:lofi:100:10:2:0:15:cm:e1-e1.0-e1-e1-e1-e1-e1-e1"},
    {"G-04", "RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1"},
    {"G-05", "RT2:lofi:100:10:2:0:15:cm:e1-e1-e108-e1-e1-e1-e1-e1"},
    {"G-06", "RT2:lofi:100:10:2:0:15:cm:e10-b1.0-e1-a1.0-e1-e1-e1-f10"},
    {"G-07", "RT2:lofi:100:10:2:0:15:cm:e1-e1-ed00-e1-e1-e1-e1-e1"},
    {"G-08", "RT2:lofi:100:10:2:0:15:cm:e1-e1-eh0000-e1-e1-e1-e1-e1"},
    {"G-09", "RT2:lofi:100:10:2:0:15:cm:e100.-e1-e1-e1-e1-e1-e1-e1"},
    {"G-10", "RT2:lofi:100:10:2:0:15:cm:e1-e1.0.0,6a1a-e1-e1-e1-e1-e1-e1"},
    {"G-11", "RT2:lofi:96:10:3:2:15:cm:e108-e1.0.0-e10000-e1.0,7a9a-e1-e10123-e1-e1"},
    {"G-12", "RT2:lofi:90:10:2:0:15:am:e10-e1-e1-e1-e100-e10123-e1-e1"},
    {"G-13", "RT2:lofi:128:6:5:3:0:fsdor:e10000-e1.0.0-e10000-e1.0.0-e1-e1-e1-e1~k9z2ab"},
    {"G-14",
     "RT2:lofi:180:10:10:10:100:csdor:fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-"
     "fhoooooooooooooooo,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-"
     "fhoooooooooooooooo,7959~zzzzzz"},
};

const char* kSongGolden =
    "RT2S:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e1-e1;"
    "lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e10123-e1;"
    "lofi:100:10:2:0:15:cm:e10-e1.0-e100000-e1.0-e10-e101-e10123-e1;"
    "lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1/AABABBCD~k9z2ab";

const std::string kSectionAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:.,-~";
const std::string kSongAlphabet = kSectionAlphabet + ";/";

bool uses_only(const std::string& text, const std::string& alphabet) {
  return text.find_first_not_of(alphabet) == std::string::npos;
}

State decoded(const char* code) {
  const Decoded result = decode(code, lofi());
  REQUIRE(result.ok);
  return result.state;
}

}  // namespace

TEST_CASE("T-15 Encode -> decode -> encode any state") {
  SUBCASE("every golden code round-trips byte for byte") {
    for (const Golden& golden : kGoldens) {
      CAPTURE(golden.id);
      const Decoded result = decode(golden.code, lofi());
      REQUIRE(result.ok);
      CHECK_FALSE(result.kit_substituted);
      CHECK(std::string(encode(result.state, lofi()).text) == golden.code);
    }
  }
  SUBCASE("a tapped state survives encode -> decode -> encode") {
    Section section = fresh_section();
    taps(section, Pad::kick, 2);
    split(section, Pad::kick);
    taps(section, Pad::snare, 2);
    taps(section, Pad::hat, 2);
    tap(section, Pad::clap, lofi());
    swap(section, Pad::clap);
    adjust_speed(section, Pad::hat, +1);
    taps(section, Pad::chord, 4);
    taps(section, Pad::bass, 2);
    taps(section, Pad::pluck, 3);
    tap(section, Pad::rim, lofi());
    skip(section, Pad::rim);
    adjust_level(section.state(), Pad::clap, -1);
    adjust_send(section.state(), Pad::clap, +8);
    section.state().bpm = 96;
    section.state().fx = 3;
    section.state().chance = 2;
    const std::string first = code_of(section.state());
    const std::string second = code_of(decoded(first.c_str()));
    CHECK(second == first);
    CHECK(decoded(first.c_str()) == section.state());
  }
  SUBCASE("the empty loop with all defaults is G-01") {
    CHECK(code_of(make_state(lofi())) == kGoldens[0].code);
  }
}

TEST_CASE("T-15 Golden codes decode to the states their descriptions give") {
  SUBCASE("G-02 one kick at 0") {
    const State state = decoded(kGoldens[1].code);
    CHECK(steps_text(state, Pad::kick) == "0");
    CHECK(hit_times(events_of(state), Pad::kick) == "0");
  }
  SUBCASE("G-04 the classic beat") {
    const State state = decoded(kGoldens[3].code);
    const EventList list = events_of(state);
    CHECK(hit_times(list, Pad::kick) == "0 1/4 1/2 3/4");
    CHECK(hit_times(list, Pad::snare) == "1/4 3/4");
    CHECK(hit_times(list, Pad::hat) == "0 1/4 1/2 3/4");
  }
  SUBCASE("G-05 hat split") {
    const State state = decoded(kGoldens[4].code);
    CHECK(track_of(state, Pad::hat).steps[1].hits == 2);
    CHECK(hit_times(events_of(state), Pad::hat) == "0 1/2 3/4");
  }
  SUBCASE("G-06 alternations") {
    const State state = decoded(kGoldens[5].code);
    CHECK(track_of(state, Pad::kick).alt == Alt::every);
    CHECK(track_of(state, Pad::snare).alt == Alt::b);
    CHECK(track_of(state, Pad::clap).alt == Alt::a);
    CHECK(track_of(state, Pad::rim).alt == Alt::fourth);
    CHECK(events_on(events_of(state, 0), Pad::snare).empty());
    CHECK(hit_times(events_of(state, 1), Pad::snare) == "1/2");
    CHECK(hit_times(events_of(state, 0), Pad::clap) == "1/2");
    CHECK(events_on(events_of(state, 1), Pad::clap).empty());
    CHECK(events_on(events_of(state, 2), Pad::rim).empty());
    CHECK(hit_times(events_of(state, 3), Pad::rim) == "0");
  }
  SUBCASE("G-07 and G-08 speeds") {
    CHECK(track_of(decoded(kGoldens[6].code), Pad::hat).speed == Speed::two);
    CHECK(hit_times(events_of(decoded(kGoldens[6].code)), Pad::hat) == "0 1/4 1/2 3/4");
    CHECK(track_of(decoded(kGoldens[7].code), Pad::hat).speed == Speed::half);
  }
  SUBCASE("G-09 skip") {
    CHECK(steps_text(decoded(kGoldens[8].code), Pad::kick) == "00.");
  }
  SUBCASE("G-10 snare level") {
    const State state = decoded(kGoldens[9].code);
    const Track& snare = track_of(state, Pad::snare);
    CHECK(snare.level == 6);
    CHECK(snare.tone == 10);
    CHECK(snare.send == 1);
    CHECK(snare.chance == 10);
  }
  SUBCASE("G-11 the PRD example") {
    const State state = decoded(kGoldens[10].code);
    CHECK(state.bpm == 96);
    CHECK(state.filter == 10);
    CHECK(state.fx == 3);
    CHECK(state.chance == 2);
    CHECK(state.swing == 15);
    CHECK(state.key == Key{0, Mode::minor});
    CHECK(hit_times(events_of(state), Pad::kick) == "0 1/2 3/4");
    const Track& clap = track_of(state, Pad::clap);
    CHECK(clap.level == 7);
    CHECK(clap.tone == 10);
    CHECK(clap.send == 9);
    CHECK(clap.chance == 10);
    CHECK(steps_text(state, Pad::chord) == "0123");
    CHECK(chord_root_classes(events_of(state)) == "0 8 3 10");
  }
  SUBCASE("G-12 chord, bass and key") {
    const State state = decoded(kGoldens[11].code);
    CHECK(state.bpm == 90);
    CHECK(state.key == Key{9, Mode::minor});
    CHECK(chord_root_classes(events_of(state)) == "9 5 0 7");
    CHECK(notes_on(events_of(state), Pad::bass) == "45 48");  // A2 then C3: the roots of Am and C
  }
  SUBCASE("G-13 non-default globals, sharp key, lineage") {
    const State state = decoded(kGoldens[12].code);
    CHECK(state.bpm == 128);
    CHECK(state.filter == 6);
    CHECK(state.fx == 5);
    CHECK(state.chance == 3);
    CHECK(state.swing == 0);
    CHECK(state.key == Key{6, Mode::dorian});
    CHECK(std::string(state.lineage) == "k9z2ab");
  }
  SUBCASE("G-14 the worst case is exactly 230 characters") {
    CHECK(std::string(kGoldens[13].code).size() == 230);
    const State state = decoded(kGoldens[13].code);
    CHECK(std::string(encode(state, lofi()).text).size() == 230);
    CHECK(state.bpm == 180);
    CHECK(state.swing == 100);
    CHECK(state.key == Key{1, Mode::dorian});
    CHECK(std::string(state.lineage) == "zzzzzz");
    for (int i = 0; i < kTrackCount; ++i) {
      const Track& track = state.tracks[i];
      CAPTURE(i);
      CHECK(track.alt == Alt::fourth);
      CHECK(track.speed == Speed::half);
      CHECK(track.step_count == 16);
      const uint8_t note = is_drum(pad_at(i)) && pad_at(i) != Pad::bass ? 0 : 7;
      for (int s = 0; s < 16; ++s) CHECK(track.steps[s] == Step{4, note});
      CHECK(track.level == 7);
      CHECK(track.tone == 9);
      CHECK(track.send == 5);
      CHECK(track.chance == 9);
    }
  }
}

TEST_CASE("T-16 Load a code with an unknown future field") {
  SUBCASE("an extra field after the tracks") {
    const Decoded result = decode("RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1:future9", lofi());
    REQUIRE(result.ok);
    CHECK(steps_text(result.state, Pad::kick) == "0");
    CHECK(code_of(result.state) == kGoldens[1].code);
  }
  SUBCASE("an extra field before the lineage") {
    const Decoded result = decode("RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1:future9~k9z2ab", lofi());
    REQUIRE(result.ok);
    CHECK(std::string(result.state.lineage) == "k9z2ab");
  }
  SUBCASE("characters after the four modifier digits") {
    const Decoded result = decode("RT2:lofi:100:10:2:0:15:cm:e10,6a1axyz-e1-e1-e1-e1-e1-e1-e1", lofi());
    REQUIRE(result.ok);
    CHECK(track_of(result.state, Pad::kick).level == 6);
    CHECK(code_of(result.state) == "RT2:lofi:100:10:2:0:15:cm:e10,6a1a-e1-e1-e1-e1-e1-e1-e1");
  }
  SUBCASE("a code that is not RT2 does not load") {
    CHECK_FALSE(decode("RT3:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1", lofi()).ok);
    CHECK_FALSE(decode("", lofi()).ok);
    CHECK_FALSE(decode("RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1", lofi()).ok);  // seven tracks
    CHECK_FALSE(decode("RT2:lofi:300:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1", lofi()).ok);  // bpm out of range
    CHECK_FALSE(decode("RT2:lofi:100:10:2:0:15:cm:e1w-e1-e1-e1-e1-e1-e1-e1", lofi()).ok);  // step char past v
    CHECK_FALSE(decode("RT2:lofi:100:10:2:0:15:cm:e100000000000000000-e1-e1-e1-e1-e1-e1-e1", lofi()).ok);  // 17 steps
    CHECK_FALSE(decode("RT2:lofi:100:10:2:0:15:xm:e10-e1-e1-e1-e1-e1-e1-e1", lofi()).ok);  // no such root
  }
}

TEST_CASE("T-60 Load a code whose numbers carry leading zeros") {
  const Decoded result = decode("RT2:lofi:096:010:02:00:015:cm:e10-e1-e1-e1-e1-e1-e1-e1", lofi());
  REQUIRE(result.ok);
  CHECK(result.state.bpm == 96);
  CHECK(result.state.filter == 10);
  CHECK(result.state.fx == 2);
  CHECK(result.state.chance == 0);
  CHECK(result.state.swing == 15);
  CHECK(hit_times(events_of(result.state), Pad::kick) == "0");
  CHECK(code_of(result.state) == "RT2:lofi:96:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1");
}

TEST_CASE("T-45 Encode any state, and every golden code") {
  for (const Golden& golden : kGoldens) {
    CAPTURE(golden.id);
    CHECK(uses_only(golden.code, kSectionAlphabet));
    CHECK(uses_only(code_of(decoded(golden.code)), kSectionAlphabet));
  }
  CHECK(uses_only(kSongGolden, kSongAlphabet));
  Section section = fresh_section();
  taps(section, Pad::chord, 4);
  section.state().key = Key{10, Mode::pentatonic_major};  // as pM
  CHECK(uses_only(code_of(section.state()), kSectionAlphabet));
}

TEST_CASE("T-51 Encode a song: four full sections, arrangement AABABBCD, a lineage") {
  SUBCASE("G-15 round-trips byte for byte") {
    const DecodedSong result = decode_song(kSongGolden, lofi());
    REQUIRE(result.ok);
    CHECK_FALSE(result.kit_substituted);
    CHECK(std::string(encode_song(result.song, lofi()).text) == kSongGolden);
    const Song& song = result.song;
    CHECK(std::string(song.arrangement, song.arrangement_length) == "AABABBCD");
    CHECK(std::string(song.lineage) == "k9z2ab");
    CHECK(steps_text(song.sections[0], Pad::chord) == "0123");
    CHECK(is_empty(track_of(song.sections[0], Pad::pluck)));
    CHECK(steps_text(song.sections[1], Pad::pluck) == "0123");
    CHECK(steps_text(song.sections[2], Pad::hat) == "00000");
    for (int i = 0; i < kTrackCount; ++i) CHECK(is_empty(song.sections[3].tracks[i]));
    for (const State& section : song.sections) CHECK(std::string(section.lineage).empty());
  }
  SUBCASE("65 letters do not load") {
    const std::string body = std::string(kSongGolden).substr(0, std::string(kSongGolden).find('/'));
    CHECK(decode_song((body + "/" + std::string(64, 'A')).c_str(), lofi()).ok);
    CHECK_FALSE(decode_song((body + "/" + std::string(65, 'A')).c_str(), lofi()).ok);
    CHECK_FALSE(decode_song((body + "/").c_str(), lofi()).ok);
  }
  SUBCASE("a letter outside A-D does not load") {
    const std::string body = std::string(kSongGolden).substr(0, std::string(kSongGolden).find('/'));
    CHECK_FALSE(decode_song((body + "/AABE").c_str(), lofi()).ok);
    CHECK_FALSE(decode_song((body + "/aabb").c_str(), lofi()).ok);
  }
  SUBCASE("sections naming different kits do not load") {
    std::string mixed = kSongGolden;
    mixed.replace(mixed.find(";lofi:"), 6, ";jazz:");
    CHECK_FALSE(decode_song(mixed.c_str(), lofi()).ok);
  }
  SUBCASE("a section code is not a song and a song is not a section") {
    CHECK_FALSE(decode_song(kGoldens[3].code, lofi()).ok);
    CHECK_FALSE(decode(kSongGolden, lofi()).ok);
  }
}

TEST_CASE("T-52 Load RT2:jazz:... on a device or player that has only lofi") {
  // The status line "no kit jazz, using lofi" is app/; the engine reports the substitution.
  const Decoded result = decode("RT2:jazz:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e10-e1-e1", lofi());
  REQUIRE(result.ok);
  CHECK(result.kit_substituted);
  CHECK(std::string(result.requested_kit) == "jazz");
  CHECK(std::string(result.state.kit) == "lofi");
  CHECK(track_of(result.state, Pad::chord).send == 4);  // lofi's default for the omitted group
  CHECK(track_of(result.state, Pad::kick).send == 1);
  CHECK(hit_times(events_of(result.state), Pad::kick) == "0");
  CHECK(code_of(result.state) == "RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e10-e1-e1");

  std::string song = kSongGolden;
  for (size_t at = song.find("lofi:"); at != std::string::npos; at = song.find("lofi:", at + 1)) {
    song.replace(at, 5, "jazz:");
  }
  const DecodedSong loaded = decode_song(song.c_str(), lofi());
  REQUIRE(loaded.ok);
  CHECK(loaded.kit_substituted);
  CHECK(std::string(loaded.requested_kit) == "jazz");
  CHECK(std::string(encode_song(loaded.song, lofi()).text) == kSongGolden);
}

TEST_CASE("T-62 A code past the decoder's limit does not load; one at the limit still does") {
  // A field a later version added, which this one skips (T-16). The limit leaves room
  // for as much of that as the canonical code itself takes (D-106).
  const std::string body = kGoldens[0].code;  // G-01
  const std::string at_limit = body + ":" + std::string(kMaxSectionCodeInput - body.size() - 1, 'x');
  REQUIRE(at_limit.size() == static_cast<size_t>(kMaxSectionCodeInput));
  CHECK(decode(at_limit.c_str(), lofi()).ok);
  CHECK(code_of(decoded(at_limit.c_str())) == body);  // and it re-encodes without the field
  CHECK_FALSE(decode((at_limit + "x").c_str(), lofi()).ok);

  const std::string song = kSongGolden;
  const size_t first_section_end = song.find(';');
  REQUIRE(first_section_end != std::string::npos);
  const std::string padding(kMaxSongCodeInput - song.size() - 1, 'x');
  const std::string song_at_limit = song.substr(0, first_section_end) + ":" + padding + song.substr(first_section_end);
  REQUIRE(song_at_limit.size() == static_cast<size_t>(kMaxSongCodeInput));
  CHECK(decode_song(song_at_limit.c_str(), lofi()).ok);
  CHECK_FALSE(decode_song((song_at_limit + "x").c_str(), lofi()).ok);
}
