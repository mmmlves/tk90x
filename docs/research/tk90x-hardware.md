# Microdigital TK90X: hardware reference for emulation

Status: research note, 2026-10-14. Everything here is checked against at least one primary source:
the ROM bytes themselves, emulator source code, or the CPLD clone of the TK90X ULA. Statements
that come from only one secondary source are marked as such.

Sources used (cited below by short name):

| Short name | What | Where |
|---|---|---|
| **ROM** | `roms/tk90x.rom`, 16384 bytes, CRC32 `3e785f6f` (same file and CRC as MAME `ROM_START(tk90x)`) | this repo |
| **48.rom** | Sinclair 48K ROM, CRC32 `ddee531f` | `https://raw.githubusercontent.com/chernandezba/zesarux/master/src/48.rom` |
| **ULA-VHDL** | Fábio Belavenuto and Victor Trucco, CPLD clone of the TK90X/TK95 ULA, `VHDL version/src/ula.vhd` | https://github.com/fbelavenuto/tk90x_ula (commit 630a64f, 2024-06-01) |
| **ESPectrum** | ESP32 emulator with separate "Microdigital ULA 50/60 Hz" TK90X modes | https://github.com/EremusOne/ESPectrum (commit 2a2c335, 2025-11-17) |
| **ZEsarUX** | `src/cpu.c`, `src/storage/tape.c`, `src/operaciones.c`, `src/video/screen.c` | https://github.com/chernandezba/zesarux (commit 886bdc6, 2026-10-15) |
| **MAME** | `src/mame/sinclair/spectrum.cpp`, `spectrum.h`, `spectrum_v.cpp`, `spectrum_ula.cpp` | https://github.com/mamedev/mame (commit 49dc0db, 2026-10-15) |
| **zxsp** | kio's zxsp, `MachineTk90x.cpp`, `UlaZxsp.cpp`, `ZxInfo.cpp` | https://k1.spdns.de/Develop/Projects/zxsp/zxsp.alt10/Source/Uni/ |
| **Cantinho** | "Cantinho do TK90X" blog (Flávio Matsumoto), measurements on real hardware | https://cantinhotk90x.blogspot.com/ |
| **Luccas** | "Como deixar o TK90X/TK95 rodando a 50Hz" | https://www.luccas.com.br/index.php/8-bits/artigos/18-como-deixar-o-tk90x-tk95-rodando-a-50hz |
| **SinclairCollection** | TK90X page | https://www.sinclaircollection.site/?page_id=468 |
| **Wikipedia** | en: https://en.wikipedia.org/wiki/TK90X , pt: https://pt.wikipedia.org/wiki/TK90X | |

Fuse has **no** TK90X machine. Its model list has 16K, 48K, 48K NTSC, 128K, +2, +2A, +3, +3e,
TC2048, TC2068, TS2068, Pentagon and Scorpion variants, and SE. The Cantinho blog ran the TK ROM in
Fuse as a custom ROM on a 48K machine.

---

## 0. Executive summary: what to build

| Parameter | 60 Hz (Brazilian default, PAL-M) | 50 Hz (switch or solder, PAL-N) | Sinclair 48K, for comparison |
|---|---|---|---|
| Master crystal | 14.302444 MHz (4 x PAL-M colour subcarrier) | same | 14.000 MHz |
| CPU clock | 14.302444 / 4 = **3,575,611 Hz** (measured 3.57541 MHz) | same | 3,500,000 Hz |
| T-states per scanline | **228** | **228** | 224 |
| Scanlines per frame | **262** | **312** | 312 |
| T-states per frame | **59,736** | **71,136** | 69,888 |
| Frame rate | 59.857 Hz (measured 59.8535 Hz) | 50.264 Hz | 50.080 Hz |
| Frame time | 16,706.5 us | 19,894.8 us | 19,968 us |
| Display lines | 192 | 192 | 192 |
| Bottom border lines (after display, before blank) | 32 | 56 | 56 |
| Vertical blank / sync | 8 lines (VSync 4 lines) | 8 lines (VSync 4 lines) | 8 |
| Top border lines (after blank, before display) | 30 | 56 | 56 (48 visible) |
| INT asserted | display line 223, at T 137.5 of the line | display line 247, at T 137.5 of the line | line 248 (frame top) |
| INT length | 63 pixel clocks = 31.5 T, so use **32 T** | 32 T | 32 T |
| Display start after INT, in the 48K-style convention (48K = 14335) | **8759** (ESPectrum); 8754.5 + pipeline from the VHDL; 8760 measured on real hardware (Cantinho) | **14687** (ESPectrum); 14682.5 + pipeline | 14335 |
| Contention pattern | 6,5,4,3,2,1,0,0 on 0x4000-0x7FFF and ULA I/O | same | same |

Recommendations:

- **Default to 60 Hz** (228 x 262, 3.575611 MHz). This is what a Brazilian TK90X does out of the
  box, because ULA pin 1 is left floating. It also matches a 60 Hz host display, so frame pacing
  is easier.
- Offer a **50 Hz switch** (228 x 312). The real machine had a rear lever or a solder bridge for
  it. Several programs only work at 50 Hz (section 1.4).
- Optionally offer a third **"Sinclair/Ferranti ULA"** mode (224 x 312 @ 3.5 MHz). Argentine and
  Uruguayan TK90X units used a real Ferranti ULA, and this is what ZEsarUX and MAME emulate.
- Use the ROM unchanged. Do not trap or patch anything TK-specific. The tape routines (LD-BYTES at
  0x0556, LD-EDGE at 0x05E3/0x05E7, SA-BYTES at 0x04C2), KEY-SCAN, the IM 1 handler and the key
  tables are byte-identical to the 48K ROM (section 9).

---

## 1. Clock, frame and scanline timing

### 1.1 What the hardware does (TK90X ULA clone VHDL)

`ULA-VHDL`, `VHDL version/src/ula.vhd`:

- Master counter `hcc` counts 0..911 on the 14 MHz crystal (line 160). `hc = hcc(9:1)` counts
  **0..455 pixel clocks (7 MHz)** per line (lines 176-178). 456 pixel clocks = **228 CPU T-states
  per line**.
- Vertical counter `vc` increments at `hc = 455` and wraps at **`vc = 261` when `VERT50_60 = '1'`
  (60 Hz)** or at **`vc = 311` when `VERT50_60 = '0'` (50 Hz)** (lines 187-197). That gives 262 or 312
  lines. `VERT50_60` is **ULA pin 1** (line 78).
- Display area: `Border_n = 1` when `vc < 192` and `hc < 256` (lines 326-345). Display therefore
  starts at **vc = 0**. The pixel output window `Vout` is hc 11..267, an 11-pixel pipeline delay
  (lines 349-360).
- HBlank: hc 320..415. HSync: hc 340..371 (lines 210-238). The horizontal budget, in counter
  terms, is 128 display (hc 0..255) + 32 right border (256..319) + 48 blanking (320..415) + 20 left
  border (416..455) = 228 T; after the 11-pixel output pipeline the visible borders are about
  26.5 T right and 25.5 T left. Real hardware on an LCD TV (Cantinho, section 1.3) showed 24 T
  left border + 128 T paper + 24 T right border + 52 T not displayed.
- VBlank: vc 224..231 (60 Hz) or 248..255 (50 Hz). VSync: vc 224..227 or 248..251 (lines 267-298).
- **INT**: low from (vc=223, hc=275) to (vc=223, hc=338) at 60 Hz, or vc=247 at 50 Hz (lines
  303-318). That is 63 pixel clocks = **31.5 T**, the same as the Sinclair 32 T.
- The chroma subcarrier output is commented as "3.575611 MHz" (line 49). The CPU clock is the same
  divided crystal, so CPU = 3.575611 MHz.

