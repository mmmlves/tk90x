/*
 * tape_test.c - Testes do gravador de fita (src/tape.c).
 *
 * Uso: tape_test DIR_DADOS   (ex.: build/tape_test tests/data)
 *
 * 1. Arquivos reais (TAP/TZX): o sinal gerado e comparado borda a borda com
 *    um modelo de referencia independente (escrito aqui), e um decodificador
 *    no estilo do LD-BYTES da ROM le os bytes de volta a partir das duracoes
 *    dos pulsos; tudo a 3500000 Hz e a 3575611 Hz.
 * 2. TZX sinteticos em memoria para cada tipo de bloco, com instantes e
 *    niveis exatos.
 * 3. Fuzz: truncamentos, bits trocados e lixo nao podem travar nem ler fora
 *    dos limites (rode tambem o binario com -fsanitize=address,undefined).
 */
#include "tape.h"
#include "compat.h"

#include <stdarg.h>

#define HZ_STD   3500000u
#define HZ_TK    3575611u
#define FRAME_T  71136u
#define ROM_ONE_T 1710u

static int g_pass, g_fail;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    if (ok) { g_pass++; return; }
    g_fail++;
    printf("  FALHA: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Listas de bordas                                                    */
/* ------------------------------------------------------------------ */

typedef struct Edge { uint64_t t; int level; } Edge;
typedef struct EdgeList { Edge *e; size_t n, cap; } EdgeList;

static void el_push(EdgeList *l, uint64_t t, int level)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 1024;
        l->e = (Edge *)realloc(l->e, l->cap * sizeof(Edge));
        if (!l->e) { printf("sem memoria\n"); exit(2); }
    }
    l->e[l->n].t = t;
    l->e[l->n].level = level;
    l->n++;
}

static void el_free(EdgeList *l) { free(l->e); l->e = NULL; l->n = l->cap = 0; }

/* T de fita -> T de maquina (formula independente da de tape.c) */
static uint64_t mt(uint64_t cum, uint32_t hz)
{
    if (hz == HZ_STD) return cum;
    if (cum < ((uint64_t)1 << 40))
        return (cum * hz + HZ_STD / 2) / HZ_STD;
    return (uint64_t)((double)cum * hz / HZ_STD + 0.5);
}

/* Toca a fita como a maquina faz (avanca exatamente em cada borda e
 * rebaseia o relogio a cada quadro). Registra so trocas de nivel. */
static size_t run_tape(Tape *t, uint32_t start, EdgeList *out, uint64_t *stop_t, size_t max_ev)
{
    uint64_t fbase = 0;
    size_t ev = 0;
    int level = tape_level(t);
    *stop_t = 0;
    tape_play(t, start);
    while (ev < max_ev) {
        uint32_t e = tape_next_edge(t);
        int lv;
        if (e == TAPE_NO_EDGE) break;
        while (e >= FRAME_T) {
            tape_frame_end(t, FRAME_T);
            fbase += FRAME_T;
            e = tape_next_edge(t);
        }
        lv = tape_advance(t, e);
        ev++;
        if (lv != level) {
            if (out) el_push(out, fbase + e, lv);
            level = lv;
        }
        if (!tape_is_playing(t)) { *stop_t = fbase + e; break; }
    }
    return ev;
}

/* ------------------------------------------------------------------ */
/* Modelo de referencia (independente): segmentos (nivel, duracao)      */
/* ------------------------------------------------------------------ */

typedef struct Ref {
    int level_next;
    uint64_t *dur;
    uint8_t  *lvl;
    size_t    n, cap;
} Ref;

typedef struct ExpBlock {
    uint32_t pilot, sync1, sync2, zero, one, pilot_count;
    const uint8_t *data;
    uint32_t len;
    uint64_t nbits;
} ExpBlock;

typedef struct ExpList { ExpBlock b[64]; int n; } ExpList;

static void ref_seg(Ref *r, int level, uint64_t d)
{
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 4096;
        r->dur = (uint64_t *)realloc(r->dur, r->cap * sizeof(uint64_t));
        r->lvl = (uint8_t *)realloc(r->lvl, r->cap);
        if (!r->dur || !r->lvl) { printf("sem memoria\n"); exit(2); }
    }
    r->dur[r->n] = d;
    r->lvl[r->n] = (uint8_t)level;
    r->n++;
}

static void ref_pulse(Ref *r, uint64_t d)
{
    if (d) ref_seg(r, r->level_next, d);
    r->level_next = !r->level_next;
}

static void ref_pause(Ref *r, uint32_t ms)
{
    uint64_t tot = (uint64_t)ms * 3500;
    if (!ms) return;
    if (r->level_next) {
        uint64_t h = tot < 3500 ? tot : 3500;
        ref_seg(r, 1, h);
        tot -= h;
    }
    ref_seg(r, 0, tot);
    r->level_next = 0;
}

static void ref_bits(Ref *r, const uint8_t *d, uint64_t nbits, uint32_t zero, uint32_t one)
{
    uint64_t i;
    for (i = 0; i < nbits; i++) {
        int bit = (d[i >> 3] >> (7 - (i & 7))) & 1;
        ref_pulse(r, bit ? one : zero);
        ref_pulse(r, bit ? one : zero);
    }
}

static void ref_std(Ref *r, ExpList *x, uint32_t pilot, uint32_t s1, uint32_t s2, uint32_t z,
                    uint32_t o, uint32_t pc, const uint8_t *d, uint32_t len, uint64_t nbits, uint32_t pause)
{
    uint32_t i;
    for (i = 0; i < pc; i++) ref_pulse(r, pilot);
    ref_pulse(r, s1);
    ref_pulse(r, s2);
    ref_bits(r, d, nbits, z, o);
    ref_pause(r, pause);
    if (x && x->n < 64) {
        ExpBlock *b = &x->b[x->n++];
        b->pilot = pilot; b->sync1 = s1; b->sync2 = s2; b->zero = z; b->one = o;
        b->pilot_count = pc; b->data = d; b->len = len; b->nbits = nbits;
    }
}

static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t r24(const uint8_t *p) { return r16(p) | ((uint32_t)p[2] << 16); }

/* Constroi segmentos esperados para TAP ou TZX (so os blocos dos arquivos
 * de teste). Devolve 0 ok, -1 bloco nao suportado pela referencia. */
static int ref_build(const uint8_t *d, size_t len, int tzx, Ref *r, ExpList *x)
{
    size_t pos = 0;
    uint32_t tone_len = 0, tone_cnt = 0, s1 = 0, s2 = 0;
    memset(r, 0, sizeof *r);
    x->n = 0;
    if (!tzx) {
        while (pos + 2 <= len) {
            uint32_t l = r16(d + pos);
            const uint8_t *b = d + pos + 2;
            if (pos + 2 + l > len) break;
            ref_std(r, x, 2168, 667, 735, 855, 1710, (l && (b[0] & 0x80)) ? 3223 : 8063, b, l,
                    (uint64_t)l * 8, 1000);
            pos += 2 + l;
        }
    } else {
        pos = 10;
        while (pos < len) {
            uint8_t id = d[pos];
            const uint8_t *b = d + pos + 1;
            switch (id) {
            case 0x10: {
                uint32_t l = r16(b + 2);
                ref_std(r, x, 2168, 667, 735, 855, 1710, (l && (b[4] & 0x80)) ? 3223 : 8063, b + 4, l,
                        (uint64_t)l * 8, r16(b));
                pos += 1 + 4 + l;
                break;
            }
            case 0x11: {
                uint32_t l = r24(b + 0x0F), used = b[0x0C];
                uint64_t nb = l ? (uint64_t)(l - 1) * 8 + (used ? used : 8) : 0;
                ref_std(r, x, r16(b), r16(b + 2), r16(b + 4), r16(b + 6), r16(b + 8), r16(b + 10),
                        b + 0x12, l, nb, r16(b + 0x0D));
                pos += 1 + 0x12 + l;
                break;
            }
            case 0x12: {
                uint32_t i;
                tone_len = r16(b);
                tone_cnt = r16(b + 2);
                for (i = 0; i < tone_cnt; i++) ref_pulse(r, tone_len);
                pos += 1 + 4;
                break;
            }
            case 0x13: {
                uint32_t i, n = b[0];
                for (i = 0; i < n; i++) ref_pulse(r, r16(b + 1 + 2 * i));
                if (n == 2) { s1 = r16(b + 1); s2 = r16(b + 3); }
                pos += 1 + 1 + 2 * n;
                break;
            }
            case 0x14: {
                uint32_t l = r24(b + 7), used = b[4];
                uint64_t nb = l ? (uint64_t)(l - 1) * 8 + (used ? used : 8) : 0;
                ref_bits(r, b + 0x0A, nb, r16(b), r16(b + 2));
                ref_pause(r, r16(b + 5));
                if (tone_cnt && x->n < 64) {
                    ExpBlock *e = &x->b[x->n++];
                    e->pilot = tone_len; e->pilot_count = tone_cnt; e->sync1 = s1; e->sync2 = s2;
                    e->zero = r16(b); e->one = r16(b + 2); e->data = b + 0x0A; e->len = l; e->nbits = nb;
                }
                tone_cnt = 0;
                pos += 1 + 0x0A + l;
                break;
            }
            case 0x20:
                ref_pause(r, r16(b));
                pos += 3;
                break;
            case 0x21: case 0x30: pos += 2 + b[0]; break;
            case 0x22: case 0x25: case 0x27: pos += 1; break;
            case 0x31: pos += 3 + b[1]; break;
            case 0x32: pos += 3 + r16(b); break;
            case 0x33: pos += 2 + 3 * b[0]; break;
            default:
                printf("  referencia: bloco 0x%02X nao suportado\n", id);
                return -1;
            }
        }
    }
    ref_pause(r, 1);   /* pausa implicita do fim da fita */
    return 0;
}

static void ref_free(Ref *r) { free(r->dur); free(r->lvl); memset(r, 0, sizeof *r); }

/* Bordas esperadas (nivel muda) a partir dos segmentos */
static void ref_edges(const Ref *r, uint32_t start, uint32_t hz, int level0, EdgeList *out, uint64_t *stop_t)
{
    uint64_t cum = 0;
    int level = level0;
    size_t i = 0;
    while (i < r->n) {
        /* o nivel observado num instante e o do ultimo segmento que comeca nele */
        uint64_t t0 = cum;
        int lv = r->lvl[i];
        cum += r->dur[i];
        i++;
        while (r->dur[i - 1] == 0 && i < r->n) {
            lv = r->lvl[i];
            cum += r->dur[i];
            i++;
        }
        if (lv != level) {
            el_push(out, start + mt(t0, hz), lv);
            level = lv;
        }
    }
    *stop_t = start + mt(cum, hz);
}

static uint64_t ref_total(const Ref *r)
{
    uint64_t s = 0;
    size_t i;
    for (i = 0; i < r->n; i++) s += r->dur[i];
    return s;
}

