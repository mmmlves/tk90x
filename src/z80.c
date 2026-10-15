/*
 * z80.c - Nucleo Z80 (NMOS) completo: instrucoes documentadas e nao
 * documentadas, flags 3/5, MEMPTR (WZ), Q, R, IFF/EI, HALT, IM0/1/2, NMI.
 *
 * Temporizacao por ciclo de maquina identica ao nucleo do Fuse
 * (z80/z80.pl, z80_macros.h): cada contend_read / contend_read_no_mreq /
 * contend_write_no_mreq do Fuse corresponde aqui a uma chamada de
 * bus.mem_contention no mesmo endereco e no mesmo T-state.
 *
 * Desempenho: os registradores ficam em variaveis locais durante a
 * instrucao (o compilador os mantem em registradores da CPU hospedeira) e
 * sao gravados de volta no fim de z80_step(). Decodificacao por switch.
 */
#include "z80.h"

#include <string.h>

#define FC Z80_FLAG_C
#define FN Z80_FLAG_N
#define FV Z80_FLAG_PV
#define FP Z80_FLAG_PV
#define F3 Z80_FLAG_3
#define FH Z80_FLAG_H
#define F5 Z80_FLAG_5
#define FZ Z80_FLAG_Z
#define FS Z80_FLAG_S

/* ------------------------------------------------------------------ */
/* Tabelas de flags (constantes, geradas por macro)                    */
/* ------------------------------------------------------------------ */

/* paridade par -> P/V ligado */
#define PAR_(i)  ((((0x6996 >> (((i) ^ ((i) >> 4)) & 0x0F)) & 1) ? 0 : FP))
#define SZ53_(i) (((i) & (FS | F5 | F3)) | ((i) == 0 ? FZ : 0))
#define SZ53P_(i) (SZ53_(i) | PAR_(i))

#define ROW16_(M, n) M(n), M(n + 1), M(n + 2), M(n + 3), M(n + 4), M(n + 5), M(n + 6), M(n + 7), \
    M(n + 8), M(n + 9), M(n + 10), M(n + 11), M(n + 12), M(n + 13), M(n + 14), M(n + 15)
#define TAB256_(M) ROW16_(M, 0x00), ROW16_(M, 0x10), ROW16_(M, 0x20), ROW16_(M, 0x30), \
    ROW16_(M, 0x40), ROW16_(M, 0x50), ROW16_(M, 0x60), ROW16_(M, 0x70), \
    ROW16_(M, 0x80), ROW16_(M, 0x90), ROW16_(M, 0xA0), ROW16_(M, 0xB0), \
    ROW16_(M, 0xC0), ROW16_(M, 0xD0), ROW16_(M, 0xE0), ROW16_(M, 0xF0)

static const uint8_t sz53[256]   = { TAB256_(SZ53_) };
static const uint8_t sz53p[256]  = { TAB256_(SZ53P_) };
static const uint8_t parity[256] = { TAB256_(PAR_) };

/* Bloco repetido (LDxR/CPxR/INxR/OTxR com nova iteracao): flags 3 e 5 vem dos bits
 * 11 e 13 do PC (endereco da instrucao), como no Z80 real (D. Banks 2018,
 * teste z80bltst de Ped7g; mesmo modelo do MAME). */
#define BLK_XY(f, pc) ((uint8_t)(((f) & ~(F3 | F5)) | (((pc) >> 8) & (F3 | F5))))

/* INxR/OTxR repetindo: alem de 3/5, H e P/V mudam conforme C, o byte transferido v
 * e B (ja decrementado). */
static uint8_t blk_io_flags(uint8_t f, uint16_t pc, uint8_t v, uint8_t b)
{
    f = BLK_XY(f, pc);
    if (f & FC) {
        f &= (uint8_t)~FH;
        if (v & 0x80) {
            f ^= (uint8_t)((parity[(b - 1) & 0x07] ^ FV) & FV);
            if ((b & 0x0F) == 0x00) f |= FH;
        } else {
            f ^= (uint8_t)((parity[(b + 1) & 0x07] ^ FV) & FV);
            if ((b & 0x0F) == 0x0F) f |= FH;
        }
    } else {
        f ^= (uint8_t)((parity[b & 0x07] ^ FV) & FV);
    }
    return f;
}

/* ------------------------------------------------------------------ */
/* Acesso ao barramento                                                */
/* ------------------------------------------------------------------ */

/* contencao no endereco (sem somar o ciclo) */
#define CONT(ad) do { if (mc) { t += mc(ctx, (uint16_t)(ad), t); } } while (0)

/* n T-states internos "sem MREQ" com o endereco ad no barramento */
#define IC(ad, n) do { \
    if (mc) { uint16_t ica_ = (uint16_t)(ad); int icn_; \
        for (icn_ = 0; icn_ < (n); icn_++) { t += mc(ctx, ica_, t); t++; } \
    } else t += (n); \
} while (0)

/* leitura de memoria: contencao + 3 T */
#define RD(dst, ad) do { uint16_t rda_ = (uint16_t)(ad); \
    if (mc) { t += mc(ctx, rda_, t); } t += 3; cpu->tstates = t; (dst) = rd(ctx, rda_); } while (0)

/* escrita de memoria: contencao + 3 T */
#define WR(ad, v) do { uint16_t wra_ = (uint16_t)(ad); uint8_t wrv_ = (uint8_t)(v); \
    if (mc) { t += mc(ctx, wra_, t); } t += 3; cpu->tstates = t; wr(ctx, wra_, wrv_); } while (0)

/* busca de opcode (M1): contencao + 4 T, R++ */
#define FETCH(dst) do { if (mc) { t += mc(ctx, pc, t); } t += 4; cpu->tstates = t; \
    (dst) = rd(ctx, pc); pc++; rr++; } while (0)

/* ciclo de E/S: contencao de E/S + 4 T, callback no T3 */
#define PORT_IN(dst, port) do { uint16_t pia_ = (uint16_t)(port); \
    if (cpu->bus.io_contention) { t += cpu->bus.io_contention(ctx, pia_, t); } \
    t += 3; cpu->tstates = t; (dst) = cpu->bus.in(ctx, pia_); t += 1; } while (0)
#define PORT_OUT(port, v) do { uint16_t poa_ = (uint16_t)(port); uint8_t pov_ = (uint8_t)(v); \
    if (cpu->bus.io_contention) { t += cpu->bus.io_contention(ctx, poa_, t); } \
    t += 3; cpu->tstates = t; cpu->bus.out(ctx, poa_, pov_); t += 1; } while (0)

/* ------------------------------------------------------------------ */
/* Registradores                                                        */
/* ------------------------------------------------------------------ */

