# Acceptance scenarios

Every behaviour in the PRD has one row here, and every engine test names the ID it covers (PRD §12 rule 2). T-01–T-24 are PRD §13 unchanged. T-25 onward were added on 2026-09-02 for §6 behaviours that had no scenario; the PRD section each comes from is in brackets.

Conventions: fractions are of one cycle; the default kit (lofi), C minor and 100 bpm are assumed unless stated; "tap ×n" means n taps on that pad starting from empty, following the kit's smart defaults (§6.6); per-track modifier strings are the share-code form from `spec/share-format.md` §3. IDs are never reused: retire a scenario by striking it through, not by deleting it.

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
| T-25 | Hold the snare pad (`~ sd ~ sd`) and press volume down twice (§6.2, §8.1, D-012) | Snare level 0.8 → 0.6 in state; the snare is audibly quieter; master volume unchanged; the share code carries it as the snare's per-track modifiers `,6a1a` and reloads to 0.6. |
| T-26 | Hold the hat pad and turn the filter knob one detent down (§6.2, §8.1) | Hat tone 1.0 → 0.9; global filter and every other track unchanged; hat modifiers `,891a`. |
| T-27 | Hold the clap pad and turn the fx knob one detent up (§6.2, §8.1) | Clap send 0.1 → 0.2; global fx unchanged; clap modifiers `,8a2a`. |
| T-28 | Global chance 1; hold the kick pad and turn the chance knob one detent down (§6.2, §6.5) | Kick track chance 0.9, so `p = 0.9` on the kick and `1.0` on every other track; kick modifiers `,8a19`. |
| T-29 | Hat `hh hh hh hh`, then hold hat + speed knob one detent down (§6.2) | Hat `speed = 0.5`: cycle 0 plays steps 0–1 at 0 and 1/2; cycle 1 plays steps 2–3 at 0 and 1/2; cycle 2 repeats cycle 0. Track string `eh0000`. |
| T-30 | Rim ×1, then swap three times (every → a → b → fourth) (§6.2) | Rim plays only on cycles 3, 7, 11, … (cycle index mod 4 == 3); the ring draws it at 30% opacity on the other cycles; track string `f10`. |
| T-31 | Chord ×4 (`Cm Ab Eb Bb`), then bass ×2 (§6.4, §8.4) | Bass events at 0 and 1/2; the first sounds C (root of Cm, the chord active at 0), the second Eb (root of Eb, active at 1/2). With the chord track emptied, both bass events sound the key root C. |
| T-32 | Hat ×1, ×2, ×3 (§6.6) | Tap 1: `hh hh`, events 0, 1/2. Tap 2: `hh hh hh hh`, events 0, 1/4, 1/2, 3/4. Tap 3 appends: five hits at 0, 1/5, 2/5, 3/5, 4/5. |
| T-33 | Clap ×1, ×2, ×3 (§6.6) | Tap 1: `~ cp`, event 1/2. Tap 2: `~ cp ~ cp`, events 1/4, 3/4. Tap 3 appends: `~ cp ~ cp cp`, events 1/5, 3/5, 4/5. |
| T-34 | 61 edits in section A, then undo 61 times (§6.7, §8.2) | The first 60 undos each restore the exact previous state, ending at the state after edit 1; the 61st undo changes nothing. Hold undo redoes edit 2. Section B's undo history is untouched throughout. |
| T-35 | Hold the kick pad for two cycles, then release (§6.2 `mute`) | No kick events while held; other tracks unaffected; on release the kick pattern returns unchanged; the share code never shows the mute. |
| T-36 | Any pattern, chance 0, seed 42 (§6.4) | Every first sub-hit has velocity in 0.9–1.0 after ±5% humanize; sub-hits after the first are ×0.8; ghosts (when chance > 0) are ×0.55; the same seed gives the same velocities on every run. |
| T-37 | Hat ×6 (eight steps, one per eighth), swing 0.15 (§6.3) | Events on odd eighths (1/8, 3/8, 5/8, 7/8) are delayed by 0.15 × 1/24 = 1/160 cycle (15 ms at 100 bpm); events on even eighths (0, 1/4, 1/2, 3/4) do not move; with swing 0 nothing moves. |
| T-38 | Kick with 16 steps, tap kick again (§6.1, D-013, D-024) | The tap adds nothing: still 16 steps, events unchanged; the audition sound still plays; the status line reads `kick is full`. |
| T-39 | Song with arrangement `AABABBCD` in song play mode (§6.8, D-025) | Sections play in the order A A B A B B C D, one cycle each, then loop; each change lands on a cycle boundary. |
| T-40 | Sections A and B both non-empty; switch A → B (§6.8) | B plays as it was; nothing is copied from A; the switch lands at the next cycle boundary (as T-17). |
| T-41 | Chord ×5 (§8.4, §6.4) | Steps get i, VI, III, VII, i: `Cm Ab Eb Bb Cm`; note indices 0, 1, 2, 3, 0 in the share code. |
| T-42 | Kick `bd bd`, chance 1, seed 42, 100 cycles (§6.5) | Ghosts land only at the step midpoints 1/4 and 3/4, at velocity ×0.55; the bass, chord and pluck tracks gain no ghosts in any cycle. |
| T-43 | Chord `Cm Ab Eb Bb`; change key from C minor to A minor in settings (§6.3, §8.4) | The chord track plays `Am F C G`; the bass follows the new roots; the share code's note indices stay `0123`; only the key field changes from `cm` to `am`. |
| T-44 | Pluck ×9 in C minor (§8.4, Appendix A, D-020) | Steps take lofi's pluck sequence in order — degrees `0 2 4 7 9 7 4 2`, sounding C Eb G C′ Eb′ C′ G Eb — and the ninth wraps to position 0 (C). Every note is in the scale. |
| T-45 | Encode any state, and every golden code (§10.1, D-018) | The string contains only `A–Z a–z 0–9 : . , - ~` (plus `;` and `/` in a song code); placed after `#` in a URL it needs no percent-encoding and `location.hash` returns it unchanged. |
| T-46 | Open the player from a `file://` URL with the network off, G-04 after the `#` (§9.5, §10.3, D-019) | The beat plays on the first pad tap; the page makes no external request; the player is one HTML file of at most 4 MB. |
| T-47 | Load a code with chord note `5` on lofi, whose progression has four entries (§8.4, D-020) | That step plays position 5 mod 4 = 1 (VI, Ab in C minor); the code re-encodes with `5` unchanged. |
| T-48 | Chord ×4 with root C in each of the five modes (Appendix B, D-021) | Chord roots: minor C Ab Eb Bb; major C G A F; dorian C F Bb F; pentatonic minor C Bb Eb F; pentatonic major C A D G. Every note of every chord is in the current scale; the bass plays the root degree. |
| T-49 | Hat `hh hh hh` at speed 0.5 (§6.2, D-022) | Cycle 0 plays events at 0 and 2/3; cycle 1 plays one event at 1/3; cycle 2 repeats cycle 0. |
| T-50 | Snare `~ sd`, chance 1, seed 42, 100 cycles (§6.5, D-023) | Ghosts land only at 1/4 (midpoint of the rest step) and 3/4 (midpoint of the hit step), at ×0.55 velocity. |
| T-51 | Encode a song: four full sections, arrangement `AABABBCD`, a lineage (§10.1, D-025; G-15) | Decode → encode reproduces it byte-for-byte. A song with 65 letters, a letter outside A–D, or sections naming different kits does not load; status `that code did not load`. |
| T-52 | Load `RT2:jazz:…` on a device or player that has only lofi (D-026) | The loop loads and plays with lofi; status `no kit jazz, using lofi`; omitted modifier groups take lofi's defaults; encoding now gives `RT2:lofi:…`. |
| T-53 | Kick ×4 and snare ×2 set, other tracks empty; press dice, then hold dice (§8.2, D-028) | Press: the six empty tracks are filled from one of lofi's three starting loops, chosen with the seeded PRNG; kick and snare untouched. Hold: all eight tracks are replaced from a fresh pick; undo restores the previous state. |
| T-54 | From the ring view press show twice, then A, A, B, A; then undo; then hold dice (§9.6, D-030) | The second show press opens the song view; the presses build the arrangement `AABA` without switching sections; undo leaves `AAB`; hold dice empties it and undo restores `AAB`. A 65th letter is refused and the status line reads `song is full`. |
| T-55 | Arrangement `AAB`; from the ring view hold A and press play; later press B (§6.8, D-030) | Song play starts from the top: A, A, B, one cycle each, looping, every change on a cycle boundary; the song view highlights the playing letter. Pressing B leaves song play and switches to section B at the next cycle. With an empty arrangement the gesture starts nothing and the status line reads `song is empty`. |
| T-56 | In the song view tap pad 2 (empty), edit section B, tap pad 1, then power off and on (§6.8, D-030) | Song 2 becomes a copy of song 1 (four sections and arrangement) on the first tap; the edit lands in song 2 only; song 1 plays back unchanged; nothing was saved by hand and the state survives the power cycle. |
| T-57 | Text view after chord ×4 in C minor and in C pentatonic minor (§9.2, D-031) | C minor shows `chord  Cm Ab Eb Bb`; C pentatonic minor shows `chord  C Bb Eb F`: a name is the root plus `m` for a minor triad and the root alone for any other chord. |
| T-58 | Text view after chord ×4 in keys `cm`, `em`, `csm`, `dsm`, `gsdor`, and pluck ×3 in `cm` and `gsdor` (§9.2, D-032) | `cm`: `Cm Ab Eb Bb`; `em`: `Em C G D`; `csm`: `C#m A E B` (C# minor, 4 sharps, beats Db minor, 8 flats); `dsm`: `Ebm Cb Gb Db` (6 sharps against 6 flats, tie → flats); `gsdor`: `Abm Db Gb Db` (Ab dorian, tie → flats); pluck in `cm`: `c5 eb5 g5`; pluck in `gsdor`: `ab5 cb6 eb6` (the octave belongs to the letter, as in scientific pitch notation: MIDI 83 spelled Cb is `cb6`, not `cb5`). The share code spells every one of these roots with `s`. |
| T-59 | Share a loop that was loaded from a code carrying id `k9z2ab`, then load the shared code on a second device (§9.3, §10.2) | Open, io/ scope, session 7: see Open points. The engine encodes whatever lineage the state holds. |
| T-60 | Load `RT2:lofi:096:010:02:00:015:cm:e10-e1-e1-e1-e1-e1-e1-e1`, whose numbers carry leading zeros (share-format §4, D-039) | Loads: bpm 96, filter 1.0, fx 0.2, chance 0, swing 0.15, kick at 0. Re-encodes canonically as `RT2:lofi:96:10:2:0:15:cm:e10-e1-e1-e1-e1-e1-e1-e1`; only a canonical code is promised a byte-identical round trip. |
| T-61 | Generate the lofi kit header from `spec/kits/lofi/kit.json` (§12 rule 6, Appendix A, D-027) | The header carries Appendix A exactly: pad names and sends (drums 0.1, bass 0, chord 0.4, pluck 0.3), synth octaves 2, 4 and 5, the §6.6 tap templates, the five progressions, the pluck sequence, three dice loops that each decode, swing 0.15, filter 1.0, fx 0.2, sidechain on at 5 dB with a 120 ms release. CI regenerates the header and fails on any difference. |

## Watch in testing

Design bets with a known fallback. Observe them in usability round 1 (PRD §14, phase 2); the fallback is written down so nobody re-derives it.

- **Song-view pads (D-030, T-54, T-56).** In the song view the pads switch songs instead of playing sounds. Watch whether testers tap a pad expecting a sound, and whether an accidental switch to an empty song — which copies the current one — confuses them (a duplicated song that looks like a lost one). Fallback, not implemented: hold a pad to switch songs, so a plain tap stays inert in the song view.

## Open points

A new gap goes here with its scenario ID until the PRD answers it. Everything else open on 2026-09-02 was decided the same day: see D-018–D-044 in `DECISIONS.md`.

- **T-59, lineage on share (io/, session 7).** §10.2 says the device generates a 6-char id for every loop that is shared and that loading a code stores its id as the child's lineage; `spec/share-format.md` §2 and §4 say `~id` names the loop this one was loaded from and is written only when the loop was loaded from a code that carried one. The two readings differ on what the share view writes after `~`: the shared loop's own fresh id (so the receiver's lineage names its parent, and every shared code carries an id) or the lineage the loop already holds (so a re-shared loop names its grandparent, and a loop made from scratch shares with no id). The engine encodes `State::lineage` as it stands; the id generator and the replace-on-share step are io/. Decide the reading, then fill T-59's expected column and add a golden code if the canonical form changes.