static int el_equal(const EdgeList *a, const EdgeList *b, char *why, size_t wl)
{
    size_t i, n = a->n < b->n ? a->n : b->n;
    for (i = 0; i < n; i++) {
        if (a->e[i].t != b->e[i].t || a->e[i].level != b->e[i].level) {
            snprintf(why, wl, "borda %u: obtido (%llu,%d) esperado (%llu,%d)", (unsigned)i,
                     (unsigned long long)a->e[i].t, a->e[i].level,
                     (unsigned long long)b->e[i].t, b->e[i].level);
            return 0;
        }
    }
    if (a->n != b->n) {
        snprintf(why, wl, "numero de bordas: obtido %u esperado %u", (unsigned)a->n, (unsigned)b->n);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Decodificador no estilo LD-BYTES (mede pulsos entre bordas)          */
/* ------------------------------------------------------------------ */

static int approx(double v, double ref, double tol) { return v >= ref * (1 - tol) && v <= ref * (1 + tol); }

static int rom_decode(const EdgeList *el, size_t *pos, const ExpBlock *x, double s, uint8_t *out,
                      char *why, size_t wl)
{
    size_t i = *pos, nb;
    uint64_t k;
    double pil = x->pilot * s;
    /* procura uma serie de pulsos piloto seguida de sync1/sync2 */
    for (;;) {
        size_t run = 0;
        while (i + 1 < el->n && !approx((double)(el->e[i + 1].t - el->e[i].t), pil, 0.15)) i++;
        while (i + 1 < el->n && approx((double)(el->e[i + 1].t - el->e[i].t), pil, 0.15)) { run++; i++; }
        if (i + 3 >= el->n) { snprintf(why, wl, "piloto nao encontrado"); return 0; }
        if (run < 64 && run + 1 < x->pilot_count) continue;
        if (!approx((double)(el->e[i + 1].t - el->e[i].t), x->sync1 * s, 0.25) ||
            !approx((double)(el->e[i + 2].t - el->e[i + 1].t), x->sync2 * s, 0.25))
            continue;
        if (run + 1 != x->pilot_count && run != x->pilot_count) {
            snprintf(why, wl, "piloto com %u pulsos (esperado %u)", (unsigned)run, (unsigned)x->pilot_count);
            return 0;
        }
        i += 2;
        break;
    }
    /* bits: dois pulsos; soma comparada com o meio entre "0" e "1" */
    nb = (size_t)x->nbits;
    memset(out, 0, (nb + 7) / 8);
    for (k = 0; k < nb; k++) {
        double a, b;
        if (i + 2 >= el->n) { snprintf(why, wl, "sinal acabou no bit %u", (unsigned)k); return 0; }
        a = (double)(el->e[i + 1].t - el->e[i].t);
        b = (double)(el->e[i + 2].t - el->e[i + 1].t);
        if (a < 0.5 * x->zero * s || a > 1.5 * x->one * s || b < 0.5 * x->zero * s || b > 1.5 * x->one * s) {
            snprintf(why, wl, "pulso fora de faixa no bit %u (%.0f, %.0f)", (unsigned)k, a, b);
            return 0;
        }
        if (a + b > (x->zero + x->one) * s) out[k >> 3] |= (uint8_t)(0x80 >> (k & 7));
        i += 2;
    }
    *pos = i;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Arquivos reais                                                      */
/* ------------------------------------------------------------------ */

static const char *real_files[] = {
    "dogmole.tap", "int_skip.tap", "z80bltst.tap", "z80ccf.tap", "z80docflags.tap", "z80doc.tap",
    "z80flags.tap", "z80full.tap", "z80memptr.tap", "zexall_spectrum.tap", "zexdoc_spectrum.tap",
    "rotatrix_tk90x.tzx", "abu_simbel_std.tzx", "abu_simbel_turbo.tzx", "dogmole_turbo11.tzx",
    "dogmole_tone_pulse.tzx",
};

static int g_decoded_blocks, g_files_ok;

static void test_real_file(const char *dir, const char *name, uint32_t hz)
{
    char path[1024], err[256], why[160];
    size_t len = 0, pos = 0;
    uint8_t *data;
    int tzx, i, ok_all = 1;
    Tape *t;
    Ref r;
    ExpList *x;
    EdgeList got = { 0 }, exp = { 0 };
    uint64_t stop_got, stop_exp;
    uint8_t *dec;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    data = tk_read_file(path, &len, 0);
    if (!data) { printf("  (ausente: %s)\n", path); return; }
    tzx = len >= 8 && memcmp(data, "ZXTape!\x1A", 8) == 0;
    x = (ExpList *)calloc(1, sizeof *x);
    t = tape_new();
    tape_set_cpu_hz(t, hz);
    check(tape_load_file(t, path, err, sizeof err) == 0, "%s: load falhou: %s", name, err);
    check(tape_warnings(t)[0] == 0, "%s: avisos inesperados: %s", name, tape_warnings(t));
    if (ref_build(data, len, tzx, &r, x) != 0) { ok_all = 0; check(0, "%s: referencia", name); }

    {
        /* descricoes conhecidas */
        static const struct { const char *file; int idx; const char *desc; } kd[] = {
            { "dogmole.tap", 0, "Program: DOGMOLE" }, { "dogmole.tap", 2, "Bytes: screen" },
            { "dogmole.tap", 3, "Dados 6914" }, { "int_skip.tap", 2, "Bytes: int_skip" },
            { "abu_simbel_turbo.tzx", 0, "Info: Abu Simbel Profanation" },
            { "abu_simbel_turbo.tzx", 5, "Turbo 6914" }, { "rotatrix_tk90x.tzx", 9, "Bytes: TK Patch" },
            { "dogmole_turbo11.tzx", 1, "Program: DOGMOLE" },
            { "dogmole_tone_pulse.tzx", 1, "Tom puro 8063 x 2168 T" },
        };
        size_t q;
        for (q = 0; q < sizeof kd / sizeof kd[0]; q++)
            if (strcmp(kd[q].file, name) == 0)
                check(strcmp(tape_block_desc(t, kd[q].idx), kd[q].desc) == 0, "%s: desc %d = '%s' (esperado '%s')",
                      name, kd[q].idx, tape_block_desc(t, kd[q].idx), kd[q].desc);
    }
    check(tape_progress_permille(t) == 0 && tape_current_block(t) == 0, "%s: estado inicial", name);
    run_tape(t, 1000, &got, &stop_got, (size_t)-1);
    ref_edges(&r, 1000, hz, 0, &exp, &stop_exp);
    if (!el_equal(&got, &exp, why, sizeof why)) { ok_all = 0; check(0, "%s @%u Hz: %s", name, hz, why); }
    if (stop_got != stop_exp) {
        ok_all = 0;
        check(0, "%s @%u Hz: fim em %llu, esperado %llu", name, hz,
              (unsigned long long)stop_got, (unsigned long long)stop_exp);
    }
    check(tape_at_end(t) && !tape_is_playing(t), "%s: fita deveria estar no fim", name);
    check(tape_progress_permille(t) == 1000, "%s: progresso final %d", name, tape_progress_permille(t));
    {
        double est = tape_total_seconds(t), refs = (double)(ref_total(&r) - 3500) / HZ_STD;
        check(est > refs - 1e-6 && est < refs + 1e-6, "%s: total %.6f s, esperado %.6f s", name, est, refs);
    }

    /* decodificacao estilo ROM de cada bloco de dados */
    dec = (uint8_t *)malloc(70000);
    for (i = 0; i < x->n; i++) {
        const ExpBlock *b = &x->b[i];
        if (b->nbits > 70000u * 8) continue;
        if (!rom_decode(&got, &pos, b, (double)hz / HZ_STD, dec, why, sizeof why)) {
            ok_all = 0;
            check(0, "%s @%u Hz bloco %d: %s", name, hz, i, why);
            break;
        }
        if (memcmp(dec, b->data, (size_t)(b->nbits / 8)) != 0) {
            ok_all = 0;
            check(0, "%s @%u Hz bloco %d: bytes decodificados diferem", name, hz, i);
            break;
        }
        g_decoded_blocks++;
        g_pass++;
    }
    if (ok_all) g_files_ok++;
    printf("  %-24s %7u Hz: %2d blocos de dados decodificados, %8u bordas, %s\n", name, hz, x->n,
           (unsigned)got.n, ok_all ? "ok" : "FALHOU");
    free(dec);
    el_free(&got);
    el_free(&exp);
    ref_free(&r);
    tape_free(t);
    free(x);
    free(data);
}

/* Avanco "preguicoso": consulta o nivel em passos grosseiros e compara com
 * a lista de bordas exata. */
static void test_coarse(const char *dir, const char *name, uint32_t step)
{
    char path[1024];
    size_t len = 0, k = 0;
    uint8_t *data;
    Tape *t;
    EdgeList ref = { 0 };
    uint64_t stop, now = 0, fbase = 0;
    int bad = 0, level = 0, last_prog = 0, prog_bad = 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    data = tk_read_file(path, &len, 0);
    if (!data) return;
    t = tape_new();
    tape_set_cpu_hz(t, HZ_TK);
    tape_load_file(t, path, NULL, 0);
    run_tape(t, 0, &ref, &stop, (size_t)-1);
    tape_rewind(t);
    tape_play(t, 0);
    while (tape_is_playing(t)) {
        int lv, p;
        now += step;
        while (now - fbase >= FRAME_T) { tape_frame_end(t, FRAME_T); fbase += FRAME_T; }
        lv = tape_advance(t, (uint32_t)(now - fbase));
        while (k < ref.n && ref.e[k].t <= now) { level = ref.e[k].level; k++; }
        if (lv != level && !bad) {
            bad = 1;
            printf("  nivel %d em %llu, esperado %d\n", lv, (unsigned long long)now, level);
        }
        p = tape_progress_permille(t);
        if (p < last_prog) prog_bad = 1;
        last_prog = p;
    }
    check(!bad, "%s: avanco em passos de %u T diverge", name, step);
    check(!prog_bad && last_prog == 1000, "%s: progresso nao monotono ou final != 1000", name);
    check(now >= stop && now < stop + step, "%s: parou em %llu, esperado ~%llu", name,
          (unsigned long long)now, (unsigned long long)stop);
    el_free(&ref);
    tape_free(t);
    free(data);
}

/* ------------------------------------------------------------------ */
/* Construcao de TZX sinteticos                                        */
/* ------------------------------------------------------------------ */

typedef struct Buf { uint8_t d[16384]; size_t n; } Buf;

static void b8(Buf *b, uint32_t v) { if (b->n < sizeof b->d) b->d[b->n++] = (uint8_t)v; }
static void b16(Buf *b, uint32_t v) { b8(b, v); b8(b, v >> 8); }
static void b24(Buf *b, uint32_t v) { b16(b, v); b8(b, v >> 16); }
static void b32(Buf *b, uint32_t v) { b16(b, v); b16(b, v >> 16); }
static void bmem(Buf *b, const void *p, size_t n) { size_t i; for (i = 0; i < n; i++) b8(b, ((const uint8_t *)p)[i]); }
static void bhex(Buf *b, const char *hex)
{
    unsigned v;
    while (*hex) {
        while (*hex == ' ' || *hex == '\n') hex++;
        if (!*hex) break;
        if (sscanf(hex, "%2x", &v) != 1) break;
        b8(b, v);
        hex += 2;
    }
}
static void tzx_hdr(Buf *b) { b->n = 0; bmem(b, "ZXTape!\x1A", 8); b8(b, 1); b8(b, 20); }
static void blk12(Buf *b, uint32_t len, uint32_t cnt) { b8(b, 0x12); b16(b, len); b16(b, cnt); }
static void blk20(Buf *b, uint32_t ms) { b8(b, 0x20); b16(b, ms); }

typedef struct { uint64_t t; int level; } E;

/* Carrega, toca a partir de 'start' e compara com bordas esperadas
 * (instantes em T de fita, relativos a start). */
static Tape *expect_run(const char *name, const Buf *b, uint32_t hz, uint32_t start, int keep,
                        const E *ex, size_t nex, uint64_t ex_stop, int ex_end, Tape *reuse)
{
    char err[256], why[200];
    EdgeList got = { 0 }, exp = { 0 };
    uint64_t stop;
    size_t i;
    Tape *t = reuse;
    if (!t) {
        t = tape_new();
        tape_set_cpu_hz(t, hz);
        if (tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) != 0) {
            check(0, "%s: load falhou: %s", name, err);
            tape_free(t);
            return NULL;
        }
    }
    run_tape(t, start, &got, &stop, 1000000);
    for (i = 0; i < nex; i++) el_push(&exp, start + mt(ex[i].t, hz), ex[i].level);
    check(el_equal(&got, &exp, why, sizeof why), "%s @%u: %s", name, hz, why);
    check(stop == start + mt(ex_stop, hz), "%s @%u: parou em %llu, esperado %llu", name, hz,
          (unsigned long long)stop, (unsigned long long)(start + mt(ex_stop, hz)));
    check(tape_at_end(t) == ex_end, "%s: tape_at_end=%d esperado %d", name, tape_at_end(t), ex_end);
    el_free(&got);
    el_free(&exp);
    if (!keep) { tape_free(t); return NULL; }
    return t;
}

#define NE(a) (sizeof(a) / sizeof((a)[0]))

/* Fluxos zlib gerados com Python zlib (nivel 9, Z_FIXED e nivel 0)
 * a partir do RLE de csw_rle_ref(). */
/* zlib dyn: 497 bytes -> 1296 bytes RLE */
static const uint8_t zl_dyn[497] = {
    0x78, 0xDA, 0xBD, 0xCE, 0xC9, 0x15, 0x9D, 0x2A, 0x00, 0x00, 0x50, 0x10, 0x64, 0x70, 0x44, 0x10,
    0xC5, 0x01, 0x44, 0x10, 0xB0, 0xA1, 0xDF, 0xC1, 0x6F, 0x20, 0x45, 0x65, 0x9D, 0x6D, 0x96, 0x59,
    0xE6, 0xA4, 0x83, 0x54, 0x92, 0x6D, 0x52, 0xC5, 0xAB, 0xE0, 0xDE, 0x94, 0x88, 0x12, 0xC4, 0xB5,
    0xAD, 0xEB, 0xCF, 0xC2, 0x1E, 0x5E, 0xE9, 0x65, 0x7B, 0x77, 0x3F, 0xB9, 0x5A, 0xD7, 0x2E, 0xE2,
    0x6B, 0xEB, 0x62, 0xEB, 0x3D, 0x7D, 0xEF, 0x2A, 0x5C, 0x3B, 0xC4, 0xF7, 0x02, 0x7E, 0x70, 0x00,
    0xFA, 0xB0, 0x4B, 0x51, 0x8A, 0xBA, 0x4C, 0x2D, 0x87, 0xFC, 0xE8, 0x29, 0x4A, 0x9B, 0x66, 0x55,
    0x8A, 0xE8, 0x8E, 0x5C, 0x6B, 0x12, 0x4A, 0xE3, 0x61, 0x31, 0xB6, 0xAF, 0x7D, 0x5C, 0x9D, 0x9D,
    0x24, 0xF8, 0x3A, 0x00, 0x90, 0x3E, 0x68, 0xD9, 0xE5, 0x75, 0x68, 0x8B, 0xCC, 0x9D, 0x5A, 0x2A,
    0xFD, 0xB8, 0x62, 0xA0, 0xE2, 0x5A, 0xAF, 0xD4, 0xE3, 0x03, 0xE7, 0x22, 0x9E, 0x39, 0xF6, 0x4C,
    0x44, 0x2D, 0x66, 0xD7, 0x9E, 0xF9, 0x1D, 0x20, 0xF8, 0xAD, 0x00, 0xE0, 0x21, 0x32, 0x16, 0x57,
    0x61, 0x77, 0x8D, 0xF8, 0x50, 0xE6, 0x01, 0xEA, 0x61, 0x10, 0xCF, 0x9E, 0xDE, 0xA3, 0xB3, 0x1E,
    0xF5, 0x75, 0xB3, 0x8D, 0x1B, 0xD4, 0x8D, 0x09, 0x74, 0xBA, 0x9A, 0x31, 0x72, 0xF0, 0xE7, 0xC3,
    0x56, 0x50, 0x5E, 0x9A, 0x4E, 0xD8, 0xAC, 0x60, 0x19, 0xD1, 0x7B, 0x1F, 0x73, 0x60, 0x87, 0x31,
    0xD2, 0xAD, 0xCC, 0xC8, 0x1D, 0x3D, 0x1B, 0x47, 0xCD, 0x44, 0xE0, 0x30, 0xB9, 0x15, 0x3E, 0x48,
    0x1E, 0x1E, 0xFC, 0xAA, 0xFF, 0x0D, 0x47, 0x55, 0x93, 0xAD, 0x32, 0x9C, 0xED, 0x5D, 0x9E, 0x25,
    0xC1, 0x83, 0xEB, 0x9B, 0x5D, 0xEB, 0x2D, 0xF5, 0xF9, 0x58, 0xDA, 0x36, 0xE4, 0xE7, 0x64, 0xA3,
    0x42, 0x55, 0x86, 0xE4, 0x64, 0x7A, 0x53, 0xE0, 0x3B, 0xFF, 0xAC, 0xD5, 0x39, 0xF2, 0x8E, 0x29,
    0x74, 0xCE, 0xF7, 0x51, 0x5B, 0xDE, 0x97, 0x69, 0x97, 0xFA, 0x3D, 0x67, 0x38, 0xD4, 0xA3, 0xE7,
    0xF2, 0x65, 0x07, 0x22, 0x9E, 0xF7, 0x8F, 0xD0, 0xE8, 0xC6, 0xB1, 0x5D, 0x30, 0xF8, 0x22, 0x00,
    0xD8, 0xED, 0x46, 0x4B, 0x75, 0x49, 0x13, 0xEC, 0x4D, 0x5D, 0x6D, 0x8F, 0xA1, 0xBD, 0x49, 0x12,
    0x27, 0x14, 0xB3, 0x6C, 0x88, 0x6B, 0x20, 0x8D, 0xBA, 0x7F, 0x2E, 0x7C, 0x4B, 0x4A, 0xDC, 0x50,
    0xA9, 0x01, 0xFC, 0x94, 0x9F, 0xB5, 0x56, 0x95, 0x0E, 0x98, 0x63, 0x70, 0x61, 0xBB, 0x71, 0x18,
    0xAE, 0x82, 0x3A, 0x2E, 0xE1, 0xFC, 0x64, 0x16, 0xA6, 0xDC, 0xC2, 0x42, 0x57, 0x2C, 0x45, 0xB7,
    0x27, 0x3B, 0x51, 0x73, 0x58, 0xCE, 0x1B, 0xF0, 0xED, 0x9F, 0xB5, 0xCD, 0x64, 0x22, 0x68, 0xB6,
    0xD3, 0x69, 0x69, 0x63, 0xBB, 0x5E, 0xBF, 0xE5, 0xEC, 0xC8, 0x0B, 0xCF, 0x64, 0xCC, 0x35, 0xBD,
    0x4B, 0x77, 0xD2, 0xE5, 0x4D, 0x95, 0x8E, 0xF0, 0xBA, 0x42, 0x12, 0x8B, 0x01, 0xB4, 0xF9, 0xAC,
    0x35, 0x3F, 0x75, 0x1A, 0x46, 0x5E, 0x9D, 0x9E, 0xF1, 0x32, 0x97, 0x80, 0xF4, 0x58, 0xD1, 0x3B,
    0x3D, 0x18, 0xB6, 0x2C, 0x87, 0xC9, 0xE1, 0xE4, 0xD9, 0x3A, 0x62, 0x84, 0x37, 0xDA, 0x85, 0xA7,
    0x57, 0x3B, 0xF8, 0x7F, 0x04, 0x40, 0xF9, 0x5B, 0xAE, 0x6C, 0x5C, 0x32, 0xD1, 0x52, 0x4C, 0x86,
    0x61, 0x6E, 0x4A, 0xE5, 0x47, 0x83, 0xDB, 0x0A, 0xB5, 0x84, 0x8F, 0x6A, 0x5B, 0x32, 0x4D, 0xC2,
    0x1E, 0xF3, 0xDC, 0xA1, 0xF8, 0x56, 0x19, 0xFC, 0xD7, 0x7E, 0xD6, 0xFA, 0x0B, 0xD2, 0x39, 0x6E,
    0xC7,
};
/* zlib stored: 1307 bytes -> 1296 bytes RLE */
static const uint8_t zl_stored[1307] = {
    0x78, 0x01, 0x01, 0x10, 0x05, 0xEF, 0xFA, 0x25, 0x25, 0x06, 0x13, 0x10, 0x06, 0x1E, 0x0B, 0x0B,
    0x1E, 0x0D, 0x1C, 0x27, 0x08, 0x24, 0x09, 0x02, 0x15, 0x16, 0x19, 0x28, 0x1A, 0x20, 0x11, 0x1E,
    0x05, 0x15, 0x05, 0x1E, 0x23, 0x04, 0x1F, 0x19, 0x0C, 0x23, 0x0B, 0x20, 0x20, 0x07, 0x28, 0x21,
    0x02, 0x22, 0x1F, 0x1A, 0x01, 0x04, 0x21, 0x16, 0x00, 0xC0, 0x09, 0x00, 0x00, 0x0D, 0x22, 0x1A,
    0x12, 0x10, 0x27, 0x27, 0x13, 0x1F, 0x18, 0x05, 0x12, 0x0E, 0x26, 0x24, 0x15, 0x11, 0x23, 0x12,
    0x1D, 0x25, 0x14, 0x13, 0x27, 0x27, 0x10, 0x0C, 0x1B, 0x26, 0x05, 0x15, 0x06, 0x22, 0x27, 0x0A,
    0x20, 0x01, 0x27, 0x18, 0x1D, 0x0D, 0x05, 0x20, 0x23, 0x17, 0x1E, 0x1D, 0x11, 0x12, 0x00, 0xA5,
    0x0E, 0x00, 0x00, 0x25, 0x25, 0x06, 0x13, 0x10, 0x06, 0x1E, 0x0B, 0x0B, 0x1E, 0x0D, 0x1C, 0x27,
    0x08, 0x24, 0x09, 0x02, 0x15, 0x16, 0x19, 0x28, 0x1A, 0x20, 0x11, 0x1E, 0x05, 0x15, 0x05, 0x1E,
    0x23, 0x04, 0x1F, 0x19, 0x0C, 0x23, 0x0B, 0x20, 0x20, 0x07, 0x28, 0x21, 0x02, 0x22, 0x1F, 0x1A,
    0x01, 0x04, 0x21, 0x16, 0x00, 0xC0, 0x09, 0x00, 0x00, 0x0D, 0x22, 0x1A, 0x12, 0x10, 0x27, 0x27,
    0x13, 0x1F, 0x18, 0x05, 0x12, 0x0E, 0x26, 0x24, 0x15, 0x11, 0x23, 0x12, 0x1D, 0x25, 0x14, 0x13,
    0x27, 0x27, 0x10, 0x0C, 0x1B, 0x26, 0x05, 0x15, 0x06, 0x22, 0x27, 0x0A, 0x20, 0x01, 0x27, 0x18,
    0x1D, 0x0D, 0x05, 0x20, 0x23, 0x17, 0x1E, 0x1D, 0x11, 0x12, 0x00, 0xA5, 0x0E, 0x00, 0x00, 0x1D,
    0x16, 0x28, 0x1E, 0x03, 0x19, 0x23, 0x08, 0x1E, 0x1C, 0x15, 0x12, 0x13, 0x15, 0x24, 0x1E, 0x27,
    0x18, 0x01, 0x13, 0x09, 0x15, 0x15, 0x17, 0x07, 0x20, 0x04, 0x1B, 0x04, 0x26, 0x27, 0x10, 0x24,
    0x14, 0x23, 0x0D, 0x08, 0x10, 0x23, 0x15, 0x10, 0x14, 0x1E, 0x0B, 0x1C, 0x26, 0x28, 0x0E, 0x01,
    0x00, 0xD9, 0x13, 0x00, 0x00, 0x09, 0x22, 0x23, 0x08, 0x08, 0x23, 0x17, 0x10, 0x1D, 0x1A, 0x15,
    0x03, 0x09, 0x0E, 0x27, 0x14, 0x0E, 0x01, 0x15, 0x0E, 0x0E, 0x10, 0x24, 0x1A, 0x25, 0x28, 0x1B,
    0x0C, 0x1D, 0x20, 0x03, 0x0D, 0x05, 0x0A, 0x19, 0x0F, 0x19, 0x01, 0x15, 0x0A, 0x18, 0x22, 0x07,
    0x11, 0x1F, 0x0A, 0x0F, 0x23, 0x09, 0x00, 0xF7, 0x0E, 0x00, 0x00, 0x1D, 0x16, 0x28, 0x1E, 0x03,
    0x19, 0x23, 0x08, 0x1E, 0x1C, 0x15, 0x12, 0x13, 0x15, 0x24, 0x1E, 0x27, 0x18, 0x01, 0x13, 0x09,
    0x15, 0x15, 0x17, 0x07, 0x20, 0x04, 0x1B, 0x04, 0x26, 0x27, 0x10, 0x24, 0x14, 0x23, 0x0D, 0x08,
    0x10, 0x23, 0x15, 0x10, 0x14, 0x1E, 0x0B, 0x1C, 0x26, 0x28, 0x0E, 0x01, 0x00, 0xD9, 0x13, 0x00,
    0x00, 0x09, 0x22, 0x23, 0x08, 0x08, 0x23, 0x17, 0x10, 0x1D, 0x1A, 0x15, 0x03, 0x09, 0x0E, 0x27,
    0x14, 0x0E, 0x01, 0x15, 0x0E, 0x0E, 0x10, 0x24, 0x1A, 0x25, 0x28, 0x1B, 0x0C, 0x1D, 0x20, 0x03,
    0x0D, 0x05, 0x0A, 0x19, 0x0F, 0x19, 0x01, 0x15, 0x0A, 0x18, 0x22, 0x07, 0x11, 0x1F, 0x0A, 0x0F,
    0x23, 0x09, 0x00, 0xF7, 0x0E, 0x00, 0x00, 0x22, 0x13, 0x20, 0x12, 0x18, 0x0C, 0x10, 0x1D, 0x26,
    0x13, 0x01, 0x27, 0x0F, 0x03, 0x28, 0x21, 0x1B, 0x14, 0x22, 0x08, 0x1B, 0x18, 0x18, 0x12, 0x1E,
    0x17, 0x08, 0x18, 0x12, 0x1A, 0x03, 0x24, 0x19, 0x09, 0x03, 0x0A, 0x11, 0x06, 0x01, 0x0E, 0x11,
    0x1E, 0x17, 0x01, 0x24, 0x03, 0x12, 0x1B, 0x20, 0x00, 0xCA, 0x05, 0x00, 0x00, 0x1D, 0x0F, 0x13,
    0x05, 0x06, 0x19, 0x02, 0x18, 0x09, 0x08, 0x1A, 0x0C, 0x26, 0x14, 0x12, 0x06, 0x04, 0x0E, 0x1E,
    0x0D, 0x0A, 0x1A, 0x15, 0x15, 0x19, 0x25, 0x0D, 0x26, 0x1B, 0x16, 0x0B, 0x0B, 0x22, 0x26, 0x24,
    0x1C, 0x08, 0x0F, 0x13, 0x03, 0x02, 0x26, 0x01, 0x06, 0x1C, 0x08, 0x15, 0x19, 0x13, 0x00, 0xB8,
    0x09, 0x00, 0x00, 0x22, 0x13, 0x20, 0x12, 0x18, 0x0C, 0x10, 0x1D, 0x26, 0x13, 0x01, 0x27, 0x0F,
    0x03, 0x28, 0x21, 0x1B, 0x14, 0x22, 0x08, 0x1B, 0x18, 0x18, 0x12, 0x1E, 0x17, 0x08, 0x18, 0x12,
    0x1A, 0x03, 0x24, 0x19, 0x09, 0x03, 0x0A, 0x11, 0x06, 0x01, 0x0E, 0x11, 0x1E, 0x17, 0x01, 0x24,
    0x03, 0x12, 0x1B, 0x20, 0x00, 0xCA, 0x05, 0x00, 0x00, 0x1D, 0x0F, 0x13, 0x05, 0x06, 0x19, 0x02,
    0x18, 0x09, 0x08, 0x1A, 0x0C, 0x26, 0x14, 0x12, 0x06, 0x04, 0x0E, 0x1E, 0x0D, 0x0A, 0x1A, 0x15,
    0x15, 0x19, 0x25, 0x0D, 0x26, 0x1B, 0x16, 0x0B, 0x0B, 0x22, 0x26, 0x24, 0x1C, 0x08, 0x0F, 0x13,
    0x03, 0x02, 0x26, 0x01, 0x06, 0x1C, 0x08, 0x15, 0x19, 0x13, 0x00, 0xB8, 0x09, 0x00, 0x00, 0x0C,
    0x1E, 0x06, 0x28, 0x0F, 0x25, 0x22, 0x0C, 0x1E, 0x20, 0x0D, 0x23, 0x15, 0x1D, 0x09, 0x0D, 0x27,
    0x11, 0x1A, 0x12, 0x15, 0x28, 0x1C, 0x14, 0x01, 0x0E, 0x05, 0x0F, 0x20, 0x09, 0x12, 0x28, 0x08,
    0x1B, 0x03, 0x06, 0x20, 0x09, 0x0D, 0x24, 0x10, 0x15, 0x03, 0x21, 0x04, 0x23, 0x0B, 0x16, 0x04,
    0x00, 0x71, 0x10, 0x00, 0x00, 0x1A, 0x1D, 0x19, 0x07, 0x27, 0x02, 0x1F, 0x12, 0x18, 0x22, 0x1D,
    0x21, 0x07, 0x1E, 0x05, 0x1D, 0x1B, 0x0E, 0x0B, 0x21, 0x06, 0x25, 0x10, 0x1C, 0x01, 0x10, 0x14,
    0x12, 0x0A, 0x06, 0x1E, 0x0A, 0x01, 0x07, 0x23, 0x15, 0x0D, 0x24, 0x1F, 0x04, 0x21, 0x12, 0x07,
    0x06, 0x1E, 0x0E, 0x02, 0x13, 0x0E, 0x00, 0xC8, 0x12, 0x00, 0x00, 0x0C, 0x1E, 0x06, 0x28, 0x0F,
    0x25, 0x22, 0x0C, 0x1E, 0x20, 0x0D, 0x23, 0x15, 0x1D, 0x09, 0x0D, 0x27, 0x11, 0x1A, 0x12, 0x15,
    0x28, 0x1C, 0x14, 0x01, 0x0E, 0x05, 0x0F, 0x20, 0x09, 0x12, 0x28, 0x08, 0x1B, 0x03, 0x06, 0x20,
    0x09, 0x0D, 0x24, 0x10, 0x15, 0x03, 0x21, 0x04, 0x23, 0x0B, 0x16, 0x04, 0x00, 0x71, 0x10, 0x00,
    0x00, 0x1A, 0x1D, 0x19, 0x07, 0x27, 0x02, 0x1F, 0x12, 0x18, 0x22, 0x1D, 0x21, 0x07, 0x1E, 0x05,
    0x1D, 0x1B, 0x0E, 0x0B, 0x21, 0x06, 0x25, 0x10, 0x1C, 0x01, 0x10, 0x14, 0x12, 0x0A, 0x06, 0x1E,
    0x0A, 0x01, 0x07, 0x23, 0x15, 0x0D, 0x24, 0x1F, 0x04, 0x21, 0x12, 0x07, 0x06, 0x1E, 0x0E, 0x02,
    0x13, 0x0E, 0x00, 0xC8, 0x12, 0x00, 0x00, 0x17, 0x13, 0x25, 0x1B, 0x01, 0x26, 0x23, 0x22, 0x1E,
    0x22, 0x19, 0x21, 0x04, 0x22, 0x0E, 0x1F, 0x27, 0x03, 0x0C, 0x09, 0x12, 0x01, 0x14, 0x24, 0x26,
    0x08, 0x22, 0x11, 0x26, 0x0B, 0x01, 0x27, 0x07, 0x17, 0x04, 0x12, 0x10, 0x0C, 0x1A, 0x25, 0x1D,
    0x11, 0x07, 0x18, 0x1B, 0x1D, 0x09, 0x09, 0x0A, 0x00, 0xAF, 0x10, 0x00, 0x00, 0x19, 0x14, 0x06,
    0x11, 0x06, 0x03, 0x14, 0x1D, 0x11, 0x1C, 0x1D, 0x07, 0x0A, 0x1D, 0x0C, 0x0D, 0x15, 0x28, 0x27,
    0x1C, 0x0C, 0x06, 0x28, 0x01, 0x1C, 0x25, 0x18, 0x18, 0x1F, 0x11, 0x28, 0x16, 0x0C, 0x1C, 0x07,
    0x16, 0x28, 0x25, 0x02, 0x15, 0x23, 0x01, 0x1F, 0x1F, 0x22, 0x25, 0x10, 0x16, 0x18, 0x00, 0x07,
    0x0A, 0x00, 0x00, 0x17, 0x13, 0x25, 0x1B, 0x01, 0x26, 0x23, 0x22, 0x1E, 0x22, 0x19, 0x21, 0x04,
    0x22, 0x0E, 0x1F, 0x27, 0x03, 0x0C, 0x09, 0x12, 0x01, 0x14, 0x24, 0x26, 0x08, 0x22, 0x11, 0x26,
    0x0B, 0x01, 0x27, 0x07, 0x17, 0x04, 0x12, 0x10, 0x0C, 0x1A, 0x25, 0x1D, 0x11, 0x07, 0x18, 0x1B,
    0x1D, 0x09, 0x09, 0x0A, 0x00, 0xAF, 0x10, 0x00, 0x00, 0x19, 0x14, 0x06, 0x11, 0x06, 0x03, 0x14,
    0x1D, 0x11, 0x1C, 0x1D, 0x07, 0x0A, 0x1D, 0x0C, 0x0D, 0x15, 0x28, 0x27, 0x1C, 0x0C, 0x06, 0x28,
    0x01, 0x1C, 0x25, 0x18, 0x18, 0x1F, 0x11, 0x28, 0x16, 0x0C, 0x1C, 0x07, 0x16, 0x28, 0x25, 0x02,
    0x15, 0x23, 0x01, 0x1F, 0x1F, 0x22, 0x25, 0x10, 0x16, 0x18, 0x00, 0x07, 0x0A, 0x00, 0x00, 0x14,
    0x24, 0x05, 0x25, 0x0E, 0x0F, 0x09, 0x02, 0x1C, 0x20, 0x08, 0x09, 0x27, 0x14, 0x27, 0x22, 0x03,
    0x15, 0x0F, 0x02, 0x07, 0x21, 0x25, 0x24, 0x04, 0x01, 0x0B, 0x08, 0x26, 0x22, 0x11, 0x1E, 0x04,
    0x25, 0x20, 0x08, 0x17, 0x0F, 0x04, 0x03, 0x04, 0x19, 0x07, 0x0C, 0x22, 0x24, 0x0D, 0x13, 0x1A,
    0x00, 0x64, 0x0F, 0x00, 0x00, 0x13, 0x20, 0x21, 0x12, 0x17, 0x08, 0x0F, 0x16, 0x26, 0x06, 0x15,
    0x12, 0x10, 0x11, 0x18, 0x08, 0x04, 0x09, 0x18, 0x27, 0x02, 0x20, 0x0F, 0x18, 0x04, 0x0B, 0x02,
    0x03, 0x0B, 0x06, 0x09, 0x0F, 0x13, 0x19, 0x16, 0x26, 0x07, 0x25, 0x10, 0x1D, 0x1B, 0x14, 0x14,
    0x0C, 0x03, 0x23, 0x28, 0x02, 0x26, 0x00, 0x55, 0x0B, 0x00, 0x00, 0x14, 0x24, 0x05, 0x25, 0x0E,
    0x0F, 0x09, 0x02, 0x1C, 0x20, 0x08, 0x09, 0x27, 0x14, 0x27, 0x22, 0x03, 0x15, 0x0F, 0x02, 0x07,
    0x21, 0x25, 0x24, 0x04, 0x01, 0x0B, 0x08, 0x26, 0x22, 0x11, 0x1E, 0x04, 0x25, 0x20, 0x08, 0x17,
    0x0F, 0x04, 0x03, 0x04, 0x19, 0x07, 0x0C, 0x22, 0x24, 0x0D, 0x13, 0x1A, 0x00, 0x64, 0x0F, 0x00,
    0x00, 0x13, 0x20, 0x21, 0x12, 0x17, 0x08, 0x0F, 0x16, 0x26, 0x06, 0x15, 0x12, 0x10, 0x11, 0x18,
    0x08, 0x04, 0x09, 0x18, 0x27, 0x02, 0x20, 0x0F, 0x18, 0x04, 0x0B, 0x02, 0x03, 0x0B, 0x06, 0x09,
    0x0F, 0x13, 0x19, 0x16, 0x26, 0x07, 0x25, 0x10, 0x1D, 0x1B, 0x14, 0x14, 0x0C, 0x03, 0x23, 0x28,
    0x02, 0x26, 0x00, 0x55, 0x0B, 0x00, 0x00, 0xD2, 0x39, 0x6E, 0xC7,
};
/* zlib fixed: 661 bytes -> 1296 bytes RLE */
static const uint8_t zl_fixed[661] = {
    0x78, 0x01, 0x53, 0x55, 0x65, 0x13, 0x16, 0x60, 0x93, 0xE3, 0xE6, 0x96, 0xE3, 0x95, 0x51, 0xE7,
    0x50, 0xE1, 0x64, 0x12, 0x15, 0x93, 0xD4, 0x90, 0x52, 0x10, 0x94, 0x63, 0x15, 0x65, 0x95, 0x53,
    0x66, 0x91, 0x97, 0xE4, 0x51, 0xE6, 0x56, 0x50, 0x60, 0xD7, 0x50, 0x64, 0x52, 0x92, 0x97, 0x62,
    0x64, 0x51, 0x14, 0x63, 0x38, 0xC0, 0xC9, 0xC0, 0xC0, 0xAB, 0x24, 0x25, 0x24, 0xA0, 0xAE, 0x2E,
    0x2C, 0x2F, 0xC1, 0x2A, 0xC4, 0xA7, 0xA6, 0x22, 0x2A, 0xA8, 0x2C, 0x24, 0xAB, 0x2A, 0x22, 0xAC,
    0xAE, 0x2E, 0xC0, 0x23, 0xAD, 0xC6, 0x2A, 0xCA, 0xA6, 0xA4, 0xCE, 0xA5, 0xC0, 0xA8, 0x2E, 0x21,
    0xCB, 0xCB, 0xAA, 0xA0, 0x2C, 0x2E, 0x27, 0x2B, 0x28, 0xC4, 0xB0, 0x94, 0x8F, 0x81, 0x41, 0x95,
    0x8E, 0x76, 0xC9, 0x8A, 0x69, 0xC8, 0x31, 0x4B, 0x2A, 0x73, 0xC8, 0xC9, 0x88, 0x0A, 0x09, 0x8B,
    0xAA, 0xC8, 0xA9, 0x4B, 0x30, 0x0A, 0x73, 0x8A, 0x8A, 0x8A, 0xB3, 0x2B, 0xB0, 0x48, 0xB3, 0xA8,
    0xA9, 0x0B, 0xA8, 0x88, 0x28, 0xF3, 0x72, 0x08, 0x28, 0x8B, 0x0A, 0x88, 0xC8, 0x71, 0xCB, 0xA8,
    0x69, 0xF0, 0x31, 0x32, 0xDC, 0x14, 0x66, 0x60, 0xE0, 0x54, 0x52, 0xE6, 0xE0, 0x50, 0x16, 0x17,
    0x90, 0x95, 0x12, 0x65, 0xE6, 0xE4, 0x53, 0x17, 0xE1, 0x63, 0x14, 0xE5, 0xE3, 0x13, 0x50, 0x91,
    0x52, 0xD5, 0x90, 0xE6, 0x91, 0x55, 0x60, 0xE6, 0x65, 0xE5, 0x92, 0xE4, 0x97, 0x64, 0x14, 0xE5,
    0x92, 0x50, 0x62, 0x17, 0x94, 0xE7, 0xE2, 0x57, 0xE6, 0x64, 0xF8, 0x4E, 0x67, 0xBB, 0x94, 0x84,
    0x15, 0x84, 0x24, 0x78, 0x04, 0x64, 0xD5, 0x84, 0x19, 0xD5, 0xF9, 0x99, 0x35, 0x14, 0xA5, 0x45,
    0x94, 0x38, 0xA4, 0x25, 0x24, 0x84, 0xE4, 0xC4, 0x39, 0x24, 0x84, 0xA4, 0x98, 0x55, 0x24, 0x39,
    0x99, 0xB9, 0x04, 0xD9, 0x18, 0xF9, 0x04, 0xE5, 0xC4, 0x19, 0x55, 0x98, 0x85, 0xA4, 0x15, 0x18,
    0x4E, 0xB1, 0x02, 0x5D, 0xC8, 0x2F, 0xCC, 0xCA, 0x26, 0xC9, 0x24, 0xC1, 0xC9, 0x21, 0xC5, 0xA3,
    0x26, 0x22, 0xC4, 0xC6, 0xC2, 0x27, 0xC7, 0xCB, 0x25, 0x25, 0x2A, 0x2A, 0xA9, 0xCA, 0xAB, 0x26,
    0x2D, 0xC6, 0xCD, 0xAD, 0xA4, 0xA6, 0x22, 0xC3, 0xC1, 0x2F, 0xCC, 0xCC, 0xA4, 0xC6, 0xC8, 0x26,
    0xC3, 0x21, 0x2A, 0x29, 0xCC, 0xB0, 0x83, 0x93, 0xBE, 0x76, 0xF1, 0xC8, 0xB1, 0x69, 0xF0, 0xAB,
    0x2A, 0xF1, 0xC8, 0x29, 0xF0, 0x2A, 0x8B, 0xCA, 0x72, 0xF2, 0xAA, 0x0B, 0x4A, 0x09, 0x89, 0x6A,
    0xC8, 0x88, 0x30, 0xF2, 0xB1, 0xF2, 0x2B, 0x70, 0x0A, 0x69, 0x70, 0x48, 0x33, 0xB3, 0x29, 0x70,
    0xF2, 0xAA, 0x08, 0x88, 0x32, 0x2B, 0xB2, 0x28, 0x73, 0x8B, 0xB1, 0x30, 0x14, 0x0A, 0x30, 0x30,
    0x48, 0xC9, 0x4A, 0xB2, 0xAB, 0x33, 0xC9, 0x0B, 0x49, 0x28, 0xC9, 0x2A, 0xB2, 0xCB, 0xB1, 0xCA,
    0x4A, 0xF3, 0x71, 0x2B, 0xB2, 0xA9, 0x0A, 0xC8, 0x30, 0x0A, 0x88, 0x08, 0x71, 0xB1, 0xC9, 0x71,
    0x31, 0xB2, 0x2B, 0x8B, 0xF2, 0xAA, 0xC8, 0xB3, 0x28, 0x0A, 0xB1, 0xB3, 0xC9, 0xF1, 0x31, 0x09,
    0xF3, 0x31, 0x9C, 0x10, 0xA2, 0xAF, 0x5D, 0xE2, 0xC2, 0xAA, 0xD2, 0x8C, 0x6A, 0xCA, 0x4A, 0x72,
    0x4A, 0x92, 0x8A, 0x2C, 0x4A, 0x7C, 0xF2, 0xEA, 0xCC, 0x3C, 0x9C, 0x42, 0x8C, 0x22, 0x2A, 0x6A,
    0x1C, 0x4A, 0x82, 0x6A, 0xDC, 0x8C, 0xEA, 0xEC, 0xE2, 0x2C, 0x42, 0x02, 0x3C, 0x52, 0xAA, 0xB2,
    0x82, 0xEC, 0x12, 0xD2, 0xB2, 0x9C, 0x9C, 0x5C, 0x0C, 0xEB, 0x81, 0x76, 0x49, 0x8A, 0xB0, 0x09,
    0xB2, 0x31, 0x8B, 0xC8, 0x0A, 0xCA, 0xC8, 0xB2, 0x73, 0xC9, 0xF2, 0xF0, 0x8A, 0x6A, 0xA8, 0xCB,
    0xF0, 0xB0, 0x69, 0x30, 0xCA, 0xA8, 0x4A, 0x48, 0xC8, 0x0B, 0x6A, 0x88, 0xF1, 0xC8, 0xB0, 0x8B,
    0x69, 0xA8, 0x32, 0x89, 0x2A, 0x33, 0xCA, 0xCB, 0x2B, 0xA9, 0x0A, 0x88, 0x49, 0x30, 0xB0, 0x73,
    0xD1, 0xD7, 0x2E, 0x11, 0x15, 0x56, 0x55, 0x3E, 0x7E, 0x4E, 0x26, 0x19, 0x05, 0x0E, 0x4E, 0x75,
    0x11, 0x75, 0x25, 0x66, 0x51, 0x7E, 0x26, 0x76, 0x45, 0x55, 0x15, 0x16, 0x46, 0x6E, 0x0E, 0x35,
    0x25, 0x41, 0x39, 0x16, 0x55, 0x05, 0x0E, 0x71, 0x7E, 0x16, 0x66, 0x16, 0x49, 0x76, 0x1E, 0x25,
    0x15, 0x5E, 0x61, 0x29, 0x86, 0x14, 0x7E, 0x06, 0x06, 0x61, 0x05, 0x45, 0x21, 0x71, 0x0E, 0x7E,
    0x31, 0x35, 0x36, 0x51, 0x21, 0x01, 0x41, 0x09, 0x0E, 0x16, 0x4E, 0x09, 0x75, 0x26, 0x05, 0x7E,
    0x09, 0x16, 0x6E, 0x26, 0x66, 0x6E, 0x36, 0x4E, 0x7E, 0x61, 0x49, 0x31, 0x35, 0x76, 0x55, 0x01,
    0x59, 0x69, 0x11, 0x11, 0x1E, 0x66, 0x65, 0x0D, 0x26, 0x35, 0x86, 0x50, 0x6E, 0xFA, 0xDA, 0x05,
    0x00, 0xD2, 0x39, 0x6E, 0xC7,
};

/* Mesmo gerador do script que produziu os fluxos zlib acima */
static size_t csw_rle_ref(uint8_t *out)
{
    uint32_t x = 12345, p[1200];
    size_t n = 0;
    int i;
    for (i = 0; i < 1200; i++) {
        x = x * 1103515245u + 12345u;
        if ((i / 100) % 2 == 1) p[i] = p[i - 100];
        else if (i % 50 == 49) p[i] = 1000 + ((x >> 8) & 0xFFF);
        else p[i] = 1 + ((x >> 16) % 40);
    }
    for (i = 0; i < 1200; i++) {
        if (p[i] > 255) {
            out[n++] = 0;
            out[n++] = (uint8_t)p[i]; out[n++] = (uint8_t)(p[i] >> 8);
            out[n++] = (uint8_t)(p[i] >> 16); out[n++] = (uint8_t)(p[i] >> 24);
        } else {
            out[n++] = (uint8_t)p[i];
        }
    }
    return n;
}

static void blk18(Buf *b, uint32_t rate, int comp, uint32_t npulses, const uint8_t *d, size_t n, uint32_t pause)
{
    b8(b, 0x18);
    b32(b, (uint32_t)(10 + n));
    b16(b, pause);
    b24(b, rate);
    b8(b, (uint32_t)comp);
    b32(b, npulses);
    bmem(b, d, n);
}

static void test_synthetic(void)
{
    static const uint32_t hzs[2] = { HZ_STD, HZ_TK };
    Buf *b = (Buf *)calloc(1, sizeof(Buf));
    Buf *b2 = (Buf *)calloc(1, sizeof(Buf));
    int h;
    char err[256];

    for (h = 0; h < 2; h++) {
        uint32_t hz = hzs[h], st = h ? 1234 : 0;

        /* A: 0x12 tom + 0x13 sequencia; fim com pausa implicita de 1 ms */
        {
            static const E ex[] = { {1000,1},{2000,0},{3000,1},{3500,0},{4200,1},{7700,0} };
            tzx_hdr(b); blk12(b, 1000, 3);
            b8(b, 0x13); b8(b, 2); b16(b, 500); b16(b, 700);
            expect_run("0x12+0x13", b, hz, st, 0, ex, NE(ex), 7700, 1, NULL);
        }
        /* B: 0x14 dados puros, 3 bits uteis, pausa 2 ms */
        {
            static const E ex[] = { {1000,1},{2000,0},{2500,1},{3000,0},{4000,1},{5000,0} };
            tzx_hdr(b);
            b8(b, 0x14); b16(b, 500); b16(b, 1000); b8(b, 3); b16(b, 2); b24(b, 1); b8(b, 0xA0);
            expect_run("0x14", b, hz, st, 0, ex, NE(ex), 15500, 1, NULL);
        }
        /* C: 0x15 gravacao direta (exemplo do formats.md) */
        {
            static const E ex[] = { {0,1},{316,0},{948,1},{4764,0} };
            tzx_hdr(b);
            b8(b, 0x15); b16(b, 79); b16(b, 0); b8(b, 8); b24(b, 2); b8(b, 0xF0); b8(b, 0x0F);
            expect_run("0x15", b, hz, st, 0, ex, NE(ex), 4764, 1, NULL);
        }
        /* D: 0x18 CSW RLE (vetor do formats.md) */
        {
            static const E ex[] = { {794,1},{1587,0} };
            tzx_hdr(b);
            bhex(b, "18 11 00 00 00  00 00  44 AC 00  01  03 00 00 00  0A 0A 00 2C 01 00 00");
            expect_run("0x18 RLE", b, hz, st, 0, ex, NE(ex), 25397 + 3500, 1, NULL);
        }
        /* E: 0x19 GDB com bandeiras de polaridade */
        {
            static const E ex[] = { {1000,1},{2800,0},{3000,1},{6500,0} };
            tzx_hdr(b);
            b8(b, 0x19);
            b32(b, 14 + 4 * 3 + 4 * 3);
            b16(b, 0); b32(b, 4); b8(b, 1); b8(b, 4); b32(b, 0); b8(b, 0); b8(b, 0);
            b8(b, 0); b16(b, 1000);   /* s0: borda */
            b8(b, 1); b16(b, 500);    /* s1: mesmo nivel */
            b8(b, 3); b16(b, 300);    /* s2: forca alto */
            b8(b, 2); b16(b, 200);    /* s3: forca baixo */
            b8(b, 0); b16(b, 2); b8(b, 1); b16(b, 1); b8(b, 2); b16(b, 1); b8(b, 3); b16(b, 1);
            expect_run("0x19 polaridade", b, hz, st, 0, ex, NE(ex), 6500, 1, NULL);
        }
        /* F: 0x19 GDB dados com alfabeto de 3 simbolos (2 bits/simbolo) */
        {
            static const E ex[] = { {300,1},{400,0},{500,1},{700,0},{900,1},{1200,0},{1400,1},{1600,0} };
            tzx_hdr(b);
            b8(b, 0x19);
            b32(b, 14 + 3 * 5 + 2);
            b16(b, 0); b32(b, 0); b8(b, 0); b8(b, 0); b32(b, 5); b8(b, 2); b8(b, 3);
            b8(b, 0); b16(b, 100); b16(b, 100);
            b8(b, 0); b16(b, 200); b16(b, 200);
            b8(b, 0); b16(b, 300); b16(b, 0);
            b8(b, 0x86); b8(b, 0x40);   /* 2,0,1,2,1 */
            expect_run("0x19 dados", b, hz, st, 0, ex, NE(ex), 5100, 1, NULL);
        }
        /* G: 0x20 com 0 ms para a fita; o ultimo pulso (alto, pausa 0) ainda
         * aberto ganha 1 ms baixo que o encerra antes da parada; play continua
         * no bloco seguinte */
        {
            static const E ex1[] = { {1000,1},{2000,0} };
            static const E ex2[] = { {1000,1},{2000,0} };
            Tape *t;
            tzx_hdr(b); blk12(b, 1000, 2); blk20(b, 0); blk12(b, 1000, 2);
            t = expect_run("0x20 stop (1)", b, hz, st, 1, ex1, NE(ex1), 5500, 0, NULL);
            if (t) {
                check(!tape_is_playing(t) && tape_level(t) == 0, "0x20 stop: deveria parar em nivel baixo");
                check(tape_current_block(t) == 2, "0x20 stop: bloco atual %d (esperado 2)", tape_current_block(t));
                check(tape_block_finished(t, 0), "0x20 stop: bloco 0 deveria estar terminado");
                expect_run("0x20 stop (2)", b, hz, 5000, 1, ex2, NE(ex2), 5500, 1, t);
                tape_free(t);
            }
        }
        /* G2: 0x20 com pausa > 0 */
        {
            static const E ex[] = { {1000,1},{4500,0},{19500,1},{23000,0} };
            tzx_hdr(b); blk12(b, 1000, 1); blk20(b, 5); blk12(b, 1000, 1);
            expect_run("0x20 pausa", b, hz, st, 0, ex, NE(ex), 23000, 1, NULL);
        }
        /* H: laco 0x24/0x25 */
        {
            static const E ex[] = { {500,1},{1000,0},{1500,1},{5000,0} };
            tzx_hdr(b); b8(b, 0x24); b16(b, 3); blk12(b, 500, 1); b8(b, 0x25);
            expect_run("0x24/0x25", b, hz, st, 0, ex, NE(ex), 5000, 1, NULL);
        }
        /* I: 0x2B nivel alto antes do tom; depois nivel baixo */
        {
            static const E ex[] = { {0,1},{1000,0},{2000,1},{5500,0} };
            static const E ex2[] = { {1000,1},{2000,0},{3000,1},{6500,0} };
            tzx_hdr(b); b8(b, 0x2B); b32(b, 1); b8(b, 1); blk12(b, 1000, 2);
            expect_run("0x2B alto", b, hz, st, 0, ex, NE(ex), 5500, 1, NULL);
            tzx_hdr(b); blk12(b, 1000, 2); b8(b, 0x2B); b32(b, 1); b8(b, 0); blk12(b, 1000, 1);
            expect_run("0x2B baixo", b, hz, st, 0, ex2, NE(ex2), 6500, 1, NULL);
        }
        /* J: 0x2A para a fita (maquina 48K), depois de 1 ms alto que encerra o
         * ultimo pulso (baixo) */
        {
            static const E ex[] = { {1000,1},{4500,0} };
            Tape *t;
            tzx_hdr(b); blk12(b, 1000, 1); b8(b, 0x2A); b32(b, 0); blk12(b, 1000, 1);
            t = expect_run("0x2A", b, hz, st, 1, ex, NE(ex), 4500, 0, NULL);
            if (t) {
                check(tape_current_block(t) == 2, "0x2A: bloco atual %d", tape_current_block(t));
                tape_free(t);
            }
        }
        /* K: bloco desconhecido e informativo pulados */
        {
            static const E ex[] = { {1000,1},{2000,0},{3000,1},{4000,0} };
            Tape *t;
            tzx_hdr(b); blk12(b, 1000, 2);
            b8(b, 0x4B); b32(b, 5); bmem(b, "ABCDE", 5);
            b8(b, 0x30); b8(b, 2); bmem(b, "Oi", 2);
            blk12(b, 1000, 2);
            t = expect_run("desconhecido", b, hz, st, 1, ex, NE(ex), 7500, 1, NULL);
            if (t) {
                check(tape_block_count(t) == 4, "desconhecido: %d blocos", tape_block_count(t));
                check(strstr(tape_block_desc(t, 1), "desconhecido") != NULL, "desc: '%s'", tape_block_desc(t, 1));
                check(strcmp(tape_block_desc(t, 2), "Texto: Oi") == 0, "desc: '%s'", tape_block_desc(t, 2));
                check(strstr(tape_warnings(t), "0x4B") != NULL, "aviso: '%s'", tape_warnings(t));
                tape_free(t);
            }
        }
        /* L: salto 0x23 */
        {
            static const E ex[] = { {1000,1},{2000,0} };
            tzx_hdr(b); blk12(b, 1000, 1); b8(b, 0x23); b16(b, 2); blk12(b, 7000, 1); blk12(b, 1000, 1);
            expect_run("0x23", b, hz, st, 0, ex, NE(ex), 5500, 1, NULL);
        }
        /* M: chamada 0x26 / retorno 0x27 (o 0x20 final encerra o pulso aberto) */
        {
            static const E ex[] = { {500,1},{1000,0},{2000,1},{5500,0} };
            Tape *t;
            tzx_hdr(b);
            b8(b, 0x26); b16(b, 2); b16(b, 3); b16(b, 3);
            blk12(b, 1000, 1);
            blk20(b, 0);
            blk12(b, 500, 1);
            b8(b, 0x27);
            t = expect_run("0x26/0x27", b, hz, st, 1, ex, NE(ex), 5500, 0, NULL);
            if (t) {
                check(tape_current_block(t) == 3, "0x26: bloco atual %d", tape_current_block(t));
                tape_free(t);
            }
        }
        /* N: 0x20 (0 ms) e 0x2A logo apos 1 pulso fechado por pausa: para na hora */
        {
            static const E ex[] = { {1000,1},{4500,0} };
            tzx_hdr(b); blk12(b, 1000, 1); blk20(b, 1); blk20(b, 0); blk12(b, 1000, 1);
            expect_run("0x20 apos pausa", b, hz, st, 0, ex, NE(ex), 4500, 0, NULL);
        }
        /* O: salto 0x23 para fora de um laco: o laco acaba; o laco seguinte roda
         * as suas 3 vezes (antes: 1000 700 1000 700) */
        {
            static const E ex[] = { {1000,1},{1700,0},{2400,1},{3100,0} };
            tzx_hdr(b);
            b8(b, 0x24); b16(b, 2); blk12(b, 1000, 1); b8(b, 0x23); b16(b, 2); b8(b, 0x25);
            b8(b, 0x24); b16(b, 3); blk12(b, 700, 1); b8(b, 0x25);
            expect_run("0x23 fora do laco", b, hz, st, 0, ex, NE(ex), 3100 + 3500, 1, NULL);
            /* salto dentro do proprio laco continua no laco: 2 x (500) */
            {
                static const E ex2[] = { {500,1},{1000,0} };
                tzx_hdr(b);
                b8(b, 0x24); b16(b, 2); b8(b, 0x23); b16(b, 2); blk12(b, 9999, 1); blk12(b, 500, 1);
                b8(b, 0x25);
                expect_run("0x23 dentro do laco", b, hz, st, 0, ex2, NE(ex2), 4500, 1, NULL);
            }
        }
    }

    /* 0x10 (TZX) e GDB equivalente (exemplo do formato) geram o mesmo trem */
    {
        static const char *hdr19 = "00 03 4A 50 53 50 20 20 20 20 20 20 00 1B 00 40 00 80 C1";
        EdgeList e1 = { 0 }, e2 = { 0 };
        uint64_t s1, s2;
        char why[200];
        Tape *t = tape_new();
        tzx_hdr(b); bhex(b, "10 E8 03 13 00"); bhex(b, hdr19);
        tzx_hdr(b2);
        bhex(b2, "19 3B 00 00 00 E8 03 02 00 00 00 02 02 98 00 00 00 02 02"
                 "00 78 08 00 00 00 9B 02 DF 02 00 7F 1F 01 01 00"
                 "00 57 03 57 03 00 AE 06 AE 06");
        bhex(b2, hdr19);
        check(b2->n == 10 + 64, "GDB exemplo: %u bytes", (unsigned)b2->n);
        tape_set_cpu_hz(t, HZ_TK);
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "0x10: %s", err);
        run_tape(t, 0, &e1, &s1, (size_t)-1);
        check(strcmp(tape_block_desc(t, 0), "Bytes: JPSP") == 0, "desc 0x10: '%s'", tape_block_desc(t, 0));
        check(tape_load_tzx_mem(t, b2->d, b2->n, err, sizeof err) == 0, "0x19: %s", err);
        check(tape_warnings(t)[0] == 0, "0x19: aviso '%s'", tape_warnings(t));
        run_tape(t, 0, &e2, &s2, (size_t)-1);
        check(e1.n > 8000 && el_equal(&e1, &e2, why, sizeof why) && s1 == s2, "GDB != 0x10: %s", why);
        el_free(&e1);
        el_free(&e2);
        tape_free(t);
    }

    /* CSW Z-RLE (dinamico, fixo, armazenado) == CSW RLE */
    {
        static uint8_t rle[8000];
        size_t n = csw_rle_ref(rle);
        EdgeList e0 = { 0 }, e1 = { 0 };
        uint64_t s0, s1;
        char why[200];
        const uint8_t *zs[3] = { zl_dyn, zl_fixed, zl_stored };
        size_t zn[3] = { sizeof zl_dyn, sizeof zl_fixed, sizeof zl_stored };
        int z;
        Tape *t = tape_new();
        tape_set_cpu_hz(t, HZ_TK);
        tzx_hdr(b); blk18(b, 22050, 1, 1200, rle, n, 100);
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "CSW RLE: %s", err);
        check(strcmp(tape_block_desc(t, 0), "CSW 1200 pulsos") == 0, "desc CSW: '%s'", tape_block_desc(t, 0));
        run_tape(t, 0, &e0, &s0, (size_t)-1);
        check(e0.n > 1000, "CSW RLE: %u bordas", (unsigned)e0.n);
        for (z = 0; z < 3; z++) {
            tzx_hdr(b); blk18(b, 22050, 2, 1200, zs[z], zn[z], 100);
            check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "CSW Z-RLE %d: %s", z, err);
            check(tape_warnings(t)[0] == 0, "CSW Z-RLE %d: aviso '%s'", z, tape_warnings(t));
            el_free(&e1);
            run_tape(t, 0, &e1, &s1, (size_t)-1);
            check(el_equal(&e0, &e1, why, sizeof why) && s0 == s1, "CSW Z-RLE %d difere: %s", z, why);
        }
        /* Z-RLE corrompido: bloco ignorado com aviso, sem erro fatal */
        {
            uint8_t bad[sizeof zl_dyn];
            memcpy(bad, zl_dyn, sizeof bad);
            bad[100] ^= 0x55;
            bad[sizeof bad - 1] ^= 0xFF;
            tzx_hdr(b); blk18(b, 22050, 2, 1200, bad, sizeof bad, 100); blk12(b, 1000, 1);
            check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "CSW Z-RLE ruim: %s", err);
            check(strstr(tape_warnings(t), "CSW") != NULL, "CSW Z-RLE ruim: sem aviso");
            check(strstr(tape_block_desc(t, 0), "invalido") != NULL, "desc '%s'", tape_block_desc(t, 0));
        }
        el_free(&e0);
        el_free(&e1);
        tape_free(t);
    }

    /* Escala de clock com acumulador: 20000 pulsos de 855 T sem deriva */
    {
        EdgeList e = { 0 };
        uint64_t s;
        size_t i;
        int ok = 1;
        Tape *t = tape_new();
        tape_set_cpu_hz(t, HZ_TK);
        tzx_hdr(b); blk12(b, 855, 20000);
        tape_load_tzx_mem(t, b->d, b->n, err, sizeof err);
        run_tape(t, 777, &e, &s, (size_t)-1);
        for (i = 0; i < e.n && i < 19999; i++)
            if (e.e[i].t != 777 + ((uint64_t)855 * (i + 1) * HZ_TK + HZ_STD / 2) / HZ_STD) ok = 0;
        check(ok && e.n == 20000, "escala: deriva no acumulador (%u bordas)", (unsigned)e.n);
        el_free(&e);
        tape_free(t);
    }

    /* tape_block_finished: exatamente no fim do ultimo pulso */
    {
        Tape *t = tape_new();
        tzx_hdr(b);
        b8(b, 0x14); b16(b, 500); b16(b, 1000); b8(b, 3); b16(b, 2); b24(b, 1); b8(b, 0xA0);
        blk12(b, 1000, 1);
        tape_load_tzx_mem(t, b->d, b->n, err, sizeof err);
        tape_play(t, 0);
        tape_advance(t, 4999);
        check(!tape_block_finished(t, 0), "finished: cedo demais (4999)");
        check(tape_current_block(t) == 0, "finished: bloco atual %d", tape_current_block(t));
        tape_advance(t, 5000);
        check(tape_block_finished(t, 0), "finished: deveria valer em 5000");
        tape_advance(t, 12000 + 999);
        check(tape_block_finished(t, 0) && tape_current_block(t) == 1, "finished: durante o bloco 1");
        tape_advance(t, 12000 + 1000);
        check(tape_block_finished(t, 1) && !tape_block_finished(t, 0), "finished: bloco 1 em 13000");
        tape_rewind(t);
        check(!tape_block_finished(t, 0) && !tape_block_finished(t, 1), "finished: rewind deveria limpar");
        tape_free(t);
    }

    /* tape_take_rom_block com o vetor do formats.md 4.8 */
    {
        const uint8_t *d;
        size_t n;
        Tape *t = tape_new();
        tzx_hdr(b);
        bhex(b, "10 E8 03 13 00 00 03 52 4F 4D 20 20 20 20 20 20 20 02 00 00 00 00 80 F1");
        bhex(b, "10 00 00 04 00 FF F3 AF A3");
        bhex(b, "20 00 00");
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "4.8: %s", err);
        check(strcmp(tape_block_desc(t, 0), "Bytes: ROM") == 0, "4.8 desc '%s'", tape_block_desc(t, 0));
        check(strcmp(tape_block_desc(t, 1), "Dados 4") == 0, "4.8 desc '%s'", tape_block_desc(t, 1));
        check(strcmp(tape_block_desc(t, 2), "Parar a fita") == 0, "4.8 desc '%s'", tape_block_desc(t, 2));
        check(tape_take_rom_block(t, &d, &n) == 1 && n == 19 && d[0] == 0 && d[18] == 0xF1, "take 1");
        check(tape_block_finished(t, 0) && tape_current_block(t) == 1, "take 1: posicao");
        check(tape_take_rom_block(t, &d, &n) == 1 && n == 4 && d[0] == 0xFF && d[3] == 0xA3, "take 2");
        check(tape_block_finished(t, 1), "take 2: finished");
        check(tape_take_rom_block(t, &d, &n) == 0 && tape_current_block(t) == 2, "take 3 deveria falhar");
        tape_rewind(t);
        /* durante a pausa do bloco 0 (tocando): entrega o bloco 1 */
        tape_play(t, 0);
        tape_advance(t, 17789786 + 10);
        check(tape_block_finished(t, 0), "take: bloco 0 tocado");
        check(tape_take_rom_block(t, &d, &n) == 1 && n == 4, "take durante a pausa");
        tape_free(t);
    }
    /* take em blocos nao-ROM deve recusar sem mexer na fita */
    {
        const uint8_t *d;
        size_t n;
        Tape *t = tape_new();
        tzx_hdr(b); b8(b, 0x30); b8(b, 1); b8(b, 'x'); blk12(b, 2168, 100);
        tape_load_tzx_mem(t, b->d, b->n, err, sizeof err);
        check(tape_take_rom_block(t, &d, &n) == 0 && tape_current_block(t) == 0, "take nao-ROM");
        tape_free(t);
    }

    /* erros */
    {
        Tape *t = tape_new();
        static const uint8_t one[1] = { 5 };
        err[0] = 0;
        check(tape_load_tap_mem(t, NULL, 0, err, sizeof err) == -1 && err[0], "vazio deveria falhar");
        check(tape_load_tap_mem(t, one, 1, err, sizeof err) == -1 && err[0], "TAP de 1 byte deveria falhar");
        tzx_hdr(b); b->d[8] = 2;
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == -1 && strstr(err, "Versao"), "TZX 2.x: '%s'", err);
        tzx_hdr(b); b->d[0] = 'X';
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == -1, "TZX sem assinatura");
        tzx_hdr(b);
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == -1, "TZX sem blocos");
        check(tape_load_file(t, "/nao/existe/fita.tap", err, sizeof err) == -1 && err[0], "arquivo inexistente");
        check(!tape_is_loaded(t) && tape_next_edge(t) == TAPE_NO_EDGE, "sem fita");
        /* TAP truncado: guarda os blocos completos e avisa */
        b->n = 0;
        b16(b, 3); b8(b, 0xFF); b8(b, 1); b8(b, 0xFE);
        b16(b, 10); b8(b, 0xFF); b8(b, 2);
        check(tape_load_tap_mem(t, b->d, b->n, err, sizeof err) == 0 && tape_block_count(t) == 1 &&
              strstr(tape_warnings(t), "truncado"), "TAP truncado");
        /* TZX truncado no meio do segundo bloco */
        tzx_hdr(b); blk12(b, 1000, 1); b8(b, 0x10); b16(b, 1000); b16(b, 50); b8(b, 0);
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0 && tape_block_count(t) == 1 &&
              strstr(tape_warnings(t), "truncado"), "TZX truncado");
        tape_free(t);
    }
    free(b);
    free(b2);
}

