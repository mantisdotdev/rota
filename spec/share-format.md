# Share format (RT2)

Normative form of PRD §10, incorporating D-011 (swing in hundredths), D-013 (16-step cap and code length), D-014 (per-track values in tenths), D-018 (safe character set), D-020 (note positions), D-025 (song code), D-026 (unknown kit) and D-043 (the `RT2` prefix). The share code is the canonical description of a loop: the same code must load identically in the web player, on the device and in the test suite, and the golden codes in §7 must survive decode → encode byte-for-byte on every build (PRD §12 rule 3).

## 1. Versioning and character set

- Every code starts with a version prefix: `RT2` for a section, `RT2S` for a song.
- Decoders read the fields they know and ignore what they do not: extra `:`-separated fields after the tracks field, and characters after the four per-track modifier digits, are skipped without error (T-16).
- Any change to the meaning of an existing field or character is a new version (`RT3`), never a silent change to `RT2` (CLAUDE.md, Landmines). D-043, the rename from `PB2` to `RT2`, was the last pre-release change.
- **Character set (D-018, T-45):** a section code uses only `A–Z a–z 0–9 : . , - ~`; a song code adds `;` and `/`. Every one of these is allowed unencoded in a URL fragment by RFC 3986 and left alone by browsers, is a plain byte in a byte-mode QR, survives message auto-linking, and is a valid MIDI SysEx data byte. No code ever contains `#`, `|`, space or any other character.

## 2. Grammar

```
RT2:<kit>:<bpm>:<filter>:<fx>:<chance>:<swing>:<key>:<tracks>[~<lineage>]
```

| Field | Form | Range | Meaning |
|---|---|---|---|
| kit | `[a-z0-9]{1,12}` | — | kit id, e.g. `lofi` |
| bpm | integer | 60–180 | tempo (§6.3) |
| filter | integer | 0–10 | master low-pass in tenths; 10 = fully open |
| fx | integer | 0–10 | fx knob in tenths |
| chance | integer | 0–10 | global chance in tenths |
| swing | integer | 0–100 | swing in hundredths; lofi default 15 (D-011) |
| key | root + mode | roots `c cs d ds e f fs g gs a as b` (`s` = sharp); modes `m` minor, `M` major, `dor` dorian, `pm` pentatonic minor, `pM` pentatonic major | e.g. `cm` = C minor, `fsdor` = F# dorian |
| tracks | 8 track strings joined by `-` | always 8 | pad order kick, snare, hat, clap, bass, chord, pluck, rim |
| lineage | `~` + 6 base36 chars | optional | id of the loop this one was loaded from (§10.2) |

Roots are written with sharps only (`as`, never a flat), and the sharp is the letter `s`.

### Track string

```
<alt><speed><steps>[,<level><tone><send><chance>]
```

| Part | Characters | Meaning |
|---|---|---|
| alt | `e` every cycle · `a` even cycles · `b` odd cycles · `f` fourth (cycle index mod 4 == 3) | §6.2 |
| speed | `h` 0.5 · `1` 1 · `d` 2 | §6.2 |
| steps | 0–16 characters, one per step, in order | `.` = rest; otherwise base36 of `(hits − 1) × 8 + note`, with hits 1–4 and note 0–7 |
| modifiers | `,` + four base36 tenths | level, tone, send, chance; see §3 |

`note` is a position in the kit's chord progression (chord pad) or note sequence (pluck pad), 0–7 (D-020). Codes the device writes carry note 0 on drums and bass, whose pitch never comes from it: the bass is resolved at play time from the active chord (§6.4). A decoder accepts note 0–7 on every track, ignores it on drums and bass, and re-encodes it unchanged (D-039), which is how G-14 carries `v` on the bass and still round-trips. A position past the end of the kit's list plays position mod length and is stored and re-encoded unchanged (T-47).

An empty track is `<alt><speed>` with no step characters, e.g. `e1`. `-` never appears inside a track string, so the track separator is unambiguous.

Step character table (row = hits, column = note):

| hits \ note | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| 1 | `0` | `1` | `2` | `3` | `4` | `5` | `6` | `7` |
| 2 | `8` | `9` | `a` | `b` | `c` | `d` | `e` | `f` |
| 3 | `g` | `h` | `i` | `j` | `k` | `l` | `m` | `n` |
| 4 | `o` | `p` | `q` | `r` | `s` | `t` | `u` | `v` |

## 3. Per-track modifiers (D-014)

