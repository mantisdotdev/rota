# Rota — Product Requirements Document

Working title: **Rota** (Pattern Box until D-043). Version 0.1, September 2026. Owner: [you].

This document is the source of truth for the product. It is written to be read by people and by the coding agent building it. When something here conflicts with code, the code is wrong. When something here is missing, add it here first, then build it. Decisions and their reasons live in `DECISIONS.md`.

---

## 1. One-line summary

A pocket instrument where you tap sounds onto a spinning ring and the loop stretches to fit. No grid, no wrong notes, no manual. Loops evolve on their own, can be sent to a friend as a line of text, and two devices lock together with one cable.

## 2. Why this exists

Small music devices today trade off against each other: toys are outgrown in a month, deep boxes intimidate people out of the box, and all of them use a fixed step grid that produces mechanical, samey beats. Live-coding environments (TidalCycles, Strudel) solved rhythm differently: a pattern always fits one cycle, and hits subdivide and squeeze to make room. That model gives triplets, polyrhythms and evolving loops for free, but it has only ever been available as text on a laptop.

Rota puts that model behind eight rubber pads and four knobs. The pattern model is what makes it feel alive; the constraints (one kit, one key, four knobs) are what make it feel easy.

## 3. Goals and success metrics

### Product goals
1. Anyone can make a loop they nod to within 60 seconds, with no instructions.
2. Loops sound produced by default (glue, swing, key lock) regardless of what the user does.
3. A whole loop is one short line of text that plays in any browser and loads on any device.
4. Two devices play together with zero setup.
5. The device grows with the user: the surface is simple, the text view underneath is deep.

### Usability metrics (measured in user tests, 10 people who have never made music)
| Metric | Target |
|---|---|
| Time to first loop the tester visibly nods to, no instructions | < 60 s median |
| Testers who discover split or swap unprompted within 10 min | ≥ 5 of 10 |
| Testers who ask "why did it move?" more than once | ≤ 2 of 10 |
| Testers still playing at 15 min without prompting | ≥ 7 of 10 |
| Testers who successfully share a loop to a phone | ≥ 8 of 10 |

### Business metrics (v1 launch)
- Crowdfunding campaign: 300–500 units.
- Unit cost at 500 units: ≤ $90. Retail: $179–199.
- One 30-second video shows: tapping, the loop stretching, chance evolving it, a second device joining, a QR code scanned.

## 4. Non-goals for v1

These are deliberately out. Do not build them, do not leave hooks for them unless free.
- Sample import from the user's own files.
- A step grid or any per-step editing view.
- More than one key/scale active at once. (Changing key is allowed; using two is not.)
- Effects menus, per-effect parameters, or an effects chain editor.
- Built-in microphone or audio input.
- Companion phone app. The web player is the companion.
- Wireless in v1 hardware (Bluetooth/WiFi). Jam is wired. Add radio in v2.
- Song-mode automation (parameter recording). Sections and live switching only.
- Polyphonic pad playing (pads are sequencer inputs, not a keyboard).

## 5. Users

**Primary: the curious non-musician.** Age 14–45, likes music, has never finished making any. Owns headphones and a phone. Buys it as a gift or on impulse after a video. Needs: sound good instantly, never feel stupid, have something to share.

**Secondary: the casual producer.** Owns a Pocket Operator, Volca, or a DAW they rarely open. Wants a sketchpad for the couch or a train. Needs: sync with existing gear, real audio outs, depth once the surface is learned.

**Tertiary: the live coder.** Uses Strudel or Tidal. Wants a physical instrument that speaks the same language. Needs: the text view, export to a laptop, MIDI clock. This user is small in number and loud; treat them as community, not as the design target.

---

## 6. The pattern model (engine semantics)

This is the core. It must be implemented as a pure library with no audio, UI, or platform code, so the same logic runs in the browser, in tests, and in firmware.

### 6.1 Definitions
- **Cycle**: one loop. Duration in seconds = `240 / bpm` (four beats). All timing is expressed as a fraction of the cycle in `[0, 1)`.
- **Track**: one per pad, eight total. A track has an ordered list of **steps** and a set of **modifiers**.
- **Step**: either a `hit` or a `rest`. Every step takes an equal share of the cycle: with `n` steps, step `i` spans `[i/n, (i+1)/n)`.
- **Hit**: has `hits ∈ {1,2,3,4}` (subdivision within its step) and an optional `note` index. The `k`-th sub-hit of step `i` fires at `(i + k/hits) / n`.
- **Rest**: silent. Exists so backbeats and space are possible. Shown as `~`.

Adding a step to a track redistributes all existing steps evenly. This is the elastic property and is the whole point; do not "fix" it.

A track holds at most 16 steps (§12 rule 4, D-013). A tap on a full track adds nothing: the audition sound still plays and the status line says `kick is full` (D-024).

### 6.2 Track modifiers
| Modifier | Values | Meaning |
|---|---|---|
| `alt` | `every`, `a`, `b`, `fourth` | Play every cycle; only on even cycles; only on odd cycles; only every fourth cycle (cycle index mod 4 == 3). `a` and `b` are how two tracks "take turns". |
| `speed` | `0.5`, `1`, `2` | `2`: the step list plays twice per cycle. `0.5`: the step list is stretched over two cycles: step `i` of `n` spans `[2i/n, 2(i+1)/n)` of the pair, so even cycles play the events that fall in the first cycle and odd cycles the rest (D-022). |
| `level` | 0–1 in tenths | Track gain. Default 0.8. |
| `tone` | 0–1 in tenths | Per-track low-pass. Default 1 (open). |
| `send` | 0–1 in tenths | Per-track FX send. Default = kit default for that pad. |
| `chance` | 0–1 in tenths | Per-track chance, multiplied with global chance. Default 1. |
| `mute` | bool | Transient, not saved. Set while a pad is held. |

