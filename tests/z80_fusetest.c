/*
 * z80_fusetest.c - Roda a suite de testes do nucleo Z80 do Fuse
 * (tests.in / tests.expected; formato em tests/data/FUSE_TESTS_FORMAT.md).
 *
 * Compara registradores (inclusive MEMPTR, I, R, IFF1/2, IM, halted),
 * T-states finais e a diferenca de memoria. Como bonus, compara tambem a
 * lista de eventos de barramento (MC/MR/MW/PC/PR/PW), o que valida o ponto
 * exato de cada chamada de contencao. Os eventos sao contados a parte e nao
 * reprovam o teste, a menos que se use a opcao --strict-events.
 *
 * Uso: z80_fusetest tests.in tests.expected [--strict-events] [-v]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "z80.h"
#include "compat.h"

/* ---------------- maquina de teste ---------------- */

static uint8_t mem[65536];
static uint8_t mem_orig[65536];
static Z80 cpu;

#define MAX_EVENTS 4096
#define EV_LEN 48
static char events[MAX_EVENTS][EV_LEN];
static int n_events;

/* pos-E/S pendente (linhas PC impressas pelo Fuse depois do PR/PW) */
static uint16_t io_port;
static uint32_t io_t0;

static void ev(const char *fmt_type, uint32_t t, uint16_t addr, int value)
{
    if (n_events >= MAX_EVENTS) return;
    if (value >= 0)
        snprintf(events[n_events++], EV_LEN, "%5u %s %04x %02x", (unsigned)t, fmt_type, addr, value);
    else
        snprintf(events[n_events++], EV_LEN, "%5u %s %04x", (unsigned)t, fmt_type, addr);
}

static uint8_t t_read(void *ctx, uint16_t addr)
{
    uint8_t v = mem[addr];
    ev("MR", cpu.tstates, addr, v);
    return v;
}

static void t_write(void *ctx, uint16_t addr, uint8_t v)
{
    mem[addr] = v;
    ev("MW", cpu.tstates, addr, v);
}

static unsigned t_mc(void *ctx, uint16_t addr, uint32_t t)
{
    ev("MC", t, addr, -1);
    return 0;
}

static unsigned t_ioc(void *ctx, uint16_t port, uint32_t t)
{
    /* pre-E/S do Fuse */
    io_port = port;
    io_t0 = t;
    if ((port & 0xC000) == 0x4000) ev("PC", t, port, -1);
    return 0;
}

static void io_post(void)
{
    uint16_t port = io_port;
    uint32_t t = io_t0 + 1;
    if (port & 1) {
        if ((port & 0xC000) == 0x4000) {
            ev("PC", t, port, -1);
            ev("PC", t + 1, port, -1);
            ev("PC", t + 2, port, -1);
        }
    } else {
        ev("PC", t, port, -1);
    }
}

static uint8_t t_in(void *ctx, uint16_t port)
{
    uint8_t v = (uint8_t)(port >> 8);
    /* nosso callback roda no T3 (t0+3); o Fuse registra em t0+1 */
    ev("PR", io_t0 + 1, port, v);
    io_post();
    return v;
}

static void t_out(void *ctx, uint16_t port, uint8_t v)
{
    ev("PW", io_t0 + 1, port, v);
    io_post();
}

/* ---------------- leitura dos arquivos ---------------- */

typedef struct {
    char *buf;
    size_t len, pos;
} Text;

static int load_text(Text *tx, const char *path)
{
    size_t len;
    uint8_t *b = tk_read_file(path, &len, 0);
    if (!b) return 0;
    {
        char *nb = (char *)realloc(b, len + 1);
        if (!nb) { free(b); return 0; }
        tx->buf = nb;
    }
    tx->buf[len] = 0;
    tx->len = len;
    tx->pos = 0;
    return 1;
}

/* le uma linha (sem o \n); devolve 0 no fim */
static int get_line(Text *tx, char *out, size_t outlen)
{
    size_t n = 0;
    if (tx->pos >= tx->len) return 0;
    while (tx->pos < tx->len && tx->buf[tx->pos] != '\n') {
        if (n + 1 < outlen && tx->buf[tx->pos] != '\r') out[n++] = tx->buf[tx->pos];
        tx->pos++;
    }
    if (tx->pos < tx->len) tx->pos++;
    out[n] = 0;
    return 1;
}