- Level, tone, send and track chance are stored in tenths (0.0–1.0 in steps of 0.1, §6.2) and written as one base36 digit each: `0`–`9` for 0.0–0.9, `a` for 1.0.
- Defaults: level `8` (0.8), tone `a` (1.0), chance `a` (1.0), send = the kit's default for that pad. **Kit defaults for these four values must be multiples of 0.1** (lofi sends: drums 0.1, chord 0.4, pluck 0.3, bass 0; Appendix A).
- The group is omitted when all four values equal their defaults, and written in full (all four digits) when any one differs. The encoder produces no other form, so decode → encode reproduces the input.
- Which kit's defaults apply is decided by the kit the loop is playing with. A code naming a kit the device or player lacks loads with the current kit, the status line says so, omitted groups take the current kit's defaults, and the loop encodes the current kit's id from then on (D-026, T-52).
- `mute` is transient and never encoded (§6.2).

## 4. Canonical form

Byte-identical round trips need exactly one spelling per state:

- Integers without leading zeros (`96`, not `096`; `0`, not `00`).
- Lowercase throughout, except the `RT2`/`RT2S` prefix, the `M` of the major modes (`M`, `pM`) and the arrangement letters `A`–`D`.
- Always 8 track strings, even when empty.
- Modifier group omitted iff all four values are at their defaults (§3).
- Lineage written only when the loop was loaded from a code that carried an id.
- No whitespace anywhere.

## 5. Song code (D-025)

```
RT2S:<A>;<B>;<C>;<D>/<arrangement>[~<lineage>]
```

- Each section is a full section body: everything after `RT2:` in a section code, without its lineage. One decoder serves both code kinds.
- All four sections are always written; an empty section is eight empty tracks with its globals.
- All four sections must name the same kit (the kit is a device setting, §9.4); otherwise the code does not load.
- The arrangement is 1–64 letters `A`–`D`, no separators; each letter plays that section for one cycle (§6.8, T-39). Anything else does not load (T-51).
- A song carries at most one lineage, after the arrangement.
- Song codes are never shown as a QR on the device; they are exported as text over USB (D-013). They have no length target beyond the fixed limits (at most 4 × 230 + 64 + 12 characters).

## 6. Length and QR (D-013)

- Typical loops stay under 200 characters — a design target, not a limit. G-04, the T-05 beat, is 61 characters.
- Worst case for the default kit is G-14: 16 steps on every track, all four modifiers written, swing `100`, key `csdor`, a lineage id. Arithmetic: header `RT2:lofi:180:10:10:10:100:csdor:` = 32; eight tracks of 2 + 16 + 5 = 23 each → 184; seven `-` → 191; `~zzzzzz` → 7. Total **230**. With a 12-character kit id: 238.
- The code is mixed case and uses `:`, `.`, `,`, `-` and `~`, so a QR must use byte mode: version 10, error level L holds 271 bytes. The share view draws it at 3 px per module: 57 modules = 171 px, 195 px with the 4-module quiet zone, inside the 240 px screen. The player URL prefix in the QR (§9.3) has the remaining 41 bytes, and no character needs percent-encoding (§1).

## 7. Golden codes

Each code is a test fixture: decode → encode must reproduce it byte-for-byte on host and firmware (T-15), and the decoded state must match its description. Never edit a golden code to make a test pass (CLAUDE.md, Verification). Globals not mentioned are at their defaults: lofi, 100 bpm, filter 1.0, fx 0.2, chance 0, swing 0.15, C minor.

### G-01 — empty loop, all defaults (49 characters)

`RT2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1`

All eight tracks empty; every alternation `every`, every speed 1, no per-track modifiers. Plays silence.

### G-02 — T-01, one kick

`RT2:lofi:100:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1`

Kick: one step, one hit, at 0. Everything else empty.

### G-03 — T-03, snare once

`RT2:lofi:100:10:2:0:15:cm:e1-e1.0-e1-e1-e1-e1-e1-e1`

Snare: rest then hit (`~ sd`); the hit is at 1/2.

### G-04 — T-05, the classic beat (61 characters)

`RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1`

Kick four hits at 0, 1/4, 1/2, 3/4; snare `~ sd ~ sd` at 1/4 and 3/4; hat four hits at the quarters.

### G-05 — T-06, hat split

`RT2:lofi:100:10:2:0:15:cm:e1-e1-e108-e1-e1-e1-e1-e1`

Hat: two steps; the second has hits = 2 (`8` = (2 − 1) × 8 + 0). Events at 0, 1/2, 3/4.

### G-06 — T-08 and T-30, alternations