Level, tone, send and chance are stored in tenths: 0.0–1.0 in steps of 0.1. One encoder detent, or one volume press, while holding a pad changes the value by 0.1 (§8.1). Kit defaults for these four must be multiples of 0.1 so they survive the share code unchanged (§10.1, D-014).

### 6.3 Global parameters
| Param | Range | Default | Notes |
|---|---|---|---|
| `bpm` | 60–180 | 100 | Speed knob. Tempo changes do not change pitch. |
| `filter` | 0–1 | 1 | Master low-pass, 220 Hz at 0 to fully open at 1, log curve. |
| `fx` | 0–1 | 0.2 | Scales the delay and reverb sends together. Delay time locks to tempo (dotted eighth). |
| `chance` | 0–1 | 0 | See 6.5. |
| `swing` | 0–1 | kit default (≈0.15) | Hidden. Delays events on odd eighth-note positions by `swing × cycle/24`. |
| `key` | 12 roots × {minor, major, dorian, pentatonic minor, pentatonic major} | C minor | Hidden in settings. |

### 6.4 Events
The engine exposes one function: `events(state, cycleIndex) → [{trackIndex, time (fraction), note, velocity, subIndex, isGhost}]`, sorted by time. This function is deterministic except for chance, which takes a seed so tests are reproducible.

Velocity: base 0.9–1.0 with ±5% humanize; sub-hits after the first are ×0.8; ghosts are ×0.55.

Bass notes are resolved at event time: the bass plays the root of whichever chord step is active at that fraction. If the chord track is empty, it plays the key root.

### 6.5 Chance
With global chance `c` and track chance `t`, `p = c × t`:
- Each scheduled hit is dropped with probability `0.6p`.
- On drum tracks (kick, snare, hat, clap, rim), a ghost hit is added at the midpoint of each step, rest steps included (D-023), with probability `0.3p`.
- Melodic tracks never gain ghosts. Chords are never dropped when `p < 0.5` (dropping chords sounds broken; dropping drums sounds musical).
- Chance at 0 must restore the exact authored pattern. Chance is a live performance control, not an edit.

### 6.6 Smart defaults (kit-defined first taps)
A kit may define a template sequence for the first N taps of a pad so the obvious beats are one tap away. Taps beyond the template append a plain hit.

Default kit:
| Pad | Tap 1 | Tap 2 | Tap 3 | Tap 4 |
|---|---|---|---|---|
| kick | `bd` | `bd bd` | `bd bd bd` | `bd bd bd bd` |
| snare | `~ sd` | `~ sd ~ sd` | append | append |
| hat | `hh hh` | `hh hh hh hh` | append | append |
| clap | `~ cp` | `~ cp ~ cp` | append | append |
| others | `x` | append | ... | ... |

This means: kick once = beat 1; snare once = beat 3 (half-time); snare twice = 2 and 4; kick four times = four on the floor. The user can always use skip to add rests explicitly, and undo to get out of the template.

### 6.7 Edit commit timing
Edits (add, remove, split, skip, swap, speed) are applied at the next beat boundary (quarter cycle), not instantly, so an edit never lands as a stumble mid-hit. The pad still auditions the sound instantly on tap for feedback. This is a default, not a law: it is the first thing to A/B in user testing (see `DECISIONS.md` D-003).

### 6.8 Sections and songs
- A **section** is a full state (8 tracks + global params). There are four, A–D.
- Switching to an empty section copies the current section into it.
- A **song** holds sections A–D plus an arrangement string of 1–64 letters A–D with no separators, e.g. `AABABBCD`; each letter plays that section for one cycle (D-025). There are eight songs on the device, one per pad; everything is saved as it is made, and switching to an empty song copies the current one into it, exactly as sections do (D-030). A song code (§10.1) loads into the current song.
- The arrangement is built in the song view (§9.6) by pressing section buttons, one letter per press. Song play mode steps through the arrangement; it starts with hold a section button + play (or play in the song view), play stops it, and pressing a section button during song play returns to live mode at that section. Live mode lets the user press section buttons on the beat and the change lands at the next cycle.

---

## 7. Hardware (v1)

### 7.1 Form factor
- Landscape handheld, approximately 150 × 80 × 20 mm, 200–250 g with battery.
- Held in two hands like a controller: thumbs on pads, index fingers on knobs.
- Matte plastic body, rounded corners with crisp edges, small silkscreen legends. One accent colour per colourway. First colourway: cream body, dark pads, orange knob caps and split/swap buttons.
- All ports on the top edge. Nothing on the sides or bottom edge where hands are.
- Lanyard hole. Rubber feet.

### 7.2 Controls
| Control | Count | Spec |
|---|---|---|
| Pads | 8 | Silicone, ~16 mm square, RGB LED under each, tactile but quiet. Velocity not required. |
| Knobs | 4 | Endless encoders with detents (speed, filter, fx, chance), coloured caps. Encoders, not pots, because values are saved and recalled per section. |
| Volume | 1 | Small side-edge thumbwheel or up/down rocker on top edge. Hardware, not a menu. While a pad is held it adjusts that pad's track level instead of master volume (§8.1, D-012). |
| Round buttons | 7 | split, swap, skip, undo, dice, show, play. Rubber, backlit where stateful (split, swap, show, play). |
| Section buttons | 4 | A, B, C, D. Small square, backlit. |
| Power | 1 | Slide switch. Instant on. |

