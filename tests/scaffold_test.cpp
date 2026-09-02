// Placeholder until engine work starts. Every future test names the scenario ID it
// covers from spec/scenarios.md (PRD §12 rule 2); this one covers the build itself.
#include "doctest/doctest.h"
#include "engine/limits.h"

TEST_CASE("scaffold: engine limits match PRD section 12 rule 4") {
  CHECK(engine::kTrackCount == 8);
  CHECK(engine::kMaxStepsPerTrack == 16);
  CHECK(engine::kMaxHitsPerStep == 4);
  CHECK(engine::kSectionCount == 4);
  CHECK(engine::kSongSlotCount == 8);
  CHECK(engine::kUndoDepth == 60);
  CHECK(engine::kMaxArrangementLength == 64);
  CHECK(engine::kMaxNoteSequenceLength == 8);
  CHECK(engine::kMaxDiceLoops == 4);
}
