// Shared helpers for the engine tests. Every test case is named after the scenario
// it covers in spec/scenarios.md (PRD §12 rule 2); the helpers keep the cases
// reading like the scenario table: taps, then steps and event times as text.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "engine/edits.h"
#include "engine/events.h"
#include "engine/kits/lofi.h"
#include "engine/scale.h"
#include "engine/section.h"
#include "engine/share.h"

namespace doctest {
template <>
struct StringMaker<engine::Fraction> {
  static String convert(const engine::Fraction& f) {
    return (std::to_string(f.num) + "/" + std::to_string(f.den)).c_str();
  }
};
}  // namespace doctest

namespace support {

using namespace engine;

constexpr uint32_t kSeed = 42;

inline const Kit& lofi() { return kits::kLofi; }

inline Section fresh_section() { return Section(make_state(lofi())); }

inline void taps(Section& section, Pad pad, int count) {
  for (int i = 0; i < count; ++i) tap(section, pad, lofi());
}

// "1/3", "0", "3/4": a fraction the way the scenario table writes it.
inline std::string fraction_text(Fraction f) {
  const Fraction r = reduced(f.num, f.den);
  if (r.den == 1) return std::to_string(r.num);
  return std::to_string(r.num) + "/" + std::to_string(r.den);
}

inline EventList events_of(const State& state, uint32_t cycle = 0, uint32_t seed = kSeed) {
  EventList list;
  events(state, lofi(), cycle, seed, list);
  return list;
}

inline std::vector<Event> events_on(const EventList& list, Pad pad) {
  std::vector<Event> found;
  for (int i = 0; i < list.count; ++i) {
    if (list.items[i].track == pad) found.push_back(list.items[i]);
  }
  return found;
}

inline std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const std::string& part : parts) {
    if (!out.empty()) out += " ";
    out += part;
  }
  return out;
}

// Times of the authored (non-ghost) hits on one pad, in list order: "0 1/3 2/3".
inline std::string hit_times(const EventList& list, Pad pad) {
  std::vector<std::string> parts;
  for (const Event& e : events_on(list, pad)) {
    if (!e.is_ghost) parts.push_back(fraction_text(e.time));
  }
  return join(parts);
}

inline std::string ghost_times(const EventList& list, Pad pad) {
  std::vector<std::string> parts;
  for (const Event& e : events_on(list, pad)) {
    if (e.is_ghost) parts.push_back(fraction_text(e.time));
  }
  return join(parts);
}

// Every field of every event, for whole-list comparisons (T-10, T-35).
inline std::string events_text(const EventList& list) {
  std::string out;
  char line[96];
  for (int i = 0; i < list.count; ++i) {
    const Event& e = list.items[i];
    std::snprintf(line, sizeof line, "%d@%s n%d+%d+%d v%.6f s%d g%d\n", index_of(e.track),
                  fraction_text(e.time).c_str(), e.note, e.chord_upper[0], e.chord_upper[1],
                  static_cast<double>(e.velocity), e.sub_index, e.is_ghost ? 1 : 0);
    out += line;
  }
  return out;
}

// Steps in share-code spelling: `.` rest, else base36 of (hits − 1) × 8 + note.
inline std::string steps_text(const Track& track) {
  static const char* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string out;
  for (int i = 0; i < track.step_count; ++i) {
    const Step s = track.steps[i];
    out += is_rest(s) ? '.' : kDigits[(s.hits - 1) * 8 + s.note];
  }
  return out;
}

inline std::string steps_text(const State& state, Pad pad) { return steps_text(track_of(state, pad)); }

inline std::string code_of(const State& state) { return encode(state, lofi()).text; }

inline std::vector<std::string> split_on(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == separator) {
      parts.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  parts.push_back(current);
  return parts;
}

// One pad's track string out of the encoded section code, e.g. "e1.0.0,6a1a".
inline std::string track_code(const State& state, Pad pad) {
  const std::vector<std::string> fields = split_on(code_of(state), ':');
  if (fields.size() < 9) return "";
  const std::string tracks = split_on(fields[8], '~')[0];
  const std::vector<std::string> per_track = split_on(tracks, '-');
  if (per_track.size() != kTrackCount) return "";
  return per_track[index_of(pad)];
}

inline int pitch_class(uint8_t midi) { return midi % kSemitonesPerOctave; }

// The chord track's names as the text view lists them: "Cm Ab Eb Bb", `~` for a rest.
inline std::string chord_names(const State& state) {
  std::vector<std::string> parts;
  const Track& chord = track_of(state, Pad::chord);
  for (int i = 0; i < chord.step_count; ++i) {
    const Step step = chord.steps[i];
    parts.push_back(is_rest(step) ? "~" : chord_name(state.key, chord_degree(lofi(), state.key.mode, step.note)).text);
  }
  return join(parts);
}

// The pluck track's note names in cycle 0: "c5 eb5 g5".
inline std::string pluck_names(const State& state) {
  std::vector<std::string> parts;
  for (const Event& e : events_on(events_of(state), Pad::pluck)) parts.push_back(pitch_name(state.key, e.note).text);
  return join(parts);
}

// Pitch classes of the root of every chord event, in order: "0 8 3 10" is Cm Ab Eb Bb.
inline std::string chord_root_classes(const EventList& list) {
  std::vector<std::string> parts;
  for (const Event& e : events_on(list, Pad::chord)) parts.push_back(std::to_string(pitch_class(e.note)));
  return join(parts);
}

inline std::string notes_on(const EventList& list, Pad pad) {
  std::vector<std::string> parts;
  for (const Event& e : events_on(list, pad)) parts.push_back(std::to_string(e.note));
  return join(parts);
}

}  // namespace support