### 7.3 Screen
- 2.8" IPS, 320×240 (or 320×320 square if the ring composition is better), SPI with DMA, 60 fps capable for the ring animation.
- Shows: the ring, bpm, section, battery, transient status text, and the text/share/settings views.

### 7.4 Audio
- 48 kHz, 16-bit stereo codec with headphone amp (≥ 30 mW into 32 Ω, low noise floor) and a separate line output.
- Mono class-D amp, 1–2 W, into a 28–36 mm speaker in a sealed pocket. Speaker path applies a high-pass at ~150 Hz and −3 dB; headphone/line paths are full range.
- Headphone insert detection mutes the speaker.
- Output latency target from event to sound: < 8 ms.

### 7.5 Compute and storage
- MCU: NXP i.MX RT1062 class (600 MHz Cortex-M7, as used on Teensy 4.1) or STM32H7. Prototype on a Teensy 4.1 so firmware carries straight to the custom board.
- 8–16 MB QSPI flash for firmware and kits; 8 MB PSRAM for sample voices; microSD for kits, songs and recordings.
- Polyphony: 16 voices at 48 kHz with the effects chain, measured with the busiest test scenario (§13, T-12).

### 7.6 Connectivity
| Port | Purpose |
|---|---|
| USB-C | Charging (5 V, 1 A), USB mass storage (kits, songs, recordings, firmware), USB MIDI, class-compliant audio out (nice-to-have). |
| 3.5 mm headphone | Stereo, with detect. |
| 3.5 mm line out | Stereo, fixed level. |
| 3.5 mm sync in / out | Pocket Operator–compatible pulse clock (in and out on one TRS, PO convention). |
| 3.5 mm MIDI in (TRS type A) | MIDI clock. |
| 3.5 mm MIDI out (TRS type A) | MIDI clock. The jam link uses both jacks and two cables (§11, D-111). |

### 7.7 Power
- 2500 mAh Li-Po, target 20 hours of play on headphones, 10 on speaker.
- Sleep after 10 minutes idle; state preserved; wake instantly on any button.
- Battery percentage on screen. Charging indicator on the power LED.
- Certification path: modular pre-certified components where possible; UN38.3 and IEC 62133 for the battery; FCC/CE/IC/RoHS. Budget in the plan, not an afterthought.

### 7.8 Enclosure
- Prototype: SLA/FDM prints. Pilot run (≤ 500): CNC or low-volume tooling. Injection molding only past ~1,000 units.
- Translucent "clear" colourway deferred until the PCB layout is final, since the internals become the design.

---

## 8. Interaction

### 8.1 The rule that makes it learnable
There is one gesture grammar and it never changes:
- **Tap** a pad: add a hit (following kit smart defaults).
- **Hold** a pad: mute it while held. The DJ kill.
- **Hold a pad + turn a knob or press volume**: apply that control to that sound only. Speed → track speed (0.5 / 1 / 2). Filter → track tone. FX → track send. Chance → track chance. Volume → track level (D-012).
- **Hold a pad + press a round button**: apply that button to that pad. (Split, swap and skip work with tap-then-pad too; see below.)
- **Hold a section button + press play**: play the song from the top (D-030). In the song view (§9.6) the pads are the eight songs and A–D add one cycle of that section to the arrangement.

### 8.2 Buttons
| Button | Press | Hold |
|---|---|---|
| split | Arms. Next pad tap: the last step's `hits` cycles 1→2→3→4→1. | While held: roll (retrigger the pad's sound at 1/16 cycle). Release restores. |
| swap | Arms. Next pad tap: `alt` cycles every → a → b → fourth → every. | — |
| skip | Arms. Next pad tap: appends a rest to that track. | — |
| undo | Undo last edit (60 levels per section; the arrangement has its own 60, §9.6). | Redo. |
| dice | Fills empty tracks from one of the kit's starting loops, picked with the seeded PRNG (D-028). Never overwrites tracks that have steps. | Clears every track and fills all eight from a fresh pick (with undo). In the song view: clears the arrangement (with undo). |
| show | Cycles the views: ring → text (§9.2) → song (§9.6) → ring. | Opens share view with QR (§9.3). While held, pads and dice send to a linked device (§11). |
| play | Play / stop. In the song view: play the song from the top. | Tap-tempo mode: tap play in rhythm for 4 taps to set bpm. |
| A–D | Switch section at the next cycle boundary. Empty sections copy the current one. In the song view: add one cycle of that section to the arrangement. | Hold two: swap their contents. Hold one + press play: play the song from the top (D-030). |

Armed buttons glow; a second press disarms. Arming times out after 5 s.

Removing a hit: hold the pad and press undo removes that track's last step (not the global last action). Pads with no steps go dark.

### 8.3 Knobs (global)
Speed, filter, fx, chance as in §6.3. Turning a knob shows its value on screen for 1 s. Encoders have detents; filter and fx have a soft "centre" detent at the kit default.

