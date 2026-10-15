# tests/data: sources, licenses, checksums

Everything here was downloaded on 2026-10-14 (or generated from downloaded files by
`tools/make_synthetic.py`).

Every TAP, TZX, SNA and Z80 file here was checked by a header and structure parse (Python `-I`):
- TAP: block lengths add up to the file size, every block's XOR checksum is 0, and 19-byte
  headers decode.
- TZX: the `ZXTape!\x1a` signature is present and every block parses to exactly EOF.
- SNA: the size is 49179 bytes, and PC is read from the stacked word at SP.
- Z80: the header decodes, compressed pages decompress to exactly 16384 bytes each, and
  the v1 end marker `00 ED ED 00` is present.

Full license texts are in `licenses/`.

## 1. Z80 CPU tests

| File | Size | MD5 | Source | License |
|---|---|---|---|---|
| `tests.in` | 161511 | 370bf3e63a15611ca187f980f7153ce7 | https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/z80/tests/tests.in?format=raw (master head `abb1c502a3e6`) | GPL-2.0-or-later (Fuse, (c) Philip Kendall et al.) |
| `tests.expected` | 323000 | 6fa8226283a10540b51c6e5234af8af3 | https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/z80/tests/tests.expected?format=raw | GPL-2.0-or-later |
| `zexdoc.com` | 8704 | be3691b6b910d2c2e05e194cd529e45c | https://raw.githubusercontent.com/anotherlin/z80emu/master/testfiles/zexdoc.com | GPL-2.0-or-later ((c) 1994 Frank D. Cringle; header of `testfiles/zexdoc.z80` in the same repo) |
| `zexall.com` | 8704 | 4a32f2a6691b3da5213d7688dda59822 | https://raw.githubusercontent.com/anotherlin/z80emu/master/testfiles/zexall.com | GPL-2.0-or-later (Frank D. Cringle) |
| `zexdoc_spectrum.tap` | 8716 | d2c6162ab8c56ebfe844aff5c17f06a7 | https://mdfs.net/Software/Z80/Exerciser/Spectrum/zexdoc.tap | GPL-2.0-or-later (Cringle; Spectrum port (c) 2002 J.G. Harston; CRCs updated 2007 by Stuart Brady) |
| `zexall_spectrum.tap` | 8716 | 6e0b6937608b63a9ed133a5fc77c36a4 | https://mdfs.net/Software/Z80/Exerciser/Spectrum/zexall.tap | GPL-2.0-or-later (as above) |
| `z80full.tap` | 14390 | 5d6d7158c032101cb49b69b6d1748d1b | https://github.com/raxoft/z80test/releases/download/v1.2a/z80test-1.2a.zip | MIT ((c) 2012-2023 Patrik Rak), `licenses/z80test-MIT.txt` |
| `z80doc.tap` | 14390 | a2197210f13dd828470121f389afa552 | same zip | MIT |
| `z80flags.tap` | 14390 | 10dd506859e57e54567c3200fdedb732 | same zip | MIT |
| `z80docflags.tap` | 14390 | 80bd8df81761fb24133fdc7279e49b7f | same zip | MIT |
| `z80ccf.tap` | 14875 | 308e8db397b6ac3ace822a899af2ebca | same zip | MIT |
| `z80memptr.tap` | 14390 | 94ac53ce5cc0c1058f978e4d406f603d | same zip | MIT |
| `int_skip.sna` | 49179 | bedc8233eeeda2a059654667efe39acc | https://raw.githubusercontent.com/MrKWatkins/ZXSpectrumNextTests/8adcbc8384b2311fb6d36d424478aef583d5c307/Tests/ZX48_ZX128/Z80IntSkip/int_skip.sna (v5.0, Peter Helcmanovsky "Ped7g") | MIT, `licenses/ZXSpectrumNextTests-MIT.txt` |
| `int_skip.tap` | 1035 | 8639c1ef75fb17ed2b8c73fb75336d27 | same commit, `.../Z80IntSkip/int_skip.tap` | MIT |
| `z80bltst.tap` | 1585 | 30ea9a05d3784789c44ae6ad6bd9ad3c | same commit, `Tests/ZX48_ZX128/Z80BlockInstructionFlags/z80bltst.tap` | MIT |

