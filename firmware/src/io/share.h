#pragma once

#include "engine/kit.h"
#include "engine/share.h"
#include "engine/state.h"

// Sharing a loop (PRD §9.3, §10.2, D-105). The engine spells a loop as a code; this
// decides what identity the shared copy carries.
namespace io {

// The code the share view shows and its QR carries: the loop's own code with its own
// 6-character id after `~`, in place of whatever lineage the loop itself holds. A
// device that loads the code stores that id as its lineage, which is how the loop it
// makes can say what it is based on (§10.2); the sharer's own parent stays in the
// state, for the share view's footer.
engine::SectionCode shared_code(const engine::State& state, const engine::Kit& kit);

}  // namespace io