Derived numbers, taking T = 0 when INT goes low:

- 60 Hz: INT to the start of display line 0 is (262-223) x 228 - 137.5 = **8754.5 T**, plus the
  pixel pipeline.
- 50 Hz: INT to the start of display line 0 is (312-247) x 228 - 137.5 = **14682.5 T**, plus the
  pipeline.
- Lines between display end and INT: 32 (60 Hz) or 56 (50 Hz) bottom-border lines (vc 192..223 or
  192..247).

### 1.2 What ESPectrum uses (empirically tuned, consistent with the VHDL)

`ESPectrum` `include/cpuESP.h:41-70`, `include/Video.h:45-56`, `src/CPU.cpp:91-121`,
`src/Video.cpp:325-345`:

| Constant | 48K | TK 50 Hz | TK 60 Hz |
|---|---|---|---|
| `TSTATES_PER_LINE` | 224 | 228 | 228 |
| `TSTATES_PER_FRAME` | 69888 | 71136 (= 228x312) | 59736 (= 228x262) |
| `MICROS_PER_FRAME` | 19968 | 19895 | 16707 |
| `TS_SCREEN` (start of paper) | 14335 | **14687** | **8759** |
| `INT_START`/`INT_END` | 0/32 | 0/32 | 0/32 |
| Floating bus first line (`Ports.cpp:83-95`) | 64 | 64 | 38 |

The ESPectrum TS_SCREEN values are 4.5 T after the VHDL-derived 14682.5 and 8754.5, which is the
pipeline offset. Its config is `Config::ALUTK`: 0 = Ferranti (48K timing), 1 = Microdigital 50 Hz
(**its default**), 2 = Microdigital 60 Hz (`src/ESPConfig.cpp:152`).

ESPectrum also scales tape pulses by `FACTORALUTK 1.021591929` in the Microdigital modes
(`include/Tape.h:109`, applied in `src/Tape.cpp:376-381` and `:899`). That is the clock ratio:
1.021591929 x 3.5 MHz = 3.575572 MHz, 11 ppm below the nominal 3,575,611.49 / 3,500,000 = 1.0216033
(the 128K constant `FACTOR128K 1.013376779` is likewise 3.546900/3.5). A TZX/TAP pulse length in 3.5 MHz
T-states must be multiplied by this factor when the CPU runs at 3.5756 MHz, so that real-time
pulse lengths stay correct. Our emulator must do the same: convert TAP/TZX timings from 3.5 MHz
T-states to real time, then to TK T-states.

### 1.3 Measurements on real hardware

- Cantinho, "Determinação da frequência do TK90X" (2013-03): the 60 Hz interrupt rate was measured
  as **59.8535 Hz**, by counting FRAMES over 168,600 s against NTP. With 59736 T/frame this gives
  a CPU clock of **3.57541 MHz**, against a nominal 3.575611 MHz with a 14.302444 MHz crystal. The
  59736 T/frame figure comes from another Cantinho timing test.
  https://cantinhotk90x.blogspot.com/2013/03/determinacao-da-frequencia-do-tk90x.html
