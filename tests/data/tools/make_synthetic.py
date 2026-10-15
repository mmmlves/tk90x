#!/usr/bin/env python3
"""Regenerate the synthetic test fixtures in tests/data from redistributable inputs.

Usage (from anywhere):
    python3 -I tests/data/tools/make_synthetic.py tests/data

Inputs (must already be in the data dir):
    dogmole.tap   (GPL, Mojon Twins MK1 example game)
    int_skip.sna  (MIT, Peter Helcmanovsky / ZXSpectrumNextTests)

Outputs:
    dogmole_turbo11.tzx      every TAP block re-encoded as TZX block 0x11
                             (Turbo Speed Data) using exact ROM timings, so the
                             standard ROM LOAD "" still loads it.
    dogmole_tone_pulse.tzx   every TAP block re-encoded as 0x12 (pure tone) +
                             0x13 (pulse sequence) + 0x14 (pure data), again with
                             ROM timings.
    int_skip_v1.z80          int_skip.sna converted to a version-1 .Z80
                             (30-byte header, RLE-compressed 48K image).
    int_skip_v3.z80          int_skip.sna converted to a version-3 .Z80
                             (54-byte extra header, 3 compressed 16K pages).
"""
import os
import struct
import sys

# ROM loader timings (T-states) - ZX Spectrum 48K ROM, TZX spec v1.20 block 0x10.
PILOT = 2168
SYNC1 = 667
SYNC2 = 735
ZERO = 855
ONE = 1710
PILOT_HEADER = 8063   # flag byte < 128
PILOT_DATA = 3223     # flag byte >= 128
PAUSE_MS = 1000


def tap_blocks(data):
    i = 0
    while i < len(data):
        n = struct.unpack_from('<H', data, i)[0]
        yield data[i + 2:i + 2 + n]
        i += 2 + n
    if i != len(data):
        raise ValueError('trailing garbage in TAP')


def tzx_header():
    return b'ZXTape!\x1a' + bytes([1, 20])


def tzx_text(s):
    b = s.encode('ascii')
    return bytes([0x30, len(b)]) + b


def u24(n):
    return bytes([n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff])


def make_turbo11(tap):
    out = bytearray(tzx_header())
    out += tzx_text('Synthetic: dogmole.tap as 0x11 blocks, ROM timings')
    for b in tap_blocks(tap):
        pilot_len = PILOT_HEADER if b[0] < 128 else PILOT_DATA
        out += bytes([0x11]) + struct.pack('<HHHHHHBH', PILOT, SYNC1, SYNC2, ZERO, ONE,
                                           pilot_len, 8, PAUSE_MS) + u24(len(b)) + b
    return bytes(out)


def make_tone_pulse(tap):
    out = bytearray(tzx_header())
    out += tzx_text('Synthetic: dogmole.tap as 0x12+0x13+0x14 blocks, ROM timings')
    for b in tap_blocks(tap):
        pilot_len = PILOT_HEADER if b[0] < 128 else PILOT_DATA
        out += bytes([0x12]) + struct.pack('<HH', PILOT, pilot_len)
        out += bytes([0x13, 2]) + struct.pack('<HH', SYNC1, SYNC2)
        out += bytes([0x14]) + struct.pack('<HHBH', ZERO, ONE, 8, PAUSE_MS) + u24(len(b)) + b
    return bytes(out)


def z80_compress(mem):
    """RLE as in the .Z80 spec: runs of >=5 equal bytes, or >=2 0xED bytes,
    become ED ED count byte. A single ED is emitted literally and the byte
    after it is never the start of a run (it is emitted literally)."""
    out = bytearray()
    i = 0
    n = len(mem)
    while i < n:
        b = mem[i]
        run = 1
        while i + run < n and mem[i + run] == b and run < 255:
            run += 1
        if (b == 0xED and run >= 2) or run >= 5:
            out += bytes([0xED, 0xED, run, b])
            i += run
        elif b == 0xED:
            # lone ED: emit it and the following byte literally
            out.append(0xED)
            i += 1
            if i < n:
                out.append(mem[i])
                i += 1
        else:
            out.append(b)
            i += 1
    return bytes(out)


