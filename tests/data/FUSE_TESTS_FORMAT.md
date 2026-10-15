# FUSE Z80 core tests: file format and harness semantics

Files: `tests.in` (161511 bytes, md5 `370bf3e63a15611ca187f980f7153ce7`) and
`tests.expected` (323000 bytes, md5 `6fa8226283a10540b51c6e5234af8af3`), from
Fuse `z80/tests/` (SourceForge `fuse-emulator/fuse`, branch `master`, head
`abb1c502a3e64ca2a0d91813b4ccc3125c7cef88`; the last commit that touched
`tests.expected` is `0fc0cdd40f5ef7d0f2384745692642548fcc0005`). License: GPL-2.0-or-later.

They contain **1356 tests**. The test names appear in the same order in both files.
The test names are the opcode bytes in hex, with suffixes such as `_1` or `_2`
(for example `00`, `02_1`, `cb46_3`, `ddcb06`, `edb0`, `76`).

Primary sources:
- `z80/tests/README` (format description; quoted and expanded below)
- `z80/coretest.c` (the harness that produced `tests.expected`):
  https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/z80/coretest.c
- `z80/z80_ops.c` (the opcode loop, `while( tstates < event_next_event )`, line 146)
- `z80/z80.c` `z80_reset()` (lines 172-191), `z80/z80.pl` `opcode_HALT` (line 656),
  `opcode_SCF`/`opcode_CCF` (lines 1096 and 524)

## 1. `tests.in`

Each test is laid out like this. Tokens are separated by whitespace and the harness reads them
with `fscanf`, so the column alignment does not matter:

```
<name>                                  one line, arbitrary text (fgets, max 79 chars)
AF BC DE HL AF' BC' DE' HL' IX IY SP PC MEMPTR      13 hex words
I R IFF1 IFF2 IM <halted> <end_tstates>              I, R hex; the rest decimal
<addr> <b0> <b1> ... -1                 zero or more memory lines (hex)
-1                                      end of test
<blank line(s)>
```

- The register line holds 13 16-bit values in hex (`%x`), in this order:
  `AF BC DE HL AF_ BC_ DE_ HL_ IX IY SP PC MEMPTR`. MEMPTR is also called WZ.
- The second line is `I` (hex), `R` (hex, all 8 bits: the harness sets `R = R7 = r`),
  `IFF1`, `IFF2` and `IM` (decimal, `%u`), `halted` (decimal 0/1) and `end_tstates` (decimal).
- In a memory line, `<addr>` is hex and each byte is hex. The line ends at the first value
  that is >= 0x100 (`-1` is parsed as `0xffffffff`). Bytes are stored at consecutive addresses.
- The test ends at the first *address* token that is >= 0x10000 (`-1`).

Example (`tests.in`, test `01`):
```
01
0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000
00 00 0 0 0 0     1
0000 01 12 34 -1
-1
```

## 2. How the harness runs a test (`coretest.c` `run_test`)

1. `z80_reset(1)` and `tstates = 0`. Reset sets AF=AF'=0xFFFF, I=R=0, PC=0,
   SP=0xFFFF, IFF1=IFF2=IM=0, halted=0, **Q=0**, BC/DE/HL/BC'/DE'/HL'/IX/IY=0, MEMPTR=0.
   The test's register lines then overwrite all of these.
2. All 64 KB of RAM is filled with the repeating pattern `DE AD BE EF` (address `i` gets
   `{0xde,0xad,0xbe,0xef}[i & 3]`). The memory lines are then applied on top.
   A copy of RAM is kept to make the diff in step 5.
3. The whole 64 KB is plain RAM. There is no ROM, no ULA and no interrupts. An IN from
   port `p` returns `p >> 8`, the high byte of the port address (`readport`, line 201).
4. The harness calls `z80_do_opcodes()`, which runs
   `while (tstates < end_tstates) { execute one whole instruction }`.
   The check happens only between instructions, so the final instruction always
   completes and the final `tstates` is usually **greater than** `end_tstates`.
   Most tests use `end_tstates = 1`, which means "execute exactly one instruction". In 1336
   of the 1356 tests the final count is greater than the input value.
   A prefixed opcode (CB, DD, ED, FD, DDCB, FDCB) is one instruction. `DD`/`FD` followed by a
   byte that is not a valid IX/IY opcode behaves as described in the README (`dd00`, `ddfd00`).
   Repeated block instructions (LDIR and the others) re-execute as separate instructions:
   each repeat refetches `ED xx` and adds 2 to R. That is why test `edb0` has
   `end_tstates = 331` (16 iterations of 21 T, except the last one at 16 T).
5. The harness then prints the final state and a memory diff (section 3).

**HALT** (`z80.pl` line 656): executing `0x76` sets `halted = 1` and does `PC--`, so PC
stays on the HALT opcode. Each later loop iteration fetches the HALT again (4 T, R+1).
None of the input tests start halted. Only test `76` ends with `halted = 1` (PC stays
0x0000, tstates = 4, R = 0x01).

