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
constexpr uint64_t kMicrosPerMinute = 60000000;  // tap tempo: taps are beats (§6.1)
constexpr int kSwingPerDetent = 5;   // hundredths (D-096)
constexpr int kBrightnessPerDetent = 10;
constexpr int kSwingMax = 100;
constexpr int kRootCount = 12;

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
  char text[kStatusCapacity];
  std::vsnprintf(text, sizeof text, format, args);
  say(status, at_us, duration_us, text);
}

}  // namespace

Controller::Controller(const engine::Kit& kit)
    : kit_(&kit),
      dice_(0),
      pads_{},
      buttons_{},
      armed_(kNoButton),
      armed_at_us_(0),
      tapping_(false),
      taps_seen_(0),
      first_tap_us_(0),
      last_tap_us_(0) {}

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
  if (tapping_ && held_for(last_tap_us_, now_us) >= kArmTimeoutUs) {  // the arming timeout, on play (D-102)
    tapping_ = false;
    if (taps_seen_ > 0) say(model, now_us, kStatusUs, "tempo unchanged");
  }
  for (int i = 0; i < hal::kButtonCount; ++i) {
    Press& press = buttons_[i];
    if (press.down && !press.hold_fired && held_for(press.since_us, now_us) >= kHoldUs) {
      press.hold_fired = true;
      button_hold(static_cast<hal::Button>(i), now_us, model, scheduler, audio);
    }
  }
  for (int i = 0; i < engine::kTrackCount; ++i) {
    Press& press = pads_[i];
    if (press.down && !press.hold_fired && held_for(press.since_us, now_us) >= kHoldUs) {
      press.hold_fired = true;
      pad_hold(i, now_us, model);
    }
  }
  // The wire, folded in by app::tick's Clock::follow just before this call.
  following_ = scheduler.clock().following();
  const int lost = scheduler.clock().take_lost_bpm();  // one-shot: a follow just ended
  if (lost > 0) adopt_bpm(lost, now_us, model, audio);
  if (scheduler.waiting_for_clock()) say(model, now_us, kStatusUs, "waiting for clock");  // the count-in (D-112)
}

// Hold show opens the share view and keeps it up (D-093); a pad or dice pressed while
// show is still held is the jam send gesture (§11), so it does not sound, mute, add a
// hit, fill or clear.
bool Controller::sending_gesture(const Model& model) const {
  return model.view == View::share && buttons_[static_cast<int>(hal::Button::show)].down;
}

// Pads (§8.1, D-085): the sound at once, the mute while held, the edit on release.

