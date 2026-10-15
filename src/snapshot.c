/*
 * snapshot.c - Snapshots 48K (.SNA, .Z80 v1/v2/v3) e fita virtual (TAP).
 * Referencias: docs/research/formats.md secoes 2, 6, 7 e 8.
 */
#include "snapshot.h"
#include "compat.h"

#include <stdarg.h>

#define SNA_HDR      27
#define SNA_SIZE     (SNA_HDR + 49152)
#define Z80_V3_EXTRA 54
#define Z80_MAX_FILE (4u * 1024u * 1024u)   /* limite de leitura de arquivo */
#define Z80_V3_QUARTER (69888 / 4)          /* T-states v3 convertidos para quadro de 69888 */
#define Z80_V3_FRAME   69888

/* ------------------------------------------------------------------ */
/* utilidades                                                          */

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;
    if (!err || errlen == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

SnapFormat snapshot_format_from_name(const char *path)
{
    char ext[8];
    if (!path) return SNAP_UNKNOWN;
    tk_file_ext(path, ext, sizeof ext);
    if (strcmp(ext, "sna") == 0) return SNAP_SNA;
    if (strcmp(ext, "z80") == 0) return SNAP_Z80;
    return SNAP_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* SNA                                                                 */

int snapshot_load_sna(const uint8_t *data, size_t len, Snapshot *s, char *err, size_t errlen)
{
    Snapshot *t;
    uint16_t sp;

    if (err && errlen) err[0] = 0;
    if (!data || !s) { set_err(err, errlen, "SNA: parametros invalidos"); return -1; }
    if (len == 131103 || len == 147487) {
        set_err(err, errlen, "SNA de 128K nao suportado: o TK90X/TK95 e uma maquina 48K");
        return -1;
    }
    if (len != SNA_SIZE) {
        set_err(err, errlen, "Arquivo SNA invalido (%lu bytes; esperado %d)",
                (unsigned long)len, SNA_SIZE);
        return -1;
    }
    sp = rd16(data + 0x17);
    /* O PC esta na pilha. Com SP na ROM (ou SP=0xFFFF, cujo byte alto cairia
     * em 0x0000) o PC salvo se perdeu ao ser empilhado: recusa. */
    if (sp < 0x4000 || sp == 0xFFFF) {
        set_err(err, errlen, "SNA invalido: SP=0x%04X aponta para a ROM (PC salvo perdido)", sp);
        return -1;
    }

    t = (Snapshot *)calloc(1, sizeof *t);
    if (!t) { set_err(err, errlen, "SNA: memoria insuficiente"); return -1; }
    memcpy(t->ram, data + SNA_HDR, 49152);
    t->i    = data[0x00];
    t->hl_  = rd16(data + 0x01);
    t->de_  = rd16(data + 0x03);
    t->bc_  = rd16(data + 0x05);
    t->af_  = rd16(data + 0x07);
    t->hl   = rd16(data + 0x09);
    t->de   = rd16(data + 0x0B);
    t->bc   = rd16(data + 0x0D);
    t->iy   = rd16(data + 0x0F);
    t->ix   = rd16(data + 0x11);
    t->iff2 = (uint8_t)((data[0x13] >> 2) & 1);
    t->iff1 = t->iff2;                         /* RETN: IFF1 = IFF2 */
    t->r    = data[0x14];
    t->af   = rd16(data + 0x15);
    t->im   = (uint8_t)(data[0x19] & 3);
    if (t->im == 3) t->im = 1;                 /* valor invalido: IM 1 */
    t->border = (uint8_t)(data[0x1A] & 7);
    /* POP PC (SP=0xFFFE volta para 0x0000) */
    t->pc = (uint16_t)(t->ram[sp - 0x4000] | (t->ram[sp + 1 - 0x4000] << 8));
    t->sp = (uint16_t)(sp + 2);
    t->memptr = t->pc;
    t->halted = 0;
    t->tstates = 0;
    *s = *t;
    free(t);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Compressao Z80: ED ED nn bb = bb repetido nn vezes                  */

/* Descomprime src[0..srclen) em dst (no maximo dstcap bytes).
 * Para quando dst enche ou src acaba. Devolve bytes produzidos, ou -1 se
 * a entrada estiver corrompida (sequencia ED ED truncada ou estouro).
 * *used recebe quantos bytes de src foram consumidos. */
static long z80_decompress(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap,
                           size_t *used)
{
    size_t in = 0, out = 0;
    while (out < dstcap && in < srclen) {
        if (src[in] == 0xED && in + 1 < srclen && src[in + 1] == 0xED) {
            size_t n;
            uint8_t b;
            if (in + 4 > srclen) return -1;
            n = src[in + 2];
            b = src[in + 3];
            if (out + n > dstcap) return -1;
            memset(dst + out, b, n);
            out += n;
            in += 4;
        } else {
            dst[out++] = src[in++];
        }
    }
    if (used) *used = in;
    return (long)out;
}

/* Comprime src (n bytes) em dst; dst deve ter 2*n + 4 bytes (o pior caso,
 * "ED ED x" repetido, expande 3 -> 5). Devolve o tamanho comprimido. */
static size_t z80_compress(const uint8_t *src, size_t n, uint8_t *dst)
{
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t b = src[i];
        size_t run = 1;
        while (i + run < n && src[i + run] == b && run < 255) run++;
        if (run >= 5 || (b == 0xED && run >= 2)) {
            dst[o++] = 0xED; dst[o++] = 0xED;
            dst[o++] = (uint8_t)run; dst[o++] = b;
            i += run;
        } else if (b == 0xED) {
            /* ED isolado: o byte seguinte vai literal (nunca inicia sequencia) */
            dst[o++] = 0xED;
            i++;
            if (i < n) dst[o++] = src[i++];
        } else {
            dst[o++] = b;
            i++;
        }
    }
    return o;
}

/* ------------------------------------------------------------------ */
/* Z80                                                                 */

/* Nome da maquina de um modo de hardware nao 48K, ou NULL se for 48K. */
static const char *z80_machine_name(int v3, int hw)
{
    switch (hw) {
    case 0: case 1: case 2: return NULL;          /* 48K, 48K+IF1, SamRam */
    case 3: return v3 ? NULL : "128K";            /* v3: 48K + MGT */
    case 4: return v3 ? "128K" : "128K + Interface 1";
    case 5: return v3 ? "128K + Interface 1" : "desconhecida";
    case 6: return v3 ? "128K + MGT" : "desconhecida";
    case 7: case 8: return "+3";
    case 9:  return "Pentagon 128";
    case 10: return "Scorpion 256";
    case 11: return "Didaktik Kompakt";
    case 12: return "+2";
    case 13: return "+2A";
    case 14: return "Timex TC2048";
    case 15: return "Timex TC2068";
    case 128: return "Timex TS2068";
    default: return "desconhecida";
    }
}

int snapshot_load_z80(const uint8_t *data, size_t len, Snapshot *s, char *err, size_t errlen)
{
    Snapshot *t;
    uint8_t b12;
    int version;
    size_t pos;

    if (err && errlen) err[0] = 0;
    if (!data || !s) { set_err(err, errlen, "Z80: parametros invalidos"); return -1; }
    if (len < 30) { set_err(err, errlen, "Arquivo Z80 invalido (muito curto)"); return -1; }

    t = (Snapshot *)calloc(1, sizeof *t);
    if (!t) { set_err(err, errlen, "Z80: memoria insuficiente"); return -1; }

    b12 = data[12];
    if (b12 == 255) b12 = 1;
    t->af   = (uint16_t)((data[0] << 8) | data[1]);
    t->bc   = rd16(data + 2);
    t->hl   = rd16(data + 4);
    t->pc   = rd16(data + 6);
    t->sp   = rd16(data + 8);
    t->i    = data[10];
    t->r    = (uint8_t)((data[11] & 0x7F) | ((b12 & 1) << 7));
    t->border = (uint8_t)((b12 >> 1) & 7);
    t->de   = rd16(data + 13);
    t->bc_  = rd16(data + 15);
    t->de_  = rd16(data + 17);
    t->hl_  = rd16(data + 19);
    t->af_  = (uint16_t)((data[21] << 8) | data[22]);
    t->iy   = rd16(data + 23);
    t->ix   = rd16(data + 25);
    t->iff1 = data[27] ? 1 : 0;
    t->iff2 = data[28] ? 1 : 0;
    t->im   = (uint8_t)(data[29] & 3);
    if (t->im == 3) t->im = 1;
    t->issue2   = (uint8_t)((data[29] >> 2) & 1);
    t->joystick = (uint8_t)((data[29] >> 6) & 3);

    if (t->pc != 0) {
        /* versao 1: sempre 48K */
        version = 1;
        if (b12 & 0x20) {
            long n = z80_decompress(data + 30, len - 30, t->ram, 49152, NULL);
            if (n != 49152) {
                set_err(err, errlen, "Arquivo Z80 (v1) corrompido: dados comprimidos invalidos");
                free(t);
                return -1;
            }
        } else {
            if (len < 30 + 49152) {
                set_err(err, errlen, "Arquivo Z80 (v1) truncado");
                free(t);
                return -1;
            }
            memcpy(t->ram, data + 30, 49152);
        }
    } else {
        unsigned extra, hw, got = 0;
        int v3;
        if (len < 32) {
            set_err(err, errlen, "Arquivo Z80 truncado (cabecalho)");
            free(t);
            return -1;
        }
        extra = rd16(data + 30);
        if (extra == 23) { version = 2; v3 = 0; }
        else if (extra == 54 || extra == 55) { version = 3; v3 = 1; }
        else {
            set_err(err, errlen, "Arquivo Z80 invalido: cabecalho adicional de %u bytes desconhecido",
                    extra);
            free(t);
            return -1;
        }
        if (len < 32 + (size_t)extra) {
            set_err(err, errlen, "Arquivo Z80 truncado (cabecalho)");
            free(t);
            return -1;
        }
        t->pc = rd16(data + 32);
        hw = data[34];
        {
            const char *mname = z80_machine_name(v3, (int)hw);
            if (mname) {
                set_err(err, errlen,
                        "Snapshot Z80 de %s (v%d, modo de hardware %u) nao suportado: "
                        "o TK90X/TK95 e uma maquina 48K", mname, version, hw);
                free(t);
                return -1;
            }
        }
        if (v3) {
            /* contador de T-states: baixo conta de Q-1 a 0, alto 0..3 */
            long q = Z80_V3_QUARTER;
            long low = rd16(data + 55), hi = data[57];
            long ts = (((hi + 1) % 4) + 1) * q - (low + 1);
            if (ts < 0) ts = 0;
            if (ts >= Z80_V3_FRAME) ts = Z80_V3_FRAME - 1;
            t->tstates = (uint32_t)ts;
        }

        pos = 32 + extra;
        while (pos + 3 <= len) {
            unsigned clen = rd16(data + pos);
            unsigned page = data[pos + 2];
            size_t raw, avail;
            long off;
            pos += 3;
            if (clen == 0 && page == 0) break;          /* extensao SLT: fim das paginas */
            raw = (clen == 0xFFFF) ? 16384u : clen;
            avail = len - pos;
            if (raw > avail) {
                set_err(err, errlen, "Arquivo Z80 truncado (pagina %u)", page);
                free(t);
                return -1;
            }
            switch (page) {
            case 8: off = 0x0000; break;
            case 4: off = 0x4000; break;
            case 5: off = 0x8000; break;
            default: off = -1; break;                   /* ROM / paginas estranhas: ignora */
            }
            if (off >= 0) {
                unsigned bit = (page == 8) ? 1u : (page == 4) ? 2u : 4u;
                if (got & bit) {
                    set_err(err, errlen, "Arquivo Z80 corrompido: pagina %u repetida", page);
                    free(t);
                    return -1;
                }
                got |= bit;
                if (clen == 0xFFFF) {
                    memcpy(t->ram + off, data + pos, 16384);
                } else {
                    size_t used = 0;
                    long n = z80_decompress(data + pos, raw, t->ram + off, 16384, &used);
                    if (n != 16384 || used != raw) {
                        set_err(err, errlen, "Arquivo Z80 corrompido: pagina %u invalida", page);
                        free(t);
                        return -1;
                    }
                }
            }
            pos += raw;
        }
        if (!got) {
            set_err(err, errlen, "Arquivo Z80 sem paginas de RAM 48K");
            free(t);
            return -1;
        }
        /* paginas ausentes ficam zeradas (snapshot 16K, por exemplo) */
    }
    (void)version;
    t->memptr = t->pc;
    t->halted = 0;
    *s = *t;
    free(t);
    return 0;
}

int snapshot_load_file(const char *path, Snapshot *s, char *err, size_t errlen)
{
    size_t len = 0;
    uint8_t *buf;
    int r;
    SnapFormat f;

    if (err && errlen) err[0] = 0;
    if (!path || !s) { set_err(err, errlen, "Snapshot: parametros invalidos"); return -1; }
    buf = tk_read_file(path, &len, Z80_MAX_FILE);
    if (!buf) {
        set_err(err, errlen, "Nao foi possivel ler o arquivo %s", path);
        return -1;
    }
    f = snapshot_format_from_name(path);
    if (f == SNAP_UNKNOWN)
        f = (len == SNA_SIZE || len == 131103 || len == 147487) ? SNAP_SNA : SNAP_Z80;
    if (f == SNAP_SNA) r = snapshot_load_sna(buf, len, s, err, errlen);
    else r = snapshot_load_z80(buf, len, s, err, errlen);
    free(buf);
    return r;
}

/* ------------------------------------------------------------------ */
/* Gravacao                                                            */

static int write_all(const char *path, const uint8_t *buf, size_t n, char *err, size_t errlen)
{
    FILE *f = tk_fopen(path, "wb");
    int ok;
    if (!f) { set_err(err, errlen, "Nao foi possivel criar o arquivo %s", path); return -1; }
    ok = fwrite(buf, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { set_err(err, errlen, "Erro ao gravar o arquivo %s", path); return -1; }
    return 0;
}

/* SNA 48K: PC empilhado (2 bytes abaixo de SP na imagem da RAM). */
static uint8_t *build_sna(const Snapshot *s, size_t *outlen, char *err, size_t errlen)
{
    uint8_t *b;
    uint16_t sp = (uint16_t)(s->sp - 2);
    /* os dois bytes empilhados precisam estar na RAM */
    if (sp < 0x4000 || sp == 0xFFFF) {
        set_err(err, errlen, "SNA nao pode guardar o PC com SP=0x%04X (pilha na ROM); grave em .z80",
                s->sp);
        return NULL;
    }
    b = (uint8_t *)malloc(SNA_SIZE);
    if (!b) { set_err(err, errlen, "SNA: memoria insuficiente"); return NULL; }
    memset(b, 0, SNA_HDR);
    b[0x00] = s->i;
    wr16(b + 0x01, s->hl_);
    wr16(b + 0x03, s->de_);
    wr16(b + 0x05, s->bc_);
    wr16(b + 0x07, s->af_);
    wr16(b + 0x09, s->hl);
    wr16(b + 0x0B, s->de);
    wr16(b + 0x0D, s->bc);
    wr16(b + 0x0F, s->iy);
    wr16(b + 0x11, s->ix);
    b[0x13] = (uint8_t)(s->iff2 ? 0x04 : 0x00);
    b[0x14] = s->r;
    wr16(b + 0x15, s->af);
    wr16(b + 0x17, sp);
    b[0x19] = (uint8_t)(s->im & 3);
    b[0x1A] = (uint8_t)(s->border & 7);
    memcpy(b + SNA_HDR, s->ram, 49152);
    b[SNA_HDR + sp - 0x4000]     = (uint8_t)(s->pc & 0xFF);
    b[SNA_HDR + sp + 1 - 0x4000] = (uint8_t)(s->pc >> 8);
    *outlen = SNA_SIZE;
    return b;
}

/* Z80 v3 (cabecalho adicional de 54 bytes), 48K, paginas 8/4/5 comprimidas. */
static uint8_t *build_z80(const Snapshot *s, size_t *outlen, char *err, size_t errlen)
{
    static const uint8_t pages[3] = { 8, 4, 5 };
    size_t cap = 32 + Z80_V3_EXTRA + 3 * (3 + 16384 + 4), o;
    uint8_t *b = (uint8_t *)calloc(1, cap);
    uint8_t *tmp = (uint8_t *)malloc(2 * 16384 + 4);
    uint32_t ts;
    int k;

    if (!b || !tmp) {
        free(b); free(tmp);
        set_err(err, errlen, "Z80: memoria insuficiente");
        return NULL;
    }
    b[0] = (uint8_t)(s->af >> 8);
    b[1] = (uint8_t)s->af;
    wr16(b + 2, s->bc);
    wr16(b + 4, s->hl);
    wr16(b + 6, 0);                        /* PC = 0 -> v2/v3 */
    wr16(b + 8, s->sp);
    b[10] = s->i;
    b[11] = (uint8_t)(s->r & 0x7F);
    b[12] = (uint8_t)(((s->r >> 7) & 1) | ((s->border & 7) << 1));
    wr16(b + 13, s->de);
    wr16(b + 15, s->bc_);
    wr16(b + 17, s->de_);
    wr16(b + 19, s->hl_);
    b[21] = (uint8_t)(s->af_ >> 8);
    b[22] = (uint8_t)s->af_;
    wr16(b + 23, s->iy);
    wr16(b + 25, s->ix);
    b[27] = (uint8_t)(s->iff1 ? 1 : 0);
    b[28] = (uint8_t)(s->iff2 ? 1 : 0);
    b[29] = (uint8_t)((s->im & 3) | ((s->issue2 & 1) << 2) | ((s->joystick & 3) << 6));
    wr16(b + 30, Z80_V3_EXTRA);
    wr16(b + 32, s->pc);
    b[34] = 0;                             /* 48K */
    b[37] = 0x03;                          /* emulacao de R e LDIR ligadas */
    b[61] = b[62] = 0xFF;                  /* 0-8191 e 8192-16383 sao ROM */
    if ((s->joystick & 3) == 2) {
        /* no v3 o valor 2 e "definido pelo usuario": o libspectrum (Fuse) so o
         * entende como Sinclair 2 com o mapa de teclas da IF2 esquerda (1-5) */
        static const uint16_t map[5] = { 0x0F03, 0x0803, 0x0403, 0x0203, 0x0103 };
        for (k = 0; k < 5; k++) {
            wr16(b + 63 + 2 * k, map[k]);
            wr16(b + 73 + 2 * k, (uint16_t)('1' + k));
        }
    }
    /* T-states no formato v3 (quadro de 69888) */
    ts = s->tstates;
    if (ts >= Z80_V3_FRAME) ts = Z80_V3_FRAME - 1;
    wr16(b + 55, (uint16_t)((Z80_V3_QUARTER - 1) - ts % Z80_V3_QUARTER));
    b[57] = (uint8_t)((ts / Z80_V3_QUARTER + 3) % 4);
    o = 32 + Z80_V3_EXTRA;
    for (k = 0; k < 3; k++) {
        const uint8_t *src = s->ram + (pages[k] == 8 ? 0x0000 : pages[k] == 4 ? 0x4000 : 0x8000);
        size_t n = z80_compress(src, 16384, tmp);
        if (n >= 16384) {                  /* nao compensa: pagina crua */
            wr16(b + o, 0xFFFF);
            b[o + 2] = pages[k];
            memcpy(b + o + 3, src, 16384);
            o += 3 + 16384;
        } else {
            wr16(b + o, (uint16_t)n);
            b[o + 2] = pages[k];
            memcpy(b + o + 3, tmp, n);
            o += 3 + n;
        }
    }
    free(tmp);
    *outlen = o;
    return b;
}

int snapshot_save_file(const char *path, const Snapshot *s, char *err, size_t errlen)
{
    uint8_t *buf = NULL;
    size_t n = 0;
    int r;

    if (err && errlen) err[0] = 0;
    if (!path || !s) { set_err(err, errlen, "Snapshot: parametros invalidos"); return -1; }
    switch (snapshot_format_from_name(path)) {
    case SNAP_SNA: buf = build_sna(s, &n, err, errlen); break;
    case SNAP_Z80: buf = build_z80(s, &n, err, errlen); break;
    default:
        set_err(err, errlen, "Extensao de snapshot desconhecida (use .z80 ou .sna)");
        return -1;
    }
    if (!buf) return -1;
    r = write_all(path, buf, n, err, errlen);
    free(buf);
    return r;
}

/* ------------------------------------------------------------------ */
/* Fita virtual                                                        */

typedef struct {
    uint8_t *p;
    size_t n, cap;
    int fail;
} Buf;

static void buf_put(Buf *b, const void *data, size_t n)
{
    if (b->fail) return;
    if (b->n + n > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        uint8_t *np;
        while (nc < b->n + n) nc *= 2;
        np = (uint8_t *)realloc(b->p, nc);
        if (!np) { b->fail = 1; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
}

static void buf_byte(Buf *b, uint8_t v) { buf_put(b, &v, 1); }

/* Bloco TAP: tamanho LE (flag + dados + checksum), flag, dados, XOR. */
static void tap_block(Buf *b, uint8_t flag, const uint8_t *data, size_t n)
{
    uint8_t len[2], x = flag;
    size_t i;
    wr16(len, (uint16_t)(n + 2));
    buf_put(b, len, 2);
    buf_byte(b, flag);
    buf_put(b, data, n);
    for (i = 0; i < n; i++) x ^= data[i];
    buf_byte(b, x);
}

/* Nome de 10 caracteres: ASCII imprimivel; cada caractere UTF-8 ou de
 * controle vira um '?' (rede de seguranca: machine.c ja entrega o nome sem
 * acentos); completa com espacos. */
static void tape_name(const char *name, uint8_t out[10])
{
    int k = 0;
    const unsigned char *p = (const unsigned char *)(name ? name : "");
    while (*p && k < 10) {
        unsigned c = *p++;
        if (c >= 0x20 && c < 0x7F) {
            out[k++] = (uint8_t)c;
        } else {
            out[k++] = '?';
            if (c >= 0xC0)                            /* pula continuacoes UTF-8 */
                while ((*p & 0xC0) == 0x80) p++;
        }
    }
    while (k < 10) out[k++] = ' ';
}

/* Cabecalho padrao de 17 bytes (flag 0x00). */
static void tap_header(Buf *b, uint8_t type, const char *name, uint16_t dlen,
                       uint16_t p1, uint16_t p2)
{
    uint8_t h[17];
    h[0] = type;
    tape_name(name, h + 1);
    wr16(h + 11, dlen);
    wr16(h + 13, p1);
    wr16(h + 15, p2);
    tap_block(b, 0x00, h, sizeof h);
}

/* Numero em BASIC: digitos ASCII + 0x0E + forma inteira curta 00 00 lo hi 00. */
static void basic_number(Buf *b, unsigned v)
{
    char d[8];
    uint8_t f[6];
    int n = snprintf(d, sizeof d, "%u", v & 0xFFFFu);
    buf_put(b, d, (size_t)n);
    f[0] = 0x0E; f[1] = 0; f[2] = 0;
    f[3] = (uint8_t)v; f[4] = (uint8_t)(v >> 8); f[5] = 0;
    buf_put(b, f, sizeof f);
}

/* Fecha a linha 10: texto (ja terminado em 0x0D) -> programa completo. */
static void basic_line10(Buf *prog, const Buf *text)
{
    uint8_t h[4];
    h[0] = 0; h[1] = 10;                   /* numero da linha: big-endian */
    wr16(h + 2, (uint16_t)text->n);        /* tamanho: little-endian */
    buf_put(prog, h, 4);
    buf_put(prog, text->p, text->n);
}

/* Monta a fita: Program (autostart 10) + BASIC + cabecalho Bytes + dados. */
static int build_vtape(const Buf *text, const char *name, uint16_t start,
                       const uint8_t *data, size_t dlen, uint8_t **out, size_t *outlen)
{
    Buf prog = { NULL, 0, 0, 0 }, tap = { NULL, 0, 0, 0 };
    if (text->fail) return -1;
    basic_line10(&prog, text);
    if (prog.fail) { free(prog.p); return -1; }
    tap_header(&tap, 0, name, (uint16_t)prog.n, 10, (uint16_t)prog.n);
    tap_block(&tap, 0xFF, prog.p, prog.n);
    tap_header(&tap, 3, name, (uint16_t)dlen, start, 32768);
    tap_block(&tap, 0xFF, data, dlen);
    free(prog.p);
    if (tap.fail) { free(tap.p); return -1; }
    *out = tap.p;
    *outlen = tap.n;
    return 0;
}

int vtape_from_snapshot(const Snapshot *s, const char *name, uint8_t **out, size_t *outlen)
{
    static const uint8_t line[] = { 0xEF, 0x22, 0x22, 0xAF, 0x0D };   /* LOAD ""CODE */
    Buf text = { NULL, 0, 0, 0 };
    int r;
    if (!s || !out || !outlen) return -1;
    buf_put(&text, line, sizeof line);
    r = build_vtape(&text, name, 16384, s->ram, 49152, out, outlen);
    free(text.p);
    return r;
}

int vtape_from_binary(const uint8_t *data, size_t len, uint16_t org, uint16_t exec,
                      const char *name, uint8_t **out, size_t *outlen)
{
    static const uint8_t load[] = { 0xEF, 0x22, 0x22, 0xAF };         /* LOAD ""CODE */
    Buf text = { NULL, 0, 0, 0 };
    int r;
    if (!data || !out || !outlen || len == 0) return -1;
    if (org < 16384 || (size_t)org + len > 65536) return -1;
    /* CLEAR org-1 so quando a ROM aceita (RAMTOP acima da area BASIC);
     * para codigo abaixo disso (ex.: buffer da impressora) CLEAR daria
     * "M RAMTOP no good", entao ele e omitido. */
    if (org - 1 >= 24000) {
        buf_byte(&text, 0xFD);                                         /* CLEAR */
        basic_number(&text, (unsigned)org - 1);
        buf_byte(&text, ':');
    }
    buf_put(&text, load, sizeof load);
    buf_byte(&text, ':');
    buf_byte(&text, 0xF9);                                             /* RANDOMIZE */
    buf_byte(&text, 0xC0);                                             /* USR */
    basic_number(&text, exec);
    buf_byte(&text, 0x0D);
    r = build_vtape(&text, name, org, data, len, out, outlen);
    free(text.p);
    return r;
}

int vtape_from_screen(const uint8_t *scr, size_t len, const char *name,
                      uint8_t **out, size_t *outlen)
{
    static const uint8_t line[] = { 0xEF, 0x22, 0x22, 0xAA, ':', 0xF2 }; /* LOAD ""SCREEN$: PAUSE */
    uint8_t img[6912];
    Buf text = { NULL, 0, 0, 0 };
    int r;
    if (!scr || !out || !outlen) return -1;
    if (len == 6912) {
        memcpy(img, scr, 6912);
    } else if (len == 6144) {
        /* so o bitmap: atributos padrao (papel branco, tinta preta) */
        memcpy(img, scr, 6144);
        memset(img + 6144, 0x38, 768);
    } else {
        return -1;
    }
    buf_put(&text, line, sizeof line);
    basic_number(&text, 0);
    buf_byte(&text, 0x0D);
    r = build_vtape(&text, name, 16384, img, 6912, out, outlen);
    free(text.p);
    return r;
}
