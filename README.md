# Rota

A pocket instrument where you tap sounds onto a spinning ring and the loop stretches to fit. No grid, no wrong notes, no manual. Loops evolve on their own, can be sent to a friend as a line of text, and two devices lock together with one cable.

This repository is the device: firmware for a Teensy 4.1, a desktop simulator built from the same code, tools, tests and hardware documents.

## How this repo is built

- [PRD.md](PRD.md) is the product spec and the source of truth. §6 is the engine semantics, §7 the hardware, §8 the input grammar, §9 the screen views, §10 the share format, §12 the architecture and its rules, §13 the acceptance scenarios. When code and the PRD disagree, the code is wrong.
- [DECISIONS.md](DECISIONS.md) says why things are the way they are: one numbered row per decision, with what was rejected and when to look again. A choice the PRD does not cover gets a row there before the code that depends on it.
- [spec/](spec/) holds every behaviour as a plain-language scenario with an ID that tests name ([scenarios.md](spec/scenarios.md)), the normative share-code grammar with its golden codes ([share-format.md](spec/share-format.md)), and the kits as data ([kits/lofi/kit.json](spec/kits/lofi/kit.json)), from which `tools/kit_builder.py` generates the header the engine compiles in.
- [CLAUDE.md](CLAUDE.md) is the team law for people and agents alike. Its commands are the only ones used, and a change is not finished until the relevant one has run:

```bash
cmake -S host -B build
cmake --build build
./build/tests
pio run -d firmware -e teensy41
./build/simulator
```

Layout in one line: `firmware/src/engine/` and `firmware/src/sound/` are portable and pure, `firmware/src/hal/` is the only platform code (`teensy/` for the device, `sdl/` for the simulator), `host/` is the CMake build of the same sources, `tests/` are doctest suites that run on the host, `tools/` are the kit builder, sample generator and WAV renderer.
