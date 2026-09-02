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
// hold meaning once the hold passes 300 ms. Everything here runs on the main loop
// under hal::lock(), and writes the model that the scheduler reads on the beat.
namespace app {

constexpr uint32_t kHoldUs = 300000;          // a press longer than this is a hold
constexpr uint32_t kArmTimeoutUs = 5000000;   // §8.2: arming times out after 5 s
constexpr uint32_t kStatusUs = 1800000;       // §9.1: status text for 1.8 s
constexpr uint32_t kKnobStatusUs = 1000000;   // §8.3: a knob's value for 1 s
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
  void button_hold(hal::Button button, uint64_t at_us, Model& model);
  void section_press(int target, uint64_t at_us, Model& model, AudioPath& audio);
  void play_press(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void start_song(uint64_t at_us, Model& model, Scheduler& scheduler, AudioPath& audio);
  void encoder_turn(hal::Encoder encoder, int detents, uint64_t at_us, Model& model, AudioPath& audio);
  void track_knob(hal::Encoder encoder, int pad, int detents, uint64_t at_us, Model& model);
  void global_knob(hal::Encoder encoder, int detents, uint64_t at_us, Model& model);
  void apply_armed(int pad, uint64_t at_us, Model& model);
  void set_mute(Model& model, int pad, bool mute);
  void publish_params(const Model& model, AudioPath& audio);
  bool any_pad_held() const;
  void say(Model& model, uint64_t at_us, uint32_t duration_us, const char* format, ...);

  const engine::Kit* kit_;
  engine::Prng dice_;
  Press pads_[engine::kTrackCount];
  Press buttons_[hal::kButtonCount];
  int armed_;
  uint64_t armed_at_us_;
  AudioPath* audio_;  // for the hold actions run from tick()
};

}  // namespace app