- Cantinho, "Temporização de vídeo da ULA e do clone" (2013-03), 60 Hz, original Microdigital ULA:
  `minfo` and `fusetest` give **59736 T per frame**; Videotime 2 gives 228 T per line
  ((52516-8740)/192 = 228 exactly), 128 T paper, 262 lines, and the first paper point at
  **8760 T** after INT (matches ESPectrum 8759 and the VHDL 8754.5 + pipeline). The same post
  measured an early (2013) build of the CPLD clone at 60192 T = 264 lines; the current `ula.vhd`
  wraps at vc = 261 (262 lines). No 50 Hz measurement was published there ("Pretendo fazer os
  testes em 50 Hz futuramente").
  https://cantinhotk90x.blogspot.com/2013/03/temporizacao-de-video-da-ula-e-do-clone.html
- Wikipedia (pt, en) gives "Z80A 3.58 MHz", which is the same figure rounded.
- **Do not copy zxsp's TK90X timing.** Its `ZxInfo.cpp` TK90X entry (lines 996-1045) uses
  14.32 MHz / 4 = 3.58 MHz, 224 T x 264 lines = 59136 T, 60.538 Hz, and `UlaTk90x::set60Hz`
  (`UlaZxsp.cpp:455-472`) says it is borrowed from a Chilean NTSC Spectrum; in 50 Hz mode it
  switches to plain 48K Issue 3 timing. Both contradict the VHDL and the Cantinho measurements.

### 1.4 50/60 Hz switch

- Luccas: ULA **pin 1 floating = 60 Hz** (all Brazilian boards), **pin 1 grounded = 50 Hz**
  (Uruguayan boards). There is a ground pad next to it for a solder bridge or a switch. Cantinho
  (2012-11, "TK90X 50 Hz") describes "uma gota de solda" and mentions a blue lever switch on the rear
  of some units.
- Wikipedia: video "hardware-selectable to PAL-M (60 Hz), PAL-N (50 Hz) or NTSC (60 Hz)". NTSC
  units use a different crystal (about 14.318 MHz). Not needed for emulation.
- Software that breaks at 60 Hz (Cantinho, Luccas): **Comando Tracer** hangs after losing a life,
  **Higher State** (part 3) and **Shock Megademo** hang, **Aquaplane** draws its border split in the
  wrong place, **Inner Space** part-2 music is out of rhythm, and AY music plays 20% faster in
  general. Everything above works at 50 Hz.
- Every HALT- or FRAMES-based delay scales with the frame rate. PAUSE 50 lasts 0.835 s at 60 Hz.
  The key auto-repeat delays (REPDEL/REPPER, counted in interrupts) and the 50-HALT pause in
  SA-CONTRL (0x098F `LD B,$32; HALT; DJNZ`) also scale.

### 1.5 What ZEsarUX and MAME do (neither emulates the Microdigital ULA)

- **ZEsarUX** treats TK90X, TK90XS (Spanish), TK95 and TK95S as plain 48K machines.
  - Memory: `src/cpu.c:2651-2658`.
  - Timing: `MACHINE_IS_SPECTRUM_16_48`, so 224 T/line, 8 invisible + 56 top border + 192 + 56
    bottom = 312 lines, 69888 T (`src/cpu.c:2077-2086`, `2163-2170`).
  - Contention: 48K contention tables.
  - ROM: `tk90x.rom` / `tk90xs.rom` (`src/cpu.c:3384-3398`).
  - No 60 Hz option.
- **MAME** `tk90x` (`spectrum.cpp:1123`) uses `spectrum_clone`, which is the 48K config with the
  IO map swapped for `spectrum_clone_io`.
  - Floating bus: none for clones; unassigned ports return 0xFF (`spectrum.cpp:811-818`, `468-476`).
  - Clock: X1 = 14 MHz / 4 = 3.5 MHz (`spectrum.h:69`).
  - Timing: 224 T/line (`spectrum.h:88`), 312 lines.
  - IRQ: 32 T (`spectrum.cpp:716`).
  - Contention: 48K ULA contention device (pattern at `spectrum_ula.cpp:182`), "Early" by default
    (`m_is_timings_late = false`, `spectrum_ula.cpp:34`; "Contention" config at `spectrum.cpp:665`).
  - Port FE: Issue 2 by default (`spectrum.cpp:662-664`).
  - The header comment "No contended memory" is out of date.

---

## 2. Memory contention and floating bus

### 2.1 Contention: yes, same pattern as the 48K

- ULA-VHDL lines 533-560: `WaitSignal` is active (low) when `Border_n = 1` (display area: vc<192
  and hc<256) and `hc(3:0)` is in 3..14. That covers 12 of every 16 pixel clocks, about 6 T of each
  8 T. The CPU clock is held (`CPUClk <= CLKContention or hc(0)`) when:
  - the address bus is in 0x4000-0x7FFF (A15=0, A14=1), whatever the cycle type: `cdet1` is not
    gated by MREQ, exactly like the Sinclair ULA, so non-MREQ cycles with such an address and I/O
    to ports 0x40xx-0x7Fxx also contend; or the ULA chip select `CS` (pin 2, IORQ combined with
    A0=0) is low (`cdet1 = (A15 or not A14) and ioreq_n`, line 550), and
  - the condition `cdet2 = not (ioreqtw3 and mreqt23)` holds, i.e. MREQ or ULA IORQ was active at
    the previous CPU clock edge (line 551).

  This is the Chris Smith ULA logic. The VHDL README says the design is based on it.
- ESPectrum uses the 48K wait table `6,5,4,3,2,1,0,0` x16 for TK memory accesses (`Video.cpp:105`).
  The base is `TS_SCREEN_TK_50` / `_60`, and there are 128 contended T per 228-T line.
- **I/O contention differs** in ESPectrum's TK modes. `Ports.cpp:157-192` (`tkIOcon`) adds extra
  delays from tables `contention2 = {6,6,5,4,3,2,1,0}` for even (ULA) ports and `contention3`
  (6,6,12,12,11,...) for ports whose high byte is 0x40-0x7F. These apply during display lines, with
  line offset 64 (50 Hz) or 38 (60 Hz) and column offset 93. Copy it only if multicolour or
  port-timing demos matter. The standard 48K I/O contention rules are a reasonable first
  approximation.
- Recommended model, taking T = 0 at INT. A display line L (0..191) starts its contended window at
  `S + L*228`, with S = 8759 (60 Hz) or 14687 (50 Hz). Inside a line, for offset `o` in 0..127, the
  delay is `{6,5,4,3,2,1,0,0}[o & 7]`; for offset 128..227 it is 0. Apply Fuse-style rules (the M1,
  read, write and IO contention sequences) exactly as on a 48K.

### 2.2 Floating bus: probably yes (sources disagree)

- MAME: no floating bus for any clone, returns 0xFF. This is a policy choice ("any clone known to
  have the same floating bus behaviour ... should be changed to use the spectrum machine",
  `spectrum.cpp:1116`), not a TK measurement.
- ESPectrum emulates a TK floating bus (`Ports.cpp:83-95`, `getFloatBusDataTK`):
  `line = t/228 - (50Hz ? 64 : 38)`, valid when line < 192.
  `halfpix = (t % 228) - 99`, which returns 0xFF if `halfpix >= 125` or `halfpix & 4`.
  Otherwise, `halfpix & 1` selects attribute or bitmap, and the byte is
  `(halfpix>>2) + ((halfpix>>1)&1)`.
- zxsp, quoting Matsumoto and Belavenuto: on the TK90X, port FE bit 5 "is usually 1, but when the ULA
  accesses a screen attribute address it copies bit 5 from the attribute value". That only happens
  if ULA video fetches leak onto the CPU data bus, which is what a floating bus is.
- Recommendation: implement the floating bus, which Arkanoid, Cobra and Sidewize need, using
  ESPectrum's TK formula. Make it switchable.
- Phase: ESPectrum samples at the end of the I/O cycle (IOstart + 4) and returns the first bitmap
  byte at S+4; Fuse 48K reads at IOstart + 3 (after early and late contention) and returns it at
  14338 = S+3. Both mean "bitmap byte when the I/O cycle starts at S". This core reads the port at
  the T3 callback (IOstart + 3), so it uses base S+3 (`floating_bus` in machine.c).

---

## 3. Interrupt

- One maskable INT per frame, from the ULA: 63 pixel clocks = 31.5 T, **use 32 T**. It is asserted
  at hc = 275 (T 137.5) of display line 223 (60 Hz) or 247 (50 Hz), which is 32 or 56 lines after the
  last display line (ULA-VHDL 303-318).
- ESPectrum: `INT_STARTTK 0`, `INT_ENDTK 32` (`cpuESP.h:69-70`).
- No NMI source on the board. The ROM's NMI handler bug **is fixed** (section 9.3), so a "NMI
  button" would work through NMIADD (0x5CB0).
- IM 1 handler at 0x0038-0x0052 is byte-identical to the 48K. It increments FRAMES and calls
  KEYBOARD at 0x02BF.

---

## 4. Port 0xFE

### 4.1 Write (ULA decodes A0 = 0)

ULA-VHDL lines 575-585: `bit 4 -> speaker/EAR out`, `bit 3 -> MIC out`, `bits 2..0 -> border
colour`. This matches the 48K. Sound reaches the TV through RF only; there is no internal
speaker (Wikipedia pt). Beeper level mixing (EAR + MIC) can use the 48K model.

### 4.2 Read

The keyboard and EAR are read through the ULA `KEYBOARD` strobe (pin 10, VHDL lines 588-592),
which enables an external buffer. The high address byte selects keyboard half-rows exactly as on a
48K: A8..A15 = rows 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F. Bits 0-4 are active-low keys.

Per zxsp (`MachineTk90x.cpp` comment, quoting Flávio Matsumoto's article "porta 254", based on
findings by Fábio Belavenuto) and Cantinho "ROM e Porta 254 (parte 2)":

| Bit | TK90X | TK95 | Sinclair 48K |
|---|---|---|---|
| 7 | **always 0** (diode D1 on Brazilian boards; floats if D1 is missing) | 1 | 1 |
| 6 (EAR in) | **1 when no signal**, follows the tape input. It does not depend on OUT bits 3/4. | 1 when no signal | Issue 2: 1 if OUT bit 3 or 4 = 1. Issue 3: 1 if OUT bit 4 = 1 |
| 5 | 1, but = attribute bit 5 while the ULA is fetching an attribute | 1 | 1 |
| 4..0 | keys (0 = pressed) | same | same |

Notes for the emulator:

- **Bit 7** is what the *original* Microdigital ROMs use to pick the language: `IN A,($FE); RLA;
  RET NC` at 0x04B8, where bit 7 = 0 selects Portuguese and 1 selects Spanish. Our `tk90x.rom` is
  patched so that it ignores this (section 9.1). With our ROM, bit 7 only matters to games:
  **Samurai Warrior** hangs on its menu when bit 7 = 0, as it does on a real TK90X (Cantinho). Make
  it an option. "Authentic TK90X" = 0. Use 1 if you prefer maximum Spectrum compatibility.
- **Bit 6, "issue" behaviour**: the TK90X reads 1 when idle no matter what was written to bits
  3/4. With the ROM's usual border writes (bit 3 = 1, bit 4 = 0), that matches **Issue 2**, so
  "Issue 3-only" keyboard-reading bugs do not occur. Model it as `bit6 = ear_input_level`, where
  idle is high. ROM tape loading only looks at edges, so polarity does not matter there.
  ZEsarUX defaults to Issue 3 (`ula.c:69`), MAME to Issue 2 (`spectrum.cpp:662-664`), and ESPectrum to
  Issue 3 (`Config::Issue2 = false`).
- ESPectrum's default returned FE value is `port254default = 0xFF`. It sets 0x7F (bit 7 = 0) for
  the v1pt/v2pt ROMs (`ESPConfig.cpp:150-151`, `508-519`).

---

## 5. Joystick

- **Built-in Sinclair Interface 2 compatible port** (Atari 9-pin). Sources: Spectrum Computing
  entry 1000504 ("built-in Sinclair Interface 2 joystick port"), SinclairCollection ("compatible
  with joystick 1 of the ZX Interface 2"), Wikipedia pt ("padrão Interface 2").
- Mapping: joystick 1 = keys **6 (left), 7 (right), 8 (down), 9 (up), 0 (fire)**, read on port
  **0xEFFE**, bits 4,3,2,1,0 = 6,7,8,9,0, active low. That is: bit 0 = fire, 1 = up, 2 = down,
  3 = right, 4 = left.
- Naming trap: "Sinclair 1" means 6-0 in Fuse (`peripherals/joystick.c:58-61`,
  `sinclair1_key = {6,7,9,8,0}`) and in the Interface 2 manual, but zxsp calls 1-5 "S1" and 6-0
  "S2" (`Items/Joy/SinclairJoy.cpp:32-46`). Use the key numbers, not the port number, in the UI.
- zxsp (`Items/Joy/SinclairJoy.cpp:86-90`, `Tk90xJoy`) notes that the TK90X port carries the right joystick
  (67890) with COMMON on pin 8, and with an adapter also the left joystick (12345: 1 left, 2 right,
  3 down, 4 up, 5 fire, port 0xF7FE) with COMMON on pin 7.
- No Kempston (port 0x1F) on the board. MAME defaults its expansion slot to "kempjoy" for all
  Spectrum models (`spectrum.cpp`, `SPECTRUM_EXPANSION_SLOT(..., "kempjoy")`), and ESPectrum offers
  Kempston, Sinclair, Cursor and Fuller. These are conveniences, not TK hardware. Offering
  Kempston and Cursor as options is harmless, but the **default should be Sinclair port 1
  (6-0)**.
- The ROM writes 0 to **port 0xDF** once at boot (section 7). That port is A5 = 0, which a Kempston
  interface also decodes, but the write has no documented purpose. MAME note 15: "purpose unknown
  (probably display mode as TS2068) and thus is not emulated". Ignore it.

---

## 6. Keyboard

- 40-key rubber ("chiclete") keyboard (Wikipedia pt). Same 8x5 matrix and same key positions as
  the 48K (Wikipedia en: "key placement matches the original Spectrum keyboard").
- The ROM key tables 0x0205-0x028D, KEY-SCAN 0x028E, KEYBOARD 0x02BF and K-TEST/K-DECODE
  0x031E-0x03B4 are **byte-identical** to the 48K (section 9). Every key produces the same code.
  The differences are in the token names and two remapped characters:
  - Token 0xD7: `BEEP` renamed **`SOUND`** (E mode, SYMBOL SHIFT + Z). Same function, same token.
  - Token names shortened to make room: `CONTINUE` → **`CONT`** (0xE8), `GO TO` → **`GOTO`** (0xEC),
    `GO SUB` → **`GOSUB`** (0xED), `RANDOMIZE` → **`RAND`** (0xF9). Same keys, same tokens.
  - SYMBOL SHIFT + X, which gives `£` (0x60) on the 48K, is converted by KEY-INPUT (0x10C4 →
    0x1544) into code **0x00 = `UDG`** keyword. The `£` legend is replaced by UDG
    (Wikipedia en, SinclairCollection).
  - E mode + SYMBOL SHIFT + P, which gives `©` (0x7F), is converted into code **0x01 = `TRACE`**
    keyword.
  - The glyphs for 0x60 and 0x7F in the ROM font are now **Σ** and **Δ** (section 9.4).
    PRINT CHR$ 96 / CHR$ 127 show them.
- **`LOAD ""` is typed exactly as on a 48K**: in K mode press **J** (LOAD), then **SYMBOL SHIFT+P**
  twice (`""`), then **ENTER**. Verified from the identical key tables and the unchanged token
  0xEF = `LOAD`. ZEsarUX uses the same sequence for TK90X: it waits for PC = 0x12A9, then sends
  ENTER, J, SYMBOL held + P, P, ENTER (`storage/tape.c:1832-1836`, `1876-1892`;
  `operaciones.c:5542-5614`). Our ROM also reaches 0x12A9 after its banner (section 7), so the same
  trap address works.
- Matrix for an emulator (rows = A8..A15 low, bit 0..4):
  - FEFE: CAPS, Z, X, C, V
  - FDFE: A, S, D, F, G
  - FBFE: Q, W, E, R, T
  - F7FE: 1, 2, 3, 4, 5
  - EFFE: 0, 9, 8, 7, 6
  - DFFE: P, O, I, U, Y
  - BFFE: ENTER, L, K, J, H
  - 7FFE: SPACE, SYMBOL, M, N, B

---

## 7. Boot behaviour

Simulated with SkoolKit 10.1 `trace.py` (no contention) on the 48K memory map, starting at PC=0:

| Event | Sinclair 48K ROM | TK90X ROM |
|---|---|---|
| PC reaches MAIN-1 0x12A9 (banner done; the ZEsarUX autoload trap) | 5,929,236 T | 6,457,973 T |
| PC reaches WAIT-KEY 0x15DE (ready for input) | 5,930,145 T (84.9 frames, 1.69 s) | 6,458,882 T |
| TK frames at 60 Hz (59736 T) | | **108.1 frames, 1.806 s** |
| TK frames at 50 Hz (71136 T) | | **90.8 frames, 1.806 s** |

Almost all of this time is NEW/RAM-CHECK at 0x11CB-0x1218. That code is identical to the 48K.
The TK banner and beep add about 0.15 s (about 528k T). Contention adds a few percent on top.
**Allow about 120 frames (60 Hz) or about 100 frames (50 Hz) before injecting keys, or better,
trap PC = 0x12A9 like ZEsarUX.**

Boot sequence (ROM addresses):

1. 0x0000 → START 0x11CB: RAM test and system variable init, identical to the 48K.
   - UDGs = copies of A-U.
   - Border white, ATTR 0x38.
2. 0x1295 `CALL $3AFA` replaces the 48K `XOR A; LD DE,$1538; CALL $0C0A` (copyright print):
   1. 0x3AFA `CALL $0013`, which does `XOR A; OUT ($DF),A; RET`, so port 0xDF gets 0.
   2. `JR $3B01` → `JP $1533`: `LD DE,$3AA8; RES 0,(IY+2)`, then `JP $3B04` → `JP $09A1`.
   3. 0x09A1 `CALL $0C0A` prints message 0 after 0x3AA8, which is the text at 0x3AA9-0x3AC4:
      **`"     TK90X - Color Computer"` + CR**. It is printed in the **upper screen, row 0,
      column 5**.
   4. 0x09A4 `LD HL,$01B7; LD E,$8B; CALL $03B5`: a **short beep** (BEEPER).
   5. 0x09AC-0x09E4: draws a **colour bar** in screen rows 22-23. The bar has 8 blocks of 4 cells,
      with PAPER **white, yellow, cyan, green, magenta, red, blue, black** from left to right
      (attributes 0x38..0x00 written downward from 0x5AFF). It is framed by 0xFF pixel rows at
      0x50C0/0x51C0 and 0x56E0/0x57E0, and 0xC0 column pixels.
3. Back at 0x1298: `SET 0,(IY+2)` (TV_FLAG), `SET 5,(IY+2)`, `JR $12A9` → MAIN-1 → editor.

There is **no "© Microdigital" copyright line**. The 48K text "© 1982 Sinclair Research Ltd" at
0x1539 is gone, overwritten by Portuguese messages and code.

Reference screenshot (paper area only, generated by the simulation above):
`docs/research/tk90x-boot-screen.png`.

---

## 8. Colour palette

The TK90X ULA outputs digital R, G, B and BRIGHT to an RGB→PAL-M encoder (`BRIGHT <= rI and (rR or
rG or rB)`, ULA-VHDL line 762, so black stays black when BRIGHT is set). No measured TK palette is
published, so use a standard Spectrum palette:

| Index | Colour | ZEsarUX (`video/screen.c:335`) | MAME (`spectrum_v.cpp:75`) | Atkinson "real 48K" (ZEsarUX `spectrum_colortable_1648_real`) |
|---|---|---|---|---|
| 0 / 8 | black | 000000 / 000000 | 000000 / 000000 | 060800 / 060800 |
| 1 / 9 | blue | 0000C0 / 0000FF | 0000BF / 0000FF | 0D13A7 / 161CB0 |
| 2 / 10 | red | C00000 / FF0000 | BF0000 / FF0000 | BD0707 / CE1818 |
| 3 / 11 | magenta | C000C0 / FF00FF | BF00BF / FF00FF | C312AF / DC2CC8 |
| 4 / 12 | green | 00C000 / 00FF00 | 00BF00 / 00FF00 | 07BA0C / 28DC2D |
| 5 / 13 | cyan | 00C0C0 / 00FFFF | 00BFBF / 00FFFF | 0DC6B4 / 36EFDE |
| 6 / 14 | yellow | C0C000 / FFFF00 | BFBF00 / FFFF00 | BCB914 / EEEB46 |
| 7 / 15 | white | C0C0C0 / FFFFFF | BFBFBF / FFFFFF | C2C4BC / FDFFF7 |

Recommendation: normal = 0xC0 per channel, bright = 0xFF per channel (ZEsarUX and Fuse
convention).

FLASH: the ULA-VHDL `FlashCnt` is a 6-bit counter clocked by VSync, and FLASH uses bit 5
(lines 458-471). FLASH therefore toggles every **32 frames**, as on the 48K. That is 0.53 s at
60 Hz instead of 0.64 s at 50 Hz.

---

## 9. ROM analysis: `tk90x.rom` against Sinclair `48.rom`

1980 bytes differ, in 37 ranges. Script: byte-by-byte compare. Ranges closer than 5 bytes are
merged.

### 9.1 Which ROM this is

- ZEsarUX ships `src/tk90x.rom` (3e785f6f), `tk90x_v1br.rom` (24c3b2f0), `tk90x_v2es.rom`
  (496573a1), `tk90xs.rom` and `tk95.rom` (17368e07). MAME's `tk90x` and `tk95` use the same CRCs
  as ZEsarUX `tk90x.rom` and `tk95.rom`.
- Compared with ESPectrum's embedded original ROMs (`include/roms/romTK90X_v1.h`, `_v2.h`):
  - **`tk90x.rom` = original TK90X v2 ROM with 2 bytes patched at 0x04B9 and 0x04BB.**
  - Original v2 at 0x04B8: `DB FE 17 D0` = `IN A,($FE); RLA; RET NC`, which selects the
    language from port FE bit 7 (0 = Portuguese).
  - Patched: `DB BF 17 C9` = `IN A,($BF); RLA; RET`, which **always gives Portuguese**.
  - `tk90x_v2es.rom` = the same with `DB BF 17 00` (NOP), which **always gives Spanish**.
  - `tk90x_v1br.rom` = original v1 with `RET NC` patched to `RET`, forced Portuguese.
  - `tk95.rom` = TK95 with the same two-byte patch.
  - SinclairCollection says "the language is set by byte 1211". 1211 decimal = 0x04BB, the
    patched byte.
- Consequence: **with our ROM, the emulator does not need port FE bit 7 = 0 to get Portuguese
  messages.** If someone loads an original v1 or v2 dump instead, bit 7 must read 0 for
  Portuguese.
- ZEsarUX `tk90xs.rom` (CRC32 `d1b2f352`) is byte-identical to ESPectrum's `romTK90X_v1.h`: it is
  the **unpatched original v1** ROM (`DB FE 17 D0` at 0x04B8). It gives Spanish only because
  ZEsarUX returns port FE bit 7 = 1.
- v1 against v2 (both original, unpatched): 853 bytes differ (854 is `tk90x_v1br.rom` against
  `tk90x.rom`). SinclairCollection says v1 shows "0 Executado" / "0 Ejecutado"
  where v2 shows "0 OK". Our ROM shows `Ok`.

### 9.2 Key routines: identity check (same address, same bytes)

| Routine | Range | Result |
|---|---|---|
| RST 0/8/10 | 0x0000-0x0012 | identical |
| **IM 1 handler MASK-INT** | **0x0038-0x0052** | **identical** |
| NMI handler | 0x0066-0x0073 | 1 byte differs (0x006D, bug fix) |
| Keyword token table | 0x0095-0x0204 | differs from 0x0149 on (SOUND/CONT/GOTO/GOSUB/RAND + UDG, TRACE) |
| Key tables | 0x0205-0x028D | identical |
| **KEY-SCAN** | **0x028E-0x02BE** | **identical** |
| KEYBOARD / K-TEST / K-DECODE | 0x02BF-0x03B4 | identical |
| BEEPER, BEEP | 0x03B5-0x04A9 | identical |
| ZX81-NAME (unused in 48K) | 0x04AA-0x04C1 | replaced (language/message-table selectors) |
| **SA-BYTES** | **0x04C2-0x053E** | **identical** |
| SA/LD-RET | 0x053F-0x0555 | identical |
| **LD-BYTES** | **0x0556-0x05E2** | **identical** |
| **LD-EDGE-2 / LD-EDGE-1** | **0x05E3-0x05E6 / 0x05E7-0x0604** | **identical** |
| SAVE-ETC | 0x0605-0x096F | 2 bytes (0x078F message pointer) |
| CLS etc. | 0x0D6B-0x0F2B | identical |
| START/NEW/RAM-CHECK | 0x11B7-0x1294 | identical (diff starts at 0x1295) |
| Character set | 0x3D00-0x3FFF | identical except chars 0x60 and 0x7F |

So tape traps at 0x0556 (and the 0x05E2 return or 0x053F exit) work unchanged. Edge-loop
detectors (LD-EDGE at 0x05E3 or 0x05E7) for fast loading work unchanged. Keyboard injection works
unchanged.

### 9.3 Every differing range and what it does

| Range | Content in TK90X ROM |
|---|---|
| 0013-0017 | New routine at 0x0013-0x0016: `XOR A; OUT ($DF),A; RET`. Called only from 0x3AFA at boot. 0x0017 = 0x0D (48K: 0xFF), unreferenced data. |
| 001D-001E | RST 18/20 GET-CHAR calls `$3CFB` instead of SKIP-OVER `$007D`. 0x3CFB: `CP 2; CCF; JP $0025`, and 0x0025: `RET NC; JR $007D`. Codes 0x00/0x01 (UDG/TRACE) are not skipped as control codes. |
| 0025-002F | 0x0025 helper above. 0x002B: `RST 28 / DEFB $02,$E2,$38 / RET` (calculator: delete, get-mem-2, end-calc), called from 0x2E24. |
| 005F-0065 | `LD ($5C91),A; JP $0ADC` (P_FLAG store, reached from 0x0A4B) |
| 006D | **NMI bug fixed**: `JR NZ,$0070` → `JR Z,$0070`, so a non-zero NMIADD (0x5CB0) is now jumped to |
| 0149-0204 | Token table: `BEEP`→`SOUND`, `CONTINUE`→`CONT`, `GO TO`→`GOTO`, `GO SUB`→`GOSUB`, `RANDOMIZE`→`RAND`, then two extra entries **`UDG`, `TRACE`** (index 92, 93) |
| 04AA-04BE | 0x04AA: `LD DE,$3A2A; LD HL,$001E` (tape-message table). 0x04B2: `LD DE,$1391; LD HL,$24DC` (report table). 0x04B8: language select (patched, section 9.1). `ADD HL,DE; EX DE,HL; RET` switches DE to the Spanish table (0x1391+0x24DC = 0x386D). |
| 078F-0790 | Tape header names table → 0x3A66 (`"Programa: "`, `"Matriz numerica: "`, `"Matriz alfanumerica: "`, `"Bytes: "`) |
| 0976-0979 | SA-CONTRL: `CALL $04AA` (selects `"Ligue o gravador, digite ENTER"` at 0x3A2B or the Spanish `"Conecte grabador, digite ENTER"` at 0x3A49) |
| 09A1-09F6 | The 48K tape-message table is replaced by boot banner, beep and colour-bar code (0x09A1-0x09E4), plus a PRINT-OUT hook 0x09E5 that prints codes 0x00/0x01 as the keywords UDG/TRACE. 0x09F4 PRINT-OUT calls 0x09E5. |
| 0A33 | PO-BACK fix: the instruction at 0x0A32 `LD A,$18` → `LD A,$19` (operand byte at 0x0A33; it is then compared with `CP B`) |
| 0A4B-0A4E | `JP $005F` |
| 0C46-0C49 | PO-SEARCH calls 0x151C (message-table search helper) |
| 0CA0-0CA3 | "scroll?" message now at 0x3AA1 |
| 0CF8-0CFB | Old "scroll?" text space is reused: `LD B,0; ADD HL,BC; JP (HL)` |
| 10C4-10C6 | KEY-INPUT: `CALL $1544` maps 0x60 (£) → 0x00 (UDG) and 0x7F (©) → 0x01 (TRACE) |
| 1295-129B | Boot: `CALL $3AFA` (banner) instead of the copyright print; `SET 0,(IY+2)` |
| 1342-1345, 134B | Error report printing uses `CALL $04B2` (language table) |
| 1393-1554 | **Portuguese report messages** (`Ok`, `NEXT sem FOR`, `Variavel inexiste`, `Indice erro`, `Sem memoria`, `Excede area de video`, `Numero excede limite`, `RETURN sem GOSUB`, `Fim de arquivo`, `STOP executado`, `Argumento ilegal`, `Inteiro excede limite`, `Sintaxe erro`, `BREAK-CONT repete`, `Sem dados`, `Nome ilegal`, `Memoria lotada`, `STOP em INPUT`, `FOR sem NEXT`, `Periferico ilegal`, `Cor ilegal`, `BREAK-CONT prossegue`, `RAMTOP ilegal`, `Comando perdido`, `Canal ilegal`, `FN sem DEF`, `Erro de parametro`, `Erro de leitura`) followed by helper code at 0x151B-0x1554 (boot entry 0x1533, KEY-INPUT filter 0x1544). |
| 170C, 1712-171B | CLOSE stream look-up table shifted by 1 byte (`JP $0CF8`) |
| 193C-193F | Line-listing char test → `JP $3AEE` (codes 0/1 printed as keywords) |
| 1B2A-1B2B | STMT-LOOP: `CALL $3AC5` (TRACE hook) instead of `CALL $16BF` (SET-WORK) |
| 1B47-1B48 | Statement dispatch: codes below 0xCE go to `JP $3B07` (UDG/TRACE handler) instead of report C |
| 257D-257F | End of S-SCRN$-S: `JP $2AB2` (STK-STO-$) replaced by `RET` (+ dead bytes `ED 47`). Not explained; observed from the bytes only. Check how SCREEN$ behaves on the emulator against a real TK if possible. |
| 2E24-2E26 | PRINT-FP: `RST 28 / get-mem-2 / end-calc` → `CALL $002B` (extra `delete`), a calculator-stack fix |
| 3032-3034, 3200, 3223-323E | Integer/FP fixes around TRUNCATE (0x3214), likely the "-65536" small-integer bug |
| 386E-3A12 | **Spanish report messages** (`Variable inexiste`, `Indice errado`, ...) |
| 3A13-3AC4 | Message helpers, tape prompts PT/ES, header names, `"scroll?"`, banner `"     TK90X - Color Computer"` |
| 3AC5-3CFF | TRACE hook (0x3AC5), UDG/TRACE command handler (0x3B07), accent-glyph builder (0x3B4B, tables 0x3B97-0x3BC5), UDG editor (0x3BC6-0x3CFA) |
| 3F01-3F05 | Font char 0x60 `£` → **Σ** |
| 3FF8-3FFF | Font char 0x7F `©` → **Δ** |

### 9.4 What the TK90X ROM adds

- **`UDG n`**, entered with SYMBOL SHIFT+X (stored as code 0x00, handler at 0x3B07):
  - `UDG 0`: loads **Portuguese accented letters** into UDG A..U. They were captured from the
    simulation:
    - acute: A=Á B=É C=Í D=Ó E=Ú F=á G=é H=í I=ó J=ú
    - grave: K=À L=à
    - circumflex: M=Â N=Ê O=â P=ê Q=ô
    - tilde: R=ã S=õ
    - cedilla: T=Ç U=ç
  - `UDG 1`: **Spanish set**. A..J are the same acute vowels; K=Ô, L=ñ, M=Ñ, N=Ã, O=Õ, P=Ü, Q=ü. R..U
    are left unchanged.
  - `UDG 2`: built-in **interactive UDG editor**. It shows an 8x8 grid. Keys 5/6/7/8 move the cursor,
    1 clears the glyph, 4 inverts it, 0 exits, and other keys select or toggle.
  - Any other argument: error report `Q` ("Erro de parametro").
- **`TRACE n`**, entered with E mode + SYMBOL SHIFT+P (stored as code 0x01):
  - `TRACE 1` sets FLAGS2 (0x5C6A) bit 7. `TRACE 0` clears it.
  - While it is set and a program is running, the hook at 0x3AC5 prints **`#<line number> `**
    before each statement executed.
  - Any other argument: report Q.
- No accented characters in the ROM font itself. They exist only as UDGs after `UDG 0` or `UDG 1`.
  At boot the UDGs are the plain A..U copies, as on the 48K.
- Bug fixes: NMI (0x006D), PO-BACK (0x0A33), PRINT-FP stack (0x2E24), TRUNCATE (0x3032/0x3223).
- Bilingual messages, Portuguese and Spanish. The table is chosen at 0x04B8, which is forced to
  Portuguese in our ROM.

---

## 10. Checklist: TK90X versus 48K special cases for the emulator

1. **Timing**: 228 T/line; 262 lines (60 Hz, default) or 312 (50 Hz); CPU at 3,575,611 Hz.
   - Frame length is 59,736 T or 71,136 T.
   - Contention or display start (48K-style constant) is 8759 or 14687.
   - INT lasts 32 T.
2. **Speed and audio**: run at 3.575611 MHz real-time.
   - The beeper sample rate maps from that clock.
   - Tape pulse lengths given in 3.5 MHz T-states must be scaled by x1.0216 (ESPectrum
     `FACTORALUTK`).
3. **Port FE read**: bit 7 = 0 by default (option to read 1); bit 6 = EAR level, idle 1, no
   issue-3 dependence on OUT bits; bit 5 = 1, or the attribute bit while the ULA is fetching (optional).
4. **Joystick**: Sinclair port 1 (keys 6-0 on 0xEFFE) by default. Kempston and Cursor are optional
   extras.
5. **Port 0xDF**: ignore writes (the ROM writes 0 at boot).
6. **Floating bus**: implement with TK offsets (line offset 38 at 60 Hz or 64 at 50 Hz, column
   offset 99), switchable.
7. **FLASH**: 32 frames per phase, so it is faster at 60 Hz.
8. **Boot**:
   - Reset → about 108 frames at 60 Hz → banner "TK90X - Color Computer" at the top and a colour
     bar at the bottom.
   - Trap PC = 0x12A9 for autoload, then type ENTER(optional), J, SS+P, SS+P, ENTER.
9. **No ROM traps are TK-specific**. All 48K tape and keyboard entry points are identical.
10. **Optional modes**:
    - 50 Hz switch: Comando Tracer, Higher State, Shock Megademo and Aquaplane need it.
    - "Ferranti ULA" 48K timing: 224 T x 312, 3.5 MHz, Issue 3 FE.
    - The 16K model: the TK90X was also sold with 16 KB RAM (Wikipedia pt). Emulate 48K by
      default.
11. **Known incompatibilities on real TK90X** (useful as test cases):
    - Samurai Warrior hangs because bit 7 = 0.
    - Mikie runs only on the TK95 (Wikipedia en).
    - The 60 Hz programs above.

---

## Appendix A: reproducing the analysis

Scratch files: `/tmp/claude-1000/-home-mmalves-workspace-emuladores-tk90x/659b9444-5a73-4631-80c8-b446ba89b5b4/scratchpad/`.

- ROM diff:
  - `scripts/diff.py` (byte compare plus range merge)
  - `scripts/chk.py` (routine identity table)
  - `scripts/tok.py`, `scripts/msgs.py` (token and message decoding)
- Disassembly: `scripts/dis.py`, using SkoolKit 10.1 `skoolkit.disassembler` unpacked from the PyPI
  wheel into `sk/ext/`.
- Boot timing and screenshot:

  ```
  python3 -I scripts/tr.py sk/ext --rom tk90x.rom -s 0 -S 0x15DE --stats 48 out.png
  ```

  The same tool ran the UDG 0/1 routines from a post-boot snapshot (`hw/boot/udg0.z80`,
  `udg1.z80`), and `scripts/udg.py` rendered the glyphs.
- Source checkouts: `zsrc/z` (ZEsarUX), `zsrc/mame`, `zsrc/esp` (ESPectrum), `zsrc/ula/u`
  (TK90X ULA clone).

---

## Verification

Adversarial re-check, 2026-10-14. All sources were downloaded fresh into
`/tmp/claude-1000/-home-mmalves-workspace-emuladores-tk90x/659b9444-5a73-4631-80c8-b446ba89b5b4/scratchpad/verify/dl/`
(not the earlier checkouts): `ula.vhd` (tk90x_ula master), ESPectrum master (`cpuESP.h`, `Video.h`,
`Video.cpp`, `CPU.cpp`, `Ports.cpp`, `Tape.h`, `Tape.cpp`, `ESPConfig.cpp`, `ESPectrum.cpp`,
`include/roms/romTK90X_v1.h`, `_v2.h`), ZEsarUX master (`cpu.c`, `cpu.h`, `ula.c`,
`storage/tape.c`, all TK ROMs), MAME master (`sinclair/spectrum.cpp`, `.h`, `spectrum_ula.cpp`,
`spectrum_v.cpp`), zxsp alt10 (`Machine/MachineTk90x.cpp`, `Items/Joy/SinclairJoy.cpp`,
`Items/Ula/UlaZxsp.cpp`, `ZxInfo/ZxInfo.cpp`), Fuse `peripherals/joystick.c`, and the two Cantinho
posts. ROM checks were byte compares of `roms/tk90x.rom` against ZEsarUX `48.rom` (script
`verify/scripts/romchk.py`, `hrom.py`).

Legend: CONFIRMED / WRONG (fixed in place above) / UNVERIFIABLE (no primary source reachable).

### Frame timing (50/60 Hz)

| Claim | Verdict | Evidence |
|---|---|---|
| 912 master clocks per line, 228 T per line | CONFIRMED | `ula.vhd:160` (`hcc = 911` wrap), `:178` (`hc = hcc(9:1)`); Cantinho Videotime 2: (52516-8740)/192 = 228 |
| 262 lines at 60 Hz, 312 at 50 Hz; pin 1 high = 60 Hz | CONFIRMED | `ula.vhd:187-197`, `:78`; Luccas (pin 1 unconnected = 60 Hz, grounded = 50 Hz) |
| 59,736 T/frame at 60 Hz | CONFIRMED | VHDL 228x262; ESPectrum `cpuESP.h:43`; real hardware (Cantinho, `minfo`/`fusetest`) |
| 71,136 T/frame at 50 Hz | CONFIRMED by VHDL + ESPectrum (`cpuESP.h:42`) only | No real-hardware 50 Hz measurement found |
| CPU = 14.302444 MHz / 4 = 3,575,611 Hz | CONFIRMED | `ula.vhd:47,49` (OSC 14.30244 MHz, subcarrier 3.575611 MHz), `:560` (`CPUClk <= ... or hc(0)`, hc(0) = OSC/4) |
| Measured 59.8535 Hz → 3.57541 MHz | CONFIRMED | Cantinho: 10,091,294 interrupts / 168,600 s |
| 59.857 / 50.264 Hz, 16,706.5 / 19,894.9 us | CONFIRMED | arithmetic; ESPectrum `MICROS_PER_FRAME_TK_60 16707`, `_TK_50 19895` (`cpuESP.h:51,55`) |
| Border/blank line counts 32/8/30 (60 Hz), 56/8/56 (50 Hz); VSync 4 lines | CONFIRMED | `ula.vhd:267-298`, `:326-345` |
| Horizontal budget "26 + 128 + 48 + 26" | IMPRECISE, fixed | Counter split is 128 + 32 + 48 + 20; ~26.5/25.5 only after the 11-pixel pipeline |
| Display start 8759 (60 Hz) / 14687 (50 Hz) | CONFIRMED | `Video.h:52-53`; VHDL 8754.5 / 14682.5 + pipeline; real hardware 8760 at 60 Hz (Cantinho) |
| The 4.5 T offset "is the pipeline" | UNVERIFIABLE (plausible) | VHDL pipeline is 11 px = 5.5 T; exact offset needs a gate-level sim |
| FACTORALUTK = clock ratio | CONFIRMED (refined) | 1.021591929 x 3.5 = 3.575572 MHz; applied to all pulse lengths, `Tape.cpp:376-381` |
| ZEsarUX: TK = plain 48K, 224 T, 312 lines, no 60 Hz | CONFIRMED | `cpu.h:561` (`MACHINE_IS_SPECTRUM_16_48` covers ids <= 5), `cpu.c:2077-2086`, `:2163-2170`, `:2651-2658`, `:3384-3398` |
| MAME: `spectrum_clone`, 3.5 MHz, 224 T, 32 T IRQ | CONFIRMED | `spectrum.cpp:811-818`, `:732`, `:716`, `:1123`; `spectrum.h:69,88` |
| MAME line refs `spectrum.cpp:661`, `spectrum_ula.cpp:190` | WRONG (line drift), fixed | Issue 2 at `spectrum.cpp:662-664`; early timing `spectrum_ula.cpp:34`; pattern `:182` |
| (new) zxsp TK90X timing | WRONG in zxsp, warning added | 224 x 264 = 59136 T at 3.58 MHz, copied from a Chilean NTSC Spectrum (`UlaZxsp.cpp:455-472`) |

### Contention

| Claim | Verdict | Evidence |
|---|---|---|
| Contention exists, 6 of every 8 T stopped during the 128 paper T of each display line | CONFIRMED | `ula.vhd:533-548` (hc(3:0) in 3..14 while Border_n = 1), `:550-560` |
| Applies to 0x4000-0x7FFF and to ULA I/O (even ports) | CONFIRMED | `cdet1`, `ula.vhd:550`; `CS` = IORQ + A0 (`:69`, `:520`) |
| "access is MREQ with A15=0, A14=1" | WRONG, fixed | `cdet1` is address-only (not MREQ-gated), as on the Sinclair ULA; MREQ enters only via the delayed `cdet2` |
| Pattern 6,5,4,3,2,1,0,0 | CONFIRMED structurally (VHDL) and by ESPectrum `Video.cpp:105` `wait_st_std`; exact phase vs INT UNVERIFIABLE without simulation | |
| ESPectrum `tkIOcon` tables and offsets 64/38, 93 | CONFIRMED | `Ports.cpp:157-192` |
| Floating bus: ESPectrum formula, MAME 0xFF for clones | CONFIRMED | `Ports.cpp:83-95`; `spectrum.cpp:468-476`, `:811-818`, `:1116` |
| Floating bus exists on real TK | UNVERIFIABLE | only the zxsp/Matsumoto port-FE bit-5 note; no direct test found |

### Interrupt

| Claim | Verdict | Evidence |
|---|---|---|
| INT low at vc=223 (60 Hz) / vc=247 (50 Hz), hc=275 (T 137.5), high at hc=338 → 63 px = 31.5 T | CONFIRMED | `ula.vhd:303-321` |
| INT to paper: 8754.5 T / 14682.5 T | CONFIRMED | (262-223)x228-137.5; (312-247)x228-137.5 |
| ESPectrum INT 0..32 T | CONFIRMED | `cpuESP.h:69-70` |
| IM 1 handler 0x0038-0x0052 identical | CONFIRMED | byte compare |
| NMI fix at 0x006D (`JR NZ` → `JR Z`) | CONFIRMED | 48K `20 01`, TK `28 01` |

### Joystick

| Claim | Verdict | Evidence |
|---|---|---|
| Built-in Interface 2-compatible port, keys 6-0 | CONFIRMED (secondary sources only) | Spectrum Computing 1000504; SinclairCollection "compatible with joystick 1 of ZX Interface 2 (67890)"; pt.wikipedia "padrão 'Interface 2'", "Entrada de 9 pinos padrão Atari 2600"; zxsp `SinclairJoy.cpp:86-90`. No schematic checked. |
| Port 0xEFFE, bit0 fire, 1 up, 2 down, 3 right, 4 left, active low | CONFIRMED | Fuse `joystick.c:58-61`; zxsp `SinclairJoy.cpp:32-46` |
| 1-5 on 0xF7FE via adapter | CONFIRMED as zxsp's claim only | zxsp comment (German), not otherwise sourced |
| MAME default slot "kempjoy" | CONFIRMED | `spectrum.cpp:771`, inherited by `spectrum_clone` |
| No Kempston on board | UNVERIFIABLE (no source says either way except absence) | |
| Port 0xDF write at boot, MAME note 15 | CONFIRMED | ROM 0x0013 `AF D3 DF C9`; `spectrum.cpp:169-170` |
| (new) "Sinclair 1/2" naming is inverted between Fuse and zxsp | added | see section 5 |

### ROM routine equivalences and boot message

| Claim | Verdict | Evidence |
|---|---|---|
| 1980 bytes differ, 37 ranges (gap < 5 merged) | CONFIRMED | `romchk.py` (71 strictly contiguous runs) |
| Identical: RST 0-10, MASK-INT, key tables, KEY-SCAN 0x028E, KEYBOARD..K-DECODE, BEEPER, SA-BYTES 0x04C2-0x053E, SA/LD-RET 0x053F, LD-BYTES 0x0556-0x05E2, LD-EDGE-2/1 0x05E3/0x05E7-0x0604, CLS block, START/NEW 0x11B7-0x1294 | CONFIRMED | byte compare, 0 differences in each range |
| SAVE-ETC differs only at 0x078F-0x0790; font only at 0x3F01-05, 0x3FF8-FF | CONFIRMED | `LD DE,$09C0` → `LD DE,$3A66` |
| SA-CONTRL 50-HALT pause at 0x098F | CONFIRMED | `06 32 76 10 FD` in both ROMs |
| 0x0013-0x0017 is the routine | IMPRECISE, fixed | routine is 0x0013-0x0016; 0x0017 = 0x0D |
| PO-BACK fix "`CP $18` → `CP $19`" | WRONG, fixed | it is `LD A,$18` → `LD A,$19` at 0x0A32 (followed by `CP B`) |
| tk90x.rom = original v2 + 2 bytes (0x04B9 FE→BF, 0x04BB D0→C9) | CONFIRMED | ESPectrum `romTK90X_v2.h` (CRC `6cc96389`) differs in exactly those 2 bytes |
| v2es = `DB BF 17 00`; v1br = v1 with 0x04BB D0→C9 (keeps `IN A,($FE)`); tk95.rom also `DB BF 17 C9` | CONFIRMED | v2es differs from tk90x.rom only at 0x04BB; v1br differs from ESPectrum v1 only at 0x04BB |
| v1 vs v2 = 854 bytes | WRONG, fixed | 853 between the original v1 and v2; 854 is v1br vs tk90x.rom |
| (new) ZEsarUX `tk90xs.rom` | added | CRC `d1b2f352` = ESPectrum v1, unpatched; v1 report 0 is "...Executado" (bytes at 0x1393) |
| Language table: 0x04B2 `LD DE,$1391; LD HL,$24DC`, Spanish at 0x386D | CONFIRMED | `11 91 13 21 DC 24`; SinclairCollection "byte at 1211: 201 Portuguese; 208 (v.1) or 0 (v.2) Spanish" |
| Boot: 0x1295 `CALL $3AFA` replaces `XOR A; LD DE,$1538; CALL $0C0A`; 0x1298 `SET 0,(IY+2)` | CONFIRMED | disassembly of both ROMs |
| 0x3AFA → `CALL $0013` → `JP $1533` | CONFIRMED, wording fixed | path is `JR $3B01` then `JP $1533` |
| Message text 0x3AA9-0x3AC4 `"     TK90X - Color Computer"` + 0x8D | CONFIRMED | 0x3AA8 = 0xBF (end of "scroll?"), 27 chars, 0x3AC4 = 0x8D |
| Printed at upper screen row 0, column 5 | CONFIRMED from code (`RES 0,(IY+2)` after CLS, 5 leading spaces) and the simulation screenshot; not compared with a real-machine photo | |
| Beep `LD HL,$01B7; LD E,$8B; CALL $03B5` at 0x09A4 | CONFIRMED | HL (pitch) is fixed; D (high byte of the cycle count, i.e. duration) is not loaded, so the length depends on D left by PO-MSG (UNVERIFIABLE here) |
| Colour bar rows 22-23, white..black left to right, frame at 0x50C0/0x51C0/0x56E0/0x57E0 + 0xC0 left edge | CONFIRMED | disassembly 0x09AC-0x09E4 (attrs 0x00..0x38 written downward from 0x5AFF, 4 cells each, 2 rows) |
| 48K copyright text at 0x1539 | CONFIRMED | 0x1538 = 0x80, 0x1539 = 0x7F (©) |
| ZEsarUX autoload trap PC = 0x12A9 for TK machines | CONFIRMED | `storage/tape.c:1832-1836`, `:1876-1892` |
| Boot T-state counts (6,457,973 T etc.) | UNVERIFIABLE here | not re-simulated; consistent with the identical RAM-check code |

### Other spot checks

- `BRIGHT <= rI and (rR or rG or rB)` at `ula.vhd:762`; FLASH from `FlashCnt(5)` clocked by VSync
  (`:458-471`): CONFIRMED.
- Software failing at 60 Hz: Aquaplane and Comando Tracer CONFIRMED by Luccas; Higher State, Shock
  Megademo and Inner Space rest on Cantinho only (not re-checked). Luccas names the AY-music games
  Auf Wiedersehen Monty, Rainbow Islands, Elevator Action and Cybernoid 2.
- 16 KB / 48 KB versions: CONFIRMED (pt.wikipedia "Foram produzidas duas versões ... 16 ou 48KB").
- Mikie runs only on the TK95: CONFIRMED (en.wikipedia).