Notes:
- **FUSE**: 1356 tests. The format is described in `FUSE_TESTS_FORMAT.md`.
- **ZEXDOC/ZEXALL, CP/M `.com` versions** (`zexdoc.com`, `zexall.com`). The 67 expected
  CRCs in the binaries match the `; expected crc` lines in `zexdoc.z80` byte for byte.
  The program starts `C3 13 01` (`jp start`). `start:` runs `ld hl,(6)` and `ld sp,hl`, so it
  takes its **stack from the word at 0x0006**. It ends with `jp 0` (zexdoc.z80 lines 80-98).
  All output goes through `call 5` with `C=9` (print `$`-terminated string at DE) or `C=2`
  (print char in E). The `bdos` wrapper (lines 1194-1203) saves AF/BC/DE/HL itself.

  Minimal shim:
  1. Clear 64 KB of RAM. Load the file at **0x0100** and set PC = 0x0100.
  2. Write `C3 00 F0` at 0x0005 (`JP F000`, so SP = 0xF000) and `C9` (RET) at 0xF000.
     Optionally write `76` (HALT) at 0x0000.
  3. Before each instruction, if PC == 0x0005: when C == 2 print the character in E; when
     C == 9 print bytes from DE up to (not including) `'$'`. Then let the RET at
     0x0005 → 0xF000 run, or emulate the RET by popping PC yourself.
  4. If PC == 0x0000, stop (warm boot = "Tests complete").

  No interrupts and no I/O are used. Expected output on a correct core:
  `Z80 instruction exerciser`, then 67 lines ending in `  OK`, then `Tests complete`.
  On failure a line shows `  ERROR **** crc expected:xxxxxxxx found:yyyyyyyy`.
  It runs several billion T-states, so run it headless at full speed.
- **ZEX Spectrum TAPs** (mdfs.net). Each is a BASIC loader (42 bytes) plus `CODE 32768,8624`
  that auto-runs at 0x8000. They print with ROM CHAN-OPEN (`CALL 0x1601`) and `RST 0x10`. The
  67 CRCs in each TAP match the `zexall.src`/`zexdoc.src` sources on the same page, which
  contain the 2007 CRC update. The mdfs page says "68 tests" and quotes a 2003 hardware run
  with failures. Both predate the CRC fix and can be ignored. The TK90X ROM
  (`roms/tk90x.rom`) is byte-identical to the 48K ROM at 0x0010 (first 3 bytes `C3 F2 15`),
  0x0038, 0x0556 (LD-BYTES), 0x05E7, 0x0C0A, 0x0D6B, 0x15F2 and 0x1601. Overall 1980 bytes differ.
- **z80test 1.2a** (Patrik Rak). Each TAP is a BASIC loader
  (`10 CLEAR 32767:LOAD "" CODE:CLS` / `20 RANDOMIZE USR 32768`) plus CODE at 32768. A run
  ends with `all tests passed.` or `N of M tests failed.` (`src/main.asm` lines 56-66). The
  CRCs come from a real 48K Spectrum with a **Zilog** Z80. `z80ccf` assumes Zilog SCF/CCF
  behaviour; read its result knowing which CPU the TK90X is meant to have (see the doubts
  section).
- **int_skip** tests that interrupts are blocked after EI, after long DD/FD prefix chains and
  (in v5.0) after LD A,I/LD A,R. It also checks the LD A,I/R IFF2 read during INT. SNA state:
  SP=0x8FFA with PC=0x9000 on the stack, IM 1, IFF2=0, border 7.
  **z80bltst** checks the flags of LDxR/CPxR/INxR/OTxR when an interrupt arrives mid-repeat
  (YF=PC.13 and XF=PC.11, plus INxR/OTxR PF/HF). It uses IM 2 and reads port 0xFE,
  0x1F (Kempston, expects b7=0) and 0xFF (floating bus, expects 0xFF around the INT).

## 2. Spectrum programs for loader tests

