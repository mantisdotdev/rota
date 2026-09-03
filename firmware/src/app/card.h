#pragma once

#include <cstdint>

#include "app/model.h"
#include "engine/kit.h"

// The card from the app's side (D-104): what the device reads at boot and what it
// writes as the player plays. io/ owns the files; this owns when they are touched.
// Both calls run on the main loop and take hal::lock() only to copy the model,
// because a card takes milliseconds and the scheduler's timer must not wait for one.
namespace app {

// Boot: the settings, the song they name, and which slots hold a song. Called from
// app::init, where no timer is running yet.
void read_card(Model& model, const engine::Kit& kit);

// Every frame: the pick the song view made, the erase a factory reset asked for,
// and the song and the settings themselves once they have stopped changing.
void keep_card(uint64_t now_us, Model& model, const engine::Kit& kit);

}  // namespace app
