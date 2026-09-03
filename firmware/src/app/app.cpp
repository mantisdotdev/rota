#include "app/app.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "app/card.h"
#include "app/controller.h"
#include "app/params.h"
#include "app/scheduler.h"
#include "engine/kits/lofi.h"
#include "engine/share.h"
#include "hal/hal.h"
#include "io/share.h"
#include "ui/color.h"
#include "ui/draw.h"
#include "ui/leds.h"
#include "ui/overlay.h"
#include "ui/ring.h"
#include "ui/settings.h"
#include "ui/share.h"
#include "ui/song.h"
#include "ui/text.h"
#include "ui/tutorial.h"

namespace app {

// ui/ and hal/ each state the screen and button counts, since neither may include the other (§12 rule 1).
static_assert(ui::kWidth == hal::kScreenWidth, "ui and hal disagree on the screen width");
static_assert(ui::kHeight == hal::kScreenHeight, "ui and hal disagree on the screen height");
static_assert(ui::kButtonCount == hal::kButtonCount, "ui and hal disagree on the button count");
// The lights index buttons by number; a reorder of hal::Button must not compile.
static_assert(ui::kSplit == static_cast<int>(hal::Button::split), "ui and hal disagree on the button order");
static_assert(ui::kSwap == static_cast<int>(hal::Button::swap), "ui and hal disagree on the button order");
static_assert(ui::kShow == static_cast<int>(hal::Button::show), "ui and hal disagree on the button order");
static_assert(ui::kPlay == static_cast<int>(hal::Button::play), "ui and hal disagree on the button order");
static_assert(ui::kSectionA == static_cast<int>(hal::Button::section_a), "ui and hal disagree on the button order");

namespace {

constexpr uint32_t kFramePeriodUs = 16667;                    // §7.3: 60 fps
constexpr int64_t kFlashFrames = sound::kSampleRate / 4;      // §9.1: 250 ms
constexpr int64_t kLedFlashFrames = sound::kSampleRate / 10;  // a pad lights fully for 100 ms after its hit
constexpr int kInputBatch = 32;
constexpr int kLineCapacity = 320;
constexpr int kFooterCapacity = 32;
const char* const kTapMarker = "tap";  // the top row while tap tempo waits (§8.2, D-102)

const engine::Kit& kit = engine::kits::kLofi;

// The engine (207 KB) and the model (85 KB) go to the platform's bulk memory; the
// scheduler's 30 KB event list and the queues stay with the ordinary statics.
HAL_BULK_MEMORY sound::Engine sound_engine;
HAL_BULK_MEMORY Model the_model(kit);
Scheduler scheduler(kit);
Controller controller(kit);
AudioPath audio;
FiredLog the_fired_log;
uint64_t last_frame_us = 0;
uint64_t last_tick_us = 0;
uint32_t worst_tick_gap_us = 0;  // since the last latency report: bounds what the loop adds before a press is read
engine::State frame_state;       // the edited section as of the last frame, copied under the lock
int64_t frame_position = 0;      // the audio position read for that frame
ui::Flash flashes[ui::kMaxFlashes];
char line[kLineCapacity];
char footer[kFooterCapacity];
engine::SectionCode shown_code;  // the code the share view's QR was made from
ui::QrCode qr;
int applied_brightness = -1;

// The rest of what a frame reads from the model, copied under the lock so no view
// sees it half changed.
struct Frame {
  View view;
  Status status;
  Status knob;
  Arrangement arrangement;
  bool song_mode;
  int song_position;
  int song;  // the slot being played and edited, 1–8
  bool filled[engine::kSongSlotCount];  // which slots hold a song, the one being edited included (§9.6)
  bool song_hint_dismissed;
  bool transport;
  bool roll;
  int current;
  int playing;
  io::Settings settings;
  int settings_cursor;
  Tutorial tutorial;
  int armed;
  bool tap_tempo;
  engine::Fraction playhead;
  uint32_t cycle_index;
};
Frame frame;

void render(float* left, float* right) { audio.render(left, right); }

void on_timer() {
  hal::lock();
  scheduler.tick(the_model, audio);
  hal::unlock();
}

void report_latency() {
  uint32_t last_us = 0;
  uint32_t worst_us = 0;
  uint32_t count = 0;
  if (!audio.take_latency(last_us, worst_us, count)) return;
  const int buffer_frames = hal::audio_buffer_frames();
  const unsigned buffer_us = static_cast<unsigned>(static_cast<uint64_t>(buffer_frames) * 1000000 / sound::kSampleRate);
  const unsigned last = last_us;
  const unsigned worst = worst_us;
  const unsigned gap = worst_tick_gap_us;
  worst_tick_gap_us = 0;
  std::snprintf(line, sizeof line,
                "audition latency: press read to render pickup %u.%u ms measured (worst %u.%u ms over %u); "
                "main loop gaps up to %u.%u ms before the read; the platform's %d-frame output buffer adds %u.%u ms; "
                "driver latency past it not measured",
                last / 1000, (last % 1000) / 100, worst / 1000, (worst % 1000) / 100, static_cast<unsigned>(count),
                gap / 1000, (gap % 1000) / 100, buffer_frames, buffer_us / 1000, (buffer_us % 1000) / 100);
  hal::log(line);
}

bool showing(const Status& status, uint64_t now_us) {
  return status.duration_us != 0 && now_us - status.shown_at_us < status.duration_us;
}

// What the bottom-left corner says while nothing transient does.
const char* footer_text() {
  switch (frame.view) {
    case View::settings:
      return ui::kSettingsHint;
    case View::share:
      if (frame_state.lineage[0] == '\0') return nullptr;
      std::snprintf(footer, sizeof footer, "based on %s", frame_state.lineage);  // §9.3
      return footer;
    case View::ring:
    case View::text:
    case View::song:
      return nullptr;
  }
  return nullptr;
}

const char* armed_text(int armed) {
  if (armed == static_cast<int>(hal::Button::split)) return "split";
  if (armed == static_cast<int>(hal::Button::swap)) return "swap";
  if (armed == static_cast<int>(hal::Button::skip)) return "skip";
  return nullptr;
}

int collect_flashes(int64_t position) {
  int count = 0;
  const uint32_t total = the_fired_log.total;
  const uint32_t from = total > FiredLog::kCapacity ? total - FiredLog::kCapacity : 0;
  for (uint32_t seq = from; seq < total && count < ui::kMaxFlashes; ++seq) {
    const Fired& fired = the_fired_log.at(seq);
    if (fired.audition) continue;
    const int64_t age = position - fired.sample;
    if (age < 0 || age > kFlashFrames) continue;
    flashes[count++] = ui::Flash{fired.event.track, fired.event.time, fired.event.sub_index, fired.event.is_ghost,
                                 static_cast<float>(age) / static_cast<float>(kFlashFrames)};
  }
  return count;
}

void draw_ring(uint16_t* framebuffer, int64_t position, int bottom) {
  ui::RingModel ring{};
  ring.bottom = bottom;
  ring.state = &frame_state;
  ring.cycle_index = frame.cycle_index;
  ring.playhead = frame.playhead;
  ring.playing = frame.transport;
  ring.bpm = frame_state.bpm;
  ring.section = letter_of(frame.current);
  ring.song = frame.song;
  ring.battery = hal::battery_percent();
  ring.flashes = flashes;
  ring.flash_count = collect_flashes(position);
  ui::draw_ring(framebuffer, ring);
}

void draw_song(uint16_t* framebuffer) {
  ui::SongModel song{};
  song.song = frame.song;
  for (int i = 0; i < engine::kSongSlotCount; ++i) song.filled[i] = frame.filled[i];
  song.letters = frame.arrangement.letters;
  song.length = frame.arrangement.length;
  song.playing = frame.song_mode ? frame.song_position : ui::kNoLetter;
  song.show_hint = !frame.song_hint_dismissed;
  ui::draw_song_view(framebuffer, song);
}

// The QR is made again only when the code changes: an encode at version 10 is
// milliseconds, a frame is not. The code carries the loop's own id, not the id of
// the loop it came from, which stays in the state for the footer (§10.2, D-105).
void draw_share(uint16_t* framebuffer, int bottom) {
  const engine::SectionCode code = io::shared_code(frame_state, kit);
  if (std::strcmp(code.text, shown_code.text) != 0) {
    shown_code = code;
    ui::encode_share_qr(shown_code.text, qr);
  }
  ui::draw_share_view(framebuffer, ui::ShareModel{shown_code.text, &qr}, bottom);
}

void draw_settings(uint16_t* framebuffer) {
  const io::Settings& settings = frame.settings;
  const ui::SettingsModel model{frame_state.key,      frame_state.swing,      kit.id,           settings.brightness,
                                settings.sleep_minutes, settings.midi_clock_in, settings.midi_clock_out, settings.sync_in,
                                settings.sync_out,      kFirmwareVersion,       frame.settings_cursor};
  ui::draw_settings_view(framebuffer, model);
}

void draw(uint64_t now_us) {
  hal::lock();
  frame_state = the_model.sections[the_model.current].state();
  const int64_t position = audio.position();
  frame_position = position;
  frame.playhead = scheduler.playhead(position);
  frame.cycle_index = scheduler.cycle_index();
  frame.view = the_model.view;
  frame.status = the_model.status;
  frame.knob = the_model.knob;
  frame.arrangement = the_model.arrangement;
  frame.song_mode = the_model.song_mode;
  frame.song_position = the_model.song_position;
  frame.song = the_model.settings.song;
  for (int i = 0; i < engine::kSongSlotCount; ++i) frame.filled[i] = the_model.song_filled[i];
  // The song being edited counts as filled the moment it has something in it, card or no card.
  for (int i = 0; i < engine::kSectionCount; ++i) {
    if (!is_empty(the_model.sections[i].state())) frame.filled[frame.song - 1] = true;
  }
  if (the_model.arrangement.length > 0) frame.filled[frame.song - 1] = true;
  frame.song_hint_dismissed = the_model.song_hint_dismissed;
  frame.transport = the_model.transport;
  frame.roll = the_model.roll;
  frame.current = the_model.current;
  frame.playing = the_model.playing;
  frame.settings = the_model.settings;
  frame.settings_cursor = the_model.settings_cursor;
  frame.tutorial = the_model.tutorial;
  frame.armed = controller.armed();
  frame.tap_tempo = controller.tapping_tempo();
  hal::unlock();

  // The prompt rows are reserved while the tutorial runs, except in settings, which
  // the tutorial does not narrate; every view lays out above them (Appendix D).
  ui::Overlay overlay{};
  if (frame.tutorial.active && frame.view != View::settings) {
    const ui::Prompt& prompt = ui::tutorial_prompt(frame.tutorial.step);
    overlay.prompt = prompt.lines;
    overlay.prompt_count = prompt.count;
  }
  const int bottom = ui::content_bottom(overlay.prompt_count);
  uint16_t* framebuffer = hal::framebuffer();
  switch (frame.view) {
    case View::ring:
      draw_ring(framebuffer, position, bottom);
      break;
    case View::text:
      ui::draw_text_view(framebuffer, frame_state, kit, bottom);
      break;
    case View::song:
      draw_song(framebuffer);
      break;
    case View::share:
      draw_share(framebuffer, bottom);
      break;
    case View::settings:
      draw_settings(framebuffer);
      break;
  }
  overlay.status = showing(frame.status, now_us) ? frame.status.text : nullptr;
  overlay.knob = showing(frame.knob, now_us) ? frame.knob.text : nullptr;
  overlay.footer = footer_text();
  overlay.armed = frame.tap_tempo ? kTapMarker : armed_text(frame.armed);  // the mode play's hold opened (D-102)
  if (overlay.armed != nullptr) {  // after the bpm on the ring, at the right of the top row elsewhere
    overlay.armed_x = frame.view == View::ring ? ui::kArmedAfterBpm : ui::kWidth - ui::kMargin - ui::text_width(overlay.armed);
  }
  ui::draw_overlay(framebuffer, overlay);
}

// The pads and the button backlights from the frame's copy of the state, never
// the model, which the timer may be changing (D-099).
void light_leds(int64_t position, const engine::State& state) {
  ui::LedModel model{};
  model.state = &state;
  const uint32_t total = the_fired_log.total;
  const uint32_t from = total > FiredLog::kCapacity ? total - FiredLog::kCapacity : 0;
  for (uint32_t seq = from; seq < total; ++seq) {
    const Fired& fired = the_fired_log.at(seq);
    const int64_t age = position - fired.sample;
    if (age >= 0 && age <= kLedFlashFrames) model.hit[engine::index_of(fired.event.track)] = true;
  }
  model.armed = frame.armed == kNoButton ? ui::kNoButton : frame.armed;
  model.tap_tempo = frame.tap_tempo;
  model.roll = frame.roll;
  model.showing = frame.view != View::ring;
  model.transport = frame.transport;
  model.current_section = frame.current;
  model.playing_section = frame.playing;
  ui::Leds leds;
  ui::light(model, leds);
  for (int i = 0; i < engine::kTrackCount; ++i) hal::set_led(i, leds.pads[i].red, leds.pads[i].green, leds.pads[i].blue);
  for (int i = 0; i < ui::kButtonCount; ++i) {
    hal::set_button_led(static_cast<hal::Button>(i), leds.buttons[i].red, leds.buttons[i].green, leds.buttons[i].blue);
  }
  hal::show_leds();
}

// What the card should hold for the tutorial as it stands. Read under the lock.
uint8_t tutorial_byte(const Model& model) { return model.tutorial.active ? kTutorialPending : kTutorialRan; }

// First boot, or not: one byte on the card (D-097). Read before the lock, since
// the card is slow and the timer may already tick.
bool tutorial_done() {
  uint8_t flag = 0;
  uint32_t size = 0;
  return hal::read_file(kTutorialDoneFile, &flag, 1, &size) && size == 1 && flag == kTutorialRan;
}

}  // namespace

// Builds everything afresh, so the tests can start over as often as they like; on
// the device it runs once. Placement new is construction in place, not heap
// allocation, and it keeps an 85 KB model off the stack.
void init(const sound::SampleBank& samples) {
  const bool first_run = !tutorial_done();
  read_card(kit);  // both reads happen before the lock: a card takes milliseconds (D-104)
  hal::lock();  // a timer already ticking (the harness re-initialises) cannot see the app half made
  new (&sound_engine) sound::Engine();
  new (&the_model) Model(kit);
  new (&scheduler) Scheduler(kit);
  new (&controller) Controller(kit);
  audio.reset();
  the_fired_log = FiredLog{};
  last_frame_us = 0;
  last_tick_us = 0;
  worst_tick_gap_us = 0;
  shown_code.text[0] = '\0';
  applied_brightness = -1;
  the_model.tutorial = Tutorial{first_run, 0, false};
  apply_card(the_model);  // the settings and the song the device was left on
  audio.init(sound_engine, kit, samples);
  const uint32_t seed = static_cast<uint32_t>(hal::now_us());
  scheduler.set_seed(seed);
  controller.set_seed(seed);
  audio.params.publish(params_of(the_model.sections[0].state(), kit, the_model.master_volume));
  hal::unlock();
  hal::start_audio(&render);
  hal::start_timer(kTimerPeriodUs, &on_timer);
}

void tick() {
  const uint64_t now_us = hal::now_us();
  const uint64_t gap = last_tick_us == 0 ? 0 : now_us - last_tick_us;
  if (gap > worst_tick_gap_us) worst_tick_gap_us = static_cast<uint32_t>(gap);
  last_tick_us = now_us;
  hal::InputEvent events[kInputBatch];
  const int count = hal::read_input(events, kInputBatch);
  bool save_tutorial = false;
  uint8_t tutorial_flag = kTutorialRan;
  hal::lock();
  for (int i = 0; i < count; ++i) controller.handle(events[i], the_model, scheduler, audio);
  controller.tick(now_us, the_model, scheduler, audio);
  if (the_model.tutorial.save_pending) {  // written below, outside the lock: the card is slow
    save_tutorial = true;
    tutorial_flag = tutorial_byte(the_model);
  }
  hal::unlock();
  // The flag stays pending until the card takes it, so a refused write is retried
  // on the next tick rather than lost; a reset that could not be recorded must not
  // come back as a device that has already run its tutorial (D-097).
  if (save_tutorial && hal::write_file(kTutorialDoneFile, &tutorial_flag, 1)) {
    hal::lock();
    if (tutorial_byte(the_model) == tutorial_flag) the_model.tutorial.save_pending = false;
    hal::unlock();
  }

  Fired fired;
  while (audio.fired.pop(fired)) the_fired_log.append(fired);
  report_latency();

  if (now_us - last_frame_us < kFramePeriodUs) return;
  last_frame_us = now_us;
  draw(now_us);
  hal::present();
  light_leds(frame_position, frame_state);
  keep_card(now_us, the_model, kit);
  if (frame.settings.brightness != applied_brightness) {
    applied_brightness = frame.settings.brightness;
    hal::set_brightness(applied_brightness);
  }
}

const Model& model() { return the_model; }
const FiredLog& fired_log() { return the_fired_log; }
int64_t audio_position() {
  hal::lock();
  const int64_t position = audio.position();
  hal::unlock();
  return position;
}

}  // namespace app
