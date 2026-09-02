#pragma once

#include <cstdint>

#include "engine/kit.h"
#include "engine/limits.h"
#include "engine/section.h"
#include "engine/state.h"

// What the app is: four sections, the song, the transport and what the screen
// says. The input grammar (controller) and the scheduler both change it, under
// hal::lock(); the ring view reads a copy (D-084, D-086).
namespace app {

enum class View : uint8_t { ring, text, song };  // show cycles them (§8.2, D-030)

constexpr int kStatusCapacity = 40;

// Transient text in the bottom-left corner (§9.1): 1.8 s for what happened, 1 s for a
// knob value. duration_us 0 means nothing is showing.
struct Status {
  char text[kStatusCapacity];
  uint64_t shown_at_us;
  uint32_t duration_us;
};

struct Arrangement {
  uint8_t length;
  char letters[engine::kMaxArrangementLength];  // A–D, one cycle each (§6.8)
};

constexpr int kNoSection = -1;

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
  View view;
  Status status;
  engine::Tenths master_volume;  // §9.5: −6 dB by default, one detent 0.1 (D-087)
};

constexpr engine::Tenths kDefaultMasterVolume = 5;

bool is_empty(const engine::State& state);
inline char letter_of(int section) { return static_cast<char>('A' + section); }

}  // namespace app
