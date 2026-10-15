# Program file formats: implementation spec (TAP, TZX 1.20, SNA, Z80, raw binaries, ROM flash-load trap)

Status: research spec for `src/tape.c` and `src/snapshot.c`. Written 2026-10-14.
All multi-byte values are **little-endian** unless stated. "T" = Z80 T-state.
Hex dumps are space-separated bytes. Offsets are 0-based.

## 0. Sources (fetched 2026-10-14)

| Tag | Source |
|---|---|
| [TZX] | TZX format v1.20 (19 Dec 2006), https://worldofspectrum.net/TZXformat.html |
| [FAQ] | comp.sys.sinclair FAQ, file formats (TAP, SNA, SCR), https://worldofspectrum.org/faq/reference/formats.htm (`$Id: formats.htm,v 1.13 2005/09/18`) |
| [Z80] | Z80 file format, https://worldofspectrum.org/faq/reference/z80format.htm (`$Id: z80format.htm,v 1.12 2004/07/05`) |
| [WIKI-TAP] / [WIKI-SNA] / [WIKI-Z80] | https://sinclair.wiki.zxnet.co.uk/wiki/TAP_format , `.../SNA_format` , `.../Z80_format` |
| [LS] | libspectrum (Fuse), `git.code.sf.net/p/fuse-emulator/libspectrum`, commit `0cdee1fe` (2026-10-14): `tap.c`, `tzx_read.c`, `tape.c`, `tape_block.c`, `sna.c`, `z80.c`, `utilities.c` |
| [FUSE] | Fuse, `git.code.sf.net/p/fuse-emulator/fuse`, commit `abb1c502` (2026-10-12): `tape_traps.c`, `z80/z80.pl` |
| [PZXT] | pzxtools by Patrik Rak, https://github.com/raxoft/pzxtools commit `7bc8ff69`: `src/tzx.cpp`, `src/tap.h` (reference TZX-to-pulse renderer) |
| [ZX] | ZEsarUX, https://github.com/chernandezba/zesarux `src/storage/tape.c` (master, 2026-10-14) |
| [ROM] | Byte dumps of `roms/tk90x.rom` (CRC32 3e785f6f) and ZEsarUX `src/48.rom` (CRC32 ddee531f) |

ROM check done for this spec: bytes 0x04C2-0x0604 (SA-BYTES, SA/LD-RET, LD-BYTES, LD-EDGE) are
**identical** in `48.rom`, `tk90x.rom`, `tk90x_v1br.rom`, `tk90x_v2es.rom` and `tk95.rom`. The first
difference in 0x0605-0x0801 is at 0x078F-0x0790 (`LD DE,$09C0` becomes `LD DE,$3A66`: the TK ROMs
keep the "Program:/Bytes:" message table at a different address). **So every ROM address in this
document applies unchanged to the TK90X and TK95.**

### 0.1 Clock scaling (TK-specific)

TZX/TAP timings are defined in T-states of a **3.5 MHz** clock ([TZX] §2: "1 T state = (1/3500000)s").
The TK90X/TK95 runs at **3 575 611 Hz** (ARCHITECTURE.md). Tape pulses are real time, so convert:

```
emu_T = tzx_T * 3575611 / 3500000        (factor 1.0216032)
pause:  emu_T = ms * 3575.611
```

Do this with a running 64-bit accumulator (convert the *cumulative* tape time, then take the
difference) so rounding never drifts: `edge_at_emu = (cum_tzx_T * 3575611 + 1750000) / 3500000`.
The ROM loader tolerates far more than 2 % error, so even unscaled timings load; scaling just makes
real-time loading take the correct wall-clock time. All numbers below are in 3.5 MHz T-states.

---

## 1. Standard ROM tape encoding (the timings everything else refers to)

Source: [TZX] block 0x11 defaults and §5 table; [LS] `tape.c:87-92`, `tape_block.c:37-38,292-297`;
[PZXT] `tap.h:21-28`; ROM bytes at 0x04C2 (`21 80 1F` = LD HL,$1F80 / `21 98 0C` = LD HL,$0C98).

| Item | T-states (3.5 MHz) | Notes |
|---|---|---|
| Pilot pulse | 2168 | |
| Pilot pulse count, header (flag byte < 128) | 8063 | ~5.0 s. ROM SA-BYTES loads HL=$1F80=8064 |
| Pilot pulse count, data (flag byte >= 128) | 3223 | ~2.0 s. ROM uses $0C98=3224 |
| Sync 1 | 667 | |
| Sync 2 | 735 | |
| Bit 0 | 2 pulses x 855 | |
| Bit 1 | 2 pulses x 1710 | |
| Bit order | MSb first, bytes in file order | |
| Pause after block | 1000 ms default (TAP blocks always 1000 ms) | |
| (optional) tail pulse | 945 | pzxtools ends a ROM data block with one 945 T pulse before the pause ([PZXT] `tap.h:27`). Not required. |

Pilot count is chosen from **the first data byte** (flag): `data[0] & 0x80 ? 3223 : 8063`; an empty
block uses 8063 ([LS] `tape_block.c:294-297`).

Block layout on tape: pilot, sync1, sync2, then all bytes (flag, payload, checksum) as bits.

Duration of a ROM block = `pilots*2168 + 667 + 735 + Σbits (bit?3420:1710)`.
Example (the 19-byte header in §2.3): 17 789 786 T = 5.083 s; then the 4-byte data block: 7 084 626 T = 2.024 s.

---

## 2. TAP

Sources: [FAQ] "TAP Format"; [WIKI-TAP]; [LS] `tap.c:49-133`.

### 2.1 Container

```
repeat until EOF:
  WORD  len          number of bytes that follow (flag + payload + checksum)
  BYTE  data[len]    data[0] = flag, data[len-1] = checksum
```

* No file header, no magic. Files can be concatenated byte-wise.
* `flag`: 0x00 for a ROM header, 0xFF for a ROM data block; any value is legal (custom loaders).
* `checksum` = XOR of all bytes from flag up to the last payload byte. Equivalently, XOR of all
  `len` bytes == 0 for a good block. Checksum is **not** validated by the container: bad blocks
  must still be played (the loader reports "R Tape loading error" itself).
* Each TAP block plays exactly like TZX block 0x10 with pause = 1000 ms ([LS] `tap.c:126`).
* Robustness: if fewer than 2 bytes remain, or `len` exceeds the remaining bytes, Fuse rejects the
  whole file ([LS] `tap.c:62-91`). Recommended for us: keep all complete blocks, drop the truncated
  tail, and warn. A block with `len == 0` is legal-but-useless: keep it (plays pilot+sync only) or skip.