| File | Format | Size | MD5 | Source | License |
|---|---|---|---|---|---|
| `dogmole.tap` | TAP | 34356 | cef4f2f6859009e6b88582f28810f557 | https://raw.githubusercontent.com/mojontwins/MK1/master/examples/dogmole/dev/dogmole.tap (MK1 commit `eb56cb0205db98eb316f69116b21b8cc7a3ef965`) | GPL-2.0 (repo `LICENSE`); see the doubts section |
| `rotatrix_tk90x.tzx` | TZX 1.13, standard 0x10 blocks only | 1213 | c6c70cf11e2e96cdbf4c09b4412630b3 | https://spectrumcomputing.co.uk/pub/sinclair/utils/r/Rotatrix(TK90X).tzx.zip (ZXDB entry 0027591, Einar Saukas, 2012). Identical copy in https://github.com/chernandezba/zesarux-extras `extras/media/spectrum/rainbow/Rotatrix (TK90X).tzx` | Freeware; ZXDB "Available". ZEsarUX `src/ACKNOWLEDGEMENTS` line 20: "Einar Saukas, for granting permission to include ... and others" |
| `abu_simbel_std.tzx` | TZX 1.13, standard 0x10 blocks only | 46420 | 616b7909992406dfdab59d712f810ea9 | https://spectrumcomputing.co.uk/pub/sinclair/games/a/AbuSimbelProfanation(SmallCase-Different).tzx.zip (ZXDB entry 0000048, Dinamic 1985) | ZXDB "Available" (not denied); no explicit license |
| `abu_simbel_turbo.tzx` | TZX 1.13, 0x32 + 0x10 + **custom turbo 0x11** | 47770 | 3d248169f5b14af2b1454b8ea5442ecf | https://spectrumcomputing.co.uk/pub/sinclair/games/a/AbuSimbelProfanation(SmallCase).tzx.zip (original 1985 release) | as above |
| `dogmole_turbo11.tzx` | TZX 1.20, 0x30 + 0x11 | 34520 | aa5677d562db853958681434a0e6db2e | **generated** from `dogmole.tap` by `tools/make_synthetic.py` | GPL-2.0 (derived) |
| `dogmole_tone_pulse.tzx` | TZX 1.20, 0x30 + 0x12/0x13/0x14 | 34548 | 10f6caed40371993c5b0d8727170a9c5 | **generated** from `dogmole.tap` | GPL-2.0 (derived) |
| `int_skip.sna` | SNA 48K | 49179 | bedc8233eeeda2a059654667efe39acc | see section 1 | MIT |
| `int_skip_v1.z80` | Z80 v1, compressed | 1959 | 683ffc45bfb66e933e521b823040f361 | **generated** from `int_skip.sna` | MIT (derived) |
| `int_skip_v3.z80` | Z80 v3 (54-byte ext header), 48K, compressed pages 8/4/5 | 2024 | cec8341bacf7a634a4ff7a0e1690681c | **generated** from `int_skip.sna` | MIT (derived) |
| `libspectrum_empty.z80` | Z80 v3, 48K, written by libspectrum | 877 | 1ec51b4d2ce03a4b921f9d74300cf7ea | https://raw.githubusercontent.com/speccytools/libspectrum/master/test/empty.z80 (GitHub mirror of Fuse's libspectrum `test/`) | GPL-2.0-or-later |

Block layouts (from the parse):

- `dogmole.tap`:
  - `Program: DOGMOLE` (66 bytes, LINE 10)
  - `Bytes: screen` 6912 @ 16384
  - `Bytes: main` 27303 @ 24000
- `rotatrix_tk90x.tzx`:
  - 0x30 text "Rotatrix (Einar Saukas)"
  - five header+data pairs, all 0x10 with pause 3000 ms:
    - `Program: TK Demo` (71, LINE 10)
    - `Demo CODE` 21 @ 64500
    - `TK DATA` 493 @ 64000
    - `Rotatrix` 422 @ 64859
    - `TK Patch` 16 @ 64975

  This is the TK90X-specific variant of a 1 KB rainbow (multicolour) effect. The
  plain Spectrum version (`Rotatrix.tzx`, 1146 bytes) has no `TK Patch` block. Being a raster
  effect, it should give a visual check of TK90X frame and contention timing. (This is my
  inference from the name and blocks; I have not seen the source.)
- `abu_simbel_turbo.tzx`:
  - 0x32 archive info (Dinamic Software, 1985, "Small case version, D.L.: M-14052-1985")
  - 0x10 `Program: ABU SIMBEL` (150, LINE 10)
  - 0x10 `Bytes: 2` 1200 @ 23400 (the custom loader)
  - three 0x11 blocks, all with pilot 2165 T, sync 714/714 T, 8 bits in the last byte:

    | data length | zero / one pulse (T) | pilot pulses | pause after (ms) |
    |---|---|---|---|
    | 6914 | 608 / 1217 | 3005 | 2267 |
    | 39152 | 614 / 1228 | 3256 | 1854 |
    | 12 | 610 / 1220 | 3258 | 28843 |

    That is about 1.4x ROM speed (ROM: 855/1710).
- `abu_simbel_std.tzx`:
  - 0x30 text
  - `Program: ABU SIMBEL` (225, LINE 10)
  - `Bytes: scr` 6912 @ 32768
  - `Bytes: abu` 39150 @ 26100

  All blocks are standard 0x10.
- `dogmole_turbo11.tzx`: each TAP block is one 0x11 block with exact ROM timings: pilot
  2168, sync 667/735, 0=855, 1=1710, pilot count 8063 (flag < 128) or 3223, 8 used bits,
  pause 1000 ms. ROM `LOAD ""` must load it like the TAP.
