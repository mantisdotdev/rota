#pragma once

#include <cstdint>

#include "app/audio_path.h"
#include "app/model.h"
#include "app/scheduler.h"
#include "engine/kit.h"
#include "engine/prng.h"
#include "hal/hal.h"

// The input grammar (PRD §8, D-085): pads audition on press and edit on a short
// release; a pad held is muted and turns every knob and button into its per-track
// version (§8.1); round and section buttons act on a short release and do their
// hold meaning once the hold passes 300 ms: play's opens tap tempo (D-102) and two
// section buttons held together swap their contents (D-103). The views' gestures
// live here too: show's hold and undo + show (§9.3, §9.4), the settings rows
// (D-096) and the tutorial's steps (§8.5, D-097). Everything here runs on the main loop under
// hal::lock(), and writes the model that the scheduler reads on the beat.
namespace app {

constexpr uint32_t kHoldUs = 300000;          // a press longer than this is a hold
constexpr uint32_t kArmTimeoutUs = 5000000;   // §8.2: arming times out after 5 s
constexpr int kTapTempoTaps = 4;              // §8.2: four taps in rhythm set the bpm
constexpr int kNoButton = -1;

class Controller {
 public:
  explicit Controller(const engine::Kit& kit);

  // Seeds the dice (D-028); set once at init.
  void set_seed(uint32_t seed);

  void handle(const hal::InputEvent& event, Model& model, Scheduler& scheduler, AudioPath& audio);

  // Timeouts and holds; call every main-loop tick with the clock.
  void tick(uint64_t now_us, Model& model, Scheduler& scheduler, AudioPath& audio);

  // The armed button (split, swap or skip) as a hal::Button index, or kNoButton.
  int armed() const { return armed_; }

  // Tap-tempo mode: play's hold opened it and it is still waiting for taps (§8.2).
  bool tapping_tempo() const { return tapping_; }

 private:
  struct Press {
    bool down;
    uint64_t since_us;
    bool used;        // acted as a modifier, so the release does nothing
    bool hold_fired;  // the hold meaning has run
  };

  void pad_down(int pad, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void pad_up(int pad, uint64_t at_us, Model& model, AudioPath& audio);
  void pad_tap(int pad, uint64_t at_us, Model& model);
  void button_down(hal::Button button, uint64_t at_us, Model& model, AudioPath& audio);
  void button_up(hal::Button button, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void button_press(hal::Button button, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void button_hold(hal::Button button, uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void open_settings(Model& model, hal::Button other);
  void settings_turn(hal::Encoder encoder, int detents, Model& model);
  void settings_play(uint64_t at_us, Model& model);
  void factory_reset(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void section_press(int target, uint64_t at_us, Model& model, AudioPath& audio);
  void section_hold(int held, uint64_t at_us, Model& model, AudioPath& audio);
  int other_section_held(int except) const;
  void play_press(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void tap_tempo(uint64_t at_us, Model& model, AudioPath& audio);
  void stop_transport(Model& model, Scheduler& scheduler, AudioPath& audio);
  void start_song(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void stop_song(Model& model);
  void leave_song(Model& model, uint64_t at_us, const char* status);
  void pick_song(int slot, uint64_t at_us, Model& model);
  void encoder_turn(hal::Encoder encoder, int detents, uint64_t at_us, Model& model, AudioPath& audio);
  void track_knob(hal::Encoder encoder, int pad, int detents, uint64_t at_us, Model& model, engine::Section& section);
  void global_knob(hal::Encoder encoder, int detents, uint64_t at_us, Model& model, engine::Section& section);
  void apply_armed(int pad, uint64_t at_us, Model& model);
  void set_mute(Model& model, int pad, bool mute);
  void publish_params(const Model& model, AudioPath& audio);
  bool any_pad_held() const;

  // The tutorial waits for one gesture per step (§8.5); the rest is ignored.
  enum class TutorialEvent : uint8_t { kick_tap, snare_tap, chance_turn, share_opened, song_started };
  void tutorial_saw(Model& model, TutorialEvent event, uint64_t at_us);
  void end_tutorial(Model& model, uint64_t at_us, const char* status);

#if defined(__GNUC__)
  __attribute__((format(printf, 5, 6)))  // `this` is argument 1
#endif
  void say(Model& model, uint64_t at_us, uint32_t duration_us, const char* format, ...);
#if defined(__GNUC__)
  __attribute__((format(printf, 4, 5)))
#endif
  void show_knob(Model& model, uint64_t at_us, const char* format, ...);

  const engine::Kit* kit_;
  engine::Prng dice_;
  Press pads_[engine::kTrackCount];
  Press buttons_[hal::kButtonCount];
  int armed_;
  uint64_t armed_at_us_;
  bool tapping_;           // waiting for the four taps of §8.2
  int taps_seen_;
  uint64_t first_tap_us_;  // the first of them: the three intervals are measured from it
  uint64_t last_tap_us_;   // the mode's last activity, for the 5 s timeout
};

}  // namespace app