### 2.2 ROM header block payload (17 bytes, after flag 0x00 => TAP len = 19 = 0x0013)

| Off (in payload) | Size | Field |
|---|---|---|
| 0 | 1 | type: 0 = Program, 1 = Number array, 2 = Character array, 3 = Bytes (CODE/SCREEN$) |
| 1 | 10 | file name, padded with spaces (0x20) |
| 11 | 2 | length of the following data block payload (without flag/checksum) |
| 13 | 2 | param1 |
| 15 | 2 | param2 |

| type | param1 | param2 |
|---|---|---|
| 0 Program | autostart LINE; >= 32768 (ROM writes 0x8000) means no autostart | length of program without variables (offset of VARS from PROG) |
| 1 Number array | byte 13 unused; **byte 14 = array name** (`0x80 | (letter & 0x1F)`, e.g. `a` -> 0x81) | unused |
| 2 Char array | byte 14 = array name (`0xC0 | (letter & 0x1F)`) | unused |
| 3 Bytes | start address | 32768 (unused) |

SCREEN$ = type 3, start 16384, length 6912. The array-name encoding mirrors the VARS area
(number array `100xxxxx`, char array `110xxxxx`).

### 2.3 Test vector (`SAVE "ROM" CODE 0,2`)

```
13 00                                   len = 19
00                                      flag (header)
03 52 4F 4D 20 20 20 20 20 20 20        type 3, "ROM       "
02 00 00 00 00 80                       length 2, param1 0, param2 32768
F1                                      checksum (XOR of 00..80)
04 00                                   len = 4
FF F3 AF A3                             flag FF, first two ROM bytes F3 AF, checksum FF^F3^AF = A3
```
Whole file (27 bytes): `13 00 00 03 52 4F 4D 20 20 20 20 20 20 20 02 00 00 00 00 80 F1 04 00 FF F3 AF A3`.
Verified by script (XOR checks).

### 2.4 Related variants (detect only; not required)

SPC/STA/LTP: stored length is `len-2`; SPC parity excludes the flag; STA has no parity byte
([WIKI-TAP]; [LS] `tap.c:79-115`). ZXT = TAP behind a 128-byte +3DOS header ("PLUS3DOS").

---

## 3. Tape signal model (normative for `tape.c`)

The TZX spec words its rules in terms of a **"current pulse level"**, which is the level at which
the *next* pulse will be played ([TZX] §2). pzxtools implements exactly that ([PZXT]
`tzx.cpp:128-139` `tzx_render_pulse`: output pulse at `level`, then `level = !level`). We adopt it.

State of the player:

```c
int  next_level;   /* TZX "current pulse level": level of the next pulse (0 = low, 1 = high) */
int  ear;          /* level currently being output on EAR (what port 0xFE bit 6 reads)        */
```

Primitives (every block below is defined in terms of these):

```c
/* PULSE(d): standard pulse. Also used for d == 0 (zero time, still flips polarity). */
ear = next_level;  hold(d);  next_level = !ear;

/* LEVEL(v, d): sample-style output (blocks 0x15). */
ear = v;  hold(d);  next_level = v;          /* "current level = last level played" */

/* PAUSE(ms), ms > 0  ([TZX] §2, [PZXT] tzx.cpp:299-316) */
total = ms * 3500;
if (next_level == 1) { ear = 1; hold(min(3500, total)); total -= min(3500,total); }  /* 1 ms "opposite level" */
ear = 0;  hold(total);  next_level = 0;
/* ms == 0: nothing happens, next_level unchanged. */
```

Rules:

1. Tape start, rewind, or seek to any block: `next_level = 0`, `ear = 0` ([TZX] §2).
2. After blocks 0x10/0x11/0x12/0x13/0x14/0x19 `next_level` is the opposite of the last pulse, so the
   following pulse produces an edge (falls out of PULSE).
3. After 0x15 and 0x18, `next_level` = last level played (no edge at the start of the next pulse)
   ([TZX] §2; [PZXT] `tzx.cpp:620-624`).
4. A pause (block 0x20 or the "pause after this block" field) ends with `ear = 0`, `next_level = 0`;
   so the first pulse of the next block is low and does not immediately produce an edge (the spec
   says this explicitly). A zero pause changes nothing.
5. **Pause field == 0 inside a data block (0x10/0x11/0x14/0x15/0x18/0x19) means "no pause, next block
   starts immediately" — it does NOT stop the tape.** Only block 0x20 with 0 ms stops the tape.
