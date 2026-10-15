/*
 * tape.c - Gravador de fita virtual: TAP e TZX 1.20 -> sinal EAR.
 *
 * O arquivo e lido inteiro (copia propria) e vira uma lista de blocos com
 * ponteiros para dentro dessa copia. A reproducao e um gerador preguicoso:
 * cada chamada produz o proximo "segmento" (nivel, duracao em T de 3,5 MHz).
 * O modelo de sinal segue docs/research/formats.md secao 3: next_level e o
 * nivel do proximo pulso; PULSE inverte, pausas terminam em nivel baixo.
 *
 * Conversao de relogio: t_maquina = base + round(cum * cpu_hz / 3500000),
 * onde cum e o tempo de fita acumulado desde base (sem deriva).
 */
#include "tape.h"
#include "compat.h"

#include <stdarg.h>

#define FILE_HZ        3500000u
#define MS_T           3500u
#define TAPE_MAX_FILE  (64u * 1024u * 1024u)
#define CSW_MAX_OUT    (64u * 1024u * 1024u)        /* CSW Z-RLE descomprimido: total da fita */
#define TAPE_MAX_BLOCKS 65536                        /* fitas reais tem dezenas/centenas */
#define DUR_MAX        ((uint64_t)1 << 40)          /* limite por segmento */
#define REBASE_AT      ((uint64_t)FILE_HZ * 1024u)   /* rebaseia cum a cada ~17 min */
#define IDLE_MAX       (1u << 24)                    /* passos sem tempo antes de desistir */

/* Temporizacao padrao da ROM (T de 3,5 MHz) */
#define ROM_PILOT       2168
#define ROM_SYNC1       667
#define ROM_SYNC2       735
#define ROM_ZERO        855
#define ROM_ONE         1710
#define ROM_PILOT_HDR   8063
#define ROM_PILOT_DATA  3223

enum { GEN_SEG, GEN_STOP, GEN_END };

enum {
    PH_START = 0, PH_PILOT, PH_SYNC1, PH_SYNC2, PH_BITS, PH_TONE, PH_SEQ,
    PH_SAMPLES, PH_CSW, PH_GDB_P, PH_GDB_D, PH_PAUSE, PH_PAUSE_LO, PH_NEXT
};

typedef struct Block {
    uint8_t  id;            /* id TZX; bloco TAP vira 0x10 */
    uint8_t  bad;           /* 1 = corpo invalido: pulado na reproducao */
    uint8_t  level;         /* 0x2B */
    const uint8_t *data;    /* 0x10/11/14/15: bytes; 0x13: WORDs; 0x26: offsets */
    uint32_t data_len;
    uint32_t pilot, sync1, sync2, zero, one, pilot_count;
    uint64_t nbits;         /* 0x10/11/14: bits; 0x15: amostras */
    uint32_t pause_ms;
    uint32_t tps;           /* 0x15: T por amostra */
    uint32_t count;         /* 0x12 pulsos; 0x13 n; 0x24 vezes; 0x26 n */
    int32_t  jump;          /* 0x23: deslocamento; 0x24: indice do 0x25 do laco (ou nblocks) */
    /* 0x18 CSW */
    const uint8_t *rle;
    size_t   rle_len;
    uint8_t *owned;         /* fluxo Z-RLE descomprimido */
    uint32_t rate;
    /* 0x19 GDB */
    uint32_t totp, totd, npp, asp, npd, asd, nb;
    const uint8_t *symp, *prle, *symd, *ds;
    uint64_t est;           /* duracao estimada (T de 3,5 MHz) */
    char     desc[64];
} Block;

typedef struct Gen {
    int      blk;
    int      phase;
    int      next_level;    /* nivel do proximo pulso (TZX "current level") */
    uint64_t i, n, j, k;    /* contadores da fase atual */
    uint64_t csw_s, csw_t;  /* 0x18: amostras acumuladas / T convertido */
    size_t   csw_off;
    int      loop_active, loop_start, loop_end;   /* loop_end: indice do 0x25 do laco */
    uint32_t loop_left;
    int      call_active, call_blk;
    uint32_t call_idx;
    int      end_pause;     /* pausa implicita de 1 ms no fim ja feita */
    int      tail_open;     /* ultimo pulso emitido ainda sem a borda que o termina */
    int      stop_after;    /* 0x20 (0 ms) / 0x2A: para depois da pausa de 1 ms */
    uint32_t idle;          /* passos seguidos sem tempo (protecao contra lacos) */
} Gen;

typedef struct Seg {
    int      level;
    uint64_t dur;
    int      blk;
} Seg;

struct Tape {
    uint8_t *buf;
    size_t   buf_len;
    Block   *blocks;
    int      nblocks;
    uint64_t total_est;
    uint32_t cpu_hz;
    Gen      g;
    int      playing, at_end, ear;
    int64_t  base, cur_end;  /* relogio do quadro */
    uint64_t cum, cur_dur;   /* T de 3,5 MHz */
    int      cur_blk;
    int      done_block;
    uint64_t elapsed;
    char     warn[256];
};

/* ------------------------------------------------------------------ */
/* Utilitarios                                                         */
/* ------------------------------------------------------------------ */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd24(const uint8_t *p) { return rd16(p) | ((uint32_t)p[2] << 16); }
static uint32_t rd32(const uint8_t *p) { return rd24(p) | ((uint32_t)p[3] << 24); }