#define BC ((uint16_t)((b << 8) | c))
#define DE ((uint16_t)((d << 8) | e))
#define HL ((uint16_t)((h << 8) | l))
#define SET_BC(v) do { uint16_t s16_ = (uint16_t)(v); b = (uint8_t)(s16_ >> 8); c = (uint8_t)s16_; } while (0)
#define SET_DE(v) do { uint16_t s16_ = (uint16_t)(v); d = (uint8_t)(s16_ >> 8); e = (uint8_t)s16_; } while (0)
#define SET_HL(v) do { uint16_t s16_ = (uint16_t)(v); h = (uint8_t)(s16_ >> 8); l = (uint8_t)s16_; } while (0)
#define IR ((uint16_t)((cpu->i << 8) | r7 | (rr & 0x7F)))
#define XH ((uint8_t)(xy >> 8))
#define XL ((uint8_t)xy)
#define SET_XH(v) xy = (uint16_t)((xy & 0x00FF) | ((uint8_t)(v) << 8))
#define SET_XL(v) xy = (uint16_t)((xy & 0xFF00) | (uint8_t)(v))

/* ------------------------------------------------------------------ */
/* Operacoes com flags                                                  */
/* ------------------------------------------------------------------ */

#define ADD_A(v) do { unsigned ov_ = (v), or_ = a + ov_; \
    f = (uint8_t)(sz53[or_ & 0xFF] | ((a ^ ov_ ^ or_) & FH) | \
        ((((a ^ ~ov_) & (a ^ or_)) >> 5) & FV) | ((or_ >> 8) & FC)); \
    a = (uint8_t)or_; q = f; } while (0)
#define ADC_A(v) do { unsigned ov_ = (v), or_ = a + ov_ + (f & FC); \
    f = (uint8_t)(sz53[or_ & 0xFF] | ((a ^ ov_ ^ or_) & FH) | \
        ((((a ^ ~ov_) & (a ^ or_)) >> 5) & FV) | ((or_ >> 8) & FC)); \
    a = (uint8_t)or_; q = f; } while (0)
#define SUB_A(v) do { unsigned ov_ = (v), or_ = a - ov_; \
    f = (uint8_t)(sz53[or_ & 0xFF] | ((a ^ ov_ ^ or_) & FH) | \
        ((((a ^ ov_) & (a ^ or_)) >> 5) & FV) | ((or_ >> 8) & FC) | FN); \
    a = (uint8_t)or_; q = f; } while (0)
#define SBC_A(v) do { unsigned ov_ = (v), or_ = a - ov_ - (f & FC); \
    f = (uint8_t)(sz53[or_ & 0xFF] | ((a ^ ov_ ^ or_) & FH) | \
        ((((a ^ ov_) & (a ^ or_)) >> 5) & FV) | ((or_ >> 8) & FC) | FN); \
    a = (uint8_t)or_; q = f; } while (0)
#define CP_A(v) do { unsigned ov_ = (v), or_ = a - ov_; \
    f = (uint8_t)((or_ & FS) | ((or_ & 0xFF) ? 0 : FZ) | (ov_ & (F3 | F5)) | \
        ((a ^ ov_ ^ or_) & FH) | ((((a ^ ov_) & (a ^ or_)) >> 5) & FV) | ((or_ >> 8) & FC) | FN); \
    q = f; } while (0)
#define AND_A(v) do { a &= (uint8_t)(v); f = (uint8_t)(FH | sz53p[a]); q = f; } while (0)
#define XOR_A(v) do { a ^= (uint8_t)(v); f = sz53p[a]; q = f; } while (0)
#define OR_A(v)  do { a |= (uint8_t)(v); f = sz53p[a]; q = f; } while (0)

/* x: lvalue uint8_t */
#define INC8(x) do { (x)++; f = (uint8_t)((f & FC) | ((x) == 0x80 ? FV : 0) | \
    (((x) & 0x0F) ? 0 : FH) | sz53[(x)]); q = f; } while (0)
#define DEC8(x) do { f = (uint8_t)((f & FC) | (((x) & 0x0F) ? 0 : FH) | FN); (x)--; \
    f |= (uint8_t)(((x) == 0x7F ? FV : 0) | sz53[(x)]); q = f; } while (0)

/* x: lvalue uint16_t */
#define ADD16(x, y) do { unsigned ax_ = (x), ay_ = (y), ar_ = ax_ + ay_; \
    wz = (uint16_t)(ax_ + 1); \
    f = (uint8_t)((f & (FV | FZ | FS)) | ((ar_ >> 16) & FC) | ((ar_ >> 8) & (F3 | F5)) | \
        (((ax_ ^ ay_ ^ ar_) >> 8) & FH)); \
    (x) = (uint16_t)ar_; q = f; } while (0)
#define ADC16(y) do { unsigned ax_ = HL, ay_ = (y), ar_ = ax_ + ay_ + (f & FC); \
    wz = (uint16_t)(ax_ + 1); \
    f = (uint8_t)(((ar_ >> 16) & FC) | ((((ax_ ^ ~ay_) & (ax_ ^ ar_)) >> 13) & FV) | \
        ((ar_ >> 8) & (F3 | F5 | FS)) | (((ax_ ^ ay_ ^ ar_) >> 8) & FH) | ((ar_ & 0xFFFF) ? 0 : FZ)); \
    SET_HL(ar_); q = f; } while (0)
#define SBC16(y) do { unsigned ax_ = HL, ay_ = (y), ar_ = ax_ - ay_ - (f & FC); \
    wz = (uint16_t)(ax_ + 1); \
    f = (uint8_t)(((ar_ >> 16) & FC) | ((((ax_ ^ ay_) & (ax_ ^ ar_)) >> 13) & FV) | \
        ((ar_ >> 8) & (F3 | F5 | FS)) | (((ax_ ^ ay_ ^ ar_) >> 8) & FH) | ((ar_ & 0xFFFF) ? 0 : FZ) | FN); \
    SET_HL(ar_); q = f; } while (0)

/* rotacoes/deslocamentos do grupo CB (op 0..7); x: lvalue uint8_t */
#define ROTSHIFT(op, x) do { uint8_t rs_ = (x); \
    switch ((op) & 7) { \
    case 0: rs_ = (uint8_t)((rs_ << 1) | (rs_ >> 7)); f = (uint8_t)((rs_ & FC) | sz53p[rs_]); break; \
    case 1: f = rs_ & FC; rs_ = (uint8_t)((rs_ >> 1) | (rs_ << 7)); f |= sz53p[rs_]; break; \
    case 2: { uint8_t o_ = rs_; rs_ = (uint8_t)((rs_ << 1) | (f & FC)); f = (uint8_t)((o_ >> 7) | sz53p[rs_]); } break; \
    case 3: { uint8_t o_ = rs_; rs_ = (uint8_t)((rs_ >> 1) | (f << 7)); f = (uint8_t)((o_ & FC) | sz53p[rs_]); } break; \
    case 4: f = rs_ >> 7; rs_ = (uint8_t)(rs_ << 1); f |= sz53p[rs_]; break; \
    case 5: f = rs_ & FC; rs_ = (uint8_t)((rs_ & 0x80) | (rs_ >> 1)); f |= sz53p[rs_]; break; \
    case 6: f = rs_ >> 7; rs_ = (uint8_t)((rs_ << 1) | 1); f |= sz53p[rs_]; break; \
    default: f = rs_ & FC; rs_ = (uint8_t)(rs_ >> 1); f |= sz53p[rs_]; break; \
    } \
    (x) = rs_; q = f; } while (0)