6. Edges are what matter to loaders (the ROM's LD-EDGE detects changes of EAR); polarity matters
   only to a few level-sensitive custom loaders and is why rules 1-4 exist.

Notes on other implementations (all acceptable to the ROM loader):
* Fuse holds the *whole* pause at the toggled level and forces low at the start of the next block
  ([LS] `tape.c:861-871` `do_tail_pause`, `tape.c:626-675` `force_low_level`).
* pzxtools replaces the 1 ms by a 945 T tail pulse for ROM/turbo/pure-data blocks ([PZXT] `tzx.cpp:191-217`).

Unit-test expectation (ROM block 0x10, tape start): pulses are low(2168), high(2168), ... ; pilot
pulse #8063 (index 8062) is low, sync1 high 667, sync2 low 735, first data pulse high.

---

## 4. TZX 1.20

### 4.1 File header (10 bytes) [TZX §3]

| Off | Size | Value |
|---|---|---|
| 0 | 7 | `"ZXTape!"` = `5A 58 54 61 70 65 21` |
| 7 | 1 | 0x1A |
| 8 | 1 | major version (1) |
| 9 | 1 | minor version (20 = 0x14) |

Accept major == 1 (any minor; warn if minor > 20). Reject major != 1 ("unsupported TZX version").
Then blocks follow until EOF: `BYTE id` + body. All offsets below are **relative to the first byte
after the ID**. Block indices (for jumps/loops) count **every** block, including info blocks.

### 4.2 Block summary

Body length column uses the spec notation: `[a,b]` = little-endian value read at those offsets.

| ID | Name | Body length | Playable? |
|---|---|---|---|
| 10 | Standard speed data | `[02,03] + 04` | yes |
| 11 | Turbo speed data | `[0F,10,11] + 12` | yes |
| 12 | Pure tone | `04` | yes |
| 13 | Pulse sequence | `[00]*2 + 01` | yes |
| 14 | Pure data | `[07,08,09] + 0A` | yes |
| 15 | Direct recording | `[05,06,07] + 08` | yes |
| 18 | CSW recording | `[00..03] + 04` | yes |
| 19 | Generalized data | `[00..03] + 04` | yes |
| 20 | Pause / stop the tape | `02` | yes |
| 21 | Group start | `[00] + 01` | no (info) |
| 22 | Group end | `00` | no |
| 23 | Jump to block | `02` | flow |
| 24 | Loop start | `02` | flow |
| 25 | Loop end | `00` | flow |
| 26 | Call sequence | `[00,01]*2 + 02` | flow |
| 27 | Return from sequence | `00` | flow |
| 28 | Select block | `[00,01] + 02` | flow/UI |
| 2A | Stop the tape if in 48K mode | `04` (DWORD 0) | yes (stops: TK is 48K) |
| 2B | Set signal level | `05` (DWORD 1 + BYTE) | yes |
| 30 | Text description | `[00] + 01` | no |
| 31 | Message | `[01] + 02` | no (UI) |
| 32 | Archive info | `[00,01] + 02` | no |
| 33 | Hardware type | `[00]*3 + 01` | no |
| 35 | Custom info | `[10,11,12,13] + 14` | no |
| 5A | Glue | `09` | no |
| 16 | (deprecated) C64 ROM data | `[00..03]` (DWORD = whole body incl. itself) | skip |
| 17 | (deprecated) C64 turbo data | `[00..03]` (same) | skip |
| 34 | (deprecated) Emulation info | `08` | skip |
| 40 | (deprecated) Snapshot | `[01,02,03] + 04` | skip (see 4.6) |
| other | unknown | `[00..03] + 04` (extension rule) | skip + warn |

### 4.3 Data blocks

**0x10 Standard speed data** [TZX]

| Off | Type | Field |
|---|---|---|
| 00 | WORD | pause after block, ms {1000} |
| 02 | WORD | N = data length |
| 04 | BYTE[N] | data as in TAP (flag ... checksum) |

Play: `PULSE(2168)` x (data[0] < 128 ? 8063 : 3223), `PULSE(667)`, `PULSE(735)`, then for every bit
MSb first `PULSE(b?1710:855)` twice, then `PAUSE(pause)`.

**0x11 Turbo speed data**

| Off | Type | Field {ROM default} |
|---|---|---|
| 00 | WORD | pilot pulse length {2168} |
| 02 | WORD | sync 1 {667} |
| 04 | WORD | sync 2 {735} |
| 06 | WORD | zero-bit pulse {855} |
| 08 | WORD | one-bit pulse {1710} |
| 0A | WORD | pilot pulse count {8063 / 3223} |
| 0C | BYTE | used bits in last byte {8} (MSb-aligned: 6 means `xxxxxx00`) |
| 0D | WORD | pause ms {1000} |
| 0F | BYTE[3] | N = data length (24-bit) |
| 12 | BYTE[N] | data |

Play as 0x10 with these values. Pilot count 0 => no pilot, go straight to sync ([LS] `tape.c:978-986`).
Bit count = `(N-1)*8 + used_bits`. **used_bits normalisation** ([LS] `tzx_read.c:1206-1216`): if
`used_bits == 0` and N > 0, treat as `N-1` bytes with 8 bits; if `used_bits > 8`, use 8.

**0x12 Pure tone**: `00 WORD pulse length`, `02 WORD count`. Play `PULSE(len)` x count.

**0x13 Pulse sequence**: `00 BYTE n`, `01 WORD[n] lengths`. Play `PULSE(len[i])` for i = 0..n-1.

**0x14 Pure data** (no pilot/sync)

| Off | Type | Field |
|---|---|---|
| 00 | WORD | zero-bit pulse |
| 02 | WORD | one-bit pulse |
| 04 | BYTE | used bits in last byte |
| 05 | WORD | pause ms |
| 07 | BYTE[3] | N |
| 0A | BYTE[N] | data |

Play: each bit = two `PULSE()` of the bit's length, then `PAUSE`.

**0x15 Direct recording**

| Off | Type | Field |
|---|---|---|
| 00 | WORD | T-states per sample (79 = 44.1 kHz, 158 = 22.05 kHz) |
| 02 | WORD | pause ms |
| 04 | BYTE | used bits (samples) in last byte, 1..8 (normalise like 0x11) |
| 05 | BYTE[3] | N |
| 08 | BYTE[N] | samples, 1 bit each, **MSb first**, 1 = high, 0 = low |

Play: sample count `(N-1)*8 + used`; each sample `LEVEL(bit, Tps)` (merge runs of equal bits into
one hold for speed, as [LS] `tape.c:1226-1254` does). After the block `next_level = last sample`;
then `PAUSE(pause)`.
Example: Tps=79, data `F0 0F`, used=8 -> high 316 T, low 632 T, high 316 T.

**0x18 CSW recording**

| Off | Type | Field |
|---|---|---|
| 00 | DWORD | block length L = 10 + N (excluding these 4 bytes) |
| 04 | WORD | pause ms |
| 06 | BYTE[3] | sample rate, Hz |
| 09 | BYTE | compression: 1 = RLE, 2 = Z-RLE (zlib stream, RFC 1950) |
| 0A | DWORD | number of pulses after decompression (validation only) |
| 0E | BYTE[N] | CSW v2 data |

RLE stream (after inflating if Z-RLE): each byte `b != 0` is one pulse of `b` samples; a byte `0x00`
is followed by a DWORD giving the pulse length in samples ([LS] `tzx_read.c:563-572`). Pulse k plays
with `PULSE()` semantics (first pulse at the incoming `next_level`, alternating afterwards); after the
last pulse set `next_level = level of the last pulse` ([PZXT] `tzx.cpp:620-624`); then `PAUSE`.
Convert samples to T with a cumulative sum: `T_end(k) = round(S_k * 3500000 / rate)`, where S_k is the
running sample total (Fuse uses truncated `3500000/rate` = 79 at 44.1 kHz, which drifts 0.5 %).
A mismatch between the pulse count field and the real count is a warning only. Rate 0 or
compression not in {1,2} => corrupt block: skip it with a warning.
Z-RLE requires an inflater (zlib or a small single-file inflate such as `tinf`/`miniz`); if the
build has none, skip the block with "Z-RLE CSW not supported".

Test vector (RLE, 44.1 kHz, pulses 10, 10, 300 samples, pause 0):
```
18 11 00 00 00  00 00  44 AC 00  01  03 00 00 00  0A 0A 00 2C 01 00 00
```
Cumulative T: 794, 1587, 25397, so the pulses last 794, 793 and 23810 T.

**0x19 Generalized data block (GDB)**

| Off | Type | Field |
|---|---|---|
| 00 | DWORD | block length (excluding these 4 bytes) |
| 04 | WORD | pause ms |
| 06 | DWORD | TOTP = number of pilot/sync **PRLE records** (0 = none) |
| 0A | BYTE | NPP = max pulses per pilot/sync symbol |
| 0B | BYTE | ASP = pilot/sync alphabet size (0 = 256) |
| 0C | DWORD | TOTD = number of data symbols (0 = none) |
| 10 | BYTE | NPD = max pulses per data symbol |
| 11 | BYTE | ASD = data alphabet size (0 = 256) |
| 12 | SYMDEF[ASP] | pilot/sync alphabet, only if TOTP > 0; each `2*NPP+1` bytes |
| ... | PRLE[TOTP] | only if TOTP > 0; each 3 bytes: `BYTE symbol, WORD repetitions` |
| ... | SYMDEF[ASD] | data alphabet, only if TOTD > 0; each `2*NPD+1` bytes |
| ... | BYTE[DS] | data stream, only if TOTD > 0 |

SYMDEF = `BYTE flags, WORD pulse[MAXP]`; a 0 entry terminates a shorter symbol early (it is not
played). Flags bits 0-1 give the polarity of the symbol's **first** pulse:

| bits | [TZX] wording | Action before the first PULSE (our model) |
|---|---|---|
| 00 | opposite to current level (edge) — default | nothing (`next_level` is already the opposite) |
| 01 | same as current level (no edge, prolongs previous pulse) | `next_level = !next_level` |
| 10 | force low | `next_level = 0` |
| 11 | force high | `next_level = 1` |

(Mapping per [PZXT] `tzx.cpp:336-381`.) Then each nonzero `pulse[i]` is a `PULSE()`. A symbol whose
first entry is 0 has no pulses; apply the polarity flag anyway (pzxtools does; Fuse ignores it, [LS]
`tape.c:1295-1324`) — the difference only shows if the next pulse comes from another symbol.

Data stream: `NB = ceil(log2(ASD))` bits per symbol (ASD 256 -> 8, ASD 2 -> 1, ASD 1 -> 0),
read **MSb first**, packed across byte boundaries; `DS = ceil(NB*TOTD/8)`. Order of play: all PRLE
records (symbol repeated `repetitions` times), then TOTD data symbols, then `PAUSE(pause)`.
Validate: computed end must equal `04 + block length`; otherwise warn and skip the block by its DWORD.

Test vector: the spec's example (standard header as a GDB), 63 bytes incl. ID, verified by script:
```
19 3B 00 00 00  E8 03  02 00 00 00  02 02  98 00 00 00  02 02
00 78 08 00 00   00 9B 02 DF 02                     pilot sym0 (2168), sym1 (667,735)
00 7F 1F  01 01 00                                  sym0 x 8063, sym1 x 1
00 57 03 57 03   00 AE 06 AE 06                     data sym0 (855,855), sym1 (1710,1710)
00 03 4A 50 53 50 20 20 20 20 20 20 00 1B 00 40 00 80 C1   152 bits ("JPSP" SCREEN$ header)
```
It must produce exactly the same pulse train as `10 E8 03 13 00` + the same 19 bytes.

### 4.4 Pause, stop and level blocks

**0x20 Pause / stop**: `00 WORD ms`. `ms > 0`: `PAUSE(ms)`. `ms == 0`: **stop the tape** (motor off);
the position moves to the next block ([TZX]; [LS] `tape.c:475-490`). If the last pulse is still
open (the previous block had pause 0, so no edge has ended it yet), first play `PAUSE(1 ms)`, as at
the end of the tape: the ROM needs that edge to read the last bit, and the TZX spec asks for at
least 1 ms of the opposite level after the last pulse (Fuse toggles EAR at the end of every pulse).
After a pause (or a 0x2B/0x15 that already set the level), stop at once with `next_level` and `ear`
unchanged. Pressing play continues with the next block.

**0x2A Stop the tape if in 48K mode**: `00 DWORD 0`. The TK90X/TK95 are 48K machines: always stop
(same 1 ms rule as 0x20 with 0 ms).

**0x2B Set signal level**: `00 DWORD 1`, `04 BYTE level` (0 low, 1 high). Our model: `next_level = level`
(the next pulse is played at that level, as pzxtools `tzx.cpp:832-835`). Also set `ear = level`
(zero-time; makes the level observable if the tape stops here).
Divergence: Fuse treats it as "EAR is now `level`, the next pulse toggles away from it" ([LS]
`tape.c:529-533`), i.e. the opposite polarity for the next pulse. Follow the spec/pzxtools reading.

### 4.5 Flow control

All offsets are **signed 16-bit, relative to the index of the block that contains them**
(`target = this_index + offset`).

* **0x23 Jump**: `00 WORD offset`. 0 = loop forever (treat as error: skip to next block),
  1 = next block, 2 = skip one, -1 = previous. Out-of-range target: warn, continue at next block.
* **0x24 Loop start**: `00 WORD count` (> 1). The blocks between 0x24 and the matching 0x25 are played
  `count` times in total. **0x25 Loop end**: no body. No nesting (an inner 0x24 while a loop is active:
  warn, treat as no-op). Count 0 or 1: play once. A 0x23 jump to a block outside the loop body
  (before the first block or after the matching 0x25) ends the loop, so a later 0x24 starts a new
  one (libspectrum restarts the loop on every 0x24).
* **0x26 Call sequence**: `00 WORD n`, `02 WORD[n] offsets`. For each offset in order: jump to
  `this_index + offset` and play until a **0x27 Return** (no body), then continue with the next
  offset; after the last one continue at `this_index + 1`. No nesting.
  (Fuse's reader does not support 0x26/0x27 at all; pzxtools does, `tzx.cpp:886-910`.)
* **0x28 Select block**: `00 WORD L` (rest of body), `02 BYTE n`, then n x `{WORD offset, BYTE len,
  CHAR[len] text}`. An emulator may offer a menu; default behaviour: ignore and continue with the next block.

### 4.6 Information blocks (no signal; zero time; `next_level` untouched)

* **0x21 Group start**: `00 BYTE L`, `01 CHAR[L] name`. **0x22 Group end**: no body.
* **0x30 Text**: `00 BYTE L`, `01 CHAR[L]` (ISO-8859-1; lines separated by 0x0D).
* **0x31 Message**: `00 BYTE seconds` (0 = until a key), `01 BYTE L`, `02 CHAR[L]`. Show in the OSD;
  must not stop the tape or create silence.
* **0x32 Archive info**: `00 WORD L`, `02 BYTE n`, n x `{BYTE id, BYTE len, CHAR[len]}`;
  ids 00 title, 01 publisher, 02 author(s), 03 year, 04 language, 05 type, 06 price, 07 loader,
  08 origin, FF comment.
* **0x33 Hardware**: `00 BYTE n`, n x `{BYTE type, BYTE id, BYTE info}`. Relevant to us: type 00
  (computers) id **0x10 = Microdigital TK 90-X**, **0x11 = Microdigital TK 95**, 0x01 = 48K;
  info 00 runs, 01 uses special hardware, 02 runs but does not use it, 03 does NOT run.
  Optional warning if `{00, 01|10|11, 03}` is present.
* **0x35 Custom info**: `00 CHAR[16]` ID (the spec table prints `CHAR[10]`, which is hex: 16 bytes),
  `10 DWORD L`, `14 BYTE[L]`.
* **0x5A Glue**: 9 bytes `"XTape!" 1A major minor` (a second file's header after concatenation). Skip.

Deprecated blocks [TZX §6] (practically absent in real files; skip, never fail):
* **0x16 / 0x17** (C64): DWORD at offset 00 is "length of the WHOLE block including the data";
  the spec's length formula `[00,01,02,03]` means the body is that many bytes **counting the DWORD
  itself**. Skip `DWORD` bytes after the ID. (pzxtools uses DWORD+4 instead; both are rare enough
  not to matter.)
* **0x34 Emulation info**: fixed 8-byte body.
* **0x40 Snapshot**: `00 BYTE type` (0 = .Z80, 1 = .SNA), `01 BYTE[3] L`, `04 BYTE[L]`. Body = 4 + L.
  The spec says to load it and continue; we skip it with a warning.

### 4.7 Unknown IDs

[TZX §2] "General Extension Rule: ALL custom blocks added after version 1.10 have the length of the
block in the first 4 bytes (DWORD) after the ID (not including these 4 bytes)". This is a rule about
**future** block IDs, not an ID range: blocks 0x30-0x35 do *not* follow it (0x30/0x31/0x33 use a
1-byte count, 0x32 a WORD). Blocks that do follow it: 0x18, 0x19, 0x2A, 0x2B (and 0x16/0x17 loosely).

Policy for an ID not in §4.2: read DWORD L at body offset 0, skip `1 + 4 + L` bytes, warn
"unknown TZX block 0xNN skipped" (same as [PZXT] `tzx.cpp:52-55,85,951-954`). Fuse instead rejects the
file ([LS] `tzx_read.c:260-266`), which we do not want.

Truncation: if any block's computed length passes EOF, stop parsing there, keep the blocks already
read, and warn ([PZXT] `tzx.cpp:92-125`). Validate the header fields (e.g. 0x11 needs 18 body bytes)
before reading them.

### 4.8 TZX test vector (header + data of §2.3, last block stops the tape)

```
5A 58 54 61 70 65 21 1A 01 14                         "ZXTape!" 1A, v1.20
10 E8 03 13 00  00 03 52 4F 4D 20 20 20 20 20 20 20 02 00 00 00 00 80 F1   0x10, pause 1000, 19 bytes
10 00 00 04 00  FF F3 AF A3                           0x10, pause 0 (data block follows immediately)
20 00 00                                              stop the tape
```

---

## 5. ROM flash-load trap (LD-BYTES)

### 5.1 The ROM routine (TK90X ROM = 48K ROM here; bytes verified in `roms/tk90x.rom`)

Entry contract of **LD-BYTES at 0x0556**:

| Register | Meaning |
|---|---|
| A | expected flag byte (0x00 header, 0xFF data) |
| F carry | 1 = LOAD, 0 = VERIFY |
| IX | destination address |
| DE | number of payload bytes (flag and checksum not counted) |

Disassembly (from the ROM bytes):
```
0556 14        INC D          ; resets Z
0557 08        EX AF,AF'      ; A' = flag, F' = carry(LOAD/VERIFY), Z=0
0558 15        DEC D
0559 F3        DI
055A 3E 0F     LD A,$0F
055C D3 FE     OUT ($FE),A    ; border white, MIC bit set
055E 21 3F 05  LD HL,$053F    ; SA/LD-RET
0561 E5        PUSH HL        ; every exit RETs through 053F
0562 DB FE     IN A,($FE)
0564 1F        RRA
0565 E6 20     AND $20
0567 F6 02     OR $02
0569 4F        LD C,A         ; C = $02/$22 (border red + last EAR in bit 5)
056A BF        CP A           ; Z = 1
056B C0        LD-BREAK: RET NZ      <-- trap here (Fuse, z80.pl:984)
056C CD E7 05  LD-START: CALL LD-EDGE-1
...
05A9           LD-LOOP ... 05B3 LD-FLAG (RL C; XOR L; RET NZ = flag mismatch)
05CA           LD-8-BITS ... 05D8 LD A,H; XOR L; LD H,A ; parity
05DF 7C        LD A,H
05E0 FE 01     CP $01         ; carry set iff parity accumulator == 0
05E2 C9        RET            ; -> 053F
053F F5 3A 48 5C E6 38 0F 0F 0F D3 FE 3E 7F DB FE 1F FB 38 02 CF 0C F1 C9
               ; SA/LD-RET: PUSH AF; border from BORDCR(5C48); test BREAK; EI;
               ; JR C,0554; RST 08 "D BREAK - CONT repeats"; 0554 POP AF; RET
```
Callers: LD-HEADER uses `A=00, DE=17, IX=header workspace, SCF`; the data block `A=FF`.

### 5.2 Where to trap

Fuse traps the `RET NZ` at **0x056B** (it checks `PC == 0x056C` after the opcode fetch: [FUSE]
`z80/z80.pl:984-986`) and, after loading, sets **PC = 0x05E2** so the `RET` there pops 0x053F and the
ROM's own exit path restores the border, checks BREAK and does `EI` ([FUSE] `tape_traps.c:152-161`).
At 0x056B the ROM has already moved the arguments: **flag is in A', LOAD/VERIFY is F' carry**, IX and
DE unchanged, SP points at 0x053F, interrupts disabled.
(ZEsarUX traps 0x0562 = 1378, also after the PUSH, and returns with `PC = pop()` ([ZX] `tape.c:1102,1346`,
`tap_load` at `tape.c:988-1230`) — same effect without running SA/LD-RET.)

Fire the trap only if all hold (else just execute `RET NZ` normally):
1. flash-load enabled; the ROM area is the TK ROM (no other ROM paged);
2. PC is 0x056B executed from ROM;
3. a tape is inserted and, after skipping info-only blocks (0x21, 0x22, 0x24, 0x30-0x35, 0x5A, and
   unknown skipped IDs; cf. [LS] `tape_block.c:422-455`), the current block is a **TAP block or TZX 0x10**
   positioned at its start. For any other block type (turbo, GDB, CSW, ...) start real-time playback
   and let the ROM read EAR (Fuse does the same: `tape_traps.c:136-150`).

After the trap, advance the tape to the block following the loaded one (its pause is consumed).
Leave the tape stopped (or keep playing, if it was playing).

### 5.3 What the trap does and the exit state

Let `blk` = block bytes (blk[0] = flag), `len` = block length, `n = min(len-1, DE)` bytes available
for the payload. Algorithm = Fuse `tape_trap_load_block` ([FUSE] `tape_traps.c:360-440`):

```
if len == 0:            L = 1; F' = 0x01; F.carry = 0; PC = 0x05E2; done
parity = blk[0]; L = blk[0]; A = 0
if DE == 0:             (ROM zero-length bug) B = 0xB0; A = parity; CP 1; C = 1; H = parity; done
AF' = 0x0145            (A' = 0x01, F' = Z|P|C  -- real ROM gives F' = 0x44 for VERIFY)
if A'_requested ^ blk[0] != 0:     A = that XOR; flags as XOR; carry = 0;  -> finish
if VERIFY: compare blk[1..n] with memory at IX; on first mismatch L = tape byte, carry = 0 -> finish
if LOAD:   write blk[1..n] to IX.. (writes below 0x4000 are ignored by the memory map)
parity ^= each of the n bytes
if n == DE and len > DE+1:          /* enough data: byte blk[DE+1] is read as the checksum */
    parity ^= blk[DE+1]; A = parity; CP 1     -> success iff A == 0
    B = 0xB0
else:                               /* tape ran out of bytes: ROM timeout path */
    B = 0; L = 1; carry = 0
finish: C = 1; H = parity; DE -= processed; IX += processed; PC = 0x05E2
```

Exit state on success (full block, good checksum, LOAD):

| Reg | Value |
|---|---|
| A | 0x00 |
| F | 0x93 (result of `CP $01` with A=0: S, H, N, C set; Z, P/V, F3, F5 clear). **Carry = 1** |
| B | 0xB0 |
| C | 0x01 (real ROM: 0x01/0x21/0xFE/0xDE depending on edge parity; Fuse uses 0x01; ZEsarUX also 1 — needed by "Sweet Fighter II" per [ZX] comment) |
| D, E | DE - n = 0x0000 |
| H | 0x00 (parity accumulator after XOR with checksum) |
| L | last byte read. Real ROM: the checksum byte. Fuse: last payload byte |
| IX | IX + n |
| A' F' | 0x01, 0x45 |
| PC | 0x05E2 (then RET -> 0x053F -> POP AF -> RET to caller with A=0, carry=1) |
| SP | unchanged at trap time (0x053F still on top) |
| IFF | unchanged (DI'd); SA/LD-RET does EI |

Failure = same but **carry = 0** (checksum: A = H = nonzero parity, `CP 1` leaves carry clear;
flag mismatch: carry clear, IX/DE untouched). The BASIC LOAD command then prints
"R Tape loading error". Note: when `len-1 > DE` the ROM reads byte DE+1 of the block as the checksum,
so the check normally fails exactly as on a real machine; when `len-1 < DE` the load fails with
B = 0 after copying the available bytes.

ZEsarUX differences worth knowing ([ZX] `tape.c:988-1230`): ignores VERIFY, sets `DE -= len`,
`IX` past the data, `H` = checksum result, `C = 1`, and always clears Z.

Save trap (optional, for completeness): Fuse traps `EX AF,AF'` at 0x04D0 inside SA-BYTES
(`z80.pl:603-607`): `A` = flag, `IX`, `DE`; append block `flag + data + XOR`, pause 1000 ms;
return with `PC = 0x053E` ([FUSE] `tape_traps.c:447-509`).

---

## 6. SNA (48K)

Sources: [FAQ] SNA; [WIKI-SNA]; [LS] `sna.c:76-178`.

### 6.1 Layout (49 179 bytes = 27 + 49 152)

| Off | Size | Field |
|---|---|---|
| 0x00 | 1 | I |
| 0x01 | 2 | HL' |
| 0x03 | 2 | DE' |
| 0x05 | 2 | BC' |
| 0x07 | 2 | AF' (F' at 0x07, A' at 0x08) |
| 0x09 | 2 | HL |
| 0x0B | 2 | DE |
| 0x0D | 2 | BC |
| 0x0F | 2 | IY |
| 0x11 | 2 | IX |
| 0x13 | 1 | interrupt: **bit 2 = IFF2** (1 = EI). Only bit 2 is defined (bit 0 is NOT IFF1) |
| 0x14 | 1 | R (all 8 bits) |
| 0x15 | 2 | AF (F at 0x15, A at 0x16) |
| 0x17 | 2 | SP |
| 0x19 | 1 | interrupt mode 0/1/2 (use `& 3`) |
| 0x1A | 1 | border colour 0-7 (use `& 7`) |
| 0x1B | 49152 | RAM 0x4000-0xFFFF |

There is **no PC**: the snapshot was taken by pushing PC on the stack; resume as a `RETN`.

### 6.2 Loading = emulate RETN

```
copy RAM; set registers;
if SP < 0x4000 or SP == 0xFFFF: reject "SNA: invalid SP"          ([LS] sna.c:157-163)
PC  = mem[SP] | mem[SP+1] << 8;   SP += 2        (wraps to 0x0000 for SP = 0xFFFE)
IFF1 = IFF2 = (hdr[0x13] >> 2) & 1               (RETN copies IFF2 to IFF1)
IM = hdr[0x19] & 3; border = hdr[0x1A] & 7; MEMPTR = PC; halted = 0
frame T-state = 0 (not stored in the file)
```
Fuse does not clear the two stack bytes ([LS] `sna.c:166-171`); neither should we (some docs
suggest zeroing them for snapshots taken with a full stack; optional).

Size check: exactly 49 179 => 48K. **131 103 or 147 487 => 128K SNA: reject** with
"128K SNA snapshots are not supported: the TK90X/TK95 is a 48K machine". Any other size: "not a valid SNA".

Test vector (header only; RAM follows):
```
3F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 2A 44 00 40 FF 01 07
```
I=0x3F, IFF2=1 (EI), R=0x2A, F=0x44, A=0x00, SP=0xFF40, IM 1, border 7. PC is the word at file
offset `27 + (0xFF40 - 0x4000)` = 0xBF5B; after load SP = 0xFF42.

---

## 7. Z80 (versions 1, 2, 3)

Sources: [Z80]; [WIKI-Z80]; [LS] `z80.c:202-400` (header), `z80.c:679-1178` (blocks), `z80.c:1884-1935` (decompress).

### 7.1 Common 30-byte header

| Off | Size | Field |
|---|---|---|
| 0 | 1 | A |
| 1 | 1 | F |
| 2 | 2 | BC |
| 4 | 2 | HL |
| 6 | 2 | PC (**0 => version 2 or 3**) |
| 8 | 2 | SP |
| 10 | 1 | I |
| 11 | 1 | R (bit 7 not significant) |
| 12 | 1 | bit 0 = R bit 7; bits 1-3 = border; bit 4 = SamRom; **bit 5 = data compressed (v1 only)**; **255 => treat as 1** |
| 13 | 2 | DE |
| 15 | 2 | BC' |
| 17 | 2 | DE' |
| 19 | 2 | HL' |
| 21 | 1 | A' |
| 22 | 1 | F' |
| 23 | 2 | IY |
| 25 | 2 | IX |
| 27 | 1 | IFF1 (0 = DI, else EI) |
| 28 | 1 | IFF2 |
| 29 | 1 | bits 0-1 IM; bit 2 issue-2 emulation; bit 3 double int freq; bits 4-5 video sync; bits 6-7 joystick (0 Cursor/Protek/AGF, 1 Kempston, 2 Sinclair 2 Left / user-defined in v3, 3 Sinclair 2 Right) |

Derived: `if (b12 == 255) b12 = 1;` `R = (b11 & 0x7F) | ((b12 & 1) << 7)`; `border = (b12 >> 1) & 7`;
`IM = b29 & 3`. (libspectrum does not apply the 255 rule; we should, as the spec requires.)

### 7.2 Version 1 (PC != 0): always 48K

Data starts at offset 30.
* bit 5 of byte 12 clear: 49 152 raw bytes = RAM 0x4000-0xFFFF.
* bit 5 set: compressed stream terminated by the end marker **`00 ED ED 00`**.

**Compression (shared by all versions)**: `ED ED nn bb` = byte `bb` repeated `nn` times. Encoders only
use it for runs >= 5, or runs >= 2 of 0xED; a byte that directly follows a single literal 0xED is
never part of a run. Decoder:
```
while out < limit:
    if in+1 < end and src[in] == 0xED and src[in+1] == 0xED:
        need 4 bytes (else corrupt); nn = src[in+2]; bb = src[in+3]; emit bb x nn; in += 4
    else: emit src[in++]
```
For v1: decode until 49 152 bytes have been produced (robust; then optionally check that the next 4
bytes are `00 ED ED 00`). Fuse instead searches for the first `00 ED ED 00` and requires the
decoded length to be exactly 0xC000 ([LS] `z80.c:1033-1121`). Output != 49 152 => "corrupt Z80".
Bound every write (a malicious `ED ED FF xx` near the end must not overflow).

Vectors (encoder per spec; round-trip verified):
```
ED 00 00 00 00 00 00   -> ED 00 ED ED 05 00
ED ED                  -> ED ED 02 ED
01 02 03 03 03 03 03 03 04 -> 01 02 ED ED 06 03 04
00 00 00 00            -> 00 00 00 00        (run of 4 not encoded)
```
v1 header example: `44 00 00 00 00 5C 00 80 40 FF 3F 05 25 00 00 ...(zeros)... 01 01 01`
-> A=0x44, HL=0x5C00, PC=0x8000, SP=0xFF40, I=0x3F, R=0x85, border 2, compressed, IFF1=IFF2=1, IM 1.

### 7.3 Versions 2 and 3 (PC field == 0)

Offset 30: WORD `extra_len` = **23 => v2**, **54 or 55 => v3**; anything else: reject "unknown Z80
header length" ([LS] `z80.c:265-285`). Extra header (offsets from file start):

| Off | Size | Field |
|---|---|---|
| 32 | 2 | PC |
| 34 | 1 | hardware mode (table below) |
| 35 | 1 | SamRam: 74LS259 state / 128K: last OUT 0x7FFD / Timex: last OUT 0xF4 |
| 36 | 1 | 0xFF if Interface 1 ROM paged / Timex: last OUT 0xFF |
| 37 | 1 | bit 0 R emulation, bit 1 LDIR emulation, bit 2 AY in use (even 48K), bit 6 Fuller box (with bit 2), **bit 7 modify hardware** (48K -> 16K, 128K -> +2, +3 -> +2A) |
| 38 | 1 | last OUT to 0xFFFD (AY register) |
| 39 | 16 | AY registers |
| 55 | 2 | (v3) low T-state counter |
| 57 | 1 | (v3) high T-state counter |
| 58 | 1 | (v3) Spectator flag (ignore) |
| 59 | 1 | (v3) 0xFF if MGT ROM paged |
| 60 | 1 | (v3) 0xFF if Multiface ROM paged (should be 0) |
| 61 | 1 | (v3) 0xFF if 0-8191 is ROM |
| 62 | 1 | (v3) 0xFF if 8192-16383 is ROM |
| 63 | 10 | (v3) user joystick key mappings |
| 73 | 10 | (v3) ASCII of those keys |
| 83 | 1 | (v3) MGT type: 0 Disciple+Epson, 1 Disciple+HP, 16 +D |
| 84 | 1 | (v3) Disciple inhibit button |
| 85 | 1 | (v3) Disciple inhibit flag |
| 86 | 1 | (v3, only if extra_len = 55) last OUT to 0x1FFD |

Memory blocks start at `32 + extra_len`, repeated until EOF:

| Off | Size | Field |
|---|---|---|
| 0 | 2 | compressed length; **0xFFFF => 16 384 bytes stored uncompressed** |
| 2 | 1 | page number |
| 3 | n | data (no end marker in v2/v3) |

Each page must decode to exactly 16 384 bytes.

**Hardware mode (byte 34) and what the TK90X does with it**

| Value | v2 meaning | v3 meaning | Action |
|---|---|---|---|
| 0 | 48K | 48K | load |
| 1 | 48K + IF1 | 48K + IF1 | load, ignore IF1 (warn) |
| 2 | SamRam | SamRam | load as 48K (warn), Fuse does the same |
| 3 | **128K** | 48K + MGT | v2: reject; v3: load, ignore MGT (warn) |
| 4 | 128K + IF1 | **128K** | reject |
| 5 | - | 128K + IF1 | reject |
| 6 | - | 128K + MGT | reject |
| 7, 8 | +3 (8 = XZX-Pro bug) | | reject |
| 9 | Pentagon 128 | | reject |
| 10 | Scorpion 256 | | reject |
| 11 | Didaktik Kompakt | | reject |
| 12, 13 | +2, +2A | | reject |
| 14, 15, 128 | TC2048, TC2068, TS2068 | | reject (Timex hardware) |

Values >= 7 mean the same in v2 and v3 ([LS] `z80.c:60-90,417-447`). Byte 37 bit 7 on a 48K mode =>
16K snapshot: load (RAM above 0x8000 absent from the file; leave it as is), warn.
Reject message: `"Z80 snapshot for <machine> (v<ver>, hardware mode <n>) is not supported: the TK90X/TK95 is a 48K machine"`.
Byte 37 bit 2 (AY in use on a 48K snapshot): do not reject, just warn "AY state ignored".

**Page numbers for 48K**: **8 => 0x4000-0x7FFF, 4 => 0x8000-0xBFFF, 5 => 0xC000-0xFFFF**.
Pages 0 (48K ROM), 1 (IF1/Disciple/+D ROM), 2, and 11 (Multiface ROM) => ignore. Any other page in a
48K snapshot (3, 6, 7, 9, 10, ...) => warn and ignore. Duplicate page => corrupt ([LS] `z80.c:908-1030`).
Missing page => leave that RAM zero-filled and warn.

**SLT extension**: a 3-byte block header `00 00 00` followed by `"SLT"` (`00 00 00 53 4C 54`) marks
level-loader data appended after the pages ([FAQ] SLT; [LS] `z80.c:1132-1139`). Stop parsing pages
there; ignoring the SLT data is acceptable.

**T-state counter (v3)**: low counter counts down from `Q-1` to 0 in each quarter frame, high counter
counts 0..3 and is 3 just after the interrupt ([Z80]). Fuse's formula ([LS] `z80.c:307-321`):
```
Q = frame_tstates / 4;      /* 69888/4 = 17472 on a Spectrum; TK 50 Hz: 71136/4 = 17784; 60 Hz: 59736/4 = 14934 */
t = (((hi + 1) % 4) + 1) * Q - (low + 1);
if (t >= 4*Q) t = 0;
```
Check: hi = 3, low = Q-1 => t = 0 (frame start). Use the current TK frame length. v1/v2: t = 0.

Other v2/v3 fields: IFF1/IFF2/IM/issue-2/joystick from the common header; page block for 0x4000
must exist for a sane snapshot. A page block `04 01 08` + `(ED ED FF 00) x 64 + ED ED 40 00`
(260 bytes) decodes to 16 384 zero bytes at 0x4000 — handy unit test.

---

## 8. Raw headerless binaries

| Ext | Convention | Load |
|---|---|---|
| `.scr` | exact SCREEN$ dump: 6144 bytes bitmap + 768 attributes = **6912 bytes**, loaded at **16384 (0x4000)** ([FAQ] SCR) | write to 0x4000..0x5AFF; registers untouched. 12288/12289-byte .scr are Timex hi-colour/hi-res ([FAQ]): reject. Accept 6144 (bitmap only) with a warning; other sizes reject |
| `.bin`, `.cod`, `.raw`, `.obj` | raw machine code, **no header**; address not in file | `--org` (default 32768) and `--exec` (default = org), as in ARCHITECTURE.md. Require `0x4000 <= org` and `org + size <= 0x10000` (else reject: "binary does not fit in RAM") |
| any, starting with `PLUS3DOS\x1A` | +3DOS 128-byte header: 0-7 `"PLUS3DOS"`, 8 = 0x1A, 9 issue, 10 version, 11-14 DWORD file length incl. header, **15-22 = TAP-style header (type, length, param1, param2)**, 127 = checksum (sum of bytes 0-126 mod 256) | type 3: org = param1, length = WORD at 16; data at offset 128 |
| `.rom` (16384 bytes) | ROM image, not a program | offer as `--rom`, do not load into RAM |

Recommended way to run a .bin "as from tape" (matches ARCHITECTURE.md's virtual tape): build a TAP in
memory: a BASIC loader `10 CLEAR org-1: LOAD "" CODE: RANDOMIZE USR exec` plus a type-3 header
(param1 = org, param2 = 32768) and the data block. For .scr: `LOAD "" SCREEN$` or a type-3 header with
start 16384, length 6912.

---

## 9. Format detection

1. TZX: first 8 bytes `5A 58 54 61 70 65 21 1A`.
2. PZX (`"PZXT"`) / CSW file (`"Compressed Square Wave" 1A`): not supported; say so explicitly.
3. SNA: file size 49 179 (128K sizes 131 103 / 147 487 => reject message).
4. Z80: extension `.z80`; size >= 30; if PC == 0, size >= 32 + extra_len.
5. TAP: extension `.tap`, and the length chain `len, data[len], len, ...` lands exactly on EOF.
6. `.scr` by size 6912; `.bin/.cod/...` by extension; `PLUS3DOS` magic anywhere it appears.

## 10. Summary of error/warning messages

* `TZX: unsupported version %d.%d` (major != 1) / `TZX: unknown block 0x%02X skipped` /
  `TZX: block 0x%02X truncated at offset %u; tape ends here`.
* `TAP: truncated block at offset %u ignored`.
* `SNA: 128K snapshots are not supported (48K machine)` / `SNA: invalid SP 0x%04X`.
* `Z80: <machine> snapshot not supported (48K machine)` / `Z80: page %d decompresses to %u bytes` /
  `Z80: missing page %d (RAM 0x%04X) filled with zeros`.