/* TZX com todos os tipos de bloco (semente do fuzz e teste de leitura) */
static void build_all_blocks(Buf *b)
{
    static uint8_t rle[8000];
    csw_rle_ref(rle);
    tzx_hdr(b);
    bhex(b, "10 0A 00 13 00 00 00 41 42 43 44 45 46 47 48 49 4A 05 00 0A 00 00 80 00");  /* 0 */
    b8(b, 0x11); b16(b, 2000); b16(b, 600); b16(b, 700); b16(b, 500); b16(b, 1000); b16(b, 100);
    b8(b, 6); b16(b, 5); b24(b, 3); b8(b, 0xFF); b8(b, 0x12); b8(b, 0xFC);                /* 1 */
    blk12(b, 1500, 10);                                                                   /* 2 */
    b8(b, 0x13); b8(b, 3); b16(b, 100); b16(b, 0); b16(b, 300);                           /* 3 */
    b8(b, 0x14); b16(b, 400); b16(b, 800); b8(b, 8); b16(b, 1); b24(b, 2); b8(b, 0x55); b8(b, 0xAA); /* 4 */
    b8(b, 0x15); b16(b, 158); b16(b, 3); b8(b, 5); b24(b, 3); b8(b, 0x0F); b8(b, 0xF0); b8(b, 0xA8); /* 5 */
    blk18(b, 44100, 1, 1200, rle, 60, 2);                                                 /* 6 */
    blk18(b, 22050, 2, 1200, zl_dyn, sizeof zl_dyn, 1);                                   /* 7 */
    b8(b, 0x19); b32(b, 14 + 3 * 5 + 2);
    b16(b, 1); b32(b, 0); b8(b, 0); b8(b, 0); b32(b, 5); b8(b, 2); b8(b, 3);
    b8(b, 0); b16(b, 100); b16(b, 100); b8(b, 1); b16(b, 200); b16(b, 200); b8(b, 2); b16(b, 300); b16(b, 0);
    b8(b, 0x86); b8(b, 0x40);                                                             /* 8 */
    blk20(b, 7);                                                                          /* 9 */
    b8(b, 0x21); b8(b, 5); bmem(b, "Grupo", 5);                                           /* 10 */
    b8(b, 0x22);                                                                          /* 11 */
    b8(b, 0x23); b16(b, 2);                                                               /* 12 */
    blk12(b, 9999, 9);                                                                    /* 13 (pulado) */
    b8(b, 0x24); b16(b, 2);                                                               /* 14 */
    blk12(b, 600, 3);                                                                     /* 15 */
    b8(b, 0x25);                                                                          /* 16 */
    b8(b, 0x26); b16(b, 1); b16(b, 0x0010);                                               /* 17 -> 33 */
    b8(b, 0x28); b16(b, 7); b8(b, 1); b16(b, 1); b8(b, 3); bmem(b, "abc", 3);             /* 18 */
    b8(b, 0x2B); b32(b, 1); b8(b, 1);                                                     /* 19 */
    b8(b, 0x30); b8(b, 4); bmem(b, "Fita", 4);                                            /* 20 */
    b8(b, 0x31); b8(b, 5); b8(b, 3); bmem(b, "Msg", 3);                                   /* 21 */
    b8(b, 0x32); b16(b, 9); b8(b, 2); b8(b, 0); b8(b, 2); bmem(b, "TK", 2); b8(b, 3); b8(b, 2); bmem(b, "85", 2); /* 22 */
    b8(b, 0x33); b8(b, 1); b8(b, 0); b8(b, 0x10); b8(b, 0);                               /* 23 */
    b8(b, 0x35); bmem(b, "POKEs           ", 16); b32(b, 2); b8(b, 1); b8(b, 2);          /* 24 */
    b8(b, 0x5A); bmem(b, "XTape!\x1A\x01\x14", 9);                                        /* 25 */
    b8(b, 0x16); b32(b, 8); b32(b, 0);                                                    /* 26 */
    b8(b, 0x17); b32(b, 6); b16(b, 0);                                                    /* 27 */
    b8(b, 0x34); b32(b, 0); b32(b, 0);                                                    /* 28 */
    b8(b, 0x40); b8(b, 0); b24(b, 2); b16(b, 0);                                          /* 29 */
    b8(b, 0x7E); b32(b, 3); bmem(b, "xyz", 3);                                            /* 30 */
    b8(b, 0x2A); b32(b, 0);                                                               /* 31 */
    blk20(b, 0);                                                                          /* 32 */
    blk12(b, 800, 4);                                                                     /* 33 */
    b8(b, 0x27);                                                                          /* 34 */
}

