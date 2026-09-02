"""T-76 for the Python tools (spec/scenarios.md): sample_generator.py writes the lofi
kit's samples deterministically, and kit_builder.py refuses a WAV the engine could not
play. Standard library only, like the tools. CI runs it with

    python3 -m unittest discover -s tests -p 'tools_test.py'
"""

import contextlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest
import wave

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import kit_builder  # noqa: E402
import sample_generator  # noqa: E402

LOFI_KIT = os.path.join(REPO_ROOT, "spec", "kits", "lofi", "kit.json")
LOFI_HEADER = os.path.join(REPO_ROOT, "firmware", "src", "engine", "kits", "lofi.h")
SAMPLE_NAMES = ["kick", "snare", "hat", "clap", "rim"]
SAMPLE_RATE = 48000
MAX_SAMPLE_FRAMES = SAMPLE_RATE * 2
BANNER_LINES = 2  # the header's first two lines name the paths it was generated from and to


def read_wav(path):
    with wave.open(path, "rb") as sample:
        frames = sample.readframes(sample.getnframes())
        return sample.getnchannels(), sample.getsampwidth(), sample.getframerate(), frames


def write_wav(path, channels, rate, width, frames):
    with wave.open(path, "wb") as out:
        out.setnchannels(channels)
        out.setsampwidth(width)
        out.setframerate(rate)
        out.writeframes(b"\x00" * (frames * channels * width))


def quiet(function, *args):
    """Runs a tool's main() with its output captured; returns (exit code, stderr text)."""
    err = io.StringIO()
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
        code = function(*args)
    return code, err.getvalue()