def z80_decompress(data, expected=None):
    out = bytearray()
    i = 0
    while i < len(data):
        if data[i] == 0xED and i + 1 < len(data) and data[i + 1] == 0xED:
            out += bytes([data[i + 3]]) * data[i + 2]
            i += 4
        else:
            out.append(data[i])
            i += 1
    if expected is not None and len(out) != expected:
        raise ValueError('decompressed length %d != %d' % (len(out), expected))
    return bytes(out)


def sna_to_regs(sna):
    if len(sna) != 49179:
        raise ValueError('only 48K SNA supported')
    (i_reg, hl_, de_, bc_, af_, hl, de, bc, iy, ix, iff2, r, af, sp, im,
     border) = struct.unpack_from('<BHHHHHHHHHBBHHBB', sna, 0)
    ram = bytearray(sna[27:])
    pc = ram[sp - 0x4000] | (ram[sp - 0x4000 + 1] << 8)
    sp = (sp + 2) & 0xffff          # emulate the RETN that SNA loaders execute
    iff = (iff2 >> 2) & 1
    return dict(I=i_reg, HL_=hl_, DE_=de_, BC_=bc_, AF_=af_, HL=hl, DE=de, BC=bc, IY=iy,
                IX=ix, IFF1=iff, IFF2=iff, R=r, AF=af, SP=sp, IM=im & 3, border=border & 7,
                PC=pc), bytes(ram)


def z80_header30(rg, pc, compressed):
    a, f = rg['AF'] >> 8, rg['AF'] & 0xff
    a_, f_ = rg['AF_'] >> 8, rg['AF_'] & 0xff
    byte12 = ((rg['R'] >> 7) & 1) | (rg['border'] << 1) | (0x20 if compressed else 0)
    byte29 = rg['IM']  # bits 2..7 zero: no issue-2 emulation, no double int freq, cursor joystick
    return struct.pack('<BBHHHHBBBHHHHBBHHBBB', a, f, rg['BC'], rg['HL'], pc, rg['SP'],
                       rg['I'], rg['R'] & 0x7f, byte12, rg['DE'], rg['BC_'], rg['DE_'],
                       rg['HL_'], a_, f_, rg['IY'], rg['IX'], rg['IFF1'], rg['IFF2'], byte29)


def make_z80_v1(sna):
    rg, ram = sna_to_regs(sna)
    return z80_header30(rg, rg['PC'], True) + z80_compress(ram) + b'\x00\xed\xed\x00'


def make_z80_v3(sna):
    rg, ram = sna_to_regs(sna)
    out = bytearray(z80_header30(rg, 0, False))   # PC=0 marks v2/v3; byte12 bit5 unused
    ext = bytearray(54)
    struct.pack_into('<H', ext, 0, rg['PC'])
    ext[2] = 0          # hardware mode 0 = 48K
    # all remaining fields (out 0x7ffd, IF1 paged, flags, AY regs, T-state counter,
    # etc.) left 0; T-state low word/high byte = 0 means "start of frame".
    out += struct.pack('<H', 54) + ext
    # 48K page numbers: 8 = 0x4000, 4 = 0x8000, 5 = 0xC000
    for page, base in ((8, 0x4000), (4, 0x8000), (5, 0xC000)):
        chunk = ram[base - 0x4000: base - 0x4000 + 0x4000]
        comp = z80_compress(chunk)
        if len(comp) >= 0x4000:
            out += struct.pack('<HB', 0xFFFF, page) + chunk
        else:
            out += struct.pack('<HB', len(comp), page) + comp
    return bytes(out)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..')
    tap = open(os.path.join(d, 'dogmole.tap'), 'rb').read()
    sna = open(os.path.join(d, 'int_skip.sna'), 'rb').read()
    outputs = {
        'dogmole_turbo11.tzx': make_turbo11(tap),
        'dogmole_tone_pulse.tzx': make_tone_pulse(tap),
        'int_skip_v1.z80': make_z80_v1(sna),
        'int_skip_v3.z80': make_z80_v3(sna),
    }
    # self-check the RLE round trip
    _, ram = sna_to_regs(sna)
    v1 = outputs['int_skip_v1.z80']
    assert v1[-4:] == b'\x00\xed\xed\x00'
    assert z80_decompress(v1[30:-4], 49152) == ram
    for name, blob in outputs.items():
        with open(os.path.join(d, name), 'wb') as f:
            f.write(blob)
        print(name, len(blob))


if __name__ == '__main__':
    main()