static void test_all_blocks(void)
{
    Buf *b = (Buf *)calloc(1, sizeof(Buf));
    char err[256];
    EdgeList e = { 0 };
    uint64_t s;
    int i, ok = 1;
    Tape *t = tape_new();
    build_all_blocks(b);
    check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "todos os blocos: %s", err);
    check(tape_block_count(t) == 35, "todos os blocos: %d blocos (esperado 35)", tape_block_count(t));
    for (i = 0; i < tape_block_count(t); i++)
        if (strstr(tape_block_desc(t, i), "invalido")) { ok = 0; printf("  bloco %d: %s\n", i, tape_block_desc(t, i)); }
    check(ok, "todos os blocos: algum bloco invalido");
    check(strcmp(tape_block_desc(t, 0), "Program: ABCDEFGHIJ") == 0, "desc 0: '%s'", tape_block_desc(t, 0));
    check(strcmp(tape_block_desc(t, 22), "Info: TK") == 0, "desc 22: '%s'", tape_block_desc(t, 22));
    check(strstr(tape_warnings(t), "0x7E") && strstr(tape_warnings(t), "obsoleto"), "avisos: '%s'", tape_warnings(t));
    /* toca: chamada 17 -> 33 ... 34 retorna para 18; para em 31 (0x2A) */
    run_tape(t, 0, &e, &s, 1000000);
    check(!tape_is_playing(t) && tape_current_block(t) == 32, "todos os blocos: parou no bloco %d (esperado 32)",
          tape_current_block(t));
    run_tape(t, 0, NULL, &s, 1000000);   /* 0x20 com 0 ms: para de novo */
    check(tape_current_block(t) == 33 && !tape_at_end(t), "todos os blocos: bloco %d apos 0x20", tape_current_block(t));
    run_tape(t, 0, NULL, &s, 1000000);   /* 33, 0x27 sem chamada, fim */
    check(tape_at_end(t), "todos os blocos: deveria chegar ao fim");
    printf("  todos os tipos de bloco: %d blocos, %u bordas, avisos: %s\n", tape_block_count(t),
           (unsigned)e.n, tape_warnings(t));
    el_free(&e);
    tape_free(t);
    free(b);
}

