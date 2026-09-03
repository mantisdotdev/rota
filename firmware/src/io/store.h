#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/limits.h"
#include "engine/share.h"

// What the card holds (PRD §7.5, §6.8, D-104): eight songs and the settings the
// device comes back to. Both are text, because USB mass storage shows the card as
// it is (§7.6) and a section code is already the loop's canonical spelling.
//
// Everything here reads or writes a whole file through hal::. It runs on the main
// loop only and never under hal::lock(): a card takes milliseconds and the
// scheduler's timer must not wait for it.
namespace io {

// The §9.4 rows that are the app's own (D-096) plus the song that was open. Key and
// swing belong to a section and travel with the song; the master volume starts at
// −6 dB every boot (D-087); the settings cursor is where the view was, not a setting.
struct Settings {
  char kit[engine::kKitIdLength + 1];  // the folder under kits/ the device plays
  int song;           // 1–8, the slot the device comes back to
  int brightness;     // percent
  int sleep_minutes;  // 0 = never
  bool midi_clock_in;
  bool midi_clock_out;
  bool sync_in;
  bool sync_out;
};

constexpr Settings kDefaultSettings{"lofi", 1, 100, 10, true, true, true, true};  // §7.7: sleep after 10 minutes

// What the §9.4 rows accept, here rather than in the input grammar because the card
// is the other way into them and both have to agree (D-096, D-104).
constexpr int kBrightnessMin = 10;  // the screen never goes fully dark
constexpr int kBrightnessMax = 100;
constexpr int kSleepChoices[] = {0, 5, 10, 20, 30, 60};  // minutes; 0 is never
constexpr int kSleepChoiceCount = 6;

bool operator==(const Settings& a, const Settings& b);
inline bool operator!=(const Settings& a, const Settings& b) { return !(a == b); }

// A song slot, 1–8 (§6.8, D-030).
constexpr int kFirstSlot = 1;
constexpr int kLastSlot = engine::kSongSlotCount;
constexpr int kNoSlot = 0;

// Whether a slot's file was there and could be read. `invalid` is kept apart from
// `missing` because an empty slot is copied over by the next pick and a file that
// did not parse must not be (T-97).
enum class LoadResult : uint8_t { missing, invalid, loaded };

// Fills `song` from the slot's file, and logs what was wrong with a file that did
// not parse, since a card the player can edit is a boundary (CLAUDE.md).
LoadResult load_song(int slot, const engine::Kit& kit, engine::Song& song);
bool save_song(int slot, const engine::Kit& kit, const engine::Song& song);

// Missing or unreadable settings load as kDefaultSettings; an unknown key or an
// out-of-range value is ignored, so a card written by a later firmware still loads.
bool load_settings(Settings& settings);
bool save_settings(const Settings& settings);

}  // namespace io
