#!/usr/bin/env python3
"""Sample generator: synthesises placeholder drum and synth samples for a kit folder
(PRD §12). Placeholder: the kit format and sound character (Appendix D) come first."""

import sys

EXIT_NOT_IMPLEMENTED = 1


def main() -> int:
    print("sample_generator: placeholder, kit format not defined yet", file=sys.stderr)
    return EXIT_NOT_IMPLEMENTED


if __name__ == "__main__":
    sys.exit(main())