/* ------------------------------------------------------------------ */
/* Parada apos pulso aberto (ROM) e limites contra arquivos hostis      */
/* ------------------------------------------------------------------ */

/* Bloco 0x10 de pausa 0 que comeca em nivel alto (o anterior tambem tem pausa
 * 0 e numero impar de pulsos) e termina em pulso alto; o 0x20 de 0 ms
 * seguinte precisa da borda que encerra esse pulso (senao "R Tape loading
 * error" no ultimo bit). */
static void test_stop_after_rom(void)
{
    static const uint8_t hdr[19] = { 0x00, 0x00, 'P', 'R', 'O', 'G', ' ', ' ', ' ', ' ', ' ', ' ',
                                     0x0D, 0x00, 0x0A, 0x00, 0x0D, 0x00, 0x0B };
    static const uint8_t dat[4] = { 0xFF, 0x12, 0x34, 0xFF ^ 0x12 ^ 0x34 };
    Buf *b = (Buf *)calloc(1, sizeof(Buf));
    EdgeList e = { 0 };
    uint64_t stop;
    char err[256];
    int k;
    Tape *t = tape_new();
    for (k = 0; k < 2; k++) {               /* 0x20 com 0 ms, 0x2A */
        tzx_hdr(b);
        b8(b, 0x10); b16(b, 0); b16(b, sizeof hdr); bmem(b, hdr, sizeof hdr);
        b8(b, 0x10); b16(b, 0); b16(b, sizeof dat); bmem(b, dat, sizeof dat);
        if (k) { b8(b, 0x2A); b32(b, 0); } else blk20(b, 0);
        check(tape_load_tzx_mem(t, b->d, b->n, err, sizeof err) == 0, "parada ROM: %s", err);
        el_free(&e);
        run_tape(t, 0, &e, &stop, (size_t)-1);
        /* ultimo pulso: bit 1 do checksum (0xD9 termina em 1): 1710 T alto */
        check(e.n >= 3 && e.e[e.n - 1].level == 0 && e.e[e.n - 2].level == 1 &&
              e.e[e.n - 1].t - e.e[e.n - 2].t == ROM_ONE_T && stop == e.e[e.n - 1].t + 3500 &&
              tape_level(t) == 0 && !tape_is_playing(t),
              "parada apos pulso aberto (%s): ultima borda sem o fim do pulso", k ? "0x2A" : "0x20");
        check(tape_current_block(t) == 2 && tape_block_finished(t, 1),
              "parada ROM: bloco %d", tape_current_block(t));
    }
    el_free(&e);
    tape_free(t);
    free(b);
}

