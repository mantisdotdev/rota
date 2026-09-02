#!/usr/bin/env python3
"""Sample generator: synthesises the lofi kit's drum samples (PRD Appendix A, D-081)
into its kit folder as 16-bit 48 kHz mono WAVs, so the kit is complete without
recorded samples. Deterministic: the same kit.json gives byte-identical files.

    python3 tools/sample_generator.py spec/kits/lofi/kit.json

One recipe per drum pad name (kick, snare, hat, clap, rim), voiced for Appendix D:
warm, slightly lo-fi, punchy and short. Synth pads have no sample; their sound is a
preset in firmware/src/sound/synth.cpp. Standard library only.
"""

import json
import math
import os
import random
import struct
import sys
import wave

import kit_builder  # the kit format's rules live there; this tool only makes files that obey them

SAMPLE_RATE = 48000
INT16_MAX = 32767
TWO_PI = 2.0 * math.pi
END_FADE_SECONDS = 0.005  # every sample ends at zero
LOFI_LOW_PASS_HZ = 9000.0  # the top end a cassette would keep

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_USAGE = 2


class OnePole:
    """First-order low-pass; high-pass is the input minus it."""

    def __init__(self, cutoff_hz):
        self.coef = 1.0 - math.exp(-TWO_PI * cutoff_hz / SAMPLE_RATE)
        self.state = 0.0

    def low(self, x):
        self.state += (x - self.state) * self.coef
        return self.state

    def high(self, x):
        return x - self.low(x)


def frames(seconds):
    return int(round(seconds * SAMPLE_RATE))


def decay(t, tau):
    return math.exp(-t / tau)


def finish(samples, peak):
    """The lo-fi print: a gentle low-pass, a fade to zero at the end, then a fixed peak."""
    low_pass = [OnePole(LOFI_LOW_PASS_HZ), OnePole(LOFI_LOW_PASS_HZ)]
    out = []
    for x in samples:
        for stage in low_pass:
            x = stage.low(x)
        out.append(x)
    fade = frames(END_FADE_SECONDS)
    total = len(out)
    for i in range(max(total - fade, 0), total):
        out[i] *= (total - i) / fade
    loudest = max(abs(x) for x in out) or 1.0
    return [x * peak / loudest for x in out]


def kick():
    """A sine sweeping 170 Hz down to 48 Hz with a short click, driven for warmth."""
    rng = random.Random(1)
    click_tone = OnePole(3000.0)
    out = []
    phase = 0.0
    for i in range(frames(0.45)):
        t = i / SAMPLE_RATE
        phase += (48.0 + 122.0 * decay(t, 0.028)) / SAMPLE_RATE
        body = math.sin(TWO_PI * phase) * decay(t, 0.13)
        click = click_tone.low(rng.uniform(-1.0, 1.0)) * decay(t, 0.003) * 0.6
        out.append(math.tanh(1.6 * (body + click)))
    return finish(out, 0.95)


def snare():
    """Two short body tones under a bright noise burst."""
    rng = random.Random(2)
    bright = [OnePole(1300.0), OnePole(1300.0)]
    out = []
    for i in range(frames(0.32)):
        t = i / SAMPLE_RATE
        body = math.sin(TWO_PI * 186.0 * t) * decay(t, 0.06) + 0.5 * math.sin(TWO_PI * 332.0 * t) * decay(t, 0.04)
        noise = rng.uniform(-1.0, 1.0)
        for stage in bright:
            noise = stage.high(noise)
        out.append(math.tanh(1.3 * (0.7 * body + 0.9 * noise * decay(t, 0.09))))
    return finish(out, 0.85)


def hat():
    """Closed hat: high-passed noise with a metallic ring, gone in a tenth of a second."""
    rng = random.Random(3)
    high = [OnePole(6500.0), OnePole(6500.0)]
    out = []
    for i in range(frames(0.12)):
        t = i / SAMPLE_RATE
        noise = rng.uniform(-1.0, 1.0)
        for stage in high:
            noise = stage.high(noise)
        ring = sum(math.sin(TWO_PI * f * t) for f in (4100.0, 5300.0, 6900.0)) / 3.0
        out.append(noise * decay(t, 0.028) + 0.25 * ring * decay(t, 0.018))
    return finish(out, 0.55)


def clap():
    """Four noise bursts a few milliseconds apart, then a longer tail, in a 1–4.5 kHz band."""
    rng = random.Random(4)
    band = [OnePole(1000.0), OnePole(4500.0)]
    bursts = [0.0, 0.011, 0.022, 0.033]
    out = []
    for i in range(frames(0.28)):
        t = i / SAMPLE_RATE
        envelope = sum(decay(t - start, 0.004) for start in bursts if t >= start)
        if t >= bursts[-1]:
            envelope += 0.8 * decay(t - bursts[-1], 0.06)
        noise = band[1].low(band[0].high(rng.uniform(-1.0, 1.0)))
        out.append(math.tanh(2.0 * noise * envelope))
    return finish(out, 0.8)


def rim():
    """A rimshot: a 1.7 kHz tick over a short 800 Hz knock and a puff of noise."""
    rng = random.Random(5)
    high = OnePole(2000.0)
    out = []
    for i in range(frames(0.09)):
        t = i / SAMPLE_RATE
        tick = math.sin(TWO_PI * 1700.0 * t) * decay(t, 0.004)
        knock = 0.5 * math.sin(TWO_PI * 800.0 * t) * decay(t, 0.010)
        puff = 0.4 * high.high(rng.uniform(-1.0, 1.0)) * decay(t, 0.003)
        out.append(math.tanh(1.2 * (tick + knock + puff)))
    return finish(out, 0.7)


RECIPES = {"kick": kick, "snare": snare, "hat": hat, "clap": clap, "rim": rim}


def write_wav(path, samples):
    data = struct.pack("<%dh" % len(samples), *(int(round(x * INT16_MAX)) for x in samples))
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SAMPLE_RATE)
        out.writeframes(data)


def main(argv):
    if len(argv) != 2:
        print("usage: sample_generator.py spec/kits/<id>/kit.json", file=sys.stderr)
        return EXIT_USAGE
    kit_path = argv[1]
    try:
        with open(kit_path, encoding="utf-8") as source:
            kit = json.load(source)
    except (OSError, ValueError) as error:
        print(f"sample_generator: {kit_path}: {error}", file=sys.stderr)
        return EXIT_FAILED
    kit_dir = os.path.dirname(os.path.abspath(kit_path))
    sample_pads = [pad for pad in kit.get("pads", []) if pad.get("voice") == "sample"]
    # Every pad is checked before any file is written, so a bad kit leaves no half-written folder.
    for pad in sample_pads:
        name = pad.get("name")
        if name not in RECIPES:
            print(f"sample_generator: no recipe for a sample pad named {name!r}", file=sys.stderr)
            return EXIT_FAILED
        try:
            source = kit_builder.sample_file_name(pad.get("source"), f"pad {name}")
        except kit_builder.KitError as error:
            print(f"sample_generator: {error}", file=sys.stderr)
            return EXIT_FAILED
        destination = os.path.join(kit_dir, source)
        if os.path.lexists(destination) and (os.path.islink(destination) or not os.path.isfile(destination)):
            print(f"sample_generator: pad {name}: {source} is not a regular file; refusing to write over it", file=sys.stderr)
            return EXIT_FAILED
    for pad in sample_pads:
        path = os.path.join(kit_dir, pad["source"])
        write_wav(path, RECIPES[pad["name"]]())
        print(f"sample_generator: wrote {path}")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv))
