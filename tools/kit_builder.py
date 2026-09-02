#!/usr/bin/env python3
"""Kit builder: converts a kit folder (kit.json plus 16-bit 48 kHz mono WAVs) into the
on-device format (PRD §12 rule 6). Placeholder: the on-device format is not defined yet."""

import sys

EXIT_NOT_IMPLEMENTED = 1


def main() -> int:
    print("kit_builder: placeholder, on-device kit format not defined yet", file=sys.stderr)
    return EXIT_NOT_IMPLEMENTED


if __name__ == "__main__":
    sys.exit(main())