typedef struct Big { uint8_t *d; size_t n, cap; } Big;
static void big8(Big *b, uint32_t v)
{
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 65536;
        b->d = (uint8_t *)realloc(b->d, b->cap);
        if (!b->d) { printf("sem memoria\n"); exit(2); }
    }
    b->d[b->n++] = (uint8_t)v;
}
static void big32(Big *b, uint32_t v) { big8(b, v); big8(b, v >> 8); big8(b, v >> 16); big8(b, v >> 24); }

/* Fluxo zlib (deflate fixo) que infla para 1 + 258*reps bytes 0x01 */
typedef struct Bits { Big *o; uint32_t acc; int n; } Bits;
static void bits_put(Bits *w, uint32_t v, int n)       /* LSB primeiro */
{
    while (n--) {
        w->acc |= (v & 1) << w->n; v >>= 1;
        if (++w->n == 8) { big8(w->o, w->acc); w->acc = 0; w->n = 0; }
    }
}
static void bits_code(Bits *w, uint32_t code, int n)   /* codigo Huffman: MSB primeiro */
{
    while (n--) bits_put(w, (code >> n) & 1, 1);
}
static void zlib_bomb(Big *o, uint32_t reps)
{
    Bits w = { o, 0, 0 };
    uint32_t i;
    big8(o, 0x78); big8(o, 0x01);
    bits_put(&w, 1, 1); bits_put(&w, 1, 2);             /* ultimo bloco, Huffman fixo */
    bits_code(&w, 0x30 + 1, 8);                         /* literal 0x01 */
    for (i = 0; i < reps; i++) {
        bits_code(&w, 0xC5, 8);                         /* comprimento 258 (285) */
        bits_code(&w, 0, 5);                            /* distancia 1 */
    }
    bits_code(&w, 0, 7);                                /* fim do bloco */
    if (w.n) big8(o, w.acc);
}

