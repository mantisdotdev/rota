#pragma once

#include "engine/kit.h"
#include "engine/state.h"
#include "sound/engine.h"

// engine::State into sound::Params: the app's job, never sound/'s (D-074). Tenths
// become 0–1; the master volume is the app's own value.
namespace app {

sound::Params params_of(const engine::State& state, const engine::Kit& kit, engine::Tenths master_volume);

}  // namespace app