### 8.4 Melodic pads
- Bass, chord and pluck are locked to the current key. Every playable note is in the scale; there is no wrong note.
- Chord pad: each new step gets the next chord in the kit's progression (default: i, VI, III, VII). No per-step chord editing in v1 (D-006).
- A melodic step stores a position 0–7 in the kit's progression (chord) or note sequence (pluck). Taps assign positions in order and wrap at the sequence length; a position past the end plays position mod length (D-020). Chords are built from scale degrees (Appendix B, D-021).
- Bass follows the chord root automatically. Pluck steps cycle through a kit-defined pleasing note sequence (lofi: Appendix A).
- Key change lives in settings, not on the surface.

### 8.5 First run
On first boot, a 40-second interactive tutorial runs on screen:
1. "Tap the kick." (waits)
2. "Tap it again. See it stretch?"
3. "Tap the snare."
4. "Now turn chance."
5. "Hold show to share it."
6. "Press show twice and tap A A B A. Hold A and press play. That's a song."
It can be skipped with play and re-run from settings. No printed manual; the box has a single card with the gesture grammar (§8.1).

### 8.6 Accessibility
- Track colours differ in hue and in ring position; every dot also differs in radius by sub-hit, so patterns are readable without colour.
- All stateful buttons are backlit; all states are also shown on screen.
- Minimum text on screen 12 px at 320×240.

---

## 9. Screen views

### 9.1 Ring (default)
- Eight concentric bands, outermost = kick, innermost = rim. Each band draws faint dividers at step boundaries and dots at hits. Rests draw nothing. Inactive alternations draw at 30% opacity.
- Playhead sweeps clockwise from 12 o'clock, accent coloured.
- Hits flash and swell for 250 ms when they fire, so the eye follows the ear.
- Corners: bpm (top-left), section letter, song number and battery (top-right), transient status text (bottom-left, 1.8 s).
- Turning a knob overlays the value for 1 s.