static void test_limits(void)
{
    Big b = { 0 }, z = { 0 };
    char err[256];
    size_t i;
    int k, nbad = 0;
    Tape *t = tape_new();
    printf("tape_test: limites (CSW Z-RLE total, numero de blocos)\n");
    /* 3 blocos CSW Z-RLE de ~30 MB cada: o limite (64 MB) e da fita inteira */
    zlib_bomb(&z, 30u * 1024u * 1024u / 258u);
    for (i = 0; i < 10; i++) big8(&b, (uint8_t)"ZXTape!\x1A\x01\x14"[i]);
    for (k = 0; k < 3; k++) {
        big8(&b, 0x18); big32(&b, (uint32_t)(10 + z.n));
        big8(&b, 0); big8(&b, 0);                       /* pausa 0 */
        big8(&b, 0x44); big8(&b, 0xAC); big8(&b, 0x00); /* 44100 Hz */
        big8(&b, 2); big32(&b, 0);
        for (i = 0; i < z.n; i++) big8(&b, z.d[i]);
    }
    check(tape_load_tzx_mem(t, b.d, b.n, err, sizeof err) == 0, "CSW grande: %s", err);
    for (k = 0; k < tape_block_count(t); k++)
        if (strstr(tape_block_desc(t, k), "invalido")) nbad++;
    check(tape_block_count(t) == 3 && nbad == 1 && strstr(tape_warnings(t), "grande demais"),
          "CSW grande: %d blocos, %d ignorados, aviso '%s'", tape_block_count(t), nbad, tape_warnings(t));
    /* 70000 blocos 0x22 (1 byte cada): para em TAPE_MAX_BLOCKS com aviso */
    b.n = 10;
    for (i = 0; i < 70000; i++) big8(&b, 0x22);
    check(tape_load_tzx_mem(t, b.d, b.n, err, sizeof err) == 0, "blocos demais: %s", err);
    check(tape_block_count(t) == 65536 && strstr(tape_warnings(t), "blocos demais"),
          "TZX blocos demais: %d blocos, aviso '%s'", tape_block_count(t), tape_warnings(t));
    /* TAP com 70000 blocos de 2 bytes */
    b.n = 0;
    for (i = 0; i < 70000; i++) { big8(&b, 2); big8(&b, 0); big8(&b, 0xFF); big8(&b, 0xFF); }
    check(tape_load_tap_mem(t, b.d, b.n, err, sizeof err) == 0, "TAP blocos demais: %s", err);
    check(tape_block_count(t) == 65536 && strstr(tape_warnings(t), "blocos demais") &&
          !strstr(tape_warnings(t), "byte solto"),
          "TAP blocos demais: %d blocos, aviso '%s'", tape_block_count(t), tape_warnings(t));
    free(b.d);
    free(z.d);
    tape_free(t);
}

