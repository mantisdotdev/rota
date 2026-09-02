#include "engine/state.h"

#include <cstring>

#include "engine/kit.h"

namespace engine {

bool operator==(const Track& a, const Track& b) {
  if (a.alt != b.alt || a.speed != b.speed || a.step_count != b.step_count) return false;
  for (int i = 0; i < a.step_count; ++i) {
    if (a.steps[i] != b.steps[i]) return false;
  }
  return a.level == b.level && a.tone == b.tone && a.send == b.send && a.chance == b.chance &&
         a.mute == b.mute;
}

bool operator==(const State& a, const State& b) {
  if (std::strcmp(a.kit, b.kit) != 0 || std::strcmp(a.lineage, b.lineage) != 0) return false;
  if (a.bpm != b.bpm || a.filter != b.filter || a.fx != b.fx || a.chance != b.chance ||
      a.swing != b.swing || a.key != b.key) {
    return false;
  }
  for (int i = 0; i < kTrackCount; ++i) {
    if (a.tracks[i] != b.tracks[i]) return false;
  }
  return true;
}

State make_state(const Kit& kit) {
  State state{};
  std::strncpy(state.kit, kit.id, kKitIdLength);
  state.bpm = kDefaultBpm;
  state.filter = kit.filter;
  state.fx = kit.fx;
  state.chance = 0;
  state.swing = kit.swing_hundredths;
  state.key = kDefaultKey;
  for (int i = 0; i < kTrackCount; ++i) {
    Track& track = state.tracks[i];
    track.alt = Alt::every;
    track.speed = Speed::one;
    track.step_count = 0;
    track.level = kDefaultLevel;
    track.tone = kDefaultTone;
    track.send = kit.pads[i].send;
    track.chance = kDefaultTrackChance;
    track.mute = false;
  }
  return state;
}

bool is_empty(const Track& track) { return track.step_count == 0; }
bool is_full(const Track& track) { return track.step_count >= kMaxStepsPerTrack; }

}  // namespace engine
