#!/usr/bin/env python3
"""Kit builder: turns a kit folder's definition (spec/kits/<id>/kit.json, PRD Appendix
A) into the C++ header the engine compiles in (firmware/src/engine/kits/<id>.h), so
kits stay data and the engine holds no kit knowledge (PRD §12 rule 6). CI regenerates
the header and fails on any diff, so the two cannot drift.

    python3 tools/kit_builder.py spec/kits/lofi/kit.json firmware/src/engine/kits/lofi.h

The output is an aggregate initialiser for engine::Kit (engine/kit.h); field order
here must match that struct. Every value is validated against the ranges in PRD §6
and share-format §3 before anything is written, and every sample pad's WAV in the
kit folder is checked to be what sound/ plays: 16-bit 48 kHz mono PCM of at most two
seconds (D-081). Sample conversion into the on-device format comes later with io/.
"""

import json
import math
import os
import sys
import wave

PAD_ORDER = ["kick", "snare", "hat", "clap", "bass", "chord", "pluck", "rim"]
MODE_ORDER = ["minor", "major", "dorian", "pentatonic_minor", "pentatonic_major"]
MAX_STEPS_PER_TRACK = 16
MAX_TAP_TEMPLATES = 4
MAX_NOTE_SEQUENCE_LENGTH = 8
MAX_DICE_LOOPS = 4
KIT_ID_MAX_LENGTH = 12
KIT_ID_ALPHABET = set("abcdefghijklmnopqrstuvwxyz0123456789")
SAMPLE_RATE = 48000  # sound/limits.h kSampleRate (§7.4)
SAMPLE_WIDTH_BYTES = 2
MAX_SAMPLE_SECONDS = 2  # D-081: three kits' samples must fit the player's 4 MB (D-019)
MAX_SAMPLE_FRAMES = SAMPLE_RATE * MAX_SAMPLE_SECONDS
REST_TOKEN = "~"
EXACT_TENTHS_TOLERANCE = 1e-9

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXIT_OK = 0
EXIT_INVALID_KIT = 1
EXIT_USAGE = 2


class KitError(Exception):
    """A kit value the engine cannot represent."""


def tenths(value, what):
    """0.0–1.0 in steps of 0.1 → 0–10 (share-format §3, D-014)."""
    scaled = finite_number(value, what) * 10
    rounded = round(scaled)
    if abs(scaled - rounded) > EXACT_TENTHS_TOLERANCE or not 0 <= rounded <= 10:
        raise KitError(f"{what} must be a multiple of 0.1 in 0–1, got {value}")
    return int(rounded)


def hundredths(value, what):
    """0.0–1.0 in steps of 0.01 → 0–100 (D-011)."""
    scaled = finite_number(value, what) * 100
    rounded = round(scaled)
    if abs(scaled - rounded) > EXACT_TENTHS_TOLERANCE or not 0 <= rounded <= 100:
        raise KitError(f"{what} must be a multiple of 0.01 in 0–1, got {value}")
    return int(rounded)


def finite_number(value, what):
    """A JSON number C++ can hold: bools are not numbers, and Python reads NaN and
    Infinity from JSON, which the header would print as nanf or inff."""
    if type(value) not in (int, float) or not math.isfinite(value):
        raise KitError(f"{what} must be a finite number, got {value!r}")
    return float(value)


def exact_bool(value, what):
    """A JSON boolean: the string \"false\" is true to Python and would print as true."""
    if type(value) is not bool:
        raise KitError(f"{what} must be true or false, got {value!r}")
    return value


def exact_int(value, what):
    """A JSON integer. bool is an int subclass in Python, so isinstance would let
    `true` through and the header would say True."""
    if type(value) is not int:
        raise KitError(f"{what} must be an integer, got {value!r}")
    return value


def template_steps(notation, what):
    """Mini-notation from PRD §6.6 (`~ sd ~ sd`) → (hits, note) pairs: `~` is a rest,
    any other token a single hit at position 0."""
    tokens = notation.split()
    if not tokens or len(tokens) > MAX_STEPS_PER_TRACK:
        raise KitError(f"{what}: a template needs 1–{MAX_STEPS_PER_TRACK} steps, got {notation!r}")
    return [(0, 0) if token == REST_TOKEN else (1, 0) for token in tokens]


def degree_list(values, what):
    if not 1 <= len(values) <= MAX_NOTE_SEQUENCE_LENGTH:
        raise KitError(f"{what} needs 1–{MAX_NOTE_SEQUENCE_LENGTH} degrees, got {values}")
    for degree in values:
        if exact_int(degree, f"{what} degree") < 0:
            raise KitError(f"{what}: degrees are non-negative, got {degree!r}")
    return values


def validate_sample(kit_dir, source, what):
    """The WAV a sample pad names must sit in the kit folder and be 16-bit 48 kHz mono
    PCM, 1 to MAX_SAMPLE_FRAMES frames, so sound/ can play it as it is."""
    if not isinstance(source, str) or source in ("", ".", "..") or os.path.basename(source) != source:
        raise KitError(f"{what}: source must be a file name inside the kit folder, got {source!r}")
    path = os.path.join(kit_dir, source)
    try:
        with wave.open(path, "rb") as sample:
            channels = sample.getnchannels()
            width = sample.getsampwidth()
            rate = sample.getframerate()
            frames = sample.getnframes()
    except (OSError, EOFError, wave.Error) as error:
        raise KitError(f"{what}: {source}: {error}") from error
    if (channels, width, rate) != (1, SAMPLE_WIDTH_BYTES, SAMPLE_RATE):
        raise KitError(
            f"{what}: {source} must be 16-bit {SAMPLE_RATE} Hz mono, got {width * 8}-bit {rate} Hz {channels} channel(s)"
        )
    if not 1 <= frames <= MAX_SAMPLE_FRAMES:
        raise KitError(f"{what}: {source} must hold 1 to {MAX_SAMPLE_FRAMES} frames, got {frames}")


