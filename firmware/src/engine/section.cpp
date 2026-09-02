#include "engine/section.h"

namespace engine {

namespace {

// Undo and redo move the pattern; the knobs stay where the player left them (D-035).
void carry_performance(const State& from, State& to) {
  to.bpm = from.bpm;
  to.filter = from.filter;
  to.fx = from.fx;
  to.chance = from.chance;
  to.swing = from.swing;
  to.key = from.key;
  for (int i = 0; i < kTrackCount; ++i) {
    to.tracks[i].level = from.tracks[i].level;
    to.tracks[i].tone = from.tracks[i].tone;
    to.tracks[i].send = from.tracks[i].send;
    to.tracks[i].chance = from.tracks[i].chance;
    to.tracks[i].mute = from.tracks[i].mute;
  }
}

}  // namespace

Section::Section(const State& initial) : entries_{}, base_(0), count_(1), cursor_(0) { entries_[0] = initial; }

State& Section::at(int offset) { return entries_[(base_ + offset) % kCapacity]; }
const State& Section::at(int offset) const { return entries_[(base_ + offset) % kCapacity]; }

const State& Section::state() const { return at(cursor_); }
State& Section::state() { return at(cursor_); }

State& Section::push_edit() {
  const State snapshot = at(cursor_);
  count_ = cursor_ + 1;  // anything redoable is forgotten
  if (count_ == kCapacity) {
    base_ = (base_ + 1) % kCapacity;  // full: the oldest level falls off
    count_ -= 1;
    cursor_ -= 1;
  }
  cursor_ += 1;
  count_ += 1;
  at(cursor_) = snapshot;
  return at(cursor_);
}

void Section::undo() {
  if (cursor_ == 0) return;
  carry_performance(at(cursor_), at(cursor_ - 1));
  cursor_ -= 1;
}

void Section::redo() {
  if (cursor_ + 1 >= count_) return;
  carry_performance(at(cursor_), at(cursor_ + 1));
  cursor_ += 1;
}

int Section::undo_levels() const { return cursor_; }
int Section::redo_levels() const { return count_ - 1 - cursor_; }

}  // namespace engine
