#pragma once

#include <cstdint>

#include "engine/events.h"
#include "engine/kit.h"
#include "sound/delay.h"
#include "sound/dynamics.h"
#include "sound/filter.h"
#include "sound/limits.h"
#include "sound/reverb.h"
#include "sound/sidechain.h"
#include "sound/synth.h"
#include "sound/voice.h"

// The sound engine (PRD §12 sound spec, D-074): voices into per-track level and tone,
// kick ducking on the melodic bus, then the master chain in §12's order: low-pass
// (filter knob), tempo-locked delay and reverb sends (fx knob), glue compressor, soft
// clipper, limiter at −1 dBFS. Block-based, no allocation after init, no platform code.
namespace sound {

// A track's mix (§6.2), 0–1: engine::State's tenths divided by ten by the caller.
struct TrackMix {
  float level;
  float tone;
  float send;
};

// Everything the knobs and the state set. The caller (app/, or a tool) fills it from
// engine::State; sound/ never reads the state itself, only events and kit data.
struct Params {
  float bpm;
  float filter;  // §6.3, 0–1
  float fx;      // §6.3, 0–1
  float master;  // the volume control, 0–1, applied after the limiter
  TrackMix tracks[kTrackCount];
};

// 100 bpm, filter and fx from the kit, level 0.8, tone open, sends from the kit, master 1.
Params default_params(const engine::Kit& kit);

// An engine event landing `offset` samples into the block being rendered.
struct Trigger {
  engine::Event event;
  int offset;  // 0 ≤ offset < kBlockSize
};

struct StereoBlock {
  float left[kBlockSize];
  float right[kBlockSize];
};

class Engine {
 public:
  Engine();
  // Once, before the audio path runs. The bank's frames stay owned by the caller.
  void init(const engine::Kit& kit, const SampleBank& samples);
  // From the control side; gains ramp across the next block, cutoffs settle over a few.
  void set_params(const Params& params);
  // Renders one block. Triggers are in offset order.
  void render(const Trigger* triggers, int trigger_count, StereoBlock& out);
  int active_voices() const;

 private:
  struct Voice {
    enum class Kind : uint8_t { none, sample, synth };
    Kind kind;
    int pad;
    SampleVoice sample;
    SynthVoice synth;

    bool active() const;
    float loudness() const;
    void release();
    void render(float* out, int from, int to);
  };

  void start(const engine::Event& event);
  void release_pad(int pad);
  Voice& allocate();
  void render_voices(int from, int to);
  void mix_tracks();
  void master_chain(StereoBlock& out);

  const engine::Kit* kit_;
  SampleBank samples_;
  const Preset* presets_[kTrackCount];
  Voice voices_[kVoiceCount];
  Params target_;
  float level_[kTrackCount];  // gains as of the end of the last block: ramps start here
  float send_[kTrackCount];
  float master_;
  float tone_knob_[kTrackCount];  // smoothed knob positions
  float filter_knob_;
  Svf tone_[kTrackCount];
  Svf dry_filter_;
  Svf send_filter_;
  Sidechain sidechain_;
  Delay delay_;
  Reverb reverb_;
  Compressor compressor_;
  SoftClipper clipper_;
  Limiter limiter_;
  float track_[kTrackCount][kBlockSize];
  float duck_[kBlockSize];
  float dry_[kBlockSize];
  float send_bus_[kBlockSize];
};

}  // namespace sound
