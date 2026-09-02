#include "app/controller.h"

#include <cstdarg>
#include <cstdio>
#include <new>

#include "app/audition.h"
#include "app/params.h"
#include "engine/edits.h"
#include "sound/limits.h"
#include "ui/settings.h"
#include "ui/tutorial.h"

namespace app {

namespace {

constexpr int kBpmPerDetent = 1;  // D-087
constexpr int kSwingPerDetent = 5;   // hundredths (D-096)
constexpr int kBrightnessPerDetent = 10;
constexpr int kBrightnessMin = 10;
constexpr int kBrightnessMax = 100;
constexpr int kSwingMax = 100;
constexpr int kRootCount = 12;
constexpr int kSleepChoices[] = {0, 5, 10, 20, 30, 60};  // minutes; 0 is never
constexpr int kSleepChoiceCount = 6;

// Status copy (Appendix D): what happened, lowercase, specific.
// "basses" keeps "2 basses, spread evenly" inside the message row's 25 columns.
const char* const kPlural[engine::kTrackCount] = {"kicks", "snares", "hats", "claps", "basses", "chords", "plucks", "rims"};
const char* const kSpeedText[] = {"0.5", "1", "2"};
const char* const kAltText[] = {"every cycle", "takes turns", "takes turns, late", "every fourth"};
const char* const kTutorialDone = "that's a song";
const char* const kTutorialSkipped = "tutorial skipped";

int hit_count(const engine::Track& track) {
  int count = 0;
  for (int i = 0; i < track.step_count; ++i) count += engine::is_rest(track.steps[i]) ? 0 : 1;
  return count;
}

engine::Tenths nudged(engine::Tenths value, int detents) {
  const int moved = static_cast<int>(value) + detents;
  if (moved < 0) return 0;
  if (moved > engine::kTenthsMax) return engine::kTenthsMax;
  return static_cast<engine::Tenths>(moved);
}

int clamped(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
int wrapped(int value, int count) { return ((value % count) + count) % count; }

// Input timestamps come off a platform clock that may be quantised to milliseconds,
// so a release can be stamped just before its press; that is a tap, not a hold.
uint64_t held_for(uint64_t since_us, uint64_t until_us) { return until_us > since_us ? until_us - since_us : 0; }

bool is_section(hal::Button button) { return button >= hal::Button::section_a; }
int section_of(hal::Button button) { return static_cast<int>(button) - static_cast<int>(hal::Button::section_a); }
bool is_armable(hal::Button button) {
  return button == hal::Button::split || button == hal::Button::swap || button == hal::Button::skip;
}

// The sections a live change reaches (D-086): the edited one, and the one still
// playing while a switch waits for the cycle boundary. The edited one comes last,
// so the status line reads its value.
int edited_sections(const Model& model, int out[2]) {
  int count = 0;
  if (model.playing != model.current) out[count++] = model.playing;
  out[count++] = model.current;
  return count;
}

void announce(Status& status, uint64_t at_us, uint32_t duration_us, const char* format, va_list args) {
  std::vsnprintf(status.text, sizeof status.text, format, args);
  status.shown_at_us = at_us;
  status.duration_us = duration_us;
}

}  // namespace

Controller::Controller(const engine::Kit& kit)
    : kit_(&kit), dice_(0), pads_{}, buttons_{}, armed_(kNoButton), armed_at_us_(0) {}

void Controller::set_seed(uint32_t seed) { dice_ = engine::Prng(seed); }

void Controller::handle(const hal::InputEvent& event, Model& model, Scheduler& scheduler, AudioPath& audio) {
  switch (event.kind) {
    case hal::InputKind::pad_down:
      if (event.index < engine::kTrackCount) pad_down(event.index, event.time_us, model, scheduler, audio);
      break;
    case hal::InputKind::pad_up:
      if (event.index < engine::kTrackCount) pad_up(event.index, event.time_us, model, audio);
      break;
    case hal::InputKind::button_down:
      if (event.index < hal::kButtonCount) button_down(static_cast<hal::Button>(event.index), event.time_us, model, audio);
      break;
    case hal::InputKind::button_up:
      if (event.index < hal::kButtonCount) {
        button_up(static_cast<hal::Button>(event.index), event.time_us, model, scheduler, audio);
      }
      break;
    case hal::InputKind::encoder_turn:
      if (event.index < hal::kEncoderCount) {
        encoder_turn(static_cast<hal::Encoder>(event.index), event.detents, event.time_us, model, audio);
      }
      break;
    case hal::InputKind::encoder_down:
    case hal::InputKind::encoder_up:
      break;  // no meaning yet (D-054: the pushes are free)
  }
}

void Controller::tick(uint64_t now_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  if (armed_ != kNoButton && held_for(armed_at_us_, now_us) >= kArmTimeoutUs) armed_ = kNoButton;  // T-07
  for (int i = 0; i < hal::kButtonCount; ++i) {
    Press& press = buttons_[i];
    if (press.down && !press.hold_fired && held_for(press.since_us, now_us) >= kHoldUs) {
      press.hold_fired = true;
      button_hold(static_cast<hal::Button>(i), now_us, model, scheduler, audio);
    }
  }
}

// Pads (§8.1, D-085): the sound at once, the mute while held, the edit on release.

void Controller::pad_down(int pad, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  pads_[pad] = Press{true, at_us, false, false};
  set_mute(model, pad, true);
  const int64_t position = audio.position();
  const engine::Event event = audition(model.sections[model.current].state(), *kit_, engine::pad_at(pad),
                                       scheduler.chord_root_at(position), scheduler.playhead(position));
  audio.immediate.push(Immediate{event, at_us});
}

void Controller::pad_up(int pad, uint64_t at_us, Model& model, AudioPath& /*audio*/) {
  const Press press = pads_[pad];
  pads_[pad].down = false;
  set_mute(model, pad, false);
  if (!press.down || press.used || held_for(press.since_us, at_us) >= kHoldUs) return;
  pad_tap(pad, at_us, model);
}

void Controller::pad_tap(int pad, uint64_t at_us, Model& model) {
  if (model.view == View::song) {  // §9.6: pads pick songs there (io/, later); the pick is the gesture the hint waits for
    model.song_hint_dismissed = true;
    return;
  }
  if (model.view == View::settings) return;  // a menu, not a loop (D-096)
  if (armed_ != kNoButton) {
    apply_armed(pad, at_us, model);
    return;
  }
  engine::Section& section = model.sections[model.current];
  const engine::Pad which = engine::pad_at(pad);
  const char* name = engine::pad_of(*kit_, which).name;
  if (engine::is_full(engine::track_of(section.state(), which))) {
    say(model, at_us, kStatusUs, "%s is full", name);  // D-024
    return;
  }
  engine::tap(section, which, *kit_);
  const int hits = hit_count(engine::track_of(section.state(), which));
  if (hits == 1) {
    say(model, at_us, kStatusUs, "one %s", name);
  } else {
    say(model, at_us, kStatusUs, "%d %s, spread evenly", hits, kPlural[pad]);
  }
  if (which == engine::Pad::kick) tutorial_saw(model, TutorialEvent::kick_tap, at_us);
  if (which == engine::Pad::snare) tutorial_saw(model, TutorialEvent::snare_tap, at_us);
}

void Controller::apply_armed(int pad, uint64_t at_us, Model& model) {
  const hal::Button armed = static_cast<hal::Button>(armed_);
  armed_ = kNoButton;
  engine::Section& section = model.sections[model.current];
  const engine::Pad which = engine::pad_at(pad);
  const engine::Track& track = engine::track_of(section.state(), which);
  const char* name = engine::pad_of(*kit_, which).name;
  // An undoable edit moves the section's live entry, so the track is read again after it.
  switch (armed) {
    case hal::Button::split: {
      if (engine::is_empty(track) || engine::is_rest(track.steps[track.step_count - 1])) {
        say(model, at_us, kStatusUs, "add a hit first");  // T-07, D-042
        return;
      }
      engine::split(section, which);
      const engine::Track& after = engine::track_of(section.state(), which);
      const int hits = after.steps[after.step_count - 1].hits;
      if (hits == 1) {
        say(model, at_us, kStatusUs, "%s unsplit", name);
      } else {
        say(model, at_us, kStatusUs, "%s x%d", name, hits);
      }
      return;
    }
    case hal::Button::swap: {
      engine::swap(section, which);
      const engine::Track& after = engine::track_of(section.state(), which);
      say(model, at_us, kStatusUs, "%s %s", name, kAltText[static_cast<int>(after.alt)]);
      return;
    }
    case hal::Button::skip:
      if (engine::is_full(track)) {
        say(model, at_us, kStatusUs, "%s is full", name);
        return;
      }
      engine::skip(section, which);
      say(model, at_us, kStatusUs, "a rest on %s", name);
      return;
    default:
      return;
  }
}

// Buttons (§8.2): a held pad makes split, swap, skip and undo act on that pad at
// once; otherwise a short press acts on release and a hold fires from tick().

void Controller::button_down(hal::Button button, uint64_t at_us, Model& model, AudioPath& /*audio*/) {
  buttons_[static_cast<int>(button)] = Press{true, at_us, false, false};
  const bool per_pad = button == hal::Button::split || button == hal::Button::swap || button == hal::Button::skip ||
                       button == hal::Button::undo;
  if (!per_pad || !any_pad_held() || model.view == View::song || model.view == View::settings) return;
  buttons_[static_cast<int>(button)].used = true;
  engine::Section& section = model.sections[model.current];
  for (int pad = 0; pad < engine::kTrackCount; ++pad) {
    if (!pads_[pad].down) continue;
    pads_[pad].used = true;
    if (button == hal::Button::undo) {
      const engine::Pad which = engine::pad_at(pad);
      const char* name = engine::pad_of(*kit_, which).name;
      if (engine::is_empty(engine::track_of(section.state(), which))) {
        say(model, at_us, kStatusUs, "%s has no steps", name);  // nothing to remove (D-042)
        continue;
      }
      engine::remove_last_step(section, which);
      say(model, at_us, kStatusUs, "%s: last step removed", name);
      continue;
    }
    armed_ = static_cast<int>(button);
    apply_armed(pad, at_us, model);
  }
}

void Controller::button_up(hal::Button button, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  const Press press = buttons_[static_cast<int>(button)];
  buttons_[static_cast<int>(button)].down = false;
  if (button == hal::Button::split) model.roll = false;
  if (!press.down || press.used || press.hold_fired || held_for(press.since_us, at_us) >= kHoldUs) return;
  button_press(button, at_us, model, scheduler, audio);
}

void Controller::button_press(hal::Button button, uint64_t at_us, Model& model, Scheduler& scheduler,
                              AudioPath& audio) {
  if (is_section(button)) {
    section_press(section_of(button), at_us, model, audio);
    return;
  }
  if (is_armable(button)) {
    const int index = static_cast<int>(button);
    armed_ = armed_ == index ? kNoButton : index;  // a second press disarms
    armed_at_us_ = at_us;
    return;
  }
  engine::Section& section = model.sections[model.current];
  switch (button) {
    case hal::Button::undo:
      if (model.view == View::song) {
        if (model.arrangement.length > 0) model.arrangement.length -= 1;
        if (model.arrangement.length == 0) leave_song(model, at_us, "song is empty");
        return;
      }
      if (model.view == View::settings) return;
      if (section.undo_levels() == 0) {
        say(model, at_us, kStatusUs, "nothing to undo");
        return;
      }
      section.undo();
      say(model, at_us, kStatusUs, "undo");
      return;
    case hal::Button::dice: {
      if (model.view == View::song) {
        say(model, at_us, kStatusUs, "hold dice to clear");
        return;
      }
      if (model.view == View::settings) return;
      const int levels = section.undo_levels();
      engine::dice_fill_empty(section, *kit_, dice_.next());
      say(model, at_us, kStatusUs, section.undo_levels() != levels ? "filled the empty tracks" : "nothing to fill");
      return;
    }
    case hal::Button::show:
      if (model.view == View::share || model.view == View::settings) {  // back to the ring (D-093, D-096)
        model.view = View::ring;
        return;
      }
      model.view = model.view == View::ring ? View::text : model.view == View::text ? View::song : View::ring;
      return;
    case hal::Button::play:
      play_press(at_us, model, scheduler, audio);
      return;
    default:
      return;
  }
}

void Controller::button_hold(hal::Button button, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  engine::Section& section = model.sections[model.current];
  switch (button) {
    case hal::Button::split:
      if (model.view != View::song && model.view != View::settings) model.roll = true;
      return;
    case hal::Button::undo:
      if (buttons_[static_cast<int>(hal::Button::show)].down) {  // §9.4: hold undo + show together
        open_settings(model, hal::Button::show);
        return;
      }
      if (model.view == View::song || model.view == View::settings) return;
      if (section.redo_levels() == 0) {
        say(model, at_us, kStatusUs, "nothing to redo");
        return;
      }
      section.redo();
      say(model, at_us, kStatusUs, "redo");
      return;
    case hal::Button::dice:
      if (model.view == View::song) {
        model.arrangement.length = 0;
        leave_song(model, at_us, "song cleared");
        return;
      }
      if (model.view == View::settings) return;
      engine::dice_replace_all(section, *kit_, dice_.next());
      say(model, at_us, kStatusUs, "new loop");
      return;
    case hal::Button::show:
      if (buttons_[static_cast<int>(hal::Button::undo)].down) {
        open_settings(model, hal::Button::undo);
        return;
      }
      model.view = View::share;  // §9.3; stays open after the release (D-093)
      tutorial_saw(model, TutorialEvent::share_opened, at_us);
      return;
    case hal::Button::play:
      if (model.view == View::settings && model.settings.cursor == static_cast<int>(ui::SettingsRow::factory_reset)) {
        factory_reset(at_us, model, scheduler, audio);
      }
      return;  // tap tempo is a later session
    default:
      return;  // section swapping is a later session
  }
}

// Sections (§6.8, D-086): the player moves to the section at once and an empty one
// takes a copy; the scheduler moves at the next cycle boundary. In the song view a
// press adds a letter instead.
void Controller::section_press(int target, uint64_t at_us, Model& model, AudioPath& audio) {
  if (model.view == View::song) {
    model.song_hint_dismissed = true;  // §9.6: the hint has done its job
    if (model.arrangement.length >= engine::kMaxArrangementLength) {
      say(model, at_us, kStatusUs, "song is full");
      return;
    }
    model.arrangement.letters[model.arrangement.length] = letter_of(target);
    model.arrangement.length += 1;
    return;
  }
  if (model.view == View::settings) return;
  const bool leaving_song = model.song_mode || model.song_start_pending;
  model.song_mode = false;
  model.song_start_pending = false;
  if (target == model.current && !leaving_song && model.pending_section == kNoSection) return;
  engine::Section& from = model.sections[model.current];
  engine::Section& to = model.sections[target];
  bool copied = false;
  if (is_empty(to.state()) && !is_empty(from.state())) {
    engine::State copy = from.state();
    for (int i = 0; i < engine::kTrackCount; ++i) copy.tracks[i].mute = false;
    engine::load(to, copy);
    copied = true;
  }
  model.current = target;
  if (model.transport) {
    model.pending_section = target;
  } else {
    model.playing = target;
    model.pending_section = kNoSection;
    publish_params(model, audio);
  }
  if (copied) {
    say(model, at_us, kStatusUs, "copied into %c", letter_of(target));
  } else if (model.transport) {
    say(model, at_us, kStatusUs, "%c next", letter_of(target));
  }
}

// Play (§8.2, D-030): play or stop; with a section held, or in the song view, the
// song from the top. In settings it runs the selected row (D-096). During the
// tutorial a press that does not start the song skips it (§8.5, D-097).
void Controller::play_press(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  bool section_held = false;
  for (int i = static_cast<int>(hal::Button::section_a); i < hal::kButtonCount; ++i) {
    if (buttons_[i].down) {
      buttons_[i].used = true;
      section_held = true;
    }
  }
  if (model.view == View::settings) {
    settings_play(at_us, model);
    return;
  }
  const bool song_gesture = section_held || model.view == View::song;
  if (model.tutorial.active && !(song_gesture && model.arrangement.length > 0)) {
    end_tutorial(model, at_us, kTutorialSkipped);
    return;
  }
  if (song_gesture) {
    start_song(at_us, model, scheduler, audio);
    return;
  }
  if (model.transport) {
    stop_transport(model, scheduler, audio);
    return;
  }
  model.transport = true;
  scheduler.start(model, audio);
}

// Stop leaves nothing pending: no song, no switch, no roll (T-81).
void Controller::stop_transport(Model& model, Scheduler& scheduler, AudioPath& audio) {
  model.transport = false;
  model.song_mode = false;
  model.song_start_pending = false;
  model.song_position = 0;
  model.pending_section = kNoSection;
  model.roll = false;
  if (model.playing != model.current) {
    model.playing = model.current;
    publish_params(model, audio);
  }
  scheduler.stop(audio);
}

void Controller::start_song(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  if (model.arrangement.length == 0) {
    say(model, at_us, kStatusUs, "song is empty");
    return;
  }
  if (model.transport && model.song_mode) {  // play again stops it
    stop_transport(model, scheduler, audio);
    return;
  }
  model.song_start_pending = true;
  say(model, at_us, kStatusUs, "playing song");
  if (!model.transport) {
    model.transport = true;
    scheduler.start(model, audio);
  }
  tutorial_saw(model, TutorialEvent::song_started, at_us);
}

// The arrangement emptied under a playing song: song play ends at once and the
// section that was playing plays on live (T-81).
void Controller::leave_song(Model& model, uint64_t at_us, const char* status) {
  model.song_mode = false;
  model.song_start_pending = false;
  model.song_position = 0;
  say(model, at_us, kStatusUs, "%s", status);
}

// Knobs (§8.1, §8.3, D-087): a held pad takes the knob for that track alone. A knob
// is heard the moment it moves, so while a switch is pending it turns on the playing
// section and on the section waiting to play alike (D-086, revisited 2026-09-03).
// In settings the speed and filter knobs pick and set the row instead (D-096).

void Controller::encoder_turn(hal::Encoder encoder, int detents, uint64_t at_us, Model& model, AudioPath& audio) {
  if (detents == 0) return;
  if (model.view == View::settings && (encoder == hal::Encoder::speed || encoder == hal::Encoder::filter)) {
    settings_turn(encoder, detents, model);
    return;
  }
  int targets[2];
  const int count = edited_sections(model, targets);
  for (int i = 0; i < count; ++i) {
    engine::Section& section = model.sections[targets[i]];
    if (any_pad_held()) {
      for (int pad = 0; pad < engine::kTrackCount; ++pad) {
        if (!pads_[pad].down) continue;
        pads_[pad].used = true;
        track_knob(encoder, pad, detents, at_us, model, section);
      }
    } else {
      global_knob(encoder, detents, at_us, model, section);
    }
  }
  publish_params(model, audio);
  if (encoder == hal::Encoder::chance) tutorial_saw(model, TutorialEvent::chance_turn, at_us);
}

void Controller::track_knob(hal::Encoder encoder, int pad, int detents, uint64_t at_us, Model& model,
                            engine::Section& section) {
  const engine::Pad which = engine::pad_at(pad);
  const char* name = engine::pad_of(*kit_, which).name;
  engine::Track& track = engine::track_of(section.state(), which);
  switch (encoder) {
    case hal::Encoder::speed: {
      engine::adjust_speed(section, which, detents);  // undoable: the live entry moves
      const engine::Track& after = engine::track_of(section.state(), which);
      show_knob(model, at_us, "%s speed %s", name, kSpeedText[static_cast<int>(after.speed)]);
      return;
    }
    case hal::Encoder::filter:
      engine::adjust_tone(section.state(), which, detents);
      show_knob(model, at_us, "%s tone %d.%d", name, track.tone / 10, track.tone % 10);
      return;
    case hal::Encoder::fx:
      engine::adjust_send(section.state(), which, detents);
      show_knob(model, at_us, "%s send %d.%d", name, track.send / 10, track.send % 10);
      return;
    case hal::Encoder::chance:
      engine::adjust_track_chance(section.state(), which, detents);
      show_knob(model, at_us, "%s chance %d.%d", name, track.chance / 10, track.chance % 10);
      return;
    case hal::Encoder::volume:
      engine::adjust_level(section.state(), which, detents);
      show_knob(model, at_us, "%s level %d.%d", name, track.level / 10, track.level % 10);
      return;
  }
}

void Controller::global_knob(hal::Encoder encoder, int detents, uint64_t at_us, Model& model,
                             engine::Section& section) {
  engine::State& state = section.state();
  switch (encoder) {
    case hal::Encoder::speed: {
      int bpm = static_cast<int>(state.bpm) + detents * kBpmPerDetent;
      if (bpm < sound::kMinBpm) bpm = sound::kMinBpm;
      if (bpm > sound::kMaxBpm) bpm = sound::kMaxBpm;
      state.bpm = static_cast<uint8_t>(bpm);
      show_knob(model, at_us, "%d bpm", bpm);
      return;
    }
    case hal::Encoder::filter:
      state.filter = nudged(state.filter, detents);
      show_knob(model, at_us, "filter %d.%d", state.filter / 10, state.filter % 10);
      return;
    case hal::Encoder::fx:
      state.fx = nudged(state.fx, detents);
      show_knob(model, at_us, "fx %d.%d", state.fx / 10, state.fx % 10);
      return;
    case hal::Encoder::chance:
      state.chance = nudged(state.chance, detents);
      show_knob(model, at_us, "chance %d.%d", state.chance / 10, state.chance % 10);
      return;
    case hal::Encoder::volume:
      if (&section != &model.sections[model.current]) return;  // the master is the app's, set once
      model.master_volume = nudged(model.master_volume, detents);
      show_knob(model, at_us, "volume %d.%d", model.master_volume / 10, model.master_volume % 10);
      return;
  }
}

// Settings (§9.4, D-096): opened by holding undo and show together, in either
// order; the other button's own meanings do not fire. The speed knob picks the
// row, the filter knob sets it; key and swing change the sections a knob would.

void Controller::open_settings(Model& model, hal::Button other) {
  Press& press = buttons_[static_cast<int>(other)];
  press.used = true;
  press.hold_fired = true;
  model.view = View::settings;
  model.settings.cursor = 0;
}

void Controller::settings_turn(hal::Encoder encoder, int detents, Model& model) {
  Settings& settings = model.settings;
  if (encoder == hal::Encoder::speed) {
    settings.cursor = wrapped(settings.cursor + detents, ui::kSettingsRowCount);
    return;
  }
  int targets[2];
  const int count = edited_sections(model, targets);
  switch (static_cast<ui::SettingsRow>(settings.cursor)) {
    case ui::SettingsRow::key:
      for (int i = 0; i < count; ++i) {
        engine::Key& key = model.sections[targets[i]].state().key;
        key.root = static_cast<uint8_t>(wrapped(key.root + detents, kRootCount));
      }
      return;
    case ui::SettingsRow::scale:
      for (int i = 0; i < count; ++i) {
        engine::Key& key = model.sections[targets[i]].state().key;
        key.mode = static_cast<engine::Mode>(wrapped(static_cast<int>(key.mode) + detents, engine::kModeCount));
      }
      return;
    case ui::SettingsRow::swing:
      for (int i = 0; i < count; ++i) {
        uint8_t& swing = model.sections[targets[i]].state().swing;
        swing = static_cast<uint8_t>(clamped(swing + detents * kSwingPerDetent, 0, kSwingMax));
      }
      return;
    case ui::SettingsRow::brightness:
      settings.brightness = clamped(settings.brightness + detents * kBrightnessPerDetent, kBrightnessMin, kBrightnessMax);
      return;
    case ui::SettingsRow::sleep: {
      int choice = 0;
      for (int i = 0; i < kSleepChoiceCount; ++i) {
        if (kSleepChoices[i] == settings.sleep_minutes) choice = i;
      }
      settings.sleep_minutes = kSleepChoices[clamped(choice + detents, 0, kSleepChoiceCount - 1)];
      return;
    }
    case ui::SettingsRow::midi_clock_in:
      settings.midi_clock_in = detents > 0;
      return;
    case ui::SettingsRow::midi_clock_out:
      settings.midi_clock_out = detents > 0;
      return;
    case ui::SettingsRow::sync_in:
      settings.sync_in = detents > 0;
      return;
    case ui::SettingsRow::sync_out:
      settings.sync_out = detents > 0;
      return;
    case ui::SettingsRow::kit:  // one kit until io/ reads the card
    case ui::SettingsRow::firmware:
    case ui::SettingsRow::run_tutorial:
    case ui::SettingsRow::factory_reset:
      return;
  }
}

void Controller::settings_play(uint64_t at_us, Model& model) {
  switch (static_cast<ui::SettingsRow>(model.settings.cursor)) {
    case ui::SettingsRow::run_tutorial:
      model.tutorial = Tutorial{true, 0, false};
      model.view = View::ring;
      return;
    case ui::SettingsRow::factory_reset:
      say(model, at_us, kStatusUs, "hold play to reset");
      return;
    default:
      return;
  }
}

// Back to the power-on state, the tutorial included (D-096). Rebuilt in place, so
// nothing the size of the model touches the stack.
void Controller::factory_reset(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  if (model.transport) stop_transport(model, scheduler, audio);
  new (&model) Model(*kit_);
  model.tutorial = Tutorial{true, 0, true};
  armed_ = kNoButton;
  publish_params(model, audio);
  say(model, at_us, kStatusUs, "reset");
}

// The tutorial (§8.5, D-097): each step waits for its gesture and ignores the rest.
void Controller::tutorial_saw(Model& model, TutorialEvent event, uint64_t at_us) {
  static const TutorialEvent kExpected[ui::kTutorialSteps] = {
      TutorialEvent::kick_tap,    TutorialEvent::kick_tap,      TutorialEvent::snare_tap,
      TutorialEvent::chance_turn, TutorialEvent::share_opened, TutorialEvent::song_started,
  };
  if (!model.tutorial.active || event != kExpected[model.tutorial.step]) return;
  model.tutorial.step += 1;
  if (model.tutorial.step >= ui::kTutorialSteps) end_tutorial(model, at_us, kTutorialDone);
}

void Controller::end_tutorial(Model& model, uint64_t at_us, const char* status) {
  model.tutorial.active = false;
  model.tutorial.step = 0;
  model.tutorial.save_pending = true;
  say(model, at_us, kStatusUs, "%s", status);
}

// A held pad is muted in every section, so a copy made meanwhile and a switch
// landing meanwhile cannot leave a mute behind.
void Controller::set_mute(Model& model, int pad, bool mute) {
  for (int i = 0; i < engine::kSectionCount; ++i) model.sections[i].state().tracks[pad].mute = mute;
}

// The playing section is what is heard (D-086).
void Controller::publish_params(const Model& model, AudioPath& audio) {
  audio.params.publish(params_of(model.sections[model.playing].state(), *kit_, model.master_volume));
}

bool Controller::any_pad_held() const {
  for (const Press& press : pads_) {
    if (press.down) return true;
  }
  return false;
}

void Controller::say(Model& model, uint64_t at_us, uint32_t duration_us, const char* format, ...) {
  va_list args;
  va_start(args, format);
  announce(model.status, at_us, duration_us, format, args);
  va_end(args);
}

void Controller::show_knob(Model& model, uint64_t at_us, const char* format, ...) {
  va_list args;
  va_start(args, format);
  announce(model.knob, at_us, kKnobStatusUs, format, args);
  va_end(args);
}

}  // namespace app