**SCF/CCF and Q** (Fuse >= 1.6 behaviour, `z80.pl` lines 1096-1102):
`F = (F & (S|Z|P)) | C | (((last_Q ^ F) | A) & (0x28))`, where `last_Q` is the value
of Q left by the previous instruction. An instruction that changes F sets `Q = F`; any
other instruction leaves `Q = 0`. Because Q = 0 at the start of every test, the first SCF/CCF
uses `(F | A) & 0x28`. Tests `37`, `37_1`, `37_2` and `37_3` depend on this.
The harness runs the NMOS variant (`settings_current.z80_is_cmos = 0`).

**R register**: R increments once per M1 opcode fetch, which includes each
prefix byte. For `DD CB d op` and `FD CB d op` it increments by 2 only: the
displacement and the final opcode byte are ordinary memory reads (see `ddcb06` below).
Bit 7 of R is kept. The dump prints `(R7 & 0x80) | (R & 0x7f)`.

## 3. `tests.expected`

For each test:

```
<name>
<events...>                each line starts with whitespace: "%5d %s %04x[ %02x]"
AF BC DE HL AF' BC' DE' HL' IX IY SP PC MEMPTR     "%04x" x13
I R IFF1 IFF2 IM halted tstates                     "%02x %02x %d %d %d %d %d"
<addr> <bytes...> -1       one line per run of changed bytes (diff against the start)
<blank line>
```

Event types, with `<time>` printed in decimal right-aligned in 5 columns:

| type | meaning | data column |
|---|---|---|
| `MC` | memory "contend" point: start of a memory or internal cycle at that address | none |
| `MR` | memory read | byte read |
| `MW` | memory write | byte written |
| `PC` | port contention point (only printed in certain cases, see below) | none |
| `PR` | port read | byte read (= port high byte) |
| `PW` | port write | byte written |

The `MC`/`PC` lines are **timing markers only**. The harness adds no contention delay.
Each callback prints its line at the current `tstates` and then advances `tstates` by the
cycle length (`coretest.c` lines 118-220):

- Opcode fetch (M1): `contend_read(PC, 4)` gives `t MC pc`, then `t+4 MR pc op`. The
  MR line is printed *after* the 4 T are added.
- Normal memory read `readbyte(a)`: `t MC a`, +3, then `t+3 MR a v`.
- Normal memory write `writebyte(a,v)`: `t MC a`, +3, then `t+3 MW a v`.
- Extra internal cycles: `contend_read_no_mreq(a, n)` / `contend_write_no_mreq(a, n)`
  print `t MC a` and add `n` (usually 1). One line is printed per T-state when the
  core calls it n times with 1. For example, the 5 extra T of an LDIR repeat appear as
  5 `MC` lines at DE.
- Port I/O (`readport`/`writeport`):
  - pre-I/O: if `(port & 0xC000) == 0x4000`, print `t PC port`. Then add 1 in both cases.
  - print `PR port v` or `PW port v` at the current t.
  - post-I/O:
    - if `port & 1` is set: when `(port & 0xC000) == 0x4000`, print 3 `PC` lines at
      t, t+1 and t+2 (adding 1 each time); otherwise just add 3.
    - if `port & 1` is clear: print `t PC port` once and add 3.

  So one I/O cycle always costs 4 T.

Examples:

```
ed40                      IN B,(C) with BC=296b
    0 MC 0000
    4 MR 0000 ed
    4 MC 0001
    8 MR 0001 40
    9 PR 296b 29          pre-I/O +1 (no PC line), read = high byte 0x29, post +3 -> 12
8329 296b 7034 1f2f 0000 0000 0000 0000 0000 0000 0000 0002 296c
00 02 0 0 0 0 12

ddcb06                    RLC (IX+7); IX=5421
    0 MC 0000
    4 MR 0000 dd
    4 MC 0001
    8 MR 0001 cb
    8 MC 0002
   11 MR 0002 07          displacement: normal read, not M1
   11 MC 0003
   14 MR 0003 06          final opcode: normal read, not M1
   14 MC 0003
   15 MC 0003             2 internal T at the opcode byte's address (0003)
   16 MC 5428
   19 MR 5428 97
   19 MC 5428
   20 MC 5428             1 internal T before the write
   23 MW 5428 2f
0c29 f636 90a6 6117 0000 0000 0000 0000 5421 90ee 0000 0004 5428
00 02 0 0 0 0 23
5428 2f -1
```

Event counts over the whole file: MC 7844, MR 4550, MW 569, PC 79, PR 38, PW 37.

## 4. Using it in our emulator

- **Minimal check**: parse `tests.in`, set up the same state (RAM pattern `DE AD BE EF`,
  IN returns the port high byte, Q = 0), run whole instructions while
  `tstates < end_tstates`, then compare the two register lines (including MEMPTR, R with
  bit 7 kept, IFF1/IFF2/IM, halted and the final tstates) and the memory diff.
- **Bus-timing check**: give the CPU core callbacks that log `MC`/`MR`/`MW`/`PC`/`PR`/`PW`
  exactly as above, with zero contention, and diff against the event list. This checks the
  per-M-cycle timing that the ULA contention model will depend on later.
- No test triggers an interrupt or NMI, and no test starts halted. Interrupt
  acceptance timing has to be tested separately (for example `int_skip.sna`,
  `z80bltst.tap` and the ZEsarUX/Fuse timing TAPs).
- Parsing gotcha: the description line is read with `fgets` into an 80-byte buffer, and blank
  lines before a description are skipped. All the other fields are whitespace-separated,
  so read them as tokens, not by column.