void Controller::pad_down(int pad, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  if (sending_gesture(model)) {  // hold show + pad: send that pad's track (§11), no sound and no mute
    pads_[pad] = Press{true, at_us, true, false};  // used, so the release does nothing
    model.jam_request = JamRequest{true, true, pad};
    return;
  }
  pads_[pad] = Press{true, at_us, false, false};
  // A pad is not an instrument in the song view or in settings: there it picks a song
  // (§9.6) or does nothing at all (D-096), so it neither sounds nor mutes its track —
  // a press that mutes during song play is a hole in the pattern the player did not
  // ask for. The release still unmutes unconditionally, since the view may have
  // changed while the pad was down.
  if (model.view == View::song || model.view == View::settings) return;
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
  if (model.view == View::song) {  // §9.6: pads pick songs there; the pick is the gesture the hint waits for
    model.song_hint_dismissed = true;
    pick_song(pad + io::kFirstSlot, at_us, model);
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
      if (sending_gesture(model)) {  // hold show + dice: send the whole loop (§11)
        model.jam_request = JamRequest{true, false, 0};
        return;
      }
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
      if (model.view == View::settings) {
        if (model.settings_cursor == static_cast<int>(ui::SettingsRow::factory_reset)) {
          factory_reset(at_us, model, scheduler, audio);
        }
        return;
      }
      if (model.tutorial.active) {  // play skips the tutorial held as well as pressed (§8.5, D-097)
        end_tutorial(model, at_us, kTutorialSkipped);
        return;
      }
      tapping_ = true;
      taps_seen_ = 0;
      last_tap_us_ = at_us;
      say(model, at_us, kStatusUs, "tap 4 times in rhythm");
      return;
    default:
      if (is_section(button)) section_hold(section_of(button), at_us, model, audio);
      return;
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

// Two of A–D held together exchange their contents (§8.2, D-103): one undoable
// load in each, so undo in a section brings its own loop back. What the swap puts
// under the playing section lands at the next beat, as any edit does (§6.7).
void Controller::section_hold(int held, uint64_t at_us, Model& model, AudioPath& audio) {
  if (model.view == View::song || model.view == View::settings) return;
  const int other = other_section_held(held);
  if (other == kNoSection) return;  // a lone hold is half of the song gesture (D-030)
  // Both buttons are spent until they are released: neither release switches
  // section, the partner's own hold does not swap the pair back, and a third
  // button held meanwhile finds no free partner to swap with.
  buttons_[static_cast<int>(hal::Button::section_a) + held].used = true;
  Press& partner = buttons_[static_cast<int>(hal::Button::section_a) + other];
  partner.used = true;
  partner.hold_fired = true;
  engine::Section& first = model.sections[held];
  engine::Section& second = model.sections[other];
  if (is_empty(first.state()) && is_empty(second.state())) {
    say(model, at_us, kStatusUs, "nothing to swap");  // no steps either way (D-038)
    return;
  }
  const engine::State kept = first.state();
  engine::load(first, second.state());
  engine::load(second, kept);
  // Stopped, nothing else would publish what the swap put under the playing
  // section; playing, the beat boundary publishes it, so the pattern and the
  // knobs that shape it arrive together rather than a fraction of a beat apart.
  if (!model.transport) publish_params(model, audio);
  say(model, at_us, kStatusUs, "swapped %c and %c", letter_of(held < other ? held : other),
      letter_of(held < other ? other : held));
}

// The lowest section button held and not already spent in a swap, other than
// `except`. Three held is not a gesture: one pair swaps, in letter order.
int Controller::other_section_held(int except) const {
  for (int i = 0; i < engine::kSectionCount; ++i) {
    const Press& press = buttons_[static_cast<int>(hal::Button::section_a) + i];
    if (i != except && press.down && !press.used) return i;
  }
  return kNoSection;
}

// Play (§8.2, D-030): play or stop; with a section held, or in the song view, the
// song from the top. In settings it runs the selected row (D-096). During the
// tutorial a press that does not start the song skips it (§8.5, D-097).
void Controller::play_press(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio) {
  if (tapping_) {  // in tap-tempo mode every press is a tap, never a play (D-102)
    tap_tempo(at_us, model, audio);
    return;
  }
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

// Tap tempo (§8.2, D-102): the four presses after play's hold are the taps and the
// three intervals between them average into one beat. The bpm reaches the sections
// a knob would (D-086) and is clamped to the same range as the speed knob.
void Controller::tap_tempo(uint64_t at_us, Model& model, AudioPath& audio) {
  if (taps_seen_ == 0) first_tap_us_ = at_us;
  taps_seen_ += 1;
  last_tap_us_ = at_us;
  if (taps_seen_ < kTapTempoTaps) {
    say(model, at_us, kStatusUs, "%d more", kTapTempoTaps - taps_seen_);
    return;
  }
  tapping_ = false;
  // Four taps inside one microsecond of the clock would divide by zero, and they
  // are faster than any tempo anyway, so they read as the top of the range.
  const uint64_t elapsed_us = held_for(first_tap_us_, at_us);
  const uint64_t measured = elapsed_us == 0
                                ? static_cast<uint64_t>(sound::kMaxBpm)
                                : (kMicrosPerMinute * (kTapTempoTaps - 1) + elapsed_us / 2) / elapsed_us;
  const int bpm = clamped(static_cast<int>(measured), sound::kMinBpm, sound::kMaxBpm);
  int targets[2];
  const int count = edited_sections(model, targets);
  for (int i = 0; i < count; ++i) model.sections[targets[i]].state().bpm = static_cast<uint8_t>(bpm);
  publish_params(model, audio);
  say(model, at_us, kStatusUs, "%d bpm", bpm);
}

void Controller::adopt_bpm(int bpm, uint64_t at_us, Model& model, AudioPath& audio) {
  int targets[2];
  const int count = edited_sections(model, targets);
  for (int i = 0; i < count; ++i) model.sections[targets[i]].state().bpm = static_cast<uint8_t>(bpm);
  publish_params(model, audio);
  say(model, at_us, kStatusUs, "ext off, %d bpm", bpm);
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

void Controller::stop_song(Model& model) {
  model.song_mode = false;
  model.song_start_pending = false;
  model.song_position = 0;
}

// The arrangement emptied under a playing song: song play ends at once and the
// section that was playing plays on live (T-81).
void Controller::leave_song(Model& model, uint64_t at_us, const char* status) {
  stop_song(model);
  say(model, at_us, kStatusUs, "%s", status);
}

// §9.6: a pad picks a song, and an empty one becomes a copy of this one. The card
// work is app::keep_card's, on the next frame and outside the lock (D-104); the
// transport plays on, but song play does not, since the arrangement it was stepping
// through is about to be another song's (T-56).
void Controller::pick_song(int slot, uint64_t at_us, Model& model) {
  if (slot == model.settings.song || model.picked_song != io::kNoSlot) return;
  // A file nothing could be read from is not an empty slot to copy over: the tap says
  // so and teaches the hold that does replace it, as a dice press teaches its hold (D-107).
  if (model.song_slots[slot - io::kFirstSlot] == Slot::unreadable) {
    say(model, at_us, kStatusUs, "hold to replace song %d", slot);
    return;
  }
  model.picked_song = slot;
  stop_song(model);
  say(model, at_us, kStatusUs, "song %d", slot);
}

// §9.6, D-107: the one hold gesture the pads have in the song view, and it is live
// only on a slot the device already knows it cannot read — so no hold can ever
// destroy a song the player could still open.
void Controller::pad_hold(int pad, uint64_t at_us, Model& model) {
  if (model.view != View::song || model.picked_song != io::kNoSlot) return;
  if (model.song_slots[pad] != Slot::unreadable) return;
  model.song_hint_dismissed = true;
  model.picked_song = pad + io::kFirstSlot;
  model.replace_picked = true;
  stop_song(model);
  say(model, at_us, kStatusUs, "song %d replaced", pad + io::kFirstSlot);
}

// Knobs (§8.1, §8.3, D-087): a held pad takes the knob for that track alone. A knob
// is heard the moment it moves, so while a switch is pending it turns on the playing
// section and on the section waiting to play alike (D-086, revisited 2026-09-03).
// In settings the speed and filter knobs pick and set the row instead; fx, chance
// and volume keep their meaning there, because the loop plays on behind the menu
// and a performance control that stops working is worse than an inconsistency (D-096).

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
      if (following_) {  // a MIDI or sync clock owns the tempo; the knob says so (§11, C-09)
        show_knob(model, at_us, "ext");
        return;
      }
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
  tapping_ = false;  // in settings play runs the selected row (D-096)
  model.view = View::settings;
  model.settings_cursor = 0;
}

void Controller::settings_turn(hal::Encoder encoder, int detents, Model& model) {
  io::Settings& settings = model.settings;
  if (encoder == hal::Encoder::speed) {
    model.settings_cursor = wrapped(model.settings_cursor + detents, ui::kSettingsRowCount);
    return;
  }
  int targets[2];
  const int count = edited_sections(model, targets);
  switch (static_cast<ui::SettingsRow>(model.settings_cursor)) {
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
      settings.brightness =
          clamped(settings.brightness + detents * kBrightnessPerDetent, io::kBrightnessMin, io::kBrightnessMax);
      return;
    case ui::SettingsRow::sleep: {
      int choice = 0;
      for (int i = 0; i < io::kSleepChoiceCount; ++i) {
        if (io::kSleepChoices[i] == settings.sleep_minutes) choice = i;
      }
      settings.sleep_minutes = io::kSleepChoices[clamped(choice + detents, 0, io::kSleepChoiceCount - 1)];
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
  switch (static_cast<ui::SettingsRow>(model.settings_cursor)) {
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
  model.erase_pending = true;  // and the card with it: every slot back to an empty song
  armed_ = kNoButton;
  tapping_ = false;
  publish_params(model, audio);
  say(model, at_us, kStatusUs, "reset");
}

// The tutorial (§8.5, D-097): each step waits for its gesture and ignores the rest.
// Settings is the one view that hides the prompt, so a gesture made in there
// advances nothing: a step must never pass while the player cannot read it.
void Controller::tutorial_saw(Model& model, TutorialEvent event, uint64_t at_us) {
  static const TutorialEvent kExpected[ui::kTutorialSteps] = {
      TutorialEvent::kick_tap,    TutorialEvent::kick_tap,      TutorialEvent::snare_tap,
      TutorialEvent::chance_turn, TutorialEvent::share_opened, TutorialEvent::song_started,
  };
  static_assert(ui::kTutorialSteps == 6, "kExpected must name a gesture for every tutorial step (§8.5, D-097)");
  if (model.view == View::settings) return;
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

// A held pad is muted in every section, so a copy or a swap made meanwhile and a
// switch landing meanwhile cannot leave a mute behind.
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
