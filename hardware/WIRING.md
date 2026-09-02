# EVT wiring

How the EVT breadboard unit (hardware/BOM.md, Table 1) connects to the Teensy 4.1, as the firmware in `firmware/src/hal/teensy/` expects it. **Nothing in this file has been checked on a board.** Every pin number was read from the Teensy 4.1 pinout card, the audio shield's product page, the library documentation or a datasheet on 2026-09-02, and each row stays UNVERIFIED until the bring-up runbook flips it (CLAUDE.md, Landmines; D-089). `hal/teensy/pins.h` mirrors this table; change both together.

## Pins

| Signal | Teensy 4.1 pin | Goes to | Notes | Verified |
|---|---|---|---|---|
| I2S MCLK | 23 | Audio shield Rev D | Wired by the shield's header rows | no |
| I2S BCLK | 21 | Audio shield | " | no |
| I2S LRCLK | 20 | Audio shield | " | no |
| I2S TX (to codec DIN) | 7 | Audio shield | " | no |
| I2S RX (from codec DOUT) | 8 | Audio shield | Unused (no input yet) | no |
| I2C SDA | 18 | Audio shield SGTL5000 (0x0A), NeoTrellis 0x2E, NeoTrellis 0x2F | One bus, 400 kHz; pull-ups on the shield and the NeoTrellis boards (check the total) | no |
| I2C SCL | 19 | same | " | no |
| Display SCK | 27 (SCK1) | MSP2834 SCK | SPI1, dedicated to the display as ILI9341_T4 demands (D-047) | no |
| Display MOSI | 26 (MOSI1) | MSP2834 SDI | | no |
| Display MISO | 39 (MISO1) | MSP2834 SDO | Needed for the driver's vsync; 1 is the other MISO1 and is kept for Serial1 | no |
| Display CS | 36 | MSP2834 CS | Any pin will do for CS | no |
| Display DC | 38 (CS1) | MSP2834 DC | A hardware chip-select of SPI1, for the driver's fast path | no |
| Display RST | 37 | MSP2834 RESET | | no |
| Display backlight | 33 (PWM) | MSP2834 LED | Off at boot, full after init (D-052); the module's own series resistor sets the current | no |
| Pad keys and LEDs | I2C 0x2E | NeoTrellis 1, keys 0–7 | Top two rows: kick snare hat clap / bass chord pluck rim | no |
| Button keys | I2C 0x2F | NeoTrellis 2 (A0 jumper) | Row 0: split swap skip undo; row 1: dice show play (key 7 spare); row 2: A B C D; row 3 spare. Backlights not driven yet | no |
| Speed encoder A / B / push | 2 / 3 / 4 | PEC11R | 4 counts per detent; push to ground, internal pull-up | no |
| Filter encoder A / B / push | 5 / 9 / 14 | PEC11R | | no |
| FX encoder A / B / push | 16 / 17 / 22 | PEC11R | | no |
| Chance encoder A / B / push | 24 / 25 / 28 | PEC11R | | no |
| Volume encoder A / B / push | 29 / 30 / 31 | PEC11R (D-054) | | no |
| Battery sense | 40 (A16) | BAT+ through 100 kΩ, 100 kΩ to ground | 2:1 divider; a straight line 3.3–4.15 V → 0–100 % until the production gauge (D-072) | no |
| Headphone detect | 41 (A17) | Headphone jack tip switch, with a pull-up | Read as an analog level (D-065); the shield's own jack has no switch, so this is the panel jack | no |
| MIDI in / out | 0 (RX1) / 1 (TX1) | H11L1M circuit / 33 Ω + 10 Ω | Reserved for io/; not driven | no |
| Sync in / out | 32 / 34 | Sync jack through a divider / a series resistor | Reserved for io/; not driven | no |
| microSD | built-in slot (SDIO) | SanDisk Ultra, FAT32 | `SD.begin(BUILTIN_SDCARD)` | no |
| PSRAM, QSPI flash | bottom pads | ESP-PSRAM64H on the small pads, W25Q128JVSIQ on the large | Not used by the firmware yet | no |
| Speaker amp shutdown | none | PAM8302 SD from the headphone jack's switch | Hardware mute (D-050); no GPIO | no |
| Left alone | 6, 10, 11, 12, 13, 15 | Audio shield's flash and SD sockets, volume pot | Routed by the shield; unpopulated | no |

## Firmware settings that go with the wiring

- Audio at 48 kHz: `platformio.ini` sets `AUDIO_SAMPLE_RATE_EXACT=48000.0f` and `hal/teensy/audio_teensy.cpp` writes the SGTL5000's CHIP_CLK_CTRL to 48 kHz, 256 × Fs (D-083). Headphone volume 0.6, line out at the driver's default 1.29 V p-p (D-050 says the amp's trim starts near minimum).
- Display: 30 MHz SPI, which is ILI9341_T4's default and what its author runs this controller at (the library reports about 45 full frames a second at 60 MHz; BOM, "Noticed while researching"); the ILI9341 datasheet's 100 ns write cycle is 10 MHz, at which a full frame takes 120 ms, so the bring-up test decides and 10 MHz is the fallback if the image tears or shifts. Rotation 1 (landscape; try 3 if the image is upside down), 60 Hz refresh, vsync spacing 1, diff gap 4.
- Memory (D-089): RAM2 holds the sound engine (207 KB), the model with its four sections and undo (85 KB) and the display driver's copy of the screen (150 KB), 460 KB in all with 64 KB spare; RAM1 holds the app's framebuffer (150 KB), the scheduler's event list (30 KB), the queues and the driver's two 6 KB diff buffers, 275 KB of variables beside 141 KB of code, leaving 85 KB for the stack. Two framebuffers in RAM2 overflowed it by 4.7 KB and the framebuffer in RAM1 with the model left 128 bytes of stack, so this split is the one that links; the `teensy_size` line of `pio run` is the check.
- Scheduler timer: an IntervalTimer every 2 ms at NVIC priority 224, under the audio library's update at 208.

## Not wired this session

The eleven button backlights (the second NeoTrellis's LEDs), MIDI, sync, the jam link, and kit samples from the card (io/). The sample pads are silent on the device until io/ reads the WAVs; the synth pads play.