- `dogmole_tone_pulse.tzx`: each TAP block becomes `0x12` (2168 T × 8063/3223), then
  `0x13` (2 pulses: 667, 735), then `0x14` (855/1710, 8 bits, pause 1000). It loads like the TAP.
- `int_skip_v1.z80` and `int_skip_v3.z80`: PC=0x9000 (popped from the SNA stack), SP=0x8FFC,
  IM 1, IFF1=IFF2=0, border 7. The v3 file has hardware mode 0 (48K) and the T-state
  counter = 0. Running either must behave exactly like `int_skip.sna`. The bytes at
  0x8FFA/0x8FFB (the stacked PC) are left in RAM.
- `libspectrum_empty.z80`: v3 header (extra length 54), hardware mode 0, PC=0x1234,
  SP=0x8000, all registers 0, three 16 KB pages. A pure format fixture, not runnable.

## 3. Doubts and caveats

1. **Turbo-loader TZX licensing.** I could not find a TZX with a real custom turbo loader
   that carries an explicit redistribution license. `abu_simbel_turbo.tzx` and
   `abu_simbel_std.tzx` are "Available" in ZXDB and Spectrum Computing hosts them, which
   only means no distribution denial is recorded. I found no written Dinamic permission.
   Keep them as local test data and do not publish them in a public repo without checking.
   The two `dogmole_*` synthetic TZX files cover block types 0x11/0x12/0x13/0x14 with a
   clean license, but they use ROM timings, not a custom loader.
2. **Dogmole license**: the MK1 repo's `LICENSE` file is GPL-2.0, but its README says
   "licencia LGPL". The game assets are Mojon Twins' and are shipped in that repo. Either
   way, redistribution is allowed.
3. **Snapshots**: I found no real-world `.z80` from a game or demo with an explicit license.
   The `.z80` files are generated from the MIT `int_skip.sna`. They are spec-conformant
   and the RLE round trip is verified, but no third-party emulator wrote them. The one
   emulator-written `.z80` (`libspectrum_empty.z80`) is a near-empty fixture. If a
   "real" `.z80` is needed, save one from Fuse or ZEsarUX after loading `dogmole.tap`.
4. **ROM dependence**: `int_skip.sna` and the TAP-based tests were built for the Sinclair
   48K ROM. They rely only on entry points that the TK90X ROM shares byte-for-byte
   (checked above). Other TK90X ROM differences could still change printed output. In total 1980 bytes
   differ, counted per 256-byte page:

   | page | bytes differing |
   |---|---|
   | 0x00 | 23 |
   | 0x01 | 177 |
   | 0x09 | 88 |
   | 0x13 | 100 |
   | 0x14 | 248 |
   | 0x15 | 82 |
   | 0x32 | 25 |
   | 0x38-0x3C | 1142 |
   | 0x3F | 13 |
   | other pages | 82 |

   0x38-0x3C is the 48K ROM's spare 0xFF area, now TK90X code. The 0x3D00-0x3FFF font
   differs in only 13 bytes. 0x09xx and 0x13xx-0x15xx hold the message and keyword tables,
   which are probably the TK90X's Portuguese messages and extra commands (inferred).
5. **CPU variant**: z80test and the ZEX CRCs assume a genuine Zilog NMOS Z80. FUSE
   `tests.expected` is also NMOS (`z80_is_cmos = 0`). If the TK90X model we target used a
   NEC µPD780C or another second-source Z80, `z80ccf`/`z80full` SCF/CCF results on real
   hardware may differ.
6. Copies of `z80full/doc/...tap` in `zesarux-extras/extras/media/spectrum/tests/` are an
   older z80test release (13758 bytes). Use the 1.2a files here.

## 4. Not downloaded, possibly useful later

`https://github.com/chernandezba/zesarux-extras` `extras/media/spectrum/tests/` has small
timing/ULA test TAPs: `btime-orig.tap`, `stime-orig.tap`, `fusetest.tap`, `contention.tap`,
`iocontention.tap`, `IR_Contention.tap`, `ulatest2.tap`, `ulatest3.tap`, `memoryrefresh.tap`,
`imtest.tap`, `keyboardtest.tap`, `testissuekeyboard.tap`, and `Timing_Tests-48k_v1.0.sna`.
Their individual licenses are not stated in that repo. The repo itself is GPL-3.0, and
`licenses/GPL-3.0.txt` is included for that reason. The same repo has
`extras/media/spectrum/tk90x/TK90X UDG Editor 2.0 (FINAL).tap` and 13 TK90X v3 ROMs
(`tk90x_v3roms/`).
