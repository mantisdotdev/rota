#include "app/params.h"

namespace app {

static_assert(sizeof(sound::Params::tracks) / sizeof(sound::TrackMix) == engine::kTrackCount,
              "sound::Params has a mix for every engine track");

namespace {
constexpr float kTenths = 10.0f;
}

sound::Params params_of(const engine::State& state, const engine::Kit& kit, engine::Tenths master_volume) {
  sound::Params params = sound::default_params(kit);
  params.bpm = static_cast<float>(state.bpm);
  params.filter = static_cast<float>(state.filter) / kTenths;
  params.fx = static_cast<float>(state.fx) / kTenths;
  params.master = static_cast<float>(master_volume) / kTenths;
  for (int i = 0; i < engine::kTrackCount; ++i) {
    const engine::Track& track = state.tracks[i];
    params.tracks[i].level = static_cast<float>(track.level) / kTenths;
    params.tracks[i].tone = static_cast<float>(track.tone) / kTenths;
    params.tracks[i].send = static_cast<float>(track.send) / kTenths;
  }
  return params;
}

}  // namespace app
