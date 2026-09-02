#include "app/app.h"

#include <cstdio>
#include <new>

#include "app/controller.h"
#include "app/params.h"
#include "app/scheduler.h"
#include "engine/kits/lofi.h"
#include "hal/hal.h"
#include "ui/color.h"
#include "ui/ring.h"

namespace app {

// ui/ and hal/ each state the screen size, since neither may include the other (§12 rule 1).
static_assert(ui::kWidth == hal::kScreenWidth, "ui and hal disagree on the screen width");
static_assert(ui::kHeight == hal::kScreenHeight, "ui and hal disagree on the screen height");

namespace {

constexpr uint32_t kFramePeriodUs = 16667;                     // §7.3: 60 fps
constexpr int64_t kFlashFrames = sound::kSampleRate / 4;       // §9.1: 250 ms
constexpr int64_t kLedFlashFrames = sound::kSampleRate / 10;   // a pad lights fully for 100 ms after its hit
constexpr int kLedRestingPercent = 35;                         // a pad with steps
constexpr int kInputBatch = 32;
constexpr int kLineCapacity = 320;
constexpr int kSongNumber = 1;  // the one song in memory until io/ keeps eight (D-030)

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
ui::Flash flashes[ui::kMaxFlashes];
char footer[kStatusCapacity + engine::kMaxArrangementLength];
char line[kLineCapacity];

void render(float* left, float* right) { audio.render(left, right); }

void on_timer() {
  hal::lock();
  scheduler.tick(the_model, audio.position(), audio.scheduled, audio.params);
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

const char* footer_text(View view, const Arrangement& arrangement) {
  switch (view) {
    case View::ring:
      return nullptr;
    case View::text:
      return "text view: next session";
    case View::song:
      break;
  }
  const int written = std::snprintf(footer, sizeof footer, "song %d  ", kSongNumber);
  int at = written;
  for (int i = 0; i < arrangement.length && at + 1 < static_cast<int>(sizeof footer); ++i) {
    footer[at++] = arrangement.letters[i];
  }
  footer[at] = '\0';
  return footer;
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

void draw(uint64_t now_us) {
  hal::lock();
  frame_state = the_model.sections[the_model.current].state();
  const int64_t position = audio.position();
  const engine::Fraction playhead = scheduler.playhead(position);
  const uint32_t cycle_index = scheduler.cycle_index();
  const bool playing = the_model.transport;
  const int current = the_model.current;
  const View view = the_model.view;
  const Status status = the_model.status;
  const Arrangement arrangement = the_model.arrangement;
  const int armed = controller.armed();
  hal::unlock();

  const bool status_showing = status.duration_us != 0 && now_us - status.shown_at_us < status.duration_us;
  ui::RingModel ring{};
  ring.state = &frame_state;
  ring.cycle_index = cycle_index;
  ring.playhead = playhead;
  ring.playing = playing;
  ring.bpm = frame_state.bpm;
  ring.section = letter_of(current);
  ring.song = kSongNumber;
  ring.battery = hal::battery_percent();
  ring.status = status_showing ? status.text : nullptr;
  ring.footer = footer_text(view, arrangement);
  ring.armed = armed_text(armed);
  ring.flashes = flashes;
  ring.flash_count = collect_flashes(position);
  ui::draw_ring(hal::framebuffer(), ring);
}

// Pads with steps glow in their track colour, a pad that just fired lights fully,
// and a pad with no steps goes dark (§8.2). Reads the frame's copy of the state,
// never the model, which the timer may be changing.
void light_pads(int64_t position, const engine::State& state) {
  int percent[engine::kTrackCount];
  for (int i = 0; i < engine::kTrackCount; ++i) {
    percent[i] = engine::is_empty(state.tracks[i]) ? 0 : kLedRestingPercent;
  }
  const uint32_t total = the_fired_log.total;
  const uint32_t from = total > FiredLog::kCapacity ? total - FiredLog::kCapacity : 0;
  for (uint32_t seq = from; seq < total; ++seq) {
    const Fired& fired = the_fired_log.at(seq);
    const int64_t age = position - fired.sample;
    if (age >= 0 && age <= kLedFlashFrames) percent[engine::index_of(fired.event.track)] = 100;
  }
  for (int i = 0; i < engine::kTrackCount; ++i) {
    const ui::Rgb colour = ui::kTrackRgb[i];
    hal::set_led(i, static_cast<uint8_t>(colour.red * percent[i] / 100), static_cast<uint8_t>(colour.green * percent[i] / 100),
                 static_cast<uint8_t>(colour.blue * percent[i] / 100));
  }
  hal::show_leds();
}

}  // namespace

// Builds everything afresh, so the tests can start over as often as they like; on
// the device it runs once. Placement new is construction in place, not heap
// allocation, and it keeps an 85 KB model off the stack.
void init(const sound::SampleBank& samples) {
  new (&sound_engine) sound::Engine();
  new (&the_model) Model(kit);
  new (&scheduler) Scheduler(kit);
  new (&controller) Controller(kit);
  audio.reset();
  the_fired_log = FiredLog{};
  last_frame_us = 0;
  last_tick_us = 0;
  worst_tick_gap_us = 0;
  audio.init(sound_engine, kit, samples);
  const uint32_t seed = static_cast<uint32_t>(hal::now_us());
  scheduler.set_seed(seed);
  controller.set_seed(seed);
  audio.params.publish(params_of(the_model.sections[0].state(), kit, the_model.master_volume));
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
  hal::lock();
  for (int i = 0; i < count; ++i) controller.handle(events[i], the_model, scheduler, audio);
  controller.tick(now_us, the_model, scheduler, audio);
  hal::unlock();

  Fired fired;
  while (audio.fired.pop(fired)) the_fired_log.append(fired);
  report_latency();

  if (now_us - last_frame_us < kFramePeriodUs) return;
  last_frame_us = now_us;
  draw(now_us);
  hal::present();
  light_pads(audio.position(), frame_state);
}

const Model& model() { return the_model; }
const FiredLog& fired_log() { return the_fired_log; }
int64_t audio_position() { return audio.position(); }

}  // namespace app
