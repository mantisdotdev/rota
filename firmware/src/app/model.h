#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/limits.h"
#include "engine/section.h"
#include "engine/share.h"
#include "engine/state.h"
#include "io/store.h"

// What the app is: four sections, the song, the transport, the settings, the
// tutorial and what the screen says. The input grammar (controller) and the
// scheduler both change it, under hal::lock(); the views read a copy (D-084, D-086).
namespace app {

// show cycles ring, text and song (§8.2, D-030); its hold opens the share view
// (§9.3) and hold undo + show the settings (§9.4); a show press leaves either (D-093, D-096).
enum class View : uint8_t { ring, text, song, share, settings };

constexpr int kStatusCapacity = 40;
constexpr uint32_t kStatusUs = 1800000;      // §9.1: status text for 1.8 s
constexpr uint32_t kKnobStatusUs = 1000000;  // §8.3: a knob's value for 1 s

// Transient text: 1.8 s for what happened in the bottom-left corner, 1 s for a
// knob's value in the box over the middle (§9.1, D-098). duration_us 0 means
// nothing is showing.
struct Status {
  char text[kStatusCapacity];
  uint64_t shown_at_us;
  uint32_t duration_us;
};

// Puts a line in the bottom-left corner for `duration_us` (§9.1, D-098). The caller
// holds the lock; the input grammar formats through it and so does the card, which
// is the only place that knows a slot did not load.
void say(Status& status, uint64_t at_us, uint32_t duration_us, const char* text);

struct Arrangement {
  uint8_t length;
  char letters[engine::kMaxArrangementLength];  // A–D, one cycle each (§6.8)
};

// The first-run tutorial (§8.5, D-097): six steps, each waiting for one gesture.
struct Tutorial {
  bool active;
  int step;
  bool save_pending;  // the done flag must be written; app::tick does it outside the lock
};

constexpr int kNoSection = -1;
constexpr const char* kFirmwareVersion = "0.1.0";      // shown in settings; release tooling will stamp it (D-096)
// One byte on the card: kTutorialRan once the tutorial ran or was skipped.
constexpr const char* kTutorialDoneFile = "tutorial-done";
constexpr uint8_t kTutorialRan = '1';
constexpr uint8_t kTutorialPending = '0';

struct Model {
  explicit Model(const engine::Kit& kit);

  engine::Section sections[engine::kSectionCount];
  int current;          // what the player edits and sees
  int playing;          // what the scheduler plays; equals current except between a
                        // section press and the next cycle boundary, when a knob turns both (D-086)
  int pending_section;  // kNoSection, or where `playing` moves at the next cycle boundary
  Arrangement arrangement;
  char song_lineage[engine::kLineageLength + 1];  // the id of the song this one was loaded from; empty otherwise
  bool song_mode;           // stepping through the arrangement (§6.8)
  int song_position;        // index of the letter playing
  bool song_start_pending;  // song play from the top at the next cycle boundary
  bool transport;           // play (§8.2)
  bool roll;                // split held: held pads retrigger every 1/16 cycle
  bool song_hint_dismissed;  // a letter added or a song picked this power cycle: the song view's hint goes (§9.6)
  View view;
  Status status;
  Status knob;
  io::Settings settings;  // the §9.4 rows the card keeps, and the song they name
  int settings_cursor;    // the selected row, a ui::SettingsRow; where the view was, not a setting
  bool song_filled[engine::kSongSlotCount];  // which slots hold a song, for the song view's tiles (§9.6)
  // The card work app::tick does outside the lock, because a card takes milliseconds
  // and the scheduler's timer must not wait for one (D-104).
  int picked_song;     // io::kNoSlot, or the slot the song view's pads picked
  bool erase_pending;  // a factory reset asked for every slot to be emptied (§9.4)
  Tutorial tutorial;
  engine::Tenths master_volume;  // §9.5: −6 dB by default, one detent 0.1 (D-087)
};

constexpr engine::Tenths kDefaultMasterVolume = 5;

bool is_empty(const engine::State& state);
bool is_empty(const engine::Song& song);

// The four sections and the arrangement as the card keeps them (§6.8). A song
// carries no undo history: one read back from the card starts with none.
void song_of(const Model& model, engine::Song& song);
void set_song(Model& model, const engine::Song& song);
inline char letter_of(int section) { return static_cast<char>('A' + section); }

}  // namespace app