/* BIT n: src35 = fonte dos bits 3/5 (o valor, ou MEMPTR alto para (HL)/(IX+d)) */
#define BITTEST(n, v, src35) do { uint8_t bv_ = (uint8_t)((v) & (1u << (n))); \
    f = (uint8_t)((f & FC) | FH | ((src35) & (F3 | F5)) | (bv_ ? 0 : (FP | FZ)) | (bv_ & FS)); \
    q = f; } while (0)

/* condicoes cc (indice 0..7: NZ Z NC C PO PE P M) */
#define COND(n) ((n) & 1 ? (f & cond_mask[(n) >> 1]) != 0 : (f & cond_mask[(n) >> 1]) == 0)
static const uint8_t cond_mask[4] = { FZ, FC, FP, FS };

/* ------------------------------------------------------------------ */

void z80_reset(Z80 *cpu)
{
    cpu->af = cpu->af_ = 0xFFFF;
    cpu->sp = 0xFFFF;
    cpu->pc = 0;
    cpu->i = cpu->r = 0;
    cpu->iff1 = cpu->iff2 = 0;
    cpu->im = 0;
    cpu->halted = 0;
    cpu->ei_delay = 0;
    cpu->q = 0;
    cpu->iff2_read = 0;
    cpu->pending_prefix = 0;
}

void z80_init(Z80 *cpu, const Z80Bus *bus)
{
    memset(cpu, 0, sizeof(*cpu));
    if (bus) cpu->bus = *bus;
    z80_reset(cpu);
}

