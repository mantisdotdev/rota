#pragma once

#include <cstdint>

#include "sound/filter.h"
#include "sound/limits.h"

namespace sound {

enum class Waveform : uint8_t { saw, sine };

// A kit-selectable synth preset (§12 sound spec, D-076). A kit's synth pad names one
// as its source; the parameters are the sound engine's, not the kit's.
struct Preset {
  const char* name;
  Waveform waveform;
  float detune_cents;   // > 0: two oscillators per tone, one this far below and one this far above the pitch
  float sub_level;      // a sine at the fundamental under each tone
  float octave_level;   // a sine an octave above each tone (the tine of the keys)
  float cutoff_hz;      // the low-pass at rest
  float filter_env_hz;  // how far the attack opens the low-pass
  float filter_decay_s;
  float q;
  float attack_s;
  float decay_s;    // time from the peak to within 60 dB of the sustain level
  float sustain;    // 0–1 of the peak
  float release_s;  // time to fall 60 dB once the gate closes
  float max_hold_s; // the gate closes on its own after this long
  bool mono;        // a new event on the pad releases the voice still sounding
  float gain;       // trim so the presets sit at comparable loudness
};

// nullptr when the engine has no preset of that name; the pad then stays silent.
const Preset* preset_named(const char* name);

constexpr int kMaxTones = 3;  // a chord is its root plus two upper tones (D-021, D-041)

class SynthVoice {
 public:
  SynthVoice();
  void start(const Preset& preset, const uint8_t* midi_pitches, int tone_count, float gain);
  void release();
  bool active() const { return active_; }
  float loudness() const;
  // Adds the voice into out[from, to).
  void render(float* out, int from, int to);

 private:
  enum class Stage : uint8_t { attack, decay, release };

  struct Tone {
    float phase_a;
    float phase_b;
    float inc_a;
    float inc_b;
  };

  float oscillate(Tone& tone) const;

  const Preset* preset_;
  Tone tones_[kMaxTones];
  int tone_count_;
  Svf filter_;
  float filter_env_;
  float filter_decay_mul_;
  Stage stage_;
  float level_;
  float attack_step_;
  float decay_mul_;
  float release_mul_;
  int hold_remaining_;
  float gain_;
  bool active_;
};

}  // namespace sound