/* copia com truncamento silencioso */
static void copy_str(char *dst, size_t dstlen, const char *src)
{
    size_t n = strlen(src);
    if (n >= dstlen) n = dstlen - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int blank(const char *s)
{
    while (*s) { if (!isspace((unsigned char)*s)) return 0; s++; }
    return 1;
}

/* token hex/decimal seguinte (aceita "-1") */
static int get_token(Text *tx, long *val, int base)
{
    char tok[32];
    size_t n = 0;
    while (tx->pos < tx->len && isspace((unsigned char)tx->buf[tx->pos])) tx->pos++;
    if (tx->pos >= tx->len) return 0;
    while (tx->pos < tx->len && !isspace((unsigned char)tx->buf[tx->pos]) && n + 1 < sizeof(tok))
        tok[n++] = tx->buf[tx->pos++];
    tok[n] = 0;
    *val = strtol(tok, NULL, base);
    return 1;
}

/* ---------------- execucao ---------------- */

typedef struct {
    char name[96];
    uint16_t regs[13];
    unsigned i, r, iff1, iff2, im, halted;
    unsigned long end_t;
} TestIn;

static int read_test_in(Text *tx, TestIn *ti)
{
    char line[256];
    long v;
    int k;
    do {
        if (!get_line(tx, line, sizeof(line))) return 0;
    } while (blank(line));
    copy_str(ti->name, sizeof(ti->name), line);
    { char *p = ti->name + strlen(ti->name); while (p > ti->name && isspace((unsigned char)p[-1])) *--p = 0; }
    for (k = 0; k < 13; k++) { if (!get_token(tx, &v, 16)) return 0; ti->regs[k] = (uint16_t)v; }
    {
        long vals[7];
        static const int bases[7] = { 16, 16, 10, 10, 10, 10, 10 };
        for (k = 0; k < 7; k++)
            if (!get_token(tx, &vals[k], bases[k])) return 0;
        ti->i = (unsigned)vals[0]; ti->r = (unsigned)vals[1];
        ti->iff1 = (unsigned)vals[2]; ti->iff2 = (unsigned)vals[3];
        ti->im = (unsigned)vals[4]; ti->halted = (unsigned)vals[5];
        ti->end_t = (unsigned long)vals[6];
    }
    /* memoria */
    for (k = 0; k < 65536; k++) {
        static const uint8_t pat[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        mem[k] = pat[k & 3];
    }
    for (;;) {
        long addr;
        if (!get_token(tx, &addr, 16)) return 0;
        if (addr < 0 || addr >= 0x10000) break;
        for (;;) {
            if (!get_token(tx, &v, 16)) return 0;
            if (v < 0 || v >= 0x100) break;
            mem[addr & 0xFFFF] = (uint8_t)v;
            addr++;
        }
    }
    /* termina a linha atual */
    { char tmp[256]; get_line(tx, tmp, sizeof(tmp)); }
    return 1;
}

typedef struct {
    char name[96];
    char ev[MAX_EVENTS][EV_LEN];
    int n_ev;
    char regs1[256], regs2[256];
    char mem_lines[64][256];
    int n_mem;
} TestExp;

static int read_test_exp(Text *tx, TestExp *te)
{
    char line[512];
    do {
        if (!get_line(tx, line, sizeof(line))) return 0;
    } while (blank(line));
    copy_str(te->name, sizeof(te->name), line);
    { char *p = te->name + strlen(te->name); while (p > te->name && isspace((unsigned char)p[-1])) *--p = 0; }
    te->n_ev = 0;
    te->n_mem = 0;
    for (;;) {
        if (!get_line(tx, line, sizeof(line))) return 0;
        if (line[0] == ' ' || line[0] == '\t') {
            const char *s = line;
            while (*s == ' ' || *s == '\t') s++;
            /* normaliza para o formato "%5d ..." */
            {
                unsigned tt; char type[8]; unsigned ad, val; int n;
                n = sscanf(s, "%u %7s %x %x", &tt, type, &ad, &val);
                if (te->n_ev < MAX_EVENTS) {
                    if (n == 4) snprintf(te->ev[te->n_ev++], EV_LEN, "%5u %s %04x %02x", tt, type, ad, val);
                    else snprintf(te->ev[te->n_ev++], EV_LEN, "%5u %s %04x", tt, type, ad);
                }
            }
            continue;
        }
        copy_str(te->regs1, sizeof(te->regs1), line);
        break;
    }
    if (!get_line(tx, line, sizeof(line))) return 0;
    copy_str(te->regs2, sizeof(te->regs2), line);
    for (;;) {
        if (!get_line(tx, line, sizeof(line))) break;
        if (blank(line)) break;
        if (te->n_mem < 64) copy_str(te->mem_lines[te->n_mem++], 256, line);
    }
    return 1;
}

/* normaliza espacos para comparar linhas */
static void norm(const char *in, char *out, size_t outlen)
{
    size_t n = 0;
    int sp = 0;
    while (*in && isspace((unsigned char)*in)) in++;
    for (; *in && n + 1 < outlen; in++) {
        if (isspace((unsigned char)*in)) { sp = 1; continue; }
        if (sp && n + 1 < outlen) out[n++] = ' ';
        sp = 0;
        out[n++] = *in;
    }
    out[n] = 0;
}


/* ---------------- verificacoes extras (interrupcoes etc.) ---------------- */

static int extra_fail;

/* O tests.expected do Fuse traz, para 4 testes de bloco repetido (INIR/OTIR/CPDR/OTDR
 * que fazem nova iteracao), as flags do modelo antigo. O nucleo segue o Z80 real
 * (D. Banks 2018: flags 3/5 = bits 11/13 do PC; H e P/V de INxR/OTxR ajustados),
 * validado pelo teste z80bltst (Ped7g) gravado de um Z80 real e igual ao MAME.
 * Para esses 4 casos o AF esperado e o do modelo novo; o resto do teste e o do Fuse. */
static const struct { const char *name; const char *af; } fuse_fix[] = {
    { "edb2_1", "8a00" }, { "edb3_1", "3403" }, { "edb9_2", "ffaf" }, { "edbb_1", "0903" },
};
static int fixed_count;
#define XCHECK(cond, msg) do { if (!(cond)) { printf("  extra FALHOU: %s\n", msg); extra_fail++; } } while (0)

static void setup_plain(Z80Bus *bus)
{
    memset(mem, 0, sizeof(mem));
    z80_init(&cpu, bus);
    cpu.tstates = 0;
    cpu.sp = 0x8000;
    n_events = 0;
}

static int run_extras(Z80Bus *bus)
{
    int r;
    extra_fail = 0;

    /* IM1: 13 T, empilha PC, salta para 0x38, zera IFF */
    setup_plain(bus);
    cpu.pc = 0x1234; cpu.iff1 = cpu.iff2 = 1; cpu.im = 1;
    r = z80_interrupt(&cpu, 0xFF);
    XCHECK(r == 13 && cpu.pc == 0x0038 && cpu.sp == 0x7FFE && mem[0x7FFE] == 0x34 &&
           mem[0x7FFF] == 0x12 && !cpu.iff1 && !cpu.iff2 && cpu.memptr == 0x38 && cpu.r == 1,
           "IM1");
    /* IFF1 = 0: nao aceita */
    r = z80_interrupt(&cpu, 0xFF);
    XCHECK(r == 0 && cpu.pc == 0x0038, "DI bloqueia");

    /* IM2: 19 T, vetor em (I<<8)|bus */
    setup_plain(bus);
    cpu.pc = 0x4000; cpu.iff1 = cpu.iff2 = 1; cpu.im = 2; cpu.i = 0x39;
    mem[0x39FF] = 0x78; mem[0x3A00] = 0x56;
    r = z80_interrupt(&cpu, 0xFF);
    XCHECK(r == 19 && cpu.pc == 0x5678 && cpu.memptr == 0x5678, "IM2");

    /* EI: interrupcao so depois da instrucao seguinte */
    setup_plain(bus);
    mem[0] = 0xFB; mem[1] = 0x00; mem[2] = 0x00; cpu.im = 1;
    z80_step(&cpu);
    XCHECK(z80_interrupt(&cpu, 0xFF) == 0, "EI atrasa interrupcao");
    z80_step(&cpu);
    XCHECK(z80_interrupt(&cpu, 0xFF) == 13 && mem[0x7FFE] == 0x02, "aceita apos EI+1");

    /* HALT: PC fica no HALT; interrupcao avanca PC antes de empilhar */
    setup_plain(bus);
    mem[0x100] = 0x76; cpu.pc = 0x100; cpu.iff1 = cpu.iff2 = 1; cpu.im = 1;
    z80_step(&cpu); z80_step(&cpu); z80_step(&cpu);
    XCHECK(cpu.halted && cpu.pc == 0x100 && cpu.tstates == 12 && cpu.r == 3, "HALT repete M1");
    z80_interrupt(&cpu, 0xFF);
    XCHECK(!cpu.halted && mem[0x7FFE] == 0x01 && mem[0x7FFF] == 0x01, "HALT + INT empilha PC+1");

    /* NMI: 11 T, salta para 0x66, IFF1=0, IFF2 preservado */
    setup_plain(bus);
    cpu.pc = 0x2000; cpu.iff1 = cpu.iff2 = 1;
    r = z80_nmi(&cpu);
    XCHECK(r == 11 && cpu.pc == 0x66 && !cpu.iff1 && cpu.iff2 && mem[0x7FFE] == 0x00 &&
           mem[0x7FFF] == 0x20, "NMI");
    /* RETN restaura IFF1 */
    mem[0x66] = 0xED; mem[0x67] = 0x45;
    z80_step(&cpu);
    XCHECK(cpu.pc == 0x2000 && cpu.iff1, "RETN");

    /* LD A,I interrompido (NMOS): P/V zerado */
    setup_plain(bus);
    mem[0] = 0xED; mem[1] = 0x57; cpu.iff1 = cpu.iff2 = 1; cpu.im = 1;
    z80_step(&cpu);
    XCHECK(Z80_F(&cpu) & Z80_FLAG_PV, "LD A,I copia IFF2");
    z80_interrupt(&cpu, 0xFF);
    XCHECK(!(Z80_F(&cpu) & Z80_FLAG_PV), "LD A,I + INT zera P/V");

    /* cadeia DD DD FD 21 nn nn: cada prefixo extra e um passo de 4 T, sem INT */
    setup_plain(bus);
    mem[0] = 0xDD; mem[1] = 0xDD; mem[2] = 0xFD; mem[3] = 0x21; mem[4] = 0x34; mem[5] = 0x12;
    cpu.iff1 = cpu.iff2 = 1; cpu.im = 1;
    z80_step(&cpu);
    XCHECK(z80_interrupt(&cpu, 0xFF) == 0, "INT bloqueada apos prefixo");
    z80_step(&cpu);
    XCHECK(z80_interrupt(&cpu, 0xFF) == 0, "INT bloqueada apos prefixo 2");
    z80_step(&cpu);
    XCHECK(cpu.iy == 0x1234 && cpu.ix == 0 && cpu.pc == 6 && cpu.tstates == 4 + 4 + 4 + 10 &&
           cpu.r == 4, "cadeia de prefixos");

    return extra_fail;
}

int main(int argc, char **argv)
{
    Text tin, texp;
    static TestIn ti;
    static TestExp te;
    int total = 0, passed = 0, ev_ok = 0, ev_total = 0;
    int strict_events = 0, verbose = 0;
    const char *fin = NULL, *fexp = NULL;
    int k;
    char failed_names[8192];
    size_t fn_len = 0;
    Z80Bus bus;

    failed_names[0] = 0;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--strict-events")) strict_events = 1;
        else if (!strcmp(argv[k], "-v")) verbose = 1;
        else if (!fin) fin = argv[k];
        else if (!fexp) fexp = argv[k];
    }
    if (!fin || !fexp) {
        fprintf(stderr, "uso: %s tests.in tests.expected [--strict-events] [-v]\n", argv[0]);
        return 2;
    }
    if (!load_text(&tin, fin) || !load_text(&texp, fexp)) {
        fprintf(stderr, "erro: nao consegui ler %s / %s\n", fin, fexp);
        return 2;
    }

    memset(&bus, 0, sizeof(bus));
    bus.read = t_read;
    bus.write = t_write;
    bus.in = t_in;
    bus.out = t_out;
    bus.mem_contention = t_mc;
    bus.io_contention = t_ioc;

    while (read_test_in(&tin, &ti)) {
        char got1[256], got2[256], a[256], b2[256];
        int ok = 1, evmatch;
        int mi = 0;
        int addr;

        if (!read_test_exp(&texp, &te)) {
            fprintf(stderr, "erro: tests.expected terminou antes (teste %s)\n", ti.name);
            return 2;
        }
        if (strcmp(ti.name, te.name) != 0) {
            fprintf(stderr, "erro: nomes fora de ordem: %s / %s\n", ti.name, te.name);
            return 2;
        }
        total++;

        z80_init(&cpu, &bus);
        cpu.af = ti.regs[0]; cpu.bc = ti.regs[1]; cpu.de = ti.regs[2]; cpu.hl = ti.regs[3];
        cpu.af_ = ti.regs[4]; cpu.bc_ = ti.regs[5]; cpu.de_ = ti.regs[6]; cpu.hl_ = ti.regs[7];
        cpu.ix = ti.regs[8]; cpu.iy = ti.regs[9]; cpu.sp = ti.regs[10]; cpu.pc = ti.regs[11];
        cpu.memptr = ti.regs[12];
        cpu.i = (uint8_t)ti.i; cpu.r = (uint8_t)ti.r;
        cpu.iff1 = (uint8_t)ti.iff1; cpu.iff2 = (uint8_t)ti.iff2; cpu.im = (uint8_t)ti.im;
        cpu.halted = (uint8_t)ti.halted;
        cpu.tstates = 0;
        memcpy(mem_orig, mem, sizeof(mem));
        n_events = 0;

        while (cpu.tstates < ti.end_t) z80_step(&cpu);

        snprintf(got1, sizeof(got1), "%04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x",
                 cpu.af, cpu.bc, cpu.de, cpu.hl, cpu.af_, cpu.bc_, cpu.de_, cpu.hl_,
                 cpu.ix, cpu.iy, cpu.sp, cpu.pc, cpu.memptr);
        snprintf(got2, sizeof(got2), "%02x %02x %d %d %d %d %d",
                 cpu.i, cpu.r, cpu.iff1, cpu.iff2, cpu.im, cpu.halted, (int)cpu.tstates);
        norm(te.regs1, a, sizeof(a));
        for (k = 0; k < (int)(sizeof fuse_fix / sizeof fuse_fix[0]); k++)
            if (!strcmp(ti.name, fuse_fix[k].name) && strlen(a) >= 4) {
                memcpy(a, fuse_fix[k].af, 4);
                fixed_count++;
            }
        if (strcmp(got1, a)) {
            ok = 0;
            if (verbose) printf("%s: regs\n  esperado %s\n  obtido   %s\n", ti.name, a, got1);
        }
        norm(te.regs2, a, sizeof(a));
        if (strcmp(got2, a)) {
            ok = 0;
            if (verbose) printf("%s: estado\n  esperado %s\n  obtido   %s\n", ti.name, a, got2);
        }

        /* diferenca de memoria, no formato do Fuse */
        for (addr = 0; addr < 65536;) {
            if (mem[addr] == mem_orig[addr]) { addr++; continue; }
            {
                char line[256];
                size_t n = (size_t)snprintf(line, sizeof(line), "%04x ", addr);
                while (addr < 65536 && mem[addr] != mem_orig[addr]) {
                    if (n + 8 < sizeof(line)) n += (size_t)snprintf(line + n, sizeof(line) - n, "%02x ", mem[addr]);
                    addr++;
                }
                snprintf(line + n, sizeof(line) - n, "-1");
                if (mi >= te.n_mem) {
                    ok = 0;
                    if (verbose) printf("%s: memoria extra: %s\n", ti.name, line);
                } else {
                    norm(te.mem_lines[mi], b2, sizeof(b2));
                    if (strcmp(line, b2)) {
                        ok = 0;
                        if (verbose) printf("%s: memoria\n  esperado %s\n  obtido   %s\n", ti.name, b2, line);
                    }
                }
                mi++;
            }
        }
        if (mi < te.n_mem) {
            ok = 0;
            if (verbose) printf("%s: faltou alteracao de memoria: %s\n", ti.name, te.mem_lines[mi]);
        }

        /* eventos */
        evmatch = (n_events == te.n_ev);
        for (k = 0; evmatch && k < n_events; k++)
            if (strcmp(events[k], te.ev[k])) evmatch = 0;
        ev_total++;
        if (evmatch) ev_ok++;
        else if (verbose) {
            int m = n_events > te.n_ev ? n_events : te.n_ev;
            printf("%s: eventos diferentes (esperado | obtido)\n", ti.name);
            for (k = 0; k < m; k++)
                printf("  %-24s | %s\n", k < te.n_ev ? te.ev[k] : "", k < n_events ? events[k] : "");
        }
        if (strict_events && !evmatch) ok = 0;

        if (ok) passed++;
        else if (fn_len + 40 < sizeof(failed_names)) {
            fn_len += (size_t)snprintf(failed_names + fn_len, sizeof(failed_names) - fn_len, " %s", ti.name);
        }
    }

    printf("Testes Z80 do Fuse: %d/%d passaram (%d com flags de bloco repetido do Z80 real)\n",
           passed, total, fixed_count);
    if (run_extras(&bus)) {
        printf("Verificacoes extras (INT/NMI/EI/HALT/prefixos): %d falha(s)\n", extra_fail);
        passed = -1;
    } else {
        printf("Verificacoes extras (INT/NMI/EI/HALT/prefixos): OK\n");
    }
    printf("Eventos de barramento (MC/MR/MW/PC/PR/PW): %d/%d iguais\n", ev_ok, ev_total);
    if (passed != total) printf("Falharam:%s\n", failed_names);
    free(tin.buf);
    free(texp.buf);
    return (passed == total && total > 0) ? 0 : 1;
}
