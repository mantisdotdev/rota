#pragma once

#include "engine/limits.h"
#include "engine/state.h"

namespace engine {

// One of the four sections: its live state plus 60 levels of undo (§8.2, T-13).
// Undo and redo move through the pattern (steps, alternation, speed); the
// performance parameters (globals, level, tone, send, chance, mute) stay as they
// are, so undoing a tap never makes a knob jump (D-035).
class Section {
 public:
  explicit Section(const State& initial);

  const State& state() const;
  // The live state, for changes that are not undoable: knobs, key, mute.
  State& state();

  // Starts an undoable edit: the current state becomes an undo level and the
  // returned live copy is what the edit changes. Anything redoable is forgotten.
  State& push_edit();

  void undo();
  void redo();
  int undo_levels() const;
  int redo_levels() const;

 private:
  static constexpr int kCapacity = kUndoDepth + 1;  // the live state plus 60 levels

  State& at(int offset);
  const State& at(int offset) const;

  State entries_[kCapacity];  // ring buffer, oldest at base_
  int base_;
  int count_;   // entries in use: 1..kCapacity
  int cursor_;  // offset from base_ of the live entry
};

}  // namespace engine
