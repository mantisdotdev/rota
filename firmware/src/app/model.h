#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/limits.h"
#include "engine/section.h"
#include "engine/state.h"

// What the app is: four sections, the song, the transport, the settings, the
// tutorial and what the screen says. The input grammar (controller) and the
// scheduler both change it, under hal::lock(); the views read a copy (D-084, D-086).
namespace app {

// show cycles ring, text and song (§8.2, D-030); its hold opens the share view
// (§9.3) and hold undo + show the settings (§9.4); a show press leaves either (D-093, D-096).
enum class View : uint8_t { ring, text, song, share, settings };

constexpr int kStatusCapacity = 40;

// Transient text: 1.8 s for what happened in the bottom-left corner, 1 s for a
// knob's value in the box over the middle (§9.1, D-098). duration_us 0 means
// nothing is showing.
struct Status {
  char text[kStatusCapacity];
  uint64_t shown_at_us;
  uint32_t duration_us;
};

struct Arrangement {
  uint8_t length;
  char letters[engine::kMaxArrangementLength];  // A–D, one cycle each (§6.8)
};

// The rows of §9.4 that are the app's own (D-096); key and swing live in each
// section's state. Kept here until io/ keeps them on the card.
struct Settings {
  int cursor;         // the selected row, a ui::SettingsRow
  int brightness;     // percent
  int sleep_minutes;  // 0 = never
  bool midi_clock_in;
  bool midi_clock_out;
  bool sync_in;
  bool sync_out;
};

// The first-run tutorial (§8.5, D-097): six steps, each waiting for one gesture.
struct Tutorial {
  bool active;
  int step;
  bool save_pending;  // the done flag must be written; app::tick does it outside the lock
};

constexpr int kNoSection = -1;
constexpr int kDefaultBrightness = 100;
constexpr int kDefaultSleepMinutes = 10;  // §7.7
constexpr const char* kFirmwareVersion = "0.1.0";      // shown in settings; release tooling will stamp it (D-096)
constexpr const char* kTutorialDoneFile = "tutorial-done";  // one byte on the card: '1' once the tutorial ran or was skipped

struct Model {
  explicit Model(const engine::Kit& kit);

  engine::Section sections[engine::kSectionCount];
  int current;          // what the player edits and sees
  int playing;          // what the scheduler plays; equals current except between a
                        // section press and the next cycle boundary, when a knob turns both (D-086)
  int pending_section;  // kNoSection, or where `playing` moves at the next cycle boundary
  Arrangement arrangement;
  bool song_mode;           // stepping through the arrangement (§6.8)
  int song_position;        // index of the letter playing
  bool song_start_pending;  // song play from the top at the next cycle boundary
  bool transport;           // play (§8.2)
  bool roll;                // split held: held pads retrigger every 1/16 cycle
  bool song_hint_dismissed;  // a letter added or a song picked this power cycle: the song view's hint goes (§9.6)
  View view;
  Status status;
  Status knob;
  Settings settings;
  Tutorial tutorial;
  engine::Tenths master_volume;  // §9.5: −6 dB by default, one detent 0.1 (D-087)
};

constexpr engine::Tenths kDefaultMasterVolume = 5;

bool is_empty(const engine::State& state);
inline char letter_of(int section) { return static_cast<char>('A' + section); }

}  // namespace app
