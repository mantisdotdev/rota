#pragma once

#include <cstdint>

#include "app/model.h"
#include "engine/kit.h"

// The card from the app's side (D-104): what the device reads at boot and what it
// writes as the player plays. io/ owns the files; this owns when they are touched.
// Every call runs on the main loop and takes hal::lock() only to copy the model,
// because a card takes milliseconds and the scheduler's timer must not wait for one.
namespace app {

// Boot, in two halves so that holds at start-up too: read_card takes the settings,
// the kit they name, the song they name and which slots hold a song off the card, and
// apply_card puts them into a model the caller has just built, under the lock. The
// kit is read before the songs, since a song's code is decoded against it.
void read_card(engine::Kit& kit);
void apply_card(Model& model);

// Every frame: the pick the song view made, the erase a factory reset asked for,
// and the song and the settings themselves once they have stopped changing.
void keep_card(uint64_t now_us, Model& model, const engine::Kit& kit);

}  // namespace app