`RT2:lofi:100:10:2:0:15:cm:e10-b1.0-e1-a1.0-e1-e1-e1-f10`

Kick every cycle at 0. Snare `~ sd` on odd cycles only (`b`). Clap `~ cp` on even cycles only (`a`). Rim one hit at 0 on cycles 3, 7, 11, … (`f`). Hat, bass, chord and pluck empty.

### G-07 — T-09, hat at speed 2

`RT2:lofi:100:10:2:0:15:cm:e1-e1-ed00-e1-e1-e1-e1-e1`

Hat: two steps at speed 2 (`d`), so the pair plays twice per cycle: 0, 1/4, 1/2, 3/4.

### G-08 — T-29, hat at speed 0.5

`RT2:lofi:100:10:2:0:15:cm:e1-e1-eh0000-e1-e1-e1-e1-e1`

Hat: four steps at speed 0.5 (`h`): steps 0–1 on even cycles, steps 2–3 on odd cycles, at 0 and 1/2 each cycle.

### G-09 — T-14, skip

`RT2:lofi:100:10:2:0:15:cm:e100.-e1-e1-e1-e1-e1-e1-e1`

Kick `bd bd ~`: three steps, hits at 0 and 1/3, rest at 2/3.

### G-10 — T-25, snare level

`RT2:lofi:100:10:2:0:15:cm:e1-e1.0.0,6a1a-e1-e1-e1-e1-e1-e1`

Snare `~ sd ~ sd` with level 0.6, tone 1.0, send 0.1 (the kit default for drums), chance 1.0. The group is written because level differs from its default 0.8.

### G-11 — the PRD §10.1 example

`RT2:lofi:96:10:3:2:15:cm:e108-e1.0.0-e10000-e1.0,7a9a-e1-e10123-e1-e1`

96 bpm, filter open, fx 0.3, chance 0.2, swing 0.15, C minor. Kick `bd bd*2` (events 0, 1/2, 3/4: kick ×2 then split). Snare `~ sd ~ sd`. Hat four hits. Clap `~ cp` with level 0.7, tone 1.0, send 0.9, chance 1.0. Bass empty. Chord four steps at positions 0–3 of the progression: Cm Ab Eb Bb. Pluck and rim empty. Every track is reachable by tapping.

### G-12 — T-31 and T-43, chord, bass and key

`RT2:lofi:90:10:2:0:15:am:e10-e1-e1-e1-e100-e10123-e1-e1`

90 bpm, A minor. Kick at 0. Bass two hits at 0 and 1/2 with no note of their own (`0`); each sounds the root of the chord active at that moment (A, then C). Chord four steps at positions 0–3 = i, VI, III, VII = Am, F, C, G. Change the key field to `cm` and the same positions play Cm, Ab, Eb, Bb.

### G-13 — non-default globals, sharp key, lineage

`RT2:lofi:128:6:5:3:0:fsdor:e10000-e1.0.0-e10000-e1.0.0-e1-e1-e1-e1~k9z2ab`

128 bpm, filter 0.6, fx 0.5, chance 0.3, swing 0, F# dorian. Kick and hat at the quarters; snare and clap on 2 and 4. Loaded from the loop with id `k9z2ab`.

### G-14 — worst case for the default kit (230 characters)

`RT2:lofi:180:10:10:10:100:csdor:fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhoooooooooooooooo,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-fhvvvvvvvvvvvvvvvv,7959-fhoooooooooooooooo,7959~zzzzzz`

180 bpm, filter, fx and chance all 1.0, swing 1.0, C# dorian. Every track: alternation fourth, speed 0.5, sixteen steps each with hits = 4 (`o` = note 0 on the drum tracks kick, snare, hat, clap and rim; `v` = position 7 on bass, chord and pluck), level 0.7, tone 0.9, send 0.5, chance 0.9. Lineage `zzzzzz`. A test asserts the encoded length is exactly 230.

### G-15 — T-51, a song

`RT2S:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e1-e1;lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e100-e10123-e10123-e1;lofi:100:10:2:0:15:cm:e10-e1.0-e100000-e1.0-e10-e101-e10123-e1;lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1/AABABBCD~k9z2ab`

Section A: the classic beat with bass ×2 and chord ×4 (lofi dice loop 1). Section B: A plus pluck ×4. Section C: a half-time sketch — kick once, snare on 3, five hats, clap on 3, bass once, chord ×2, pluck ×4 (lofi dice loop 2). Section D: empty. Plays A A B A B B C D, one cycle each, then loops. Loaded from the song with id `k9z2ab`. Every section is reachable by tapping.