static void addwarn(char *w, size_t wl, const char *fmt, ...)
{
    char tmp[160];
    va_list ap;
    size_t cur = strlen(w);
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (strstr(w, tmp)) return;                /* nao repete o mesmo aviso */
    if (cur + strlen(tmp) + 3 >= wl) return;
    if (cur) { strcat(w, "; "); }
    strcat(w, tmp);
}

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;
    if (!err || !errlen) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static int popcount8(uint8_t v)
{
    int c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

/* Converte T de fita (3,5 MHz) em T de maquina, arredondando, sem estouro. */
static uint64_t to_machine(uint64_t cum, uint32_t hz)
{
    return (cum / FILE_HZ) * hz + ((cum % FILE_HZ) * hz + FILE_HZ / 2) / FILE_HZ;
}

/* Bit i (MSb primeiro) de um vetor de bytes */
static int getbit(const uint8_t *d, uint64_t i)
{
    return (d[i >> 3] >> (7 - (i & 7))) & 1;
}

/* Copia texto para descricao: so ASCII imprimivel, para em CR */
static void copy_text(char *out, size_t outlen, const uint8_t *s, size_t n)
{
    size_t o = 0, i;
    if (!outlen) return;
    for (i = 0; i < n && o + 1 < outlen; i++) {
        uint8_t c = s[i];
        if (c == 0x0D || c == 0) break;
        out[o++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* ------------------------------------------------------------------ */
/* Inflate minimo (RFC 1950/1951) para CSW Z-RLE                       */
/* Decodificador canonico bit a bit (estilo "puff" de Mark Adler).     */
/* ------------------------------------------------------------------ */

typedef struct Inf {
    const uint8_t *in;
    size_t   inlen, inpos;
    uint32_t bitbuf;
    int      bitcnt;
    uint8_t *out;
    size_t   outlen, outcap, outmax;
    int      err;
} Inf;

typedef struct Huff {
    short count[16];
    short symbol[288];
} Huff;

static int inf_bits(Inf *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        val |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = need < 32 ? val >> need : 0;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1u));
}

static int inf_put(Inf *s, uint8_t c)
{
    if (s->outlen == s->outcap) {
        size_t ncap = s->outcap ? s->outcap * 2 : 65536;
        uint8_t *nb;
        if (ncap > s->outmax) ncap = s->outmax;
        if (ncap <= s->outcap) { s->err = 2; return -1; }
        nb = (uint8_t *)realloc(s->out, ncap);
        if (!nb) { s->err = 3; return -1; }
        s->out = nb;
        s->outcap = ncap;
    }
    s->out[s->outlen++] = c;
    return 0;
}

/* Monta tabela; devolve <0 se super-assinada, >0 se incompleta, 0 se completa */
static int inf_construct(Huff *h, const short *length, int n)
{
    short offs[16];
    int sym, len, left;
    for (len = 0; len < 16; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int inf_decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0, len, count;
    for (len = 1; len < 16; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const short inf_lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short inf_lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short inf_dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
static const short inf_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int inf_codes(Inf *s, const Huff *lc, const Huff *dc)
{
    for (;;) {
        int sym = inf_decode(s, lc);
        if (sym < 0 || s->err) return -1;
        if (sym < 256) {
            if (inf_put(s, (uint8_t)sym)) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            int len, dsym;
            size_t dist;
            sym -= 257;
            if (sym >= 29) return -1;
            len = inf_lbase[sym] + inf_bits(s, inf_lext[sym]);
            dsym = inf_decode(s, dc);
            if (dsym < 0 || dsym >= 30 || s->err) return -1;
            dist = (size_t)inf_dbase[dsym] + (size_t)inf_bits(s, inf_dext[dsym]);
            if (s->err || dist > s->outlen) return -1;
            while (len--) {
                if (inf_put(s, s->out[s->outlen - dist])) return -1;
            }
        }
    }
}

static int inf_stored(Inf *s)
{
    uint32_t len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) return -1;
    len = rd16(s->in + s->inpos);
    if ((len ^ 0xFFFFu) != rd16(s->in + s->inpos + 2)) return -1;
    s->inpos += 4;
    if (s->inpos + len > s->inlen) return -1;
    while (len--) if (inf_put(s, s->in[s->inpos++])) return -1;
    return 0;
}

static int inf_fixed(Inf *s)
{
    Huff lc, dc;
    short lengths[288];
    int i;
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    inf_construct(&lc, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    inf_construct(&dc, lengths, 30);
    return inf_codes(s, &lc, &dc);
}

static int inf_dynamic(Inf *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[320];
    Huff lc, dc;
    int nlen, ndist, ncode, index, err;
    nlen = inf_bits(s, 5) + 257;
    ndist = inf_bits(s, 5) + 1;
    ncode = inf_bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) return -1;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)inf_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return -1;
    if (inf_construct(&lc, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = inf_decode(s, &lc), len = 0, rep;
        if (sym < 0 || s->err) return -1;
        if (sym < 16) { lengths[index++] = (short)sym; continue; }
        if (sym == 16) {
            if (index == 0) return -1;
            len = lengths[index - 1];
            rep = 3 + inf_bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + inf_bits(s, 3);
        } else {
            rep = 11 + inf_bits(s, 7);
        }
        if (s->err || index + rep > nlen + ndist) return -1;
        while (rep--) lengths[index++] = (short)len;
    }
    if (lengths[256] == 0) return -1;
    err = inf_construct(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return -1;
    err = inf_construct(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return -1;
    return inf_codes(s, &lc, &dc);
}

/* Descomprime fluxo zlib. Devolve buffer (malloc) ou NULL; *too_big = 1 se
 * o motivo foi passar de outmax bytes. */
static uint8_t *zlib_inflate(const uint8_t *in, size_t inlen, size_t *outlen, size_t outmax,
                             int *too_big)
{
    Inf s;
    int last, type;
    uint32_t a = 1, b = 0;
    size_t i;
    *too_big = 0;
    if (inlen < 2) return NULL;
    if ((in[0] & 0x0F) != 8 || (in[0] >> 4) > 7 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20))
        return NULL;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = inlen;
    s.inpos = 2;
    s.outmax = outmax;
    do {
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.err) break;
        if (type == 0) { if (inf_stored(&s) && !s.err) s.err = 4; }
        else if (type == 1) { if (inf_fixed(&s) && !s.err) s.err = 4; }
        else if (type == 2) { if (inf_dynamic(&s) && !s.err) s.err = 4; }
        else s.err = 4;
    } while (!last && !s.err);
    if (s.err) { *too_big = s.err == 2; free(s.out); return NULL; }
    /* Adler-32 (se presente) */
    for (i = 0; i < s.outlen; i++) {
        a = (a + s.out[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    if (s.inpos + 4 <= s.inlen) {
        const uint8_t *p = s.in + s.inpos;
        uint32_t ad = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        if (ad != ((b << 16) | a)) { free(s.out); return NULL; }
    }
    if (!s.out) s.out = (uint8_t *)malloc(1);
    *outlen = s.outlen;
    return s.out;
}

/* ------------------------------------------------------------------ */
/* Descricoes e estimativas                                            */
/* ------------------------------------------------------------------ */

static void desc_std(char *out, size_t n, const uint8_t *d, uint32_t len, const char *other)
{
    static const char *types[4] = { "Program", "Number array", "Character array", "Bytes" };
    if (len == 19 && d[0] == 0x00 && d[1] <= 3) {
        char name[16];
        copy_text(name, sizeof name, d + 2, 10);
        snprintf(out, n, "%s: %s", types[d[1]], name);
    } else {
        snprintf(out, n, "%s %u", other, (unsigned)len);
    }
}

static uint64_t bits_time(const Block *b)
{
    /* 2 pulsos por bit */
    uint64_t ones = 0, nbits = b->nbits, full = nbits / 8, i;
    for (i = 0; i < full; i++) ones += (uint64_t)popcount8(b->data[i]);
    if (nbits & 7) {
        uint8_t mask = (uint8_t)(0xFF00u >> (nbits & 7));
        ones += (uint64_t)popcount8((uint8_t)(b->data[full] & mask));
    }
    return 2 * (ones * b->one + (nbits - ones) * b->zero);
}

static uint64_t gdb_symdur(const uint8_t *def, uint32_t np)
{
    uint64_t s = 0;
    uint32_t i;
    for (i = 0; i < np; i++) {
        uint32_t d = rd16(def + 1 + 2 * i);
        if (!d) break;
        s += d;
    }
    return s;
}

static uint32_t gdb_sym(const Block *b, uint64_t idx)
{
    uint64_t bit = idx * b->nb;
    uint32_t v = 0, k;
    for (k = 0; k < b->nb; k++, bit++) v = (v << 1) | (uint32_t)getbit(b->ds, bit);
    return v;
}

/* Le um pulso RLE do CSW a partir de *off; devolve 0 no fim */
static int csw_next(const Block *b, size_t *off, uint32_t *samples)
{
    uint8_t c;
    if (*off >= b->rle_len) return 0;
    c = b->rle[(*off)++];
    if (c) { *samples = c; return 1; }
    if (*off + 4 > b->rle_len) { *off = b->rle_len; return 0; }
    *samples = rd32(b->rle + *off);
    *off += 4;
    return 1;
}

/* T (3,5 MHz) acumulado de s amostras a 'rate' Hz, arredondado */
static uint64_t csw_time(uint64_t s, uint32_t rate)
{
    return (s / rate) * FILE_HZ + ((s % rate) * FILE_HZ + rate / 2) / rate;
}

static uint64_t block_est(Block *b)
{
    uint64_t t = 0;
    if (b->bad) return 0;
    switch (b->id) {
    case 0x10: case 0x11:
        t = (uint64_t)b->pilot * b->pilot_count + b->sync1 + b->sync2 + bits_time(b);
        t += (uint64_t)b->pause_ms * MS_T;
        break;
    case 0x12:
        t = (uint64_t)b->pilot * b->count;
        break;
    case 0x13: {
        uint32_t i;
        for (i = 0; i < b->count; i++) t += rd16(b->data + 2 * i);
        break;
    }
    case 0x14:
        t = bits_time(b) + (uint64_t)b->pause_ms * MS_T;
        break;
    case 0x15:
        t = b->nbits * b->tps + (uint64_t)b->pause_ms * MS_T;
        break;
    case 0x18: {
        size_t off = 0;
        uint32_t smp;
        uint64_t s = 0, n = 0;
        while (csw_next(b, &off, &smp)) { s += smp; n++; }
        if (s / b->rate > ((uint64_t)1 << 32)) s = (uint64_t)b->rate << 32;
        t = csw_time(s, b->rate) + (uint64_t)b->pause_ms * MS_T;
        b->count = (uint32_t)(n > 0xFFFFFFFFu ? 0xFFFFFFFFu : n);
        break;
    }
    case 0x19: {
        uint64_t i;
        uint32_t spsz = 2 * b->npp + 1, sdsz = 2 * b->npd + 1;
        for (i = 0; i < b->totp; i++) {
            uint32_t sym = b->prle[3 * i], reps = rd16(b->prle + 3 * i + 1);
            if (sym < b->asp) t += (uint64_t)reps * gdb_symdur(b->symp + sym * spsz, b->npp);
        }
        if (b->totd) {
            if (b->nb == 0) {
                t += (uint64_t)b->totd * gdb_symdur(b->symd, b->npd);
            } else {
                uint64_t dur[256];
                uint32_t s;
                for (s = 0; s < b->asd; s++) dur[s] = gdb_symdur(b->symd + s * sdsz, b->npd);
                for (i = 0; i < b->totd; i++) {
                    uint32_t sym = gdb_sym(b, i);
                    if (sym < b->asd) t += dur[sym];
                }
            }
        }
        t += (uint64_t)b->pause_ms * MS_T;
        break;
    }
    case 0x20:
        t = (uint64_t)b->pause_ms * MS_T;
        break;
    default:
        break;
    }
    return t;
}

static void block_desc(Block *b)
{
    char *o = b->desc;
    size_t n = sizeof b->desc;
    const uint8_t *d = b->data;
    if (b->bad) {
        snprintf(o, n, "Bloco 0x%02X invalido (ignorado)", b->id);
        return;
    }
    switch (b->id) {
    case 0x10:
        desc_std(o, n, d, b->data_len, "Dados");
        break;
    case 0x11:
        if (b->pilot == ROM_PILOT && b->sync1 == ROM_SYNC1 && b->sync2 == ROM_SYNC2 &&
            b->zero == ROM_ZERO && b->one == ROM_ONE)
            desc_std(o, n, d, b->data_len, "Dados");
        else
            snprintf(o, n, "Turbo %u", (unsigned)b->data_len);
        break;
    case 0x12: snprintf(o, n, "Tom puro %u x %u T", (unsigned)b->count, (unsigned)b->pilot); break;
    case 0x13: snprintf(o, n, "Sequencia de %u pulsos", (unsigned)b->count); break;
    case 0x14: snprintf(o, n, "Dados puros %u", (unsigned)b->data_len); break;
    case 0x15: snprintf(o, n, "Gravacao direta %u", (unsigned)b->data_len); break;
    case 0x18: snprintf(o, n, "CSW %u pulsos", (unsigned)b->count); break;
    case 0x19: snprintf(o, n, "Dados generalizados %u simbolos", (unsigned)b->totd); break;
    case 0x20:
        if (b->pause_ms) snprintf(o, n, "Pausa %u ms", (unsigned)b->pause_ms);
        else snprintf(o, n, "Parar a fita");
        break;
    case 0x21: {
        char txt[48];
        copy_text(txt, sizeof txt, d, b->data_len);
        snprintf(o, n, "Grupo: %s", txt);
        break;
    }
    case 0x22: snprintf(o, n, "Fim do grupo"); break;
    case 0x23: snprintf(o, n, "Saltar %d blocos", (int)b->jump); break;
    case 0x24: snprintf(o, n, "Repetir %u vezes", (unsigned)b->count); break;
    case 0x25: snprintf(o, n, "Fim da repeticao"); break;
    case 0x26: snprintf(o, n, "Chamar %u sequencias", (unsigned)b->count); break;
    case 0x27: snprintf(o, n, "Retorno"); break;
    case 0x28: snprintf(o, n, "Selecao de bloco"); break;
    case 0x2A: snprintf(o, n, "Parar a fita (48K)"); break;
    case 0x2B: snprintf(o, n, "Nivel do sinal: %s", b->level ? "alto" : "baixo"); break;
    case 0x30: case 0x31: {
        char txt[48];
        copy_text(txt, sizeof txt, d, b->data_len);
        snprintf(o, n, "%s: %s", b->id == 0x30 ? "Texto" : "Mensagem", txt);
        break;
    }
    case 0x32: {
        /* procura o titulo (id 0) */
        uint32_t pos = 1, cnt = b->data_len ? d[0] : 0, i;
        char txt[48];
        txt[0] = 0;
        for (i = 0; i < cnt && pos + 2 <= b->data_len; i++) {
            uint32_t id = d[pos], l = d[pos + 1];
            if (pos + 2 + l > b->data_len) break;
            if (id == 0) { copy_text(txt, sizeof txt, d + pos + 2, l); break; }
            pos += 2 + l;
        }
        if (txt[0]) snprintf(o, n, "Info: %s", txt);
        else snprintf(o, n, "Informacoes do arquivo");
        break;
    }
    case 0x33: snprintf(o, n, "Tipo de hardware"); break;
    case 0x35: {
        char txt[20];
        copy_text(txt, sizeof txt, d, 16);
        snprintf(o, n, "Info: %s", txt);
        break;
    }
    case 0x5A: snprintf(o, n, "Juncao de arquivos"); break;
    case 0x16: case 0x17: case 0x34: case 0x40:
        snprintf(o, n, "Bloco obsoleto 0x%02X (ignorado)", b->id);
        break;
    default:
        snprintf(o, n, "Bloco desconhecido 0x%02X (ignorado)", b->id);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Leitura dos arquivos                                                */
/* ------------------------------------------------------------------ */

typedef struct Parse {
    Block *b;
    int    n, cap;
    size_t inflated;        /* bytes de CSW Z-RLE ja descomprimidos (limite CSW_MAX_OUT) */
    char   warn[256];
} Parse;

static Block *parse_add(Parse *p, uint8_t id)
{
    Block *b;
    if (p->n == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 64;
        Block *nb = (Block *)realloc(p->b, (size_t)ncap * sizeof(Block));
        if (!nb) return NULL;
        p->b = nb;
        p->cap = ncap;
    }
    b = &p->b[p->n++];
    memset(b, 0, sizeof *b);
    b->id = id;
    return b;
}

static void parse_free(Parse *p)
{
    int i;
    for (i = 0; i < p->n; i++) free(p->b[i].owned);
    free(p->b);
    p->b = NULL;
    p->n = p->cap = 0;
}

static void set_rom_block(Block *b, const uint8_t *data, uint32_t len, uint32_t pause)
{
    b->data = data;
    b->data_len = len;
    b->pilot = ROM_PILOT;
    b->sync1 = ROM_SYNC1;
    b->sync2 = ROM_SYNC2;
    b->zero = ROM_ZERO;
    b->one = ROM_ONE;
    b->pilot_count = (len > 0 && (data[0] & 0x80)) ? ROM_PILOT_DATA : ROM_PILOT_HDR;
    b->nbits = (uint64_t)len * 8;
    b->pause_ms = pause;
}

/* Bits uteis no ultimo byte (normalizacao como libspectrum) */
static uint64_t data_bits(uint32_t n, uint32_t used)
{
    if (n == 0) return 0;
    if (used == 0) return (uint64_t)(n - 1) * 8;
    if (used > 8) used = 8;
    return (uint64_t)(n - 1) * 8 + used;
}

static int parse_tap(Parse *p, const uint8_t *d, size_t len)
{
    size_t pos = 0;
    while (pos + 2 <= len) {
        uint32_t l = rd16(d + pos);
        Block *b;
        if (pos + 2 + l > len) {
            addwarn(p->warn, sizeof p->warn, "TAP: bloco truncado na posicao %u ignorado", (unsigned)pos);
            pos = len;
            break;
        }
        if (p->n >= TAPE_MAX_BLOCKS) {
            addwarn(p->warn, sizeof p->warn, "TAP: fita com blocos demais; leitura parada na posicao %u",
                    (unsigned)pos);
            pos = len;
            break;
        }
        b = parse_add(p, 0x10);
        if (!b) return -1;
        set_rom_block(b, d + pos + 2, l, 1000);
        pos += 2 + l;
    }
    if (pos < len)
        addwarn(p->warn, sizeof p->warn, "TAP: byte solto no fim do arquivo ignorado");
    return 0;
}

static void parse_gdb(Parse *p, Block *b, const uint8_t *body, uint64_t blen)
{
    uint64_t off, end = blen;
    if (blen < 18) { b->bad = 1; return; }
    b->pause_ms = rd16(body + 4);
    b->totp = rd32(body + 6);
    b->npp = body[10];
    b->asp = body[11] ? body[11] : 256;
    b->totd = rd32(body + 12);
    b->npd = body[16];
    b->asd = body[17] ? body[17] : 256;
    off = 18;
    if (b->totp) {
        b->symp = body + off;
        off += (uint64_t)b->asp * (2 * b->npp + 1);
        if (off > end) { b->bad = 1; return; }
        b->prle = body + off;
        off += 3 * (uint64_t)b->totp;
        if (off > end) { b->bad = 1; return; }
    }
    if (b->totd) {
        uint64_t ds;
        b->symd = body + off;
        off += (uint64_t)b->asd * (2 * b->npd + 1);
        if (off > end) { b->bad = 1; return; }
        b->nb = 0;
        while ((1u << b->nb) < b->asd) b->nb++;
        b->ds = body + off;
        ds = ((uint64_t)b->nb * b->totd + 7) / 8;
        off += ds;
        if (off > end) { b->bad = 1; return; }
    }
    if (off != end)
        addwarn(p->warn, sizeof p->warn, "TZX: bloco 0x19 com tamanho inconsistente");
}

static void parse_csw(Parse *p, Block *b, const uint8_t *body, uint64_t blen)
{
    uint32_t comp;
    if (blen < 14) { b->bad = 1; return; }
    b->pause_ms = rd16(body + 4);
    b->rate = rd24(body + 6);
    comp = body[9];
    b->rle = body + 14;
    b->rle_len = (size_t)(blen - 14);
    if (b->rate == 0 || (comp != 1 && comp != 2)) {
        addwarn(p->warn, sizeof p->warn, "TZX: bloco CSW invalido ignorado");
        b->bad = 1;
        return;
    }
    if (comp == 2) {
        /* o limite vale para a fita inteira: varios blocos pequenos que inflam
         * muito nao podem somar gigabytes */
        size_t outlen = 0, budget = CSW_MAX_OUT > p->inflated ? CSW_MAX_OUT - p->inflated : 0;
        int too_big = 1;
        uint8_t *out = budget ? zlib_inflate(b->rle, b->rle_len, &outlen, budget, &too_big) : NULL;
        if (!out) {
            addwarn(p->warn, sizeof p->warn, too_big
                    ? "TZX: CSW descomprimido grande demais; bloco ignorado"
                    : "TZX: bloco CSW Z-RLE corrompido ignorado");
            b->bad = 1;
            return;
        }
        p->inflated += outlen;
        b->owned = out;
        b->rle = out;
        b->rle_len = outlen;
    }
}

static int parse_tzx(Parse *p, const uint8_t *d, size_t len, char *err, size_t errlen)
{
    size_t pos = 10;
    if (len < 10 || memcmp(d, "ZXTape!\x1A", 8) != 0) {
        seterr(err, errlen, "Arquivo TZX invalido (assinatura ausente)");
        return -1;
    }
    if (d[8] != 1) {
        seterr(err, errlen, "Versao TZX %u.%02u nao suportada", d[8], d[9]);
        return -1;
    }
    if (d[9] > 20)
        addwarn(p->warn, sizeof p->warn, "TZX: versao 1.%02u mais nova que 1.20", d[9]);

    while (pos < len) {
        uint8_t id = d[pos];
        const uint8_t *body = d + pos + 1;
        uint64_t avail = (uint64_t)(len - pos - 1);
        uint64_t hdr, blen;
        Block *b;

        /* tamanho minimo do cabecalho para ler o comprimento */
        switch (id) {
        case 0x10: hdr = 4; break;
        case 0x11: hdr = 0x12; break;
        case 0x12: hdr = 4; break;
        case 0x13: hdr = 1; break;
        case 0x14: hdr = 0x0A; break;
        case 0x15: hdr = 8; break;
        case 0x20: case 0x23: case 0x24: hdr = 2; break;
        case 0x21: case 0x30: case 0x33: hdr = 1; break;
        case 0x22: case 0x25: case 0x27: hdr = 0; break;
        case 0x26: case 0x28: case 0x32: hdr = 2; break;
        case 0x31: hdr = 2; break;
        case 0x34: hdr = 8; break;
        case 0x35: hdr = 0x14; break;
        case 0x40: hdr = 4; break;
        case 0x5A: hdr = 9; break;
        default: hdr = 4; break;   /* 0x16/17/18/19/2A/2B e desconhecidos: DWORD */
        }
        if (hdr > avail) {
            addwarn(p->warn, sizeof p->warn, "TZX: bloco 0x%02X truncado na posicao %u; a fita termina ali",
                    id, (unsigned)pos);
            break;
        }
        switch (id) {
        case 0x10: blen = 4 + (uint64_t)rd16(body + 2); break;
        case 0x11: blen = 0x12 + (uint64_t)rd24(body + 0x0F); break;
        case 0x12: blen = 4; break;
        case 0x13: blen = 1 + 2 * (uint64_t)body[0]; break;
        case 0x14: blen = 0x0A + (uint64_t)rd24(body + 7); break;
        case 0x15: blen = 8 + (uint64_t)rd24(body + 5); break;
        case 0x20: case 0x23: case 0x24: blen = 2; break;
        case 0x21: case 0x30: blen = 1 + (uint64_t)body[0]; break;
        case 0x22: case 0x25: case 0x27: blen = 0; break;
        case 0x26: blen = 2 + 2 * (uint64_t)rd16(body); break;
        case 0x28: case 0x32: blen = 2 + (uint64_t)rd16(body); break;
        case 0x31: blen = 2 + (uint64_t)body[1]; break;
        case 0x33: blen = 1 + 3 * (uint64_t)body[0]; break;
        case 0x34: blen = 8; break;
        case 0x35: blen = 0x14 + (uint64_t)rd32(body + 0x10); break;
        case 0x40: blen = 4 + (uint64_t)rd24(body + 1); break;
        case 0x5A: blen = 9; break;
        case 0x16: case 0x17: blen = rd32(body); if (blen < 4) blen = 4; break;
        default: blen = 4 + (uint64_t)rd32(body); break;
        }
        if (blen > avail) {
            addwarn(p->warn, sizeof p->warn, "TZX: bloco 0x%02X truncado na posicao %u; a fita termina ali",
                    id, (unsigned)pos);
            break;
        }
        if (p->n >= TAPE_MAX_BLOCKS) {
            addwarn(p->warn, sizeof p->warn, "TZX: fita com blocos demais; leitura parada na posicao %u",
                    (unsigned)pos);
            break;
        }
        b = parse_add(p, id);
        if (!b) { seterr(err, errlen, "Memoria insuficiente"); return -1; }

        switch (id) {
        case 0x10:
            set_rom_block(b, body + 4, rd16(body + 2), rd16(body));
            break;
        case 0x11:
            b->pilot = rd16(body);
            b->sync1 = rd16(body + 2);
            b->sync2 = rd16(body + 4);
            b->zero = rd16(body + 6);
            b->one = rd16(body + 8);
            b->pilot_count = rd16(body + 0x0A);
            b->pause_ms = rd16(body + 0x0D);
            b->data_len = rd24(body + 0x0F);
            b->data = body + 0x12;
            b->nbits = data_bits(b->data_len, body[0x0C]);
            break;
        case 0x12:
            b->pilot = rd16(body);
            b->count = rd16(body + 2);
            break;
        case 0x13:
            b->count = body[0];
            b->data = body + 1;
            b->data_len = 2 * b->count;
            break;
        case 0x14:
            b->zero = rd16(body);
            b->one = rd16(body + 2);
            b->pause_ms = rd16(body + 5);
            b->data_len = rd24(body + 7);
            b->data = body + 0x0A;
            b->nbits = data_bits(b->data_len, body[4]);
            break;
        case 0x15:
            b->tps = rd16(body);
            b->pause_ms = rd16(body + 2);
            b->data_len = rd24(body + 5);
            b->data = body + 8;
            b->nbits = data_bits(b->data_len, body[4]);
            break;
        case 0x18:
            parse_csw(p, b, body, blen);
            break;
        case 0x19:
            parse_gdb(p, b, body, blen);
            if (b->bad) addwarn(p->warn, sizeof p->warn, "TZX: bloco 0x19 invalido ignorado");
            break;
        case 0x20:
            b->pause_ms = rd16(body);
            break;
        case 0x21: case 0x30:
            b->data = body + 1;
            b->data_len = body[0];
            break;
        case 0x23:
            b->jump = (int16_t)rd16(body);
            break;
        case 0x24:
            b->count = rd16(body);
            break;
        case 0x26:
            b->count = rd16(body);
            b->data = body + 2;
            b->data_len = 2 * b->count;
            break;
        case 0x2B:
            if (blen < 5) b->bad = 1;
            else b->level = body[4] ? 1 : 0;
            break;
        case 0x31:
            b->data = body + 2;
            b->data_len = body[1];
            break;
        case 0x32:
            b->data = body + 2;
            b->data_len = (uint32_t)(blen - 2);
            break;
        case 0x35:
            b->data = body;
            b->data_len = 16;
            break;
        case 0x22: case 0x25: case 0x27: case 0x28: case 0x2A: case 0x33: case 0x5A:
            break;
        case 0x16: case 0x17: case 0x34: case 0x40:
            addwarn(p->warn, sizeof p->warn, "TZX: bloco obsoleto 0x%02X ignorado", id);
            break;
        default:
            addwarn(p->warn, sizeof p->warn, "TZX: bloco desconhecido 0x%02X ignorado", id);
            break;
        }
        pos += 1 + (size_t)blen;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gerador de pulsos                                                   */
/* ------------------------------------------------------------------ */

static void gen_reset(Gen *g)
{
    memset(g, 0, sizeof *g);
}

/* PULSE(d): emite no nivel next_level e inverte; d == 0 so inverte */
static int pulse(Gen *g, Seg *s, uint64_t d)
{
    int lvl = g->next_level;
    g->next_level = !lvl;
    if (!d) return 0;
    s->level = lvl;
    s->dur = d;
    g->tail_open = 1;
    return 1;
}

static void gdb_polarity(Gen *g, uint8_t flags)
{
    switch (flags & 3) {
    case 1: g->next_level = !g->next_level; break;
    case 2: g->next_level = 0; break;
    case 3: g->next_level = 1; break;
    default: break;
    }
}

static void call_goto(Tape *t)
{
    Gen *g = &t->g;
    const Block *cb = &t->blocks[g->call_blk];
    while (g->call_idx < cb->count) {
        int target = g->call_blk + (int16_t)rd16(cb->data + 2 * g->call_idx);
        if (target >= 0 && target < t->nblocks && target != g->call_blk) {
            g->blk = target;
            return;
        }
        g->call_idx++;
    }
    g->call_active = 0;
    g->blk = g->call_blk + 1;
}

/* Blocos sem sinal (informativos e de fluxo). Devolve 1 se tratou
 * (g->blk ja aponta o proximo); 0 se o bloco toca, para ou muda nivel. */
static int flow_block(Tape *t, const Block *b)
{
    Gen *g = &t->g;
    if (b->bad) { g->blk++; return 1; }
    switch (b->id) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
    case 0x18: case 0x19: case 0x20: case 0x2A: case 0x2B:
        return 0;
    case 0x23: {
        int target = g->blk + b->jump;
        if (b->jump == 0 || target < 0 || target >= t->nblocks) { g->blk++; return 1; }
        /* salto para fora do laco: o laco acaba (senao um 0x25 adiante voltaria
         * para ele e o proximo 0x24 seria tomado por aninhado) */
        if (g->loop_active && (target < g->loop_start || target > g->loop_end)) g->loop_active = 0;
        g->blk = target;
        return 1;
    }
    case 0x24:
        if (!g->loop_active) {
            g->loop_active = 1;
            g->loop_start = g->blk + 1;
            g->loop_end = b->jump;
            g->loop_left = b->count > 1 ? b->count - 1 : 0;
        }
        g->blk++;
        return 1;
    case 0x25:
        if (g->loop_active && g->loop_left > 0) {
            g->loop_left--;
            g->blk = g->loop_start;
        } else {
            g->loop_active = 0;
            g->blk++;
        }
        return 1;
    case 0x26:
        if (g->call_active || b->count == 0) { g->blk++; return 1; }
        g->call_active = 1;
        g->call_blk = g->blk;
        g->call_idx = 0;
        call_goto(t);
        return 1;
    case 0x27:
        if (g->call_active) {
            g->call_idx++;
            call_goto(t);
        } else {
            g->blk++;
        }
        return 1;
    default:
        g->blk++;
        return 1;
    }
}

/* Fim dos pulsos de um bloco de dados: marca e vai para a pausa */
static void pulses_done(Tape *t, uint32_t pause_ms)
{
    t->done_block = t->g.blk;
    t->g.phase = PH_PAUSE;
    t->g.n = (uint64_t)pause_ms * MS_T;
}

static int gen_next(Tape *t, Seg *s)
{
    Gen *g = &t->g;
    for (;;) {
        const Block *b;
        if (++g->idle > IDLE_MAX) {
            addwarn(t->warn, sizeof t->warn, "Fita: laco sem pulsos; fita parada");
            g->blk = t->nblocks;
            g->end_pause = 1;
            g->phase = PH_START;
            return GEN_END;
        }
        if (g->blk < 0) g->blk = 0;
        if (g->blk >= t->nblocks) {
            b = NULL;
            if (g->phase == PH_START) {
                if (g->end_pause) return GEN_END;
                /* pausa implicita de 1 ms: garante a borda do ultimo pulso */
                g->end_pause = 1;
                g->phase = PH_PAUSE;
                g->n = MS_T;
            }
            s->blk = t->nblocks - 1;
        } else {
            b = &t->blocks[g->blk];
            s->blk = g->blk;
        }

        switch (g->phase) {
        case PH_START:
            if (flow_block(t, b)) continue;
            switch (b->id) {
            case 0x20: case 0x2A:
                if (b->id == 0x20 && b->pause_ms) {
                    g->phase = PH_PAUSE;
                    g->n = (uint64_t)b->pause_ms * MS_T;
                    continue;
                }
                /* parar: se o ultimo pulso (bloco anterior com pausa 0) ainda nao
                 * terminou, primeiro 1 ms que o encerra, como no fim da fita;
                 * senao a ROM perde a borda do ultimo bit */
                if (g->tail_open) {
                    g->stop_after = 1;
                    g->phase = PH_PAUSE;
                    g->n = MS_T;
                    continue;
                }
                g->blk++;
                return GEN_STOP;
            case 0x2B:
                g->tail_open = 0;
                g->next_level = b->level;
                g->blk++;
                s->level = b->level;
                s->dur = 0;
                return GEN_SEG;
            case 0x10: case 0x11:
                g->phase = PH_PILOT;
                g->i = 0;
                g->n = b->pilot_count;
                continue;
            case 0x12:
                g->phase = PH_TONE;
                g->i = 0;
                continue;
            case 0x13:
                g->phase = PH_SEQ;
                g->i = 0;
                continue;
            case 0x14:
                g->phase = PH_BITS;
                g->i = 0;
                g->n = 2 * b->nbits;
                continue;
            case 0x15:
                g->phase = PH_SAMPLES;
                g->i = 0;
                continue;
            case 0x18:
                g->phase = PH_CSW;
                g->csw_off = 0;
                g->csw_s = 0;
                g->csw_t = 0;
                g->k = 0;
                continue;
            case 0x19:
                g->phase = PH_GDB_P;
                g->i = g->j = g->k = 0;
                continue;
            default:
                g->blk++;
                continue;
            }

        case PH_PILOT:
            if (g->i < g->n) {
                g->i++;
                if (pulse(g, s, b->pilot)) break;
                continue;
            }
            g->phase = PH_SYNC1;
            continue;

        case PH_SYNC1:
            g->phase = PH_SYNC2;
            if (pulse(g, s, b->sync1)) break;
            continue;

        case PH_SYNC2:
            g->phase = PH_BITS;
            g->i = 0;
            g->n = 2 * b->nbits;
            if (pulse(g, s, b->sync2)) break;
            continue;

        case PH_BITS:
            if (g->i < g->n) {
                int bit = getbit(b->data, g->i >> 1);
                g->i++;
                if (pulse(g, s, bit ? b->one : b->zero)) break;
                continue;
            }
            pulses_done(t, b->pause_ms);
            continue;

        case PH_TONE:
            if (g->i < b->count) {
                g->i++;
                if (pulse(g, s, b->pilot)) break;
                continue;
            }
            pulses_done(t, 0);
            continue;

        case PH_SEQ:
            if (g->i < b->count) {
                uint32_t d = rd16(b->data + 2 * g->i);
                g->i++;
                if (pulse(g, s, d)) break;
                continue;
            }
            pulses_done(t, 0);
            continue;

        case PH_SAMPLES:
            if (g->i < b->nbits) {
                /* junta amostras iguais num so segmento (LEVEL) */
                int v = getbit(b->data, g->i);
                uint64_t j = g->i + 1;
                while (j < b->nbits && getbit(b->data, j) == v) j++;
                s->level = v;
                s->dur = (j - g->i) * b->tps;
                g->i = j;
                g->next_level = v;
                g->tail_open = 0;      /* LEVEL: o nivel ja esta definido */
                if (s->dur) break;
                continue;
            }
            pulses_done(t, b->pause_ms);
            continue;

        case PH_CSW: {
            uint32_t smp;
            if (csw_next(b, &g->csw_off, &smp) && (g->csw_s + smp) / b->rate <= ((uint64_t)1 << 32)) {
                uint64_t tt;
                g->csw_s += smp;
                tt = csw_time(g->csw_s, b->rate);
                g->k++;
                if (pulse(g, s, tt - g->csw_t)) { g->csw_t = tt; break; }
                g->csw_t = tt;
                continue;
            }
            /* depois do ultimo pulso o nivel atual e o do ultimo pulso */
            if (g->k) g->next_level = !g->next_level;
            pulses_done(t, b->pause_ms);
            continue;
        }

        case PH_GDB_P: {
            uint32_t sym, reps;
            const uint8_t *def;
            if (g->i >= b->totp) {
                g->phase = PH_GDB_D;
                g->i = g->k = 0;
                continue;
            }
            sym = b->prle[3 * g->i];
            reps = rd16(b->prle + 3 * g->i + 1);
            if (g->j >= reps || sym >= b->asp) { g->i++; g->j = 0; g->k = 0; continue; }
            def = b->symp + sym * (2 * b->npp + 1);
            if (g->k == 0) gdb_polarity(g, def[0]);
            if (g->k < b->npp) {
                uint32_t d = rd16(def + 1 + 2 * g->k);
                if (d) {
                    g->k++;
                    pulse(g, s, d);
                    break;
                }
            }
            g->j++;
            g->k = 0;
            continue;
        }

        case PH_GDB_D: {
            uint32_t sym;
            const uint8_t *def;
            if (g->i >= b->totd) {
                pulses_done(t, b->pause_ms);
                continue;
            }
            sym = gdb_sym(b, g->i);
            if (sym >= b->asd) { g->i++; g->k = 0; continue; }
            def = b->symd + sym * (2 * b->npd + 1);
            if (g->k == 0) gdb_polarity(g, def[0]);
            if (g->k < b->npd) {
                uint32_t d = rd16(def + 1 + 2 * g->k);
                if (d) {
                    g->k++;
                    pulse(g, s, d);
                    break;
                }
            }
            g->i++;
            g->k = 0;
            continue;
        }

        case PH_PAUSE:
            if (g->n == 0) { g->phase = PH_NEXT; continue; }
            g->tail_open = 0;          /* a pausa termina o ultimo pulso */
            g->phase = PH_PAUSE_LO;
            if (g->next_level) {
                /* 1 ms no nivel oposto ao do ultimo pulso */
                uint64_t d = g->n < MS_T ? g->n : MS_T;
                g->n -= d;
                g->next_level = 0;
                s->level = 1;
                s->dur = d;
                break;
            }
            continue;

        case PH_PAUSE_LO:
            /* resto da pausa em nivel baixo (pode ter duracao 0: so fixa o nivel) */
            g->next_level = 0;
            g->phase = PH_NEXT;
            s->level = 0;
            s->dur = g->n;
            if (s->dur) break;
            return GEN_SEG;

        case PH_NEXT:
        default:
            if (g->stop_after) {       /* fim do 1 ms antes de 0x20/0x2A: para */
                g->stop_after = 0;
                g->blk++;
                g->phase = PH_START;
                return GEN_STOP;
            }
            if (g->blk < t->nblocks) g->blk++;
            g->phase = PH_START;
            continue;
        }
        /* segmento com duracao > 0 */
        g->idle = 0;
        return GEN_SEG;
    }
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

Tape *tape_new(void)
{
    Tape *t = (Tape *)calloc(1, sizeof(Tape));
    if (!t) return NULL;
    t->cpu_hz = FILE_HZ;
    t->done_block = -1;
    return t;
}

void tape_free(Tape *t)
{
    if (!t) return;
    tape_eject(t);
    free(t);
}

void tape_set_cpu_hz(Tape *t, uint32_t cpu_hz)
{
    if (!t) return;
    if (cpu_hz == 0) cpu_hz = FILE_HZ;
    if (t->playing) {
        /* rebaseia no fim do segmento atual para nao saltar */
        t->base = t->cur_end;
        t->cum = 0;
    }
    t->cpu_hz = cpu_hz;
}

static void free_blocks(Tape *t)
{
    int i;
    for (i = 0; i < t->nblocks; i++) free(t->blocks[i].owned);
    free(t->blocks);
    t->blocks = NULL;
    t->nblocks = 0;
    free(t->buf);
    t->buf = NULL;
    t->buf_len = 0;
}

void tape_eject(Tape *t)
{
    if (!t) return;
    free_blocks(t);
    tape_rewind(t);
    t->ear = 0;
}

static int load_common(Tape *t, const uint8_t *data, size_t len, int tzx, char *err, size_t errlen)
{
    Parse p;
    uint8_t *copy;
    uint64_t total = 0, acc = 0, cnt = 1;
    int i, in_loop = 0;

    if (err && errlen) err[0] = 0;
    if (!t) { seterr(err, errlen, "Gravador nao inicializado"); return -1; }
    if (!data || len == 0) { seterr(err, errlen, "Arquivo de fita vazio"); return -1; }
    if (len > TAPE_MAX_FILE) { seterr(err, errlen, "Arquivo de fita grande demais"); return -1; }
    copy = (uint8_t *)malloc(len);
    if (!copy) { seterr(err, errlen, "Memoria insuficiente"); return -1; }
    memcpy(copy, data, len);
    memset(&p, 0, sizeof p);

    if (tzx) {
        if (parse_tzx(&p, copy, len, err, errlen) != 0) {
            parse_free(&p);
            free(copy);
            return -1;
        }
    } else if (parse_tap(&p, copy, len) != 0) {
        seterr(err, errlen, "Memoria insuficiente");
        parse_free(&p);
        free(copy);
        return -1;
    }
    if (p.n == 0) {
        if (p.warn[0]) seterr(err, errlen, "Fita sem blocos validos (%s)", p.warn);
        else seterr(err, errlen, "Fita sem blocos");
        parse_free(&p);
        free(copy);
        return -1;
    }

    /* cada 0x24 guarda o indice do 0x25 que o fecha (para saltos 0x23 para fora) */
    for (i = 0; i < p.n; i++) {
        if (p.b[i].id == 0x24) {
            int j = i + 1;
            while (j < p.n && p.b[j].id != 0x25) j++;
            p.b[i].jump = j;
        }
    }
    for (i = 0; i < p.n; i++) {
        Block *b = &p.b[i];
        b->est = block_est(b);
        block_desc(b);
        if (b->bad) continue;
        if (b->id == 0x24 && !in_loop) {
            in_loop = 1;
            acc = 0;
            cnt = b->count > 1 ? b->count : 1;
            continue;
        }
        if (b->id == 0x25 && in_loop) {
            total += acc * cnt;
            in_loop = 0;
            continue;
        }
        if (in_loop) acc += b->est;
        else total += b->est;
    }
    if (in_loop) total += acc;

    free_blocks(t);
    t->buf = copy;
    t->buf_len = len;
    t->blocks = p.b;
    t->nblocks = p.n;
    t->total_est = total;
    memcpy(t->warn, p.warn, sizeof t->warn);
    tape_rewind(t);
    return 0;
}

int tape_load_tap_mem(Tape *t, const uint8_t *data, size_t len, char *err, size_t errlen)
{
    return load_common(t, data, len, 0, err, errlen);
}

int tape_load_tzx_mem(Tape *t, const uint8_t *data, size_t len, char *err, size_t errlen)
{
    return load_common(t, data, len, 1, err, errlen);
}

int tape_sniff_unsupported(const uint8_t *data, size_t len, char *err, size_t errlen)
{
    if (!data) return 0;
    if (len >= 4 && memcmp(data, "PZXT", 4) == 0) {
        seterr(err, errlen, "Formato PZX nao suportado");
        return 1;
    }
    if (len >= 22 && memcmp(data, "Compressed Square Wave", 22) == 0) {
        seterr(err, errlen, "Formato CSW avulso nao suportado (use TZX)");
        return 1;
    }
    return 0;
}

int tape_load_file(Tape *t, const char *path, char *err, size_t errlen)
{
    size_t len = 0;
    uint8_t *data;
    int r;
    if (!path) { seterr(err, errlen, "Nenhum arquivo indicado"); return -1; }
    data = tk_read_file(path, &len, TAPE_MAX_FILE);
    if (!data) {
        seterr(err, errlen, "Nao foi possivel ler o arquivo de fita: %s", path);
        return -1;
    }
    if (len >= 8 && memcmp(data, "ZXTape!\x1A", 8) == 0) {
        r = tape_load_tzx_mem(t, data, len, err, errlen);
    } else if (tape_sniff_unsupported(data, len, err, errlen)) {
        r = -1;
    } else {
        r = tape_load_tap_mem(t, data, len, err, errlen);
    }
    free(data);
    return r;
}

int tape_is_loaded(const Tape *t)
{
    return t && t->nblocks > 0;
}

void tape_play(Tape *t, uint32_t now)
{
    if (!tape_is_loaded(t) || t->playing || t->at_end) return;
    t->playing = 1;
    t->base = now;
    t->cum = 0;
    t->cur_end = now;   /* o primeiro segmento comeca agora */
    t->cur_dur = 0;
    t->g.idle = 0;
    t->cur_blk = t->g.blk < t->nblocks ? t->g.blk : t->nblocks - 1;
}

void tape_stop(Tape *t)
{
    if (!t) return;
    t->playing = 0;
}

void tape_rewind(Tape *t)
{
    if (!t) return;
    gen_reset(&t->g);
    t->playing = 0;
    t->at_end = 0;
    t->ear = 0;
    t->base = t->cur_end = 0;
    t->cum = t->cur_dur = 0;
    t->cur_blk = 0;
    t->done_block = -1;
    t->elapsed = 0;
}

int tape_is_playing(const Tape *t)
{
    return t && t->playing;
}

int tape_at_end(const Tape *t)
{
    if (!tape_is_loaded(t)) return 0;
    return t->at_end || t->g.blk >= t->nblocks;
}

uint32_t tape_next_edge(const Tape *t)
{
    if (!t || !t->playing) return TAPE_NO_EDGE;
    if (t->cur_end <= 0) return 0;
    if (t->cur_end >= (int64_t)TAPE_NO_EDGE) return TAPE_NO_EDGE - 1;
    return (uint32_t)t->cur_end;
}

int tape_advance(Tape *t, uint32_t now)
{
    if (!t) return 0;
    while (t->playing && t->cur_end <= (int64_t)now) {
        Seg s;
        int r;
        t->elapsed += t->cur_dur;
        t->cur_dur = 0;
        r = gen_next(t, &s);
        if (r != GEN_SEG) {
            t->playing = 0;
            if (r == GEN_END) t->at_end = 1;
            t->cur_blk = t->g.blk < t->nblocks ? t->g.blk : t->nblocks - 1;
            break;
        }
        if (s.dur > DUR_MAX) s.dur = DUR_MAX;
        t->ear = s.level;
        t->cur_blk = s.blk;
        t->cur_dur = s.dur;
        t->cum += s.dur;
        if (t->cum >= REBASE_AT) {
            uint64_t k = t->cum / FILE_HZ;
            t->cum -= k * FILE_HZ;
            t->base += (int64_t)(k * t->cpu_hz);
        }
        t->cur_end = t->base + (int64_t)to_machine(t->cum, t->cpu_hz);
    }
    return t->ear;
}

int tape_level(const Tape *t)
{
    return t ? t->ear : 0;
}

void tape_frame_end(Tape *t, uint32_t frame_len)
{
    if (!t) return;
    if (t->playing) {
        t->base -= frame_len;
        t->cur_end -= frame_len;
    }
}

int tape_block_count(const Tape *t)
{
    return t ? t->nblocks : 0;
}

int tape_current_block(const Tape *t)
{
    if (!tape_is_loaded(t)) return -1;
    if (t->playing) return t->cur_blk;
    return t->g.blk < t->nblocks ? t->g.blk : t->nblocks - 1;
}

const char *tape_block_desc(const Tape *t, int i)
{
    if (!t || i < 0 || i >= t->nblocks) return "";
    return t->blocks[i].desc;
}

int tape_progress_permille(const Tape *t)
{
    uint64_t p;
    if (!tape_is_loaded(t)) return 0;
    if (t->at_end || t->g.blk >= t->nblocks) return 1000;
    if (t->total_est == 0) return 0;
    p = t->elapsed * 1000 / t->total_est;
    return p > 1000 ? 1000 : (int)p;
}

double tape_total_seconds(const Tape *t)
{
    if (!t) return 0.0;
    return (double)t->total_est / (double)FILE_HZ;
}

const char *tape_warnings(const Tape *t)
{
    return t ? t->warn : "";
}

/* Bloco 0x10 (ou TAP), ou 0x11 com temporizacao da ROM (tolerancia 10%) */
static int near_rom(uint32_t v, uint32_t ref)
{
    return v + ref / 10 >= ref && v <= ref + ref / 10;
}

static int is_rom_block(const Block *b)
{
    if (b->bad) return 0;
    if (b->id == 0x10) return 1;
    if (b->id != 0x11) return 0;
    return near_rom(b->pilot, ROM_PILOT) && near_rom(b->sync1, ROM_SYNC1) &&
           near_rom(b->sync2, ROM_SYNC2) && near_rom(b->zero, ROM_ZERO) &&
           near_rom(b->one, ROM_ONE) && b->nbits == (uint64_t)b->data_len * 8;
}

int tape_take_rom_block(Tape *t, const uint8_t **data, size_t *len)
{
    Gen save;
    Gen *g;
    const Block *b = NULL;
    int steps;
    if (!tape_is_loaded(t) || !data || !len) return 0;
    g = &t->g;
    save = *g;
    /* pulsos do bloco atual ja tocados (na pausa): comeca do proximo */
    if (g->phase == PH_PAUSE || g->phase == PH_PAUSE_LO || g->phase == PH_NEXT) {
        if (g->blk < t->nblocks) g->blk++;
        g->phase = PH_START;
    }
    for (steps = 0; ; steps++) {
        if (steps > 100000 || g->blk < 0 || g->blk >= t->nblocks) { *g = save; return 0; }
        b = &t->blocks[g->blk];
        if (is_rom_block(b)) break;
        if (g->phase != PH_START) { *g = save; return 0; }
        if (flow_block(t, b)) continue;
        if (b->id == 0x20 || b->id == 0x2A || b->id == 0x2B) { g->blk++; continue; }
        *g = save;
        return 0;
    }
    *data = b->data;
    *len = b->data_len;
    if (b->pause_ms) {
        g->next_level = 0;
        if (!t->playing) t->ear = 0;
    } else if (g->phase == PH_START) {
        /* nivel apos o ultimo pulso: alterna a cada pulso */
        uint64_t np = (uint64_t)b->pilot_count + 2 + 2 * b->nbits;
        g->next_level ^= (int)(np & 1);
    } else {
        g->next_level = 0;
    }
    t->done_block = g->blk;
    t->elapsed += b->est;
    g->blk++;
    g->phase = PH_START;
    g->idle = 0;
    g->tail_open = 0;               /* bloco entregue direto: nenhum pulso no ar */
    g->stop_after = 0;
    t->cur_blk = g->blk < t->nblocks ? g->blk : t->nblocks - 1;
    return 1;
}

int tape_block_finished(const Tape *t, int idx)
{
    return tape_is_loaded(t) && idx >= 0 && t->done_block == idx;
}