int z80_step(Z80 *cpu)
{
    void *ctx = cpu->bus.ctx;
    uint8_t (*rd)(void *, uint16_t) = cpu->bus.read;
    void (*wr)(void *, uint16_t, uint8_t) = cpu->bus.write;
    unsigned (*mc)(void *, uint16_t, uint32_t) = cpu->bus.mem_contention;

    uint32_t t = cpu->tstates;
    const uint32_t t0 = t;
    uint8_t a = (uint8_t)(cpu->af >> 8), f = (uint8_t)cpu->af;
    uint8_t b = (uint8_t)(cpu->bc >> 8), c = (uint8_t)cpu->bc;
    uint8_t d = (uint8_t)(cpu->de >> 8), e = (uint8_t)cpu->de;
    uint8_t h = (uint8_t)(cpu->hl >> 8), l = (uint8_t)cpu->hl;
    uint16_t sp = cpu->sp, pc = cpu->pc, wz = cpu->memptr;
    uint16_t xy = 0;
    uint8_t r7 = cpu->r & 0x80, rr = cpu->r;
    const uint8_t lastq = cpu->q;
    uint8_t q = 0;
    uint8_t op;
    int use_iy = 0;

    cpu->ei_delay = 0;
    cpu->iff2_read = 0;

    if (cpu->halted) {
        /* HALT: repete o M1 no mesmo endereco (NOP de 4 T) */
        uint8_t dummy;
        FETCH(dummy);
        (void)dummy;
        pc--;
        goto done;
    }

    if (cpu->pending_prefix) {
        /* prefixo DD/FD cujo M1 ja foi feito no passo anterior */
        op = cpu->pending_prefix;
        cpu->pending_prefix = 0;
        pc++;
    } else {
        FETCH(op);
    }

dispatch:
    switch (op) {
    case 0x00: break;                                       /* NOP */
    case 0x01: RD(c, pc); pc++; RD(b, pc); pc++; break;     /* LD BC,nn */
    case 0x02: wz = (uint16_t)(((c + 1) & 0xFF) | (a << 8)); WR(BC, a); break; /* LD (BC),A */
    case 0x03: IC(IR, 2); SET_BC(BC + 1); break;            /* INC BC */
    case 0x04: INC8(b); break;
    case 0x05: DEC8(b); break;
    case 0x06: RD(b, pc); pc++; break;
    case 0x07:                                              /* RLCA */
        a = (uint8_t)((a << 1) | (a >> 7));
        f = (uint8_t)((f & (FP | FZ | FS)) | (a & (FC | F3 | F5))); q = f;
        break;
    case 0x08: { uint16_t tmp = cpu->af_; cpu->af_ = (uint16_t)((a << 8) | f); a = (uint8_t)(tmp >> 8); f = (uint8_t)tmp; } break;
    case 0x09: { uint16_t hl = HL; IC(IR, 7); ADD16(hl, BC); SET_HL(hl); } break;
    case 0x0A: wz = (uint16_t)(BC + 1); RD(a, BC); break;
    case 0x0B: IC(IR, 2); SET_BC(BC - 1); break;
    case 0x0C: INC8(c); break;
    case 0x0D: DEC8(c); break;
    case 0x0E: RD(c, pc); pc++; break;
    case 0x0F:                                              /* RRCA */
        f = (uint8_t)((f & (FP | FZ | FS)) | (a & FC));
        a = (uint8_t)((a >> 1) | (a << 7));
        f |= (uint8_t)(a & (F3 | F5)); q = f;
        break;

    case 0x10:                                              /* DJNZ */
        IC(IR, 1);
        b--;
        if (b) goto do_jr;
        CONT(pc); t += 3; pc++;
        break;
    case 0x11: RD(e, pc); pc++; RD(d, pc); pc++; break;
    case 0x12: wz = (uint16_t)(((e + 1) & 0xFF) | (a << 8)); WR(DE, a); break;
    case 0x13: IC(IR, 2); SET_DE(DE + 1); break;
    case 0x14: INC8(d); break;
    case 0x15: DEC8(d); break;
    case 0x16: RD(d, pc); pc++; break;
    case 0x17: {                                            /* RLA */
        uint8_t o = a;
        a = (uint8_t)((a << 1) | (f & FC));
        f = (uint8_t)((f & (FP | FZ | FS)) | (a & (F3 | F5)) | (o >> 7)); q = f;
    } break;
    case 0x18:                                              /* JR e */
    do_jr: {
        uint8_t off;
        RD(off, pc);
        IC(pc, 5);
        pc = (uint16_t)(pc + (int8_t)off + 1);
        wz = pc;
    } break;
    case 0x19: { uint16_t hl = HL; IC(IR, 7); ADD16(hl, DE); SET_HL(hl); } break;
    case 0x1A: wz = (uint16_t)(DE + 1); RD(a, DE); break;
    case 0x1B: IC(IR, 2); SET_DE(DE - 1); break;
    case 0x1C: INC8(e); break;
    case 0x1D: DEC8(e); break;
    case 0x1E: RD(e, pc); pc++; break;
    case 0x1F: {                                            /* RRA */
        uint8_t o = a;
        a = (uint8_t)((a >> 1) | (f << 7));
        f = (uint8_t)((f & (FP | FZ | FS)) | (a & (F3 | F5)) | (o & FC)); q = f;
    } break;

    case 0x20: if (!(f & FZ)) goto do_jr; CONT(pc); t += 3; pc++; break;
    case 0x28: if (f & FZ) goto do_jr; CONT(pc); t += 3; pc++; break;
    case 0x30: if (!(f & FC)) goto do_jr; CONT(pc); t += 3; pc++; break;
    case 0x38: if (f & FC) goto do_jr; CONT(pc); t += 3; pc++; break;

    case 0x21: RD(l, pc); pc++; RD(h, pc); pc++; break;
    case 0x22: {                                            /* LD (nn),HL */
        uint16_t ad; uint8_t lo, hi;
        RD(lo, pc); pc++; RD(hi, pc); pc++;
        ad = (uint16_t)(lo | (hi << 8));
        WR(ad, l); ad++; wz = ad; WR(ad, h);
    } break;
    case 0x23: IC(IR, 2); SET_HL(HL + 1); break;
    case 0x24: INC8(h); break;
    case 0x25: DEC8(h); break;
    case 0x26: RD(h, pc); pc++; break;
    case 0x27: {                                            /* DAA */
        uint8_t add = 0, carry = f & FC;
        if ((f & FH) || (a & 0x0F) > 9) add = 6;
        if (carry || a > 0x99) add |= 0x60;
        if (a > 0x99) carry = FC;
        if (f & FN) { SUB_A(add); } else { ADD_A(add); }
        f = (uint8_t)((f & ~(FC | FP)) | carry | parity[a]); q = f;
    } break;
    case 0x29: { uint16_t hl = HL; IC(IR, 7); ADD16(hl, hl); SET_HL(hl); } break;
    case 0x2A: {                                            /* LD HL,(nn) */
        uint16_t ad; uint8_t lo, hi;
        RD(lo, pc); pc++; RD(hi, pc); pc++;
        ad = (uint16_t)(lo | (hi << 8));
        RD(l, ad); ad++; wz = ad; RD(h, ad);
    } break;
    case 0x2B: IC(IR, 2); SET_HL(HL - 1); break;
    case 0x2C: INC8(l); break;
    case 0x2D: DEC8(l); break;
    case 0x2E: RD(l, pc); pc++; break;
    case 0x2F:                                              /* CPL */
        a ^= 0xFF;
        f = (uint8_t)((f & (FC | FP | FZ | FS)) | (a & (F3 | F5)) | FN | FH); q = f;
        break;

    case 0x31: { uint8_t lo, hi; RD(lo, pc); pc++; RD(hi, pc); pc++; sp = (uint16_t)(lo | (hi << 8)); } break;
    case 0x32: {                                            /* LD (nn),A */
        uint16_t ad; uint8_t lo, hi;
        RD(lo, pc); pc++; RD(hi, pc); pc++;
        ad = (uint16_t)(lo | (hi << 8));
        wz = (uint16_t)(((ad + 1) & 0xFF) | (a << 8));
        WR(ad, a);
    } break;
    case 0x33: IC(IR, 2); sp++; break;
    case 0x34: { uint8_t v; RD(v, HL); IC(HL, 1); INC8(v); WR(HL, v); } break;
    case 0x35: { uint8_t v; RD(v, HL); IC(HL, 1); DEC8(v); WR(HL, v); } break;
    case 0x36: { uint8_t v; RD(v, pc); pc++; WR(HL, v); } break;
    case 0x37:                                              /* SCF */
        f = (uint8_t)((f & (FP | FZ | FS)) | (((lastq ^ f) | a) & (F3 | F5)) | FC); q = f;
        break;
    case 0x39: { uint16_t hl = HL; IC(IR, 7); ADD16(hl, sp); SET_HL(hl); } break;
    case 0x3A: {                                            /* LD A,(nn) */
        uint8_t lo, hi;
        RD(lo, pc); pc++; RD(hi, pc); pc++;
        wz = (uint16_t)(lo | (hi << 8));
        RD(a, wz); wz++;
    } break;
    case 0x3B: IC(IR, 2); sp--; break;
    case 0x3C: INC8(a); break;
    case 0x3D: DEC8(a); break;
    case 0x3E: RD(a, pc); pc++; break;
    case 0x3F:                                              /* CCF */
        f = (uint8_t)((f & (FP | FZ | FS)) | ((f & FC) ? FH : FC) | (((lastq ^ f) | a) & (F3 | F5))); q = f;
        break;

#define LDROW(base, dst) \
    case base + 0: dst = b; break; case base + 1: dst = c; break; \
    case base + 2: dst = d; break; case base + 3: dst = e; break; \
    case base + 4: dst = h; break; case base + 5: dst = l; break; \
    case base + 6: RD(dst, HL); break; case base + 7: dst = a; break;
    LDROW(0x40, b)
    LDROW(0x48, c)
    LDROW(0x50, d)
    LDROW(0x58, e)
    LDROW(0x60, h)
    LDROW(0x68, l)
    LDROW(0x78, a)
#undef LDROW
    case 0x70: WR(HL, b); break;
    case 0x71: WR(HL, c); break;
    case 0x72: WR(HL, d); break;
    case 0x73: WR(HL, e); break;
    case 0x74: WR(HL, h); break;
    case 0x75: WR(HL, l); break;
    case 0x76: cpu->halted = 1; pc--; break;                /* HALT */
    case 0x77: WR(HL, a); break;

#define ALUROW(base, OP) \
    case base + 0: OP(b); break; case base + 1: OP(c); break; \
    case base + 2: OP(d); break; case base + 3: OP(e); break; \
    case base + 4: OP(h); break; case base + 5: OP(l); break; \
    case base + 6: { uint8_t mv8; RD(mv8, HL); OP(mv8); } break; \
    case base + 7: OP(a); break;
    ALUROW(0x80, ADD_A)
    ALUROW(0x88, ADC_A)
    ALUROW(0x90, SUB_A)
    ALUROW(0x98, SBC_A)
    ALUROW(0xA0, AND_A)
    ALUROW(0xA8, XOR_A)
    ALUROW(0xB0, OR_A)
    ALUROW(0xB8, CP_A)
#undef ALUROW

    /* RET cc */
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
    case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        IC(IR, 1);
        if (COND((op >> 3) & 7)) goto do_ret;
        break;
    case 0xC9:
    do_ret: {
        uint8_t lo, hi;
        RD(lo, sp); sp++; RD(hi, sp); sp++;
        pc = (uint16_t)(lo | (hi << 8)); wz = pc;
    } break;

    case 0xC1: RD(c, sp); sp++; RD(b, sp); sp++; break;
    case 0xD1: RD(e, sp); sp++; RD(d, sp); sp++; break;
    case 0xE1: RD(l, sp); sp++; RD(h, sp); sp++; break;
    case 0xF1: RD(f, sp); sp++; RD(a, sp); sp++; break;

    /* JP cc,nn / JP nn */
    case 0xC2: case 0xCA: case 0xD2: case 0xDA:
    case 0xE2: case 0xEA: case 0xF2: case 0xFA:
    case 0xC3: {
        uint8_t lo, hi;
        RD(lo, pc); pc++; RD(hi, pc); pc++;
        wz = (uint16_t)(lo | (hi << 8));
        if (op == 0xC3 || COND((op >> 3) & 7)) pc = wz;
    } break;

    /* CALL cc,nn / CALL nn */
    case 0xC4: case 0xCC: case 0xD4: case 0xDC:
    case 0xE4: case 0xEC: case 0xF4: case 0xFC:
    case 0xCD: {
        uint8_t lo, hi;
        RD(lo, pc); pc++; RD(hi, pc);
        wz = (uint16_t)(lo | (hi << 8));
        if (op == 0xCD || COND((op >> 3) & 7)) {
            IC(pc, 1); pc++;
            sp--; WR(sp, pc >> 8); sp--; WR(sp, pc);
            pc = wz;
        } else {
            pc++;
        }
    } break;

    case 0xC5: IC(IR, 1); sp--; WR(sp, b); sp--; WR(sp, c); break;
    case 0xD5: IC(IR, 1); sp--; WR(sp, d); sp--; WR(sp, e); break;
    case 0xE5: IC(IR, 1); sp--; WR(sp, h); sp--; WR(sp, l); break;
    case 0xF5: IC(IR, 1); sp--; WR(sp, a); sp--; WR(sp, f); break;

    case 0xC6: { uint8_t v; RD(v, pc); pc++; ADD_A(v); } break;
    case 0xCE: { uint8_t v; RD(v, pc); pc++; ADC_A(v); } break;
    case 0xD6: { uint8_t v; RD(v, pc); pc++; SUB_A(v); } break;
    case 0xDE: { uint8_t v; RD(v, pc); pc++; SBC_A(v); } break;
    case 0xE6: { uint8_t v; RD(v, pc); pc++; AND_A(v); } break;
    case 0xEE: { uint8_t v; RD(v, pc); pc++; XOR_A(v); } break;
    case 0xF6: { uint8_t v; RD(v, pc); pc++; OR_A(v); } break;
    case 0xFE: { uint8_t v; RD(v, pc); pc++; CP_A(v); } break;

    /* RST */
    case 0xC7: case 0xCF: case 0xD7: case 0xDF:
    case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        IC(IR, 1);
        sp--; WR(sp, pc >> 8); sp--; WR(sp, pc);
        pc = op & 0x38; wz = pc;
        break;

    case 0xD3: {                                            /* OUT (n),A */
        uint8_t n;
        RD(n, pc); pc++;
        wz = (uint16_t)(((n + 1) & 0xFF) | (a << 8));
        PORT_OUT((uint16_t)(n | (a << 8)), a);
    } break;
    case 0xDB: {                                            /* IN A,(n) */
        uint8_t n; uint16_t port;
        RD(n, pc); pc++;
        port = (uint16_t)(n | (a << 8));
        PORT_IN(a, port);
        wz = (uint16_t)(port + 1);
    } break;
    case 0xD9: {                                            /* EXX */
        uint16_t tmp;
        tmp = cpu->bc_; cpu->bc_ = BC; SET_BC(tmp);
        tmp = cpu->de_; cpu->de_ = DE; SET_DE(tmp);
        tmp = cpu->hl_; cpu->hl_ = HL; SET_HL(tmp);
    } break;
    case 0xE3: {                                            /* EX (SP),HL */
        uint8_t lo, hi;
        RD(lo, sp); RD(hi, sp + 1); IC(sp + 1, 1);
        WR(sp + 1, h); WR(sp, l); IC(sp, 2);
        l = lo; h = hi; wz = (uint16_t)(lo | (hi << 8));
    } break;
    case 0xE9: pc = HL; break;                              /* JP (HL) */
    case 0xEB: { uint8_t tmp; tmp = d; d = h; h = tmp; tmp = e; e = l; l = tmp; } break;
    case 0xF3: cpu->iff1 = cpu->iff2 = 0; break;            /* DI */
    case 0xF9: IC(IR, 2); sp = HL; break;                   /* LD SP,HL */
    case 0xFB: cpu->iff1 = cpu->iff2 = 1; cpu->ei_delay = 1; break; /* EI */

    /* ---------------- CB ---------------- */
    case 0xCB: {
        uint8_t op2, v = 0;
        unsigned ri;
        FETCH(op2);
        ri = op2 & 7;
        switch (ri) {
        case 0: v = b; break; case 1: v = c; break;
        case 2: v = d; break; case 3: v = e; break;
        case 4: v = h; break; case 5: v = l; break;
        case 6: RD(v, HL); IC(HL, 1); break;
        default: v = a; break;
        }
        if (op2 < 0x40) {
            ROTSHIFT(op2 >> 3, v);
        } else if (op2 < 0x80) {
            BITTEST((op2 >> 3) & 7, v, ri == 6 ? (uint8_t)(wz >> 8) : v);
            break;
        } else if (op2 < 0xC0) {
            v &= (uint8_t)~(1u << ((op2 >> 3) & 7));
        } else {
            v |= (uint8_t)(1u << ((op2 >> 3) & 7));
        }
        switch (ri) {
        case 0: b = v; break; case 1: c = v; break;
        case 2: d = v; break; case 3: e = v; break;
        case 4: h = v; break; case 5: l = v; break;
        case 6: WR(HL, v); break;
        default: a = v; break;
        }
    } break;

    /* ---------------- ED ---------------- */
    case 0xED: {
        uint8_t op2;
        FETCH(op2);
        switch (op2) {
        /* IN r,(C) */
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x70: case 0x78: {
            uint8_t v;
            wz = (uint16_t)(BC + 1);
            PORT_IN(v, BC);
            f = (uint8_t)((f & FC) | sz53p[v]); q = f;
            switch ((op2 >> 3) & 7) {
            case 0: b = v; break; case 1: c = v; break;
            case 2: d = v; break; case 3: e = v; break;
            case 4: h = v; break; case 5: l = v; break;
            case 6: break;                                   /* IN F,(C): so flags */
            default: a = v; break;
            }
        } break;
        /* OUT (C),r */
        case 0x41: PORT_OUT(BC, b); wz = (uint16_t)(BC + 1); break;
        case 0x49: PORT_OUT(BC, c); wz = (uint16_t)(BC + 1); break;
        case 0x51: PORT_OUT(BC, d); wz = (uint16_t)(BC + 1); break;
        case 0x59: PORT_OUT(BC, e); wz = (uint16_t)(BC + 1); break;
        case 0x61: PORT_OUT(BC, h); wz = (uint16_t)(BC + 1); break;
        case 0x69: PORT_OUT(BC, l); wz = (uint16_t)(BC + 1); break;
        case 0x71: PORT_OUT(BC, 0); wz = (uint16_t)(BC + 1); break;  /* OUT (C),0 (NMOS) */
        case 0x79: PORT_OUT(BC, a); wz = (uint16_t)(BC + 1); break;

        case 0x42: IC(IR, 7); SBC16(BC); break;
        case 0x52: IC(IR, 7); SBC16(DE); break;
        case 0x62: IC(IR, 7); SBC16(HL); break;
        case 0x72: IC(IR, 7); SBC16(sp); break;
        case 0x4A: IC(IR, 7); ADC16(BC); break;
        case 0x5A: IC(IR, 7); ADC16(DE); break;
        case 0x6A: IC(IR, 7); ADC16(HL); break;
        case 0x7A: IC(IR, 7); ADC16(sp); break;

        /* LD (nn),rr */
        case 0x43: case 0x53: case 0x63: case 0x73: {
            uint16_t ad, v; uint8_t lo, hi;
            RD(lo, pc); pc++; RD(hi, pc); pc++;
            ad = (uint16_t)(lo | (hi << 8));
            switch (op2) {
            case 0x43: v = BC; break; case 0x53: v = DE; break;
            case 0x63: v = HL; break; default: v = sp; break;
            }
            WR(ad, v); ad++; wz = ad; WR(ad, v >> 8);
        } break;
        /* LD rr,(nn) */
        case 0x4B: case 0x5B: case 0x6B: case 0x7B: {
            uint16_t ad; uint8_t lo, hi;
            RD(lo, pc); pc++; RD(hi, pc); pc++;
            ad = (uint16_t)(lo | (hi << 8));
            RD(lo, ad); ad++; wz = ad; RD(hi, ad);
            switch (op2) {
            case 0x4B: b = hi; c = lo; break; case 0x5B: d = hi; e = lo; break;
            case 0x6B: h = hi; l = lo; break; default: sp = (uint16_t)(lo | (hi << 8)); break;
            }
        } break;

        /* NEG */
        case 0x44: case 0x4C: case 0x54: case 0x5C:
        case 0x64: case 0x6C: case 0x74: case 0x7C: {
            uint8_t v = a; a = 0; SUB_A(v);
        } break;
        /* RETN / RETI */
        case 0x45: case 0x4D: case 0x55: case 0x5D:
        case 0x65: case 0x6D: case 0x75: case 0x7D: {
            uint8_t lo, hi;
            cpu->iff1 = cpu->iff2;
            RD(lo, sp); sp++; RD(hi, sp); sp++;
            pc = (uint16_t)(lo | (hi << 8)); wz = pc;
        } break;
        /* IM */
        case 0x46: case 0x4E: case 0x66: case 0x6E: cpu->im = 0; break;
        case 0x56: case 0x76: cpu->im = 1; break;
        case 0x5E: case 0x7E: cpu->im = 2; break;

        case 0x47: IC(IR, 1); cpu->i = a; break;                     /* LD I,A */
        case 0x4F: IC(IR, 1); r7 = a & 0x80; rr = a; break;          /* LD R,A */
        case 0x57:                                                   /* LD A,I */
            IC(IR, 1); a = cpu->i;
            f = (uint8_t)((f & FC) | sz53[a] | (cpu->iff2 ? FV : 0)); q = f;
            cpu->iff2_read = 1;
            break;
        case 0x5F:                                                   /* LD A,R */
            IC(IR, 1); a = (uint8_t)(r7 | (rr & 0x7F));
            f = (uint8_t)((f & FC) | sz53[a] | (cpu->iff2 ? FV : 0)); q = f;
            cpu->iff2_read = 1;
            break;
        case 0x67: {                                                 /* RRD */
            uint8_t v;
            RD(v, HL); IC(HL, 4);
            WR(HL, (a << 4) | (v >> 4));
            a = (uint8_t)((a & 0xF0) | (v & 0x0F));
            f = (uint8_t)((f & FC) | sz53p[a]); q = f;
            wz = (uint16_t)(HL + 1);
        } break;
        case 0x6F: {                                                 /* RLD */
            uint8_t v;
            RD(v, HL); IC(HL, 4);
            WR(HL, (v << 4) | (a & 0x0F));
            a = (uint8_t)((a & 0xF0) | (v >> 4));
            f = (uint8_t)((f & FC) | sz53p[a]); q = f;
            wz = (uint16_t)(HL + 1);
        } break;

        /* LDI / LDD / LDIR / LDDR */
        case 0xA0: case 0xA8: case 0xB0: case 0xB8: {
            uint8_t v;
            int dir = (op2 & 0x08) ? -1 : 1;
            uint16_t bc;
            RD(v, HL);
            WR(DE, v);
            IC(DE, 2);
            bc = (uint16_t)(BC - 1); SET_BC(bc);
            v = (uint8_t)(v + a);
            f = (uint8_t)((f & (FC | FZ | FS)) | (bc ? FV : 0) | (v & F3) | ((v & 0x02) ? F5 : 0)); q = f;
            if ((op2 & 0x10) && bc) {
                IC(DE, 5);
                pc -= 2; wz = (uint16_t)(pc + 1);
                f = BLK_XY(f, pc); q = f;
            }
            SET_HL(HL + dir); SET_DE(DE + dir);
        } break;

        /* CPI / CPD / CPIR / CPDR */
        case 0xA1: case 0xA9: case 0xB1: case 0xB9: {
            uint8_t v, res;
            int dir = (op2 & 0x08) ? -1 : 1;
            uint16_t bc;
            RD(v, HL);
            res = (uint8_t)(a - v);
            IC(HL, 5);
            bc = (uint16_t)(BC - 1); SET_BC(bc);
            f = (uint8_t)((f & FC) | (bc ? (FV | FN) : FN) | ((a ^ v ^ res) & FH) |
                          (res ? 0 : FZ) | (res & FS));
            if (f & FH) res--;
            f |= (uint8_t)((res & F3) | ((res & 0x02) ? F5 : 0)); q = f;
            if ((op2 & 0x10) && (f & (FV | FZ)) == FV) {
                IC(HL, 5);
                pc -= 2; wz = (uint16_t)(pc + 1);
                f = BLK_XY(f, pc); q = f;
            } else {
                wz = (uint16_t)(wz + dir);
            }
            SET_HL(HL + dir);
        } break;

        /* INI / IND / INIR / INDR */
        case 0xA2: case 0xAA: case 0xB2: case 0xBA: {
            uint8_t v, v2;
            int dir = (op2 & 0x08) ? -1 : 1;
            IC(IR, 1);
            PORT_IN(v, BC);
            WR(HL, v);
            wz = (uint16_t)(BC + dir);
            b--;
            v2 = (uint8_t)(v + (uint8_t)(c + dir));
            f = (uint8_t)(((v & 0x80) ? FN : 0) | ((v2 < v) ? (FH | FC) : 0) |
                          parity[(v2 & 0x07) ^ b] | sz53[b]); q = f;
            if ((op2 & 0x10) && b) {
                IC(HL, 5);
                pc -= 2;
                f = blk_io_flags(f, pc, v, b); q = f;
            }
            SET_HL(HL + dir);
        } break;

        /* OUTI / OUTD / OTIR / OTDR */
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: {
            uint8_t v, v2;
            int dir = (op2 & 0x08) ? -1 : 1;
            IC(IR, 1);
            RD(v, HL);
            b--;
            wz = (uint16_t)(BC + dir);
            PORT_OUT(BC, v);
            SET_HL(HL + dir);
            v2 = (uint8_t)(v + l);
            f = (uint8_t)(((v & 0x80) ? FN : 0) | ((v2 < v) ? (FH | FC) : 0) |
                          parity[(v2 & 0x07) ^ b] | sz53[b]); q = f;
            if ((op2 & 0x10) && b) {
                IC(BC, 5);
                pc -= 2;
                f = blk_io_flags(f, pc, v, b); q = f;
            }
        } break;

        default: break;                                    /* NOP de 8 T */
        }
    } break;

    /* ---------------- DD / FD ---------------- */
    case 0xDD:
    case 0xFD: {
        uint8_t op2;
        use_iy = (op == 0xFD);
        FETCH(op2);
        if (op2 == 0xDD || op2 == 0xFD) {
            /* outro prefixo em seguida: este prefixo termina aqui como
               instrucao isolada; o M1 do proximo ja foi feito e fica
               pendente (PC aponta para ele). Interrupcao nao e aceita. */
            pc--;
            cpu->pending_prefix = op2;
            cpu->ei_delay = 1;
            break;
        }
        xy = use_iy ? cpu->iy : cpu->ix;
        switch (op2) {
        case 0x09: IC(IR, 7); ADD16(xy, BC); break;
        case 0x19: IC(IR, 7); ADD16(xy, DE); break;
        case 0x29: IC(IR, 7); ADD16(xy, xy); break;
        case 0x39: IC(IR, 7); ADD16(xy, sp); break;
        case 0x21: { uint8_t lo, hi; RD(lo, pc); pc++; RD(hi, pc); pc++; xy = (uint16_t)(lo | (hi << 8)); } break;
        case 0x22: {
            uint16_t ad; uint8_t lo, hi;
            RD(lo, pc); pc++; RD(hi, pc); pc++;
            ad = (uint16_t)(lo | (hi << 8));
            WR(ad, xy); ad++; wz = ad; WR(ad, xy >> 8);
        } break;
        case 0x23: IC(IR, 2); xy++; break;
        case 0x24: { uint8_t v = XH; INC8(v); SET_XH(v); } break;
        case 0x25: { uint8_t v = XH; DEC8(v); SET_XH(v); } break;
        case 0x26: { uint8_t v; RD(v, pc); pc++; SET_XH(v); } break;
        case 0x2A: {
            uint16_t ad; uint8_t lo, hi;
            RD(lo, pc); pc++; RD(hi, pc); pc++;
            ad = (uint16_t)(lo | (hi << 8));
            RD(lo, ad); ad++; wz = ad; RD(hi, ad);
            xy = (uint16_t)(lo | (hi << 8));
        } break;
        case 0x2B: IC(IR, 2); xy--; break;
        case 0x2C: { uint8_t v = XL; INC8(v); SET_XL(v); } break;
        case 0x2D: { uint8_t v = XL; DEC8(v); SET_XL(v); } break;
        case 0x2E: { uint8_t v; RD(v, pc); pc++; SET_XL(v); } break;

        case 0x34: case 0x35: {                     /* INC/DEC (XY+d) */
            uint8_t off, v;
            RD(off, pc); IC(pc, 5); pc++;
            wz = (uint16_t)(xy + (int8_t)off);
            RD(v, wz); IC(wz, 1);
            if (op2 == 0x34) { INC8(v); } else { DEC8(v); }
            WR(wz, v);
        } break;
        case 0x36: {                                /* LD (XY+d),n */
            uint8_t off, v;
            RD(off, pc); pc++;
            RD(v, pc); IC(pc, 2); pc++;
            wz = (uint16_t)(xy + (int8_t)off);
            WR(wz, v);
        } break;

        /* LD r,XYH / LD r,XYL / LD r,(XY+d) */
#define IXLDROW(base, dst) \
        case base + 4: dst = XH; break; \
        case base + 5: dst = XL; break; \
        case base + 6: { uint8_t off; RD(off, pc); IC(pc, 5); pc++; \
            wz = (uint16_t)(xy + (int8_t)off); RD(dst, wz); } break;
        IXLDROW(0x40, b)
        IXLDROW(0x48, c)
        IXLDROW(0x50, d)
        IXLDROW(0x58, e)
        IXLDROW(0x78, a)
#undef IXLDROW
        case 0x66: { uint8_t off; RD(off, pc); IC(pc, 5); pc++; wz = (uint16_t)(xy + (int8_t)off); RD(h, wz); } break;
        case 0x6E: { uint8_t off; RD(off, pc); IC(pc, 5); pc++; wz = (uint16_t)(xy + (int8_t)off); RD(l, wz); } break;

        /* LD XYH,r / LD XYL,r */
        case 0x60: SET_XH(b); break;
        case 0x61: SET_XH(c); break;
        case 0x62: SET_XH(d); break;
        case 0x63: SET_XH(e); break;
        case 0x64: break;
        case 0x65: SET_XH(XL); break;
        case 0x67: SET_XH(a); break;
        case 0x68: SET_XL(b); break;
        case 0x69: SET_XL(c); break;
        case 0x6A: SET_XL(d); break;
        case 0x6B: SET_XL(e); break;
        case 0x6C: SET_XL(XH); break;
        case 0x6D: break;
        case 0x6F: SET_XL(a); break;

        /* LD (XY+d),r */
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x77: {
            uint8_t off, v;
            RD(off, pc); IC(pc, 5); pc++;
            wz = (uint16_t)(xy + (int8_t)off);
            switch (op2 & 7) {
            case 0: v = b; break; case 1: v = c; break;
            case 2: v = d; break; case 3: v = e; break;
            case 4: v = h; break; case 5: v = l; break;
            default: v = a; break;
            }
            WR(wz, v);
        } break;

        /* ALU A,XYH / XYL / (XY+d) */
#define IXALUROW(base, OP) \
        case base + 4: OP(XH); break; \
        case base + 5: OP(XL); break; \
        case base + 6: { uint8_t off, mv8; RD(off, pc); IC(pc, 5); pc++; \
            wz = (uint16_t)(xy + (int8_t)off); RD(mv8, wz); OP(mv8); } break;
        IXALUROW(0x80, ADD_A)
        IXALUROW(0x88, ADC_A)
        IXALUROW(0x90, SUB_A)
        IXALUROW(0x98, SBC_A)
        IXALUROW(0xA0, AND_A)
        IXALUROW(0xA8, XOR_A)
        IXALUROW(0xB0, OR_A)
        IXALUROW(0xB8, CP_A)
#undef IXALUROW

        case 0xE1: { uint8_t lo, hi; RD(lo, sp); sp++; RD(hi, sp); sp++; xy = (uint16_t)(lo | (hi << 8)); } break;
        case 0xE3: {
            uint8_t lo, hi;
            RD(lo, sp); RD(hi, sp + 1); IC(sp + 1, 1);
            WR(sp + 1, xy >> 8); WR(sp, xy); IC(sp, 2);
            xy = (uint16_t)(lo | (hi << 8)); wz = xy;
        } break;
        case 0xE5: IC(IR, 1); sp--; WR(sp, xy >> 8); sp--; WR(sp, xy); break;
        case 0xE9: pc = xy; break;
        case 0xF9: IC(IR, 2); sp = xy; break;

        /* DDCB / FDCB */
        case 0xCB: {
            uint8_t off, op3, v;
            unsigned ri;
            RD(off, pc); pc++;
            wz = (uint16_t)(xy + (int8_t)off);
            RD(op3, pc); IC(pc, 2); pc++;
            RD(v, wz); IC(wz, 1);
            if (op3 >= 0x40 && op3 < 0x80) {
                BITTEST((op3 >> 3) & 7, v, (uint8_t)(wz >> 8));
                break;
            }
            if (op3 < 0x40) {
                ROTSHIFT(op3 >> 3, v);
            } else if (op3 < 0xC0) {
                v &= (uint8_t)~(1u << ((op3 >> 3) & 7));
            } else {
                v |= (uint8_t)(1u << ((op3 >> 3) & 7));
            }
            WR(wz, v);
            ri = op3 & 7;
            switch (ri) {   /* copia nao documentada no registrador */
            case 0: b = v; break; case 1: c = v; break;
            case 2: d = v; break; case 3: e = v; break;
            case 4: h = v; break; case 5: l = v; break;
            case 6: break;  default: a = v; break;
            }
        } break;

        default:
            /* opcode nao usa HL: o prefixo e ignorado e o opcode executa
               normalmente (sem nova busca M1) */
            op = op2;
            goto dispatch;
        }
        if (use_iy) cpu->iy = xy; else cpu->ix = xy;
    } break;
    }

done:
    cpu->af = (uint16_t)((a << 8) | f);
    cpu->bc = BC;
    cpu->de = DE;
    cpu->hl = HL;
    cpu->sp = sp;
    cpu->pc = pc;
    cpu->memptr = wz;
    cpu->r = (uint8_t)(r7 | (rr & 0x7F));
    cpu->q = q;
    cpu->tstates = t;
    return (int)(t - t0);
}