### 9.2 Text view (show)
One line per non-empty track in a mini-notation that reads like Strudel:
```
kick   bd bd bd
snare  ~ sd ~ sd
hat    hh hh hh hh*2
clap   <~ cp ~ cp ~>        (alt: b)
chord  Cm Ab Eb Bb
bass   bass bass
```
Melodic lines show chord names, `bass` (its pitch follows the chord) and pluck note names. A chord name is the root plus `m` when the notes above the root are a minor third and a fifth; any other chord, including the quartal chords of the pentatonic modes, shows the root alone (D-031). Pitches are spelled by the key signature of the current key (D-032): a seven-note mode uses each letter name once; a pentatonic mode is spelled like its parent (pentatonic minor as natural minor, pentatonic major as major); dorian takes the signature of the major key a whole tone below its root; when the root has two names (`cs` is C# or Db) the spelling whose signature has fewer accidentals wins, and a tie goes to flats. Pluck notes use the same spelling, lowercase with the octave of scientific pitch notation, which belongs to the letter (`eb5`; MIDI 83 spelled Cb is `cb6`). Codes are unchanged: roots stay in sharps (`cs`). Read-only on the device in v1. The purpose is discovery and export, not editing.

### 9.3 Share view (hold show)
- Shows the loop's share code (§10) and a QR encoding `https://<player>/#<code>`.
- Scanning plays the loop in the web player with no device. The player has a "send to my device" action via USB or a "copy code" button.
- Also shows a short lineage line if the loop was loaded from someone else's code ("based on a loop by Mia").

### 9.4 Settings (hold undo + show together)
Key and scale, swing, kit selection, screen brightness, sleep timer, MIDI clock in/out on/off, sync in/out on/off, firmware version, re-run tutorial, factory reset.

### 9.5 Web player and simulator

The player renders the device, not a web page. A visitor should see a cream instrument sitting on a neutral backdrop, nothing else: no header, no navigation, no marketing copy, no explanatory panels. One line of hint text below the device is the only prose on the page.

- Same controls, same layout, same legends as the hardware (§7.2, Appendix D). Pads, knobs, buttons and the screen are drawn as physical parts with press states, not as form controls.
- Knobs turn by vertical drag or scroll wheel; they are also keyboard-focusable and respond to arrow keys.
- Pads respond to pointer down, not click, so they feel instant. Keys 1–8 are the pads; S, W, K arm split, swap, skip; Z undo; Shift+Z redo; E show; space play.
- Audio starts on the first pad tap. The first sound must play on that tap on iOS Safari as well as desktop.
- Output is limited so the first tap is never loud. Default master level −6 dB.
- Loads a share code from the URL fragment and plays it. Works offline: the whole player is one self-contained HTML file (D-019).
- Responsive: at phone width the knobs move below the screen and the pads become two rows of four; touch targets stay at least 44 px.
- The player page is the firmware's host build compiled to WebAssembly (Emscripten), with the device screen drawn to a canvas and wrapped in plain HTML and CSS. One build step, static output, no framework, no localStorage; the build inlines the WebAssembly and the kits' audio, and the file stays under 4 MB.

### 9.6 Song view (show, second press)

One screen, no sub-menus (D-030). Shows the song number (1–8), the arrangement as letters grouped in fours for reading (the code itself has no spaces), the playing letter highlighted during song play, eight song tiles laid out like the pads below the screen (empty, filled, current), and one hint: "A–D add · undo removes · hold dice clears · pads pick a song" (drawn as four short rows on the device, D-095). The hint shows until the player has added a letter or picked a song in this power cycle, then goes.

- A–D: add one cycle of that section to the arrangement, up to 64; then the status line reads "song is full". Sections do not switch.
- undo / hold undo: remove / restore the last letter; the arrangement has its own 60 levels.
- hold dice: clear the arrangement (with undo). A dice press does nothing; the status line reads "hold dice to clear".
- pads: tap one to switch to that song; an empty song becomes a copy of the current one. Everything is saved as it is made; there is no save gesture.
- play: play the song from the top; press again to stop. With an empty arrangement nothing starts and the status line reads "song is empty".
- show: back to the ring. Knobs, share and settings work as everywhere else; split, swap and skip are inactive here, and so are the pads' hold gestures, with one exception: holding a pad whose song file will not parse replaces that song with the one on screen, which a tap refuses to do (D-107).

---

## 10. Share format

The share code is the canonical description of a loop. The same code must load identically in the web player, the device and the test suite. Codes are versioned and forward-compatible: unknown fields are ignored.

### 10.1 Grammar (v2)
```
RT2:<kit>:<bpm>:<filter>:<fx>:<chance>:<swing>:<key>:<tracks>[~<lineage>]

kit      kit id, lowercase, ≤ 12 chars           e.g. lofi
bpm      integer 60–180
filter   integer 0–10 (tenths)
fx       integer 0–10
chance   integer 0–10
swing    integer 0–100 (hundredths)
key      root (c, cs, d, ds, e, f, fs, g, gs, a, as, b; s = sharp) + mode (m, M, dor, pm, pM)   e.g. cm, fsdor
tracks   8 track strings joined by -
lineage  6-char base36 id of the parent loop, optional
```
Track string:
```
<alt><speed><steps>
alt     e = every, a, b, f = fourth
speed   h = 0.5, 1 = 1, d = 2
steps   one char per step:
          '.'                       rest
          base36[(hits-1)*8 + note] hit; hits 1–4, note 0–7 (a position in the kit's progression or note sequence, §8.4; the device writes 0 on drums and bass, and a decoder accepts and keeps any value there)
```
Optional per-track modifiers (level/tone/send/chance) are appended after a `,` as four base36 tenths, omitted when default.

Codes use only `A–Z a–z 0–9 : . , - ~` (plus `;` and `/` in songs), so a code sits unchanged in a URL fragment, a byte-mode QR, a text message and a MIDI SysEx payload (D-018).

Example: `RT2:lofi:96:10:3:2:15:cm:e108-e1.0.0-e10000-e1.0,7a9a-e1-e10123-e1-e1`

A song is the four section bodies (everything after `RT2:`, without lineage) joined by `;`, then `/` and the arrangement, then an optional `~lineage`: `RT2S:<A>;<B>;<C>;<D>/AABABBCD`. All four sections are always written, an empty one as eight empty tracks; they must name the same kit; the arrangement is 1–64 letters A–D, one cycle each (D-025).

### 10.2 Constraints
- Typical loops stay under 200 characters (a design target, not a limit; the T-05 beat is about 60). The worst case for the default kit — 16 steps on every track, all four per-track modifiers written, swing 100, a sharp dorian key and a lineage id — is 230 characters (238 with a 12-character kit id; `spec/share-format.md` G-14 shows the arithmetic), which fits a byte-mode QR code at version 10-L (271 bytes). The share view renders that QR at 3 px per module: 57 modules = 171 px, 195 px with the quiet zone, inside the 240 px screen. The player URL prefix in the QR (§9.3) has the remaining 41 bytes.
- Song codes (`RT2S`) are never shown as a QR on the device; they are exported as text over USB only.
- The device generates a 6-char id for every loop that is shared; loading a code stores its id as the child's lineage.

### 10.3 Web player
- One self-contained HTML file with no external assets (D-019). Loads a code from the URL fragment and plays it with the same engine and the same kits (recorded samples inlined, web audio). A code for a kit the player lacks plays with the default kit and says so (D-026).
- Has the full device simulator so anyone can play without hardware. The simulator and the firmware are the same code (§12); when hardware behaves differently, the HAL implementation is wrong, not the app.
- "Copy code", "share link", "download 30 s WAV", "send to device" (WebUSB / mass storage).

---

## 11. Sync and jam

- **Clock out**: PO-style pulse on sync out and MIDI clock on MIDI out, always on while playing (configurable). A port being followed is never driven, so a follower still clocks everything downstream of it (D-113).
- **Clock in**: if a pulse or MIDI clock is present on the input, the device follows it; the speed knob then shows "ext".
- **Jam link**: two devices connected by two TRS MIDI cables, each device's out to the other's in (D-111). The device that pressed play first is the clock: a device that presses play having heard no clock becomes it, and one that presses play while clocks are arriving follows (D-113). Patterns are exchanged as SysEx containing the share code. Gestures:
  - Hold show + press a pad: send that pad's track to the other device (it lands on the same pad, replacing, with undo).
  - Hold show + press dice: send the whole loop.
- Jam must survive unplugging: each device keeps playing its own parts on its own clock.
- Two linked devices lock cycles, not only beats: the follower counts from the leader's Start and its first cycle begins on the leader's, so play may wait up to one cycle and counts in on play's backlight (D-112).
- Latency between linked devices ≤ 3 ms at the audio output.

---

## 12. Software architecture

One C++17 codebase compiles three ways: Teensy 4.1 firmware, a desktop simulator, and later WebAssembly for the web player. Everything device-specific sits behind a small hardware abstraction layer (HAL); everything above it is portable and is tested on the host.

```
/PRD.md                 this document
/DECISIONS.md           decision log (what, why, alternatives, date)
/spec/
  scenarios.md          acceptance scenarios in plain language (§13), one ID per future test
  share-format.md       §10 in normative form, plus golden codes
  kits/                 kit definitions (json) and sample manifests
/firmware/              PlatformIO project, environment teensy41
  src/engine/           pattern model, edits, events(), share codes, undo. Pure: no platform headers, no heap after init.
  src/sound/            voices, synths, effects, master chain. Block-based, no allocation in the audio path.
  src/ui/               framebuffer drawing, views, status text, LEDs
  src/io/               storage, share, MIDI, sync, jam link
  src/app/              scheduler, input grammar, sections, songs; glues the layers together
  src/hal/              the HAL interface and hal/teensy/ implementation
/host/                  CMake build of the same src with hal/sdl/ (window = screen, keyboard = controls, audio out)
/tests/                 doctest suites, run on the host
/tools/                 render (share code → WAV), kit builder, sample generator
/hardware/              BOM, wiring, assembly runbook, enclosure (OpenSCAD), production notes
/web/                   later: the host build compiled with Emscripten, plus the player page
```

Rules:
1. `engine/` and `sound/` include nothing from `hal/`, Arduino, Teensy, or SDL. They compile and run in tests on the host.
2. Every PRD behaviour has a scenario in `spec/scenarios.md` and a test that names the scenario ID.
3. Golden share codes in `spec/share-format.md` are byte-identical across host and firmware builds: decode → encode must reproduce the input.
4. No dynamic allocation after init anywhere in firmware. Fixed-size arrays with the limits in §6 (8 tracks, 16 steps per track, 4 sections, 8 songs, 64 arrangement entries, 8-entry note sequences, 4 dice loops per kit). No allocation, locks, or logging inside the audio callback.
5. The host simulator is the reference behaviour. If hardware differs, fix the HAL implementation, not the app.
6. Kit format: a folder with `kit.json` (pad assignments with sample settings or synth preset and octave, sends, choke groups, smart-default templates, a chord progression per mode, the pluck note sequence, up to four dice starting loops, swing, filter and fx defaults, sidechain; Appendix A is the reference) and 16-bit 48 kHz mono WAVs; a tool converts a kit folder into the on-device format. Open format so the community can make kits after v1.

### Sound engine spec
- Voices: sample playback with pitch, start, decay per pad; two synth voices (bass, chord/pluck) with kit-selectable presets.
- Per-track: level, tone (LPF), send.
- Master: LPF (filter knob) → tempo-locked delay + plate reverb sends (fx knob) → sidechain bus → glue compressor (≈4:1, 3 ms attack, 150 ms release) → soft clipper → limiter (−1 dBFS).
- Sidechain: each kick event ducks bass/chord/pluck by 4–6 dB with a 120 ms release. Kit-configurable, on by default.
- Choke groups: open hat is choked by closed hat; kit-defined.
- Humanize and swing as in §6.

---

## 13. Acceptance scenarios

Written so a person can verify them by ear and eye, and so they can be turned into engine tests directly. Fractions are of one cycle.

| ID | Scenario | Expected |
|---|---|---|
| T-01 | Tap kick once | One event at 0. |
| T-02 | Tap kick three times | Events at 0, 1/3, 2/3. Ring shows three dividers. |
| T-03 | Tap snare once (default kit) | Steps `~ sd`; one event at 1/2. |
| T-04 | Tap snare twice | Steps `~ sd ~ sd`; events at 1/4 and 3/4. |
| T-05 | Kick ×4, snare ×2, hat ×2 | Classic beat: kick 0,1/4,1/2,3/4; snare 1/4,3/4; hat 0,1/4,1/2,3/4 (templates: `hh hh` then `hh hh hh hh`). |
| T-06 | Hat ×1 (template `hh hh`) then split on hat | Steps `hh hh*2`; events at 0, 1/2, 3/4. |
| T-07 | Split pressed with no pad tap for 5 s | Disarms; status "add a hit first" only if a pad with no steps is tapped. |
| T-08 | Swap on clap (`a`), swap again (`b`) | `a` plays on cycles 0,2,4…; `b` on 1,3,5…; `fourth` on 3,7,11… |
| T-09 | Hold hat + speed knob one detent up | Hat track `speed = 2`; a two-step hat plays at 0,1/4,1/2,3/4. |
| T-10 | Chance at 0 after chance at 1 | Event list identical to the authored pattern. |
| T-11 | Chance 1, seed 42, 100 cycles | Drop rate 60% ± 5 on drums; ghost rate 30% ± 5; chords never dropped below p 0.5. |
| T-12 | Every pad ×4, all split ×4, chance 1, fx 1 | Firmware keeps 16 voices without dropouts at 48 kHz; CPU < 80%. |
| T-13 | Undo after any edit | Exact previous state, including templates. 60 levels. |
| T-14 | Skip on kick with `bd bd` | `bd bd ~`; events at 0, 1/3. |
| T-15 | Encode → decode → encode any state | Identical string. Includes all golden codes. |
| T-16 | Load a code with an unknown future field | Loads; unknown field ignored; no error. |
| T-17 | Switch to empty section B | B equals A. Switch lands at the next cycle boundary. |
| T-18 | Edit at cycle fraction 0.30 | Takes effect at 0.50 (next beat), audition sound plays at 0.30. |
| T-19 | Two devices linked, second presses play | Second device follows first's clock; playhead phase difference < 3 ms. |
| T-20 | Unplug jam cable mid-loop | Both keep playing, no glitch, each on its own clock. |
| T-21 | Headphones inserted | Speaker mutes within 50 ms; headphone path full range. |
| T-22 | First-boot tutorial | Completes in ≤ 45 s for a tester following prompts; skippable with play. |
| T-23 | Battery: headphones, 100 bpm, default kit, screen on | ≥ 20 h. |
| T-24 | Cold boot to playable | ≤ 1 s. |

---

## 14. Milestones

| Phase | Deliverable | Exit criterion |
|---|---|---|
| 0. Firmware and host simulator (now) | All portable code: engine, sound, UI, input grammar, io, HAL for Teensy and SDL, tools, tests, hardware documents. Runs on a laptop with keyboard input and real audio. | Engine scenarios pass on the host; the simulator plays T-05 from keyboard input; every golden code renders to WAV; firmware compiles for teensy41. |
| 1. EVT bring-up | Teensy 4.1 + audio shield + NeoTrellis + display + encoders on a breadboard in a printed shell, firmware flashed. | T-12, T-18, T-21, T-24 pass on hardware. |
| 2. Usability round 1 | 10 non-musician testers on the EVT unit, plus the host simulator on laptops. | Metrics in §3 met, or decisions logged for what to change. |
| 3. Web player | Host build compiled to WebAssembly; three recorded kits; QR share; WAV export; tutorial. | Loops shared by testers play on other people's phones. |
| 4. DVT | Custom PCB rev A/B, production pads and encoders, pre-compliance scan, battery test. | T-19, T-20, T-23 pass; pre-compliance has no major findings. |
| 5. Campaign | Video, page, 5 beta units in the community. | 300 pre-orders. |
| 6. PVT and ship | Pilot run, certification, packaging with the gesture card. | Units ship. |

---

## 15. Decisions log (seed for DECISIONS.md)

| ID | Decision | Why | Revisit when |
|---|---|---|---|
| D-001 | Elastic cycle model, not a step grid. | It's the only thing in the category that makes rhythm feel alive, and it's what makes sharing as text possible. | Never; this is the product. |
| D-002 | Rests via skip button plus kit smart defaults. | Without rests the backbeat is impossible; without smart defaults beginners hit that wall in minute one. | After usability round 1 if testers don't find skip. |
| D-003 | Edits commit on the next beat. | Avoids mid-hit stumbles. Unproven with users. | A/B in usability round 1 against instant commit. |
| D-004 | Hold pad + knob = per-track version of that knob. | One grammar to learn instead of menus. | If testers accidentally edit tracks while trying to mute. |
| D-005 | MCU firmware, not Linux. | Instant on, 20 h battery, sub-$90 BOM. Own engine makes it possible. | If v2 needs sample import or wireless-heavy features. |
| D-006 | No per-step chord editing in v1. | Keeps the melodic pads as simple as the drums. Progression is kit-defined. | If casual producers name it as the top missing feature. |
| D-007 | Jam over TRS MIDI cable, no radio. | Removes radio certification and pairing UX from v1. | v2. |
| D-008 | Own engine and syntax; Strudel-like text view for export only. | Independence, licensing clarity, and syntax designed for the device. | — |
| D-009 | Encoders, not pots. | Values are saved per section and must recall without jumps. | Cost review at DVT. |
| D-010 | One C++17 codebase behind a HAL: Teensy firmware, desktop SDL2 simulator, and later a WebAssembly web player. | No engine port and no drift between web and device; the device is the product, so its code comes first. | If the Emscripten build is too large or slow on phones. |

---

## 16. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| "Why did it move?" confuses beginners | Product fails the first minute | Smart defaults, ring animation, next-beat commit, tested in phase 1 before hardware money |
| Sounds thin compared to competitors | Reviews say "toy" | Recorded kits, glue chain, sidechain, real headphone amp; record all demos from line out |
| Firmware/web engine drift | Codes load differently on device and web | Golden codes, shared spec, 1:1 port, CI on both |
| Pad feel | Whole product feels cheap | Source three pad suppliers at EVT; test with users blind |
| Compliance surprises | Schedule slip 2–3 months | Pre-compliance scan at DVT; modular components; budget $20k |
| Crowded market | Nobody notices | The 30-second video: stretch, chance, second device, QR. If a feature isn't in the video it's not in v1 |

---

## 17. Appendix

### A. Default kit ("lofi")

The reference kit, complete enough to write `spec/kits/lofi/kit.json` from without guessing (§12 rule 6, D-027). Values not listed take the global defaults in §6.

| Pad | Voice | Source | Send |
|---|---|---|---|
| kick | sample | `kick.wav` | 0.1 |
| snare | sample | `snare.wav` | 0.1 |
| hat | sample | `hat.wav` (closed) | 0.1 |
| clap | sample | `clap.wav` | 0.1 |
| bass | synth | preset `sub-saw`, octave 2 | 0 |
| chord | synth | preset `warm-poly`, octave 4 | 0.4 |
| pluck | synth | preset `keys`, octave 5 | 0.3 |
| rim | sample | `rim.wav` | 0.1 |

- Samples: 16-bit 48 kHz mono WAV; per pad pitch 0 semitones, start 0, decay 1.0 (full length). Octaves are MIDI octaves (C4 = 60), so the bass root in C is C2.
- Choke groups: none. A future open hat joins a group with the closed hat and is choked by it.
- Smart-default tap templates: the table in §6.6.
- Swing 0.15. Filter default 1.0 and fx default 0.2 (the soft centre detents, §8.3).
- Sidechain: on; each kick event ducks bass, chord and pluck by 5 dB with a 120 ms release.
- Chord progressions, as scale degrees of the current mode (Appendix B): minor `0 5 2 6` (i VI III VII); major `0 4 5 3` (I V vi IV); dorian `0 3 6 3` (i IV VII IV); pentatonic minor `0 4 1 2`; pentatonic major `0 4 1 3`.
- Pluck sequence, as scale degrees with octave wrap: `0 2 4 7 9 7 4 2` (in C minor: C Eb G C′ Eb′ C′ G Eb).
- Dice starting loops (§8.2, D-028), as share codes whose tracks are used and whose globals are ignored:
  1. `RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e1-e1`
  2. `RT2:lofi:100:10:2:0:15:cm:e10-e1.0-e100000-e1.0-e10-e101-e10123-e1`
  3. `RT2:lofi:100:10:2:0:15:cm:e1008-e1.0.0-e1000000-b1.0.0-e10000-e10123-e101-e10.`

### B. Keys and scales
Roots: 12, written with sharps in codes (`c cs d ds e f fs g gs a as b`); the screen spells them by key signature (§9.2, D-032). Modes and their intervals in semitones above the root: natural minor (default) `0 2 3 5 7 8 10`; major `0 2 4 5 7 9 11`; dorian `0 2 3 5 7 9 10`; pentatonic minor `0 3 5 7 10`; pentatonic major `0 2 4 7 9`.

Melodic pitch is always a scale degree: degree `d` of an `n`-note mode is the `(d mod n)`-th interval raised by `⌊d / n⌋` octaves, so degree 7 in a 7-note mode is the root an octave up. A chord on degree `d` is degrees `d`, `d+2`, `d+4` (stacked thirds; quartal in the pentatonic modes) and its root is `d` (D-021). Kits give a progression of 1–8 degrees per mode and a pluck sequence of 1–8 degrees; melodic steps store positions in those lists (§8.4, D-020).

### C. Glossary
- **Cycle**: one loop.
- **Step**: an equal slice of the cycle on one track; a hit or a rest.
- **Split**: subdivide a step's hit into 2–4 faster hits.
- **Swap**: make a track take turns (play on alternate cycles).
- **Skip**: add a rest.
- **Chance**: how much the loop is allowed to change itself.
- **Kit**: a matched set of eight sounds, a key, a progression and defaults.
- **Code**: the one-line text that fully describes a loop.

### D. Design language

The device should read as a serious instrument and a toy at the same time: a matte pocket object with tiny printed legends, one bright accent, and nothing decorative. Reference the category of small matte-plastic pocket instruments in spirit; copy nothing.

**Palette (colourway "classic", the default)**
| Role | Hex |
|---|---|
| Body | #EAE3D1 |
| Body edge / shadow | #D8CFB9 |
| Pads and dark buttons | #2C2A27 |
| Accent (knob caps, split, swap, playhead, active states) | #F26B1D |
| Screen background | #15130F |
| Screen text | #F1E9D6 |
| Page backdrop (player only) | #CDC7B8 |
| Legends | #6B665C |

Track colours, kick to rim: #F26B1D, #F5B32B, #8FD3B0, #F0A3B8, #6C9BE8, #B79BEB, #5FD6CC, #E3DCC8. Exactly one accent per colourway; other colourways swap body, pad and accent only.

**Type.** One sans-serif for legends (system UI stack is fine). Monospace only on the device screen, because it is the screen's font: bpm, section letter, status text and the text view. Legends are lowercase, small, letter-spaced slightly, like silkscreen. No headings, no all-caps labels, no marketing sentences inside the device.

**Shape and depth.** Rounded corners with crisp edges: body radius about 28 px, pads about 9 px, knobs circular. Matte surfaces. Depth only where it means something: pads and buttons sit 3 px proud and drop 2 px when pressed; the screen sits in a dark bezel. No gradients as decoration, no shadow on every element, no glass.

**Layout (desktop, ASCII).**
```
+------------------------------------------------------+
|  [ screen: ring, bpm top-left, section+battery ]  (o) |
|  [                                              ]  (o) |
|  [                                              ]  (o) |
|  [                                              ]  (o) |
|  [1] [2] [3] [4] [5] [6] [7] [8]                       |
|  kick snare hat clap bass chord pluck rim              |
|  (split)(swap)(skip)(undo)(dice)  [A][B][C][D]  (show)(play) |
+------------------------------------------------------+
```
Screen left, knobs in a column on the right, pads in one row, round buttons below, sections between them. Everything left-aligned to the screen edge; the device is centred on the page.

**Overlays.** Transient text never covers what a view shows. The top row is the view's state: bpm or title at the left, the armed button in the accent, the ring's section, song and battery at the right. The bottom row is the message row: a knob's value for a second, else the status for 1.8 s, else a footer. The tutorial's prompt rows sit above the message row. A view lays out between those rows, and the ring shrinks to fit the room left.

**Motion.** Only in response to something: a pad pressing, a hit flashing and swelling for 250 ms, the playhead sweeping, a status line appearing for 1.8 s. No entrance animations, no hover effects, no ambient movement. Respect reduced-motion settings except for the playhead, which is information.

**Copy.** Status text is short, lowercase, and specific: "one kick", "3 hats, spread evenly", "takes turns", "copied into B", "that code did not load". It says what happened, never how the system works.

**Sound character.** Warm and slightly lo-fi rather than clean or bright. Drums punchy and short; chords soft and behind the drums; bass round with a little sub. Everything glued by the master chain so any combination of pads sounds finished. Nothing harsh at any knob position, and the speaker path never distorts.
