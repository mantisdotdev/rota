# Jam link — the wire format

Normative form of the jam link's SysEx (PRD §11, D-111, D-114), the counterpart to
`share-format.md` for the cable rather than the QR. Two devices trade patterns as MIDI
System Exclusive, and the payload is an ordinary `RT2` share code (`share-format.md`),
so there is no second grammar to keep and no `RT3` to version. `io/midi.cpp` is the
envelope around that code; `firmware/src/io/midi.h` is the source of truth and this
file explains it. Golden messages are byte-identical across host and firmware builds,
and `tests/midi_test.cpp` asserts them (T-111).

## The envelope

```
F0  7D  'R'  'T'  01  <type>  <pad>  <RT2 code …>  F7
```

| Byte | Value | Why |
|---|---|---|
| `F0` | SysEx start | The parser starts a message here, and re-syncs here after any refusal. |
| `7D` | Manufacturer id | The id MIDI 1.0 reserves for non-commercial and educational use, and forbids on a shipped product (C-06). A real id is budgeted with the certification work (§7.7). |
| `52 54` | `'R' 'T'` | Marks the message as Rota's, so another maker's `7D` message is dropped rather than parsed. |
| `01` | Protocol version | A version this firmware does not know is refused, which is how a later firmware changes the format by refusing an older one. |
| `4C` / `54` | `'L'` / `'T'` | A whole loop, or one track. |
| `00`–`07` | Pad | The pad a track was sent from. A loop must name pad `00`; a track names `00`–`07`. Any other value is refused — this is the one field an untrusted wire controls, and it becomes an array index, so it is checked like every card string that reaches a path (D-109). |
| `RT2 code …` | The share code | A section code with the loop's own id after `~` (§10.2, D-105), so a receiver that keeps a whole loop can say what it is based on. A track message carries the same whole code and the receiver takes only the named pad; the seven others cost about 150 bytes on a gesture made a few times a minute, which buys one encoder path and one decoder path (D-111). |
| `F7` | SysEx end | A code past the engine's `kMaxSectionCodeInput` cap (D-106) is dropped while the parser counts on to here, so the read stays bounded and the stream re-syncs. |

## Why nothing is escaped

Every byte of an `RT2` code is one of `A–Z a–z 0–9 : . , - ~ ; /` (§10), all in `2C`–`7E`.
So every payload byte has its top bit clear: none can be mistaken for a status byte,
none is `F7`, and none is `00`. The only bytes in a message with the top bit set are the
envelope's own `F0` and `F7`. A byte the wire corrupts into the `80`–`FF` range is not a
payload byte, so the parser skips it and the message survives; a stray `F0` restarts the
message and a stray `F7` ends it. MIDI's four System Real Time bytes (`F8`, `FA`, `FB`,
`FC`) may fall anywhere in a stream, including inside a SysEx; the HAL lifts them before
`io/midi` sees the wire (D-114), and the parser skips them regardless.

## Golden messages

Bytes in hex, `payload` shown as text. The id after `~` is FNV-1a over the bare code
(D-105), so it is fixed for a given loop.

### G-JAM-01 — an empty loop (64 bytes)

payload `RT2:lofi:100:10:2:0:15:cm:e1-e1-e1-e1-e1-e1-e1-e1~av0s9e`

```
F0 7D 52 54 01 4C 00 52 54 32 3A 6C 6F 66 69 3A 31 30 30 3A 31 30 3A 32 3A 30 3A
31 35 3A 63 6D 3A 65 31 2D 65 31 2D 65 31 2D 65 31 2D 65 31 2D 65 31 2D 65 31 2D
65 31 7E 61 76 30 73 39 65 F7
```

### G-JAM-02 — the classic beat, T-05 (76 bytes)

payload `RT2:lofi:100:10:2:0:15:cm:e10000-e1.0.0-e10000-e1-e1-e1-e1-e1~46uwma`

```
F0 7D 52 54 01 4C 00 52 54 32 3A 6C 6F 66 69 3A 31 30 30 3A 31 30 3A 32 3A 30 3A
31 35 3A 63 6D 3A 65 31 30 30 30 30 2D 65 31 2E 30 2E 30 2D 65 31 30 30 30 30 2D
65 31 2D 65 31 2D 65 31 2D 65 31 2D 65 31 7E 34 36 75 77 6D 61 F7
```

The same loop sent as **one track** from pad 3 is identical but for two header bytes:
`… 01 54 03 …` in place of `… 01 4C 00 …`.