class T76KitSamples(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp(prefix="rota-kit-")
        self.addCleanup(shutil.rmtree, self.folder, ignore_errors=True)
        with open(LOFI_KIT, encoding="utf-8") as source:
            self.kit = json.load(source)

    def write_kit(self, kit=None, name="kit.json"):
        path = os.path.join(self.folder, name)
        with open(path, "w", encoding="utf-8") as out:
            json.dump(kit or self.kit, out)
        return path

    def wavs_in_folder(self):
        return sorted(name for name in os.listdir(self.folder)
                      if name.endswith(".wav") and os.path.isfile(os.path.join(self.folder, name)) and not os.path.islink(os.path.join(self.folder, name)))

    def test_T76_the_generator_writes_the_five_samples_as_16_bit_48k_mono_under_two_seconds(self):
        code, _ = quiet(sample_generator.main, ["sample_generator.py", self.write_kit()])
        self.assertEqual(code, sample_generator.EXIT_OK)
        self.assertEqual(self.wavs_in_folder(), sorted(f"{name}.wav" for name in SAMPLE_NAMES))
        for name in SAMPLE_NAMES:
            channels, width, rate, frames = read_wav(os.path.join(self.folder, f"{name}.wav"))
            self.assertEqual((channels, width, rate), (1, 2, SAMPLE_RATE), name)
            self.assertTrue(1 <= len(frames) // 2 <= MAX_SAMPLE_FRAMES, name)

    def test_T76_the_generator_is_byte_identical_on_every_run(self):
        first = tempfile.mkdtemp(prefix="rota-kit-")
        self.addCleanup(shutil.rmtree, first, ignore_errors=True)
        shutil.copy(LOFI_KIT, os.path.join(first, "kit.json"))
        self.assertEqual(quiet(sample_generator.main, ["x", os.path.join(first, "kit.json")])[0], 0)
        self.assertEqual(quiet(sample_generator.main, ["x", self.write_kit()])[0], 0)
        for name in SAMPLE_NAMES:
            with open(os.path.join(first, f"{name}.wav"), "rb") as a, open(os.path.join(self.folder, f"{name}.wav"), "rb") as b:
                self.assertEqual(a.read(), b.read(), name)

    def test_T76_the_committed_samples_are_what_the_generator_makes(self):
        # Within one least significant bit: libm rounding may differ by a platform's last ulp.
        self.assertEqual(quiet(sample_generator.main, ["x", self.write_kit()])[0], 0)
        for name in SAMPLE_NAMES:
            fresh = read_wav(os.path.join(self.folder, f"{name}.wav"))[3]
            committed = read_wav(os.path.join(os.path.dirname(LOFI_KIT), f"{name}.wav"))[3]
            self.assertEqual(len(fresh), len(committed), name)
            count = len(fresh) // 2
            largest = max(abs(a - b) for a, b in zip(struct.unpack("<%dh" % count, fresh), struct.unpack("<%dh" % count, committed)))
            self.assertLessEqual(largest, 1, name)

    def test_T76_the_generator_refuses_a_pad_it_has_no_recipe_for_before_writing_anything(self):
        self.kit["pads"][7]["name"] = "cowbell"
        code, error = quiet(sample_generator.main, ["x", self.write_kit()])
        self.assertEqual(code, sample_generator.EXIT_FAILED)
        self.assertIn("no recipe for a sample pad named 'cowbell'", error)
        self.assertEqual(self.wavs_in_folder(), [])

    def test_T76_the_generator_refuses_a_source_that_is_not_a_bare_file_name(self):
        # On the last sample pad, so a check that came after writing would have left files behind.
        for source in ["../kick.wav", "sub/kick.wav", "", ".", "..", None]:
            self.kit["pads"][7]["source"] = source
            code, error = quiet(sample_generator.main, ["x", self.write_kit()])
            self.assertEqual(code, sample_generator.EXIT_FAILED, repr(source))
            self.assertIn("source must be a file name inside the kit folder", error)
            self.assertEqual(self.wavs_in_folder(), [], repr(source))

    def test_T76_the_generator_refuses_a_directory_or_a_link_at_a_source_before_writing_anything(self):
        os.mkdir(os.path.join(self.folder, "rim.wav"))  # the last sample pad
        code, error = quiet(sample_generator.main, ["x", self.write_kit()])
        self.assertEqual(code, sample_generator.EXIT_FAILED)
        self.assertIn("rim.wav is a directory or a link", error)
        self.assertEqual(self.wavs_in_folder(), [])
        os.rmdir(os.path.join(self.folder, "rim.wav"))
        outside = os.path.join(self.folder, "..", "outside-" + os.path.basename(self.folder) + ".wav")
        with open(outside, "wb") as target:
            target.write(b"untouched")
        self.addCleanup(os.remove, outside)
        os.symlink(outside, os.path.join(self.folder, "rim.wav"))
        code, error = quiet(sample_generator.main, ["x", self.write_kit()])
        self.assertEqual(code, sample_generator.EXIT_FAILED)
        self.assertIn("rim.wav is a directory or a link", error)
        self.assertEqual(self.wavs_in_folder(), [])
        with open(outside, "rb") as target:
            self.assertEqual(target.read(), b"untouched")

    def test_T76_the_builder_accepts_a_sample_of_exactly_two_seconds(self):
        write_wav(os.path.join(self.folder, "two.wav"), 1, SAMPLE_RATE, 2, MAX_SAMPLE_FRAMES)
        kit_builder.validate_sample(self.folder, "two.wav", "pad kick")  # no KitError

    def test_T76_the_builder_refuses_a_linked_or_nul_carrying_source(self):
        os.symlink(os.path.join(os.path.dirname(LOFI_KIT), "kick.wav"), os.path.join(self.folder, "linked.wav"))
        with self.assertRaises(kit_builder.KitError) as caught:
            kit_builder.validate_sample(self.folder, "linked.wav", "pad kick")
        self.assertIn("linked.wav is a link", str(caught.exception))
        with self.assertRaises(kit_builder.KitError) as caught:
            kit_builder.validate_sample(self.folder, "kick.wav\0", "pad kick")
        self.assertIn("pad kick", str(caught.exception))

    def test_T76_the_builder_accepts_the_kit_and_regenerates_the_checked_in_header(self):
        header = os.path.join(self.folder, "lofi.h")
        code, error = quiet(kit_builder.main, ["x", LOFI_KIT, header])
        self.assertEqual(code, kit_builder.EXIT_OK, error)
        with open(header, encoding="utf-8") as fresh, open(LOFI_HEADER, encoding="utf-8") as committed:
            self.assertEqual(fresh.read().splitlines()[BANNER_LINES:], committed.read().splitlines()[BANNER_LINES:])

    def test_T76_the_builder_refuses_a_sample_the_engine_could_not_play(self):
        write_wav(os.path.join(self.folder, "stereo.wav"), 2, SAMPLE_RATE, 2, 100)
        write_wav(os.path.join(self.folder, "cd.wav"), 1, 44100, 2, 100)
        write_wav(os.path.join(self.folder, "eight.wav"), 1, SAMPLE_RATE, 1, 100)
        write_wav(os.path.join(self.folder, "long.wav"), 1, SAMPLE_RATE, 2, MAX_SAMPLE_FRAMES + 1)
        write_wav(os.path.join(self.folder, "empty.wav"), 1, SAMPLE_RATE, 2, 0)
        with open(os.path.join(self.folder, "text.wav"), "w", encoding="utf-8") as out:
            out.write("not a wav")
        expected = {
            "stereo.wav": "must be 16-bit 48000 Hz mono, got 16-bit 48000 Hz 2 channel(s)",
            "cd.wav": "got 16-bit 44100 Hz 1 channel(s)",
            "eight.wav": "got 8-bit 48000 Hz 1 channel(s)",
            "long.wav": "must hold 1 to 96000 frames, got 96001",
            "empty.wav": "must hold 1 to 96000 frames, got 0",
            "text.wav": "file does not start with RIFF id",
            "missing.wav": "No such file",
            "../lofi/kick.wav": "source must be a file name inside the kit folder",
            ".": "source must be a file name inside the kit folder",
            "..": "source must be a file name inside the kit folder",
        }
        for source, reason in expected.items():
            with self.assertRaises(kit_builder.KitError, msg=source) as caught:
                kit_builder.validate_sample(self.folder, source, "pad kick")
            self.assertIn(reason, str(caught.exception), source)

    def test_T76_the_builder_exits_1_on_a_bad_sample_and_writes_no_header(self):
        write_wav(os.path.join(self.folder, "kick.wav"), 2, SAMPLE_RATE, 2, 100)
        for name in SAMPLE_NAMES[1:]:
            shutil.copy(os.path.join(os.path.dirname(LOFI_KIT), f"{name}.wav"), self.folder)
        header = os.path.join(self.folder, "out.h")
        code, error = quiet(kit_builder.main, ["x", self.write_kit(), header])
        self.assertEqual(code, kit_builder.EXIT_INVALID_KIT)
        self.assertIn("pad kick: kick.wav must be 16-bit 48000 Hz mono", error)
        self.assertFalse(os.path.exists(header))


if __name__ == "__main__":
    unittest.main()