/* ------------------------------------------------------------------ */
/* Fuzz                                                                */
/* ------------------------------------------------------------------ */

static uint32_t rng_state = 0x1234567u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int fuzz_one(Tape *t, const uint8_t *d, size_t n, int tzx)
{
    char err[256];
    int i, r, k;
    const uint8_t *bd;
    size_t bl;
    uint64_t s;
    r = tzx ? tape_load_tzx_mem(t, d, n, err, sizeof err) : tape_load_tap_mem(t, d, n, err, sizeof err);
    if (r != 0) return 0;
    for (i = -1; i <= tape_block_count(t); i++) (void)strlen(tape_block_desc(t, i));
    (void)tape_total_seconds(t);
    (void)tape_warnings(t);
    if (rnd() & 1) {
        for (k = 0; k < 4 && tape_take_rom_block(t, &bd, &bl); k++) {
            if (bl) (void)bd[bl - 1];
        }
    }
    tape_set_cpu_hz(t, (rnd() & 1) ? HZ_TK : HZ_STD);
    for (k = 0; k < 4 && !tape_at_end(t); k++) {
        run_tape(t, rnd() % 1000, NULL, &s, 3000);
        (void)tape_progress_permille(t);
        (void)tape_current_block(t);
    }
    tape_rewind(t);
    return 1;
}

static void test_fuzz(const char *dir)
{
    static const char *seeds_f[] = { "int_skip.tap", "rotatrix_tk90x.tzx", "z80bltst.tap" };
    Buf *all = (Buf *)calloc(1, sizeof(Buf));
    uint8_t *m = (uint8_t *)malloc(70000);
    int s, i, loaded = 0, cases = 0;
    Tape *t = tape_new();
    build_all_blocks(all);
    for (s = -1; s < 3; s++) {
        uint8_t *seed;
        size_t n;
        int tzx;
        if (s < 0) {
            seed = (uint8_t *)malloc(all->n);
            memcpy(seed, all->d, all->n);
            n = all->n;
        } else {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dir, seeds_f[s]);
            seed = tk_read_file(path, &n, 0);
            if (!seed) continue;
        }
        tzx = n >= 8 && memcmp(seed, "ZXTape!\x1A", 8) == 0;
        /* truncamentos: tamanho exato (malloc) para o ASan pegar leitura fora */
        for (i = 0; i < 150; i++) {
            size_t cut = (i < 40) ? (size_t)i : (size_t)(rnd() % n);
            uint8_t *c = (uint8_t *)malloc(cut ? cut : 1);
            memcpy(c, seed, cut);
            loaded += fuzz_one(t, c, cut, tzx);
            cases++;
            free(c);
        }
        /* bits trocados */
        for (i = 0; i < 250; i++) {
            int f, nf = 1 + (int)(rnd() % 8);
            uint8_t *c = (uint8_t *)malloc(n);
            memcpy(c, seed, n);
            for (f = 0; f < nf; f++) {
                size_t p = tzx ? 10 + rnd() % (n - 10) : rnd() % n;
                c[p] ^= (uint8_t)(1u << (rnd() % 8));
            }
            if (rnd() % 4 == 0) {   /* byte inteiro aleatorio num campo de tamanho */
                size_t p = rnd() % n;
                c[p] = (uint8_t)rnd();
            }
            loaded += fuzz_one(t, c, n, tzx);
            cases++;
            free(c);
        }
        free(seed);
    }
    /* inflate: fluxos zlib mutilados dentro de um bloco CSW Z-RLE */
    {
        const uint8_t *zs[3] = { zl_dyn, zl_fixed, zl_stored };
        size_t zn[3] = { sizeof zl_dyn, sizeof zl_fixed, sizeof zl_stored };
        Buf *b = (Buf *)calloc(1, sizeof(Buf));
        for (i = 0; i < 600; i++) {
            int z = (int)(rnd() % 3), f, nf = 1 + (int)(rnd() % 6);
            size_t n = zn[z], cut = (rnd() % 4 == 0) ? rnd() % n : n;
            uint8_t zbuf[2048];
            uint8_t *c;
            memcpy(zbuf, zs[z], n);
            for (f = 0; f < nf; f++) zbuf[2 + rnd() % (n - 2)] ^= (uint8_t)(1u << (rnd() % 8));
            tzx_hdr(b);
            blk18(b, 22050, 2, 1200, zbuf, cut, 0);
            c = (uint8_t *)malloc(b->n);
            memcpy(c, b->d, b->n);
            loaded += fuzz_one(t, c, b->n, 1);
            free(c);
            cases++;
        }
        free(b);
    }
    /* lixo aleatorio, com e sem cabecalho TZX */
    for (i = 0; i < 200; i++) {
        size_t n = 1 + rnd() % 4000, k;
        for (k = 0; k < n; k++) m[k] = (uint8_t)rnd();
        if (i & 1 && n > 10) { memcpy(m, "ZXTape!\x1A\x01\x14", 10); }
        {
            uint8_t *c = (uint8_t *)malloc(n);
            memcpy(c, m, n);
            loaded += fuzz_one(t, c, n, (i & 1) && n > 10);
            free(c);
        }
        cases++;
    }
    check(1, "fuzz");
    printf("  fuzz: %d casos, %d carregados, sem falhas de memoria\n", cases, loaded);
    tape_free(t);
    free(m);
    free(all);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/data";
    size_t i;
    printf("tape_test: arquivos reais (%s)\n", dir);
    for (i = 0; i < sizeof real_files / sizeof real_files[0]; i++) {
        test_real_file(dir, real_files[i], HZ_STD);
        test_real_file(dir, real_files[i], HZ_TK);
    }
    printf("tape_test: avanco preguicoso\n");
    test_coarse(dir, "rotatrix_tk90x.tzx", 997);
    test_coarse(dir, "abu_simbel_turbo.tzx", 50000);
    printf("tape_test: blocos sinteticos\n");
    test_synthetic();
    test_all_blocks();
    test_stop_after_rom();
    test_limits();
    printf("tape_test: fuzz\n");
    test_fuzz(dir);
    printf("tape_test: %d arquivos x clock ok, %d blocos decodificados, %d verificacoes ok, %d falhas\n",
           g_files_ok, g_decoded_blocks, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