def repo_relative(path):
    """POSIX path relative to the repository root, whatever the working directory or
    the argv spelling, so the header banner is byte-identical everywhere."""
    return os.path.relpath(os.path.abspath(path), REPO_ROOT).replace(os.sep, "/")


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cpp_float(value):
    return f"{float(value)!r}f"  # always carries a decimal point: 0.0f, not 0f


def cpp_degree_list(values):
    return "{%d, {%s}}" % (len(values), ", ".join(str(v) for v in values))


def cpp_template(steps):
    inner = ", ".join("{%d, %d}" % step for step in steps)
    return "{%d, {%s}}" % (len(steps), inner)


def cpp_pad(pad, kit_dir, what):
    name = pad.get("name")
    voice = pad.get("voice")
    if voice not in ("sample", "synth"):
        raise KitError(f"{what}: voice must be sample or synth, got {voice!r}")
    if voice == "sample":
        source = pad["source"]
        validate_sample(kit_dir, source, what)
        pitch = exact_int(pad.get("pitch", 0), f"{what} pitch")
        start = finite_number(pad.get("start", 0), f"{what} start")
        decay = finite_number(pad.get("decay", 1.0), f"{what} decay")
        octave = 0
    else:
        source = pad["preset"]
        pitch, start, decay = 0, 0.0, 0.0
        octave = exact_int(pad["octave"], f"{what} octave")
    templates = pad.get("templates", [])
    if len(templates) > MAX_TAP_TEMPLATES:
        raise KitError(f"{what}: at most {MAX_TAP_TEMPLATES} templates, got {len(templates)}")
    template_items = [cpp_template(template_steps(t, what)) for t in templates]
    return (
        "        {%s, Voice::%s, %s, %d, %s, %s, %d, %d, %d,\n         {%s}},"
        % (
            cpp_string(name),
            voice,
            cpp_string(source),
            pitch,
            cpp_float(start),
            cpp_float(decay),
            octave,
            tenths(pad.get("send", 0), f"{what} send"),
            len(template_items),
            ", ".join(template_items),
        )
    )


def build_header(kit, source_path, output_path):
    kit_id = kit["id"]
    if not 1 <= len(kit_id) <= KIT_ID_MAX_LENGTH or set(kit_id) - KIT_ID_ALPHABET:
        raise KitError(f"kit id must be 1–{KIT_ID_MAX_LENGTH} of a–z 0–9, got {kit_id!r}")
    pads = kit["pads"]
    names = [pad.get("name") for pad in pads]
    if names != PAD_ORDER:
        raise KitError(f"pads must be exactly {PAD_ORDER} in order, got {names}")
    if kit.get("choke_groups", []) != []:
        raise KitError("choke groups are not supported yet (Appendix A defines none)")
    progressions = kit["progressions"]
    if sorted(progressions) != sorted(MODE_ORDER):
        raise KitError(f"progressions must cover exactly the modes {MODE_ORDER}")
    dice_loops = kit.get("dice_loops", [])
    if not 1 <= len(dice_loops) <= MAX_DICE_LOOPS:
        raise KitError(f"a kit defines 1–{MAX_DICE_LOOPS} dice loops (D-028), got {len(dice_loops)}")
    sidechain = kit["sidechain"]

    kit_dir = os.path.dirname(os.path.abspath(source_path))
    pad_lines = "\n".join(cpp_pad(pad, kit_dir, f"pad {pad.get('name')}") for pad in pads)
    progression_items = ", ".join(
        cpp_degree_list(degree_list(progressions[mode], f"{mode} progression")) for mode in MODE_ORDER
    )
    pluck = cpp_degree_list(degree_list(kit["pluck_sequence"], "pluck sequence"))
    loops = ",\n     ".join(cpp_string(code) for code in dice_loops)
    variable = "k" + kit_id[0].upper() + kit_id[1:]
    rel_source = repo_relative(source_path)
    rel_output = repo_relative(output_path)
    return f"""// Generated by tools/kit_builder.py from {rel_source}. Do not edit: change the
// json and rerun `python3 tools/kit_builder.py {rel_source} {rel_output}`.
#pragma once

#include "engine/kit.h"

namespace engine::kits {{

inline constexpr Kit {variable}{{
    {cpp_string(kit_id)},
    {{
{pad_lines}
    }},
    {{{progression_items}}},
    {pluck},
    {len(dice_loops)},
    {{{loops}}},
    {hundredths(kit["swing"], "swing")},
    {tenths(kit["filter"], "filter")},
    {tenths(kit["fx"], "fx")},
    {{{"true" if exact_bool(sidechain["on"], "sidechain on") else "false"}, {exact_int(sidechain["duck_db"], "sidechain duck_db")}, {exact_int(sidechain["release_ms"], "sidechain release_ms")}}},
}};

}}  // namespace engine::kits
"""


def main(argv):
    if len(argv) != 3:
        print("usage: kit_builder.py spec/kits/<id>/kit.json firmware/src/engine/kits/<id>.h", file=sys.stderr)
        return EXIT_USAGE
    source_path, output_path = argv[1], argv[2]
    try:
        with open(source_path, encoding="utf-8") as source:
            kit = json.load(source)
        header = build_header(kit, source_path, output_path)
    except (KitError, KeyError, ValueError, OSError) as error:
        print(f"kit_builder: {source_path}: {error}", file=sys.stderr)
        return EXIT_INVALID_KIT
    with open(output_path, "w", encoding="utf-8", newline="\n") as output:
        output.write(header)
    print(f"kit_builder: wrote {output_path}")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv))