int z80_interrupt(Z80 *cpu, uint8_t bus_value)
{
    void *ctx = cpu->bus.ctx;
    uint8_t (*rd)(void *, uint16_t) = cpu->bus.read;
    void (*wr)(void *, uint16_t, uint8_t) = cpu->bus.write;
    unsigned (*mc)(void *, uint16_t, uint32_t) = cpu->bus.mem_contention;
    uint32_t t = cpu->tstates;
    const uint32_t t0 = t;
    uint16_t pc, sp;

    if (!cpu->iff1 || cpu->ei_delay) return 0;

    /* NMOS: LD A,I / LD A,R interrompido copia IFF2 ja zerado */
    if (cpu->iff2_read) {
        cpu->af &= (uint16_t)~FP;
        cpu->iff2_read = 0;
    }
    if (cpu->halted) { cpu->pc++; cpu->halted = 0; }
    cpu->iff1 = cpu->iff2 = 0;
    cpu->r = (uint8_t)((cpu->r & 0x80) | ((cpu->r + 1) & 0x7F));

    t += 7;     /* M1 de reconhecimento (sem contencao, como no Fuse) */
    pc = cpu->pc;
    sp = cpu->sp;
    sp--; WR(sp, pc >> 8);
    sp--; WR(sp, pc);
    cpu->sp = sp;

    if (cpu->im == 2) {
        uint16_t vec = (uint16_t)((cpu->i << 8) | bus_value);
        uint8_t lo, hi;
        RD(lo, vec); vec++; RD(hi, vec);
        pc = (uint16_t)(lo | (hi << 8));
    } else {
        pc = 0x0038;    /* IM 1; IM 0 com 0xFF no barramento = RST 38h */
    }
    cpu->pc = pc;
    cpu->memptr = pc;
    cpu->q = 0;
    cpu->tstates = t;
    return (int)(t - t0);
}

int z80_nmi(Z80 *cpu)
{
    void *ctx = cpu->bus.ctx;
    void (*wr)(void *, uint16_t, uint8_t) = cpu->bus.write;
    unsigned (*mc)(void *, uint16_t, uint32_t) = cpu->bus.mem_contention;
    uint32_t t = cpu->tstates;
    const uint32_t t0 = t;
    uint16_t pc, sp;

    if (cpu->halted) { cpu->pc++; cpu->halted = 0; }
    cpu->iff1 = 0;
    cpu->iff2_read = 0;
    cpu->ei_delay = 0;
    cpu->pending_prefix = 0;    /* PC aponta para o prefixo: sera buscado de novo */
    cpu->r = (uint8_t)((cpu->r & 0x80) | ((cpu->r + 1) & 0x7F));
    t += 5;
    pc = cpu->pc;
    sp = cpu->sp;
    sp--; WR(sp, pc >> 8);
    sp--; WR(sp, pc);
    cpu->sp = sp;
    cpu->pc = 0x0066;
    cpu->q = 0;
    cpu->tstates = t;
    return (int)(t - t0);
}
