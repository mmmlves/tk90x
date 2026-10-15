/*
 * snapshot_test.c - Testes de src/snapshot.c: carga de SNA/Z80, ida e volta
 * (gravar/ler), compressao Z80, fitas virtuais (TAP e BASIC tokenizado) e
 * fuzz (truncamentos / bits trocados).
 *
 * Uso: snapshot_test <dir_tests_data> [dir_saida]   (dir_saida padrao: build)
 *
 * Inclui snapshot.c direto para testar as funcoes estaticas de compressao.
 */
#include "snapshot.c"
#include "tape.h"

#include <stdio.h>

static int g_fail = 0, g_ok = 0;

#define CHECK(cond, ...) do { \
    if (cond) g_ok++; \
    else { g_fail++; printf("  FALHA (%s:%d): ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* PRNG deterministico */
static uint32_t rng_state = 0x12345678u;
static uint32_t rnd(void)
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static void join(char *out, size_t n, const char *dir, const char *file)
{
    snprintf(out, n, "%s/%s", dir, file);
}

static void print_snap(const char *label, const Snapshot *s)
{
    printf("  %-24s PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n"
           "  %-24s AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X R=%02X IFF=%d/%d IM%d border=%d T=%lu\n",
           label, s->pc, s->sp, s->af, s->bc, s->de, s->hl, s->ix, s->iy,
           "", s->af_, s->bc_, s->de_, s->hl_, s->i, s->r, s->iff1, s->iff2, s->im, s->border,
           (unsigned long)s->tstates);
}

/* Compara registradores (sem RAM); devolve 0 se iguais */
static int regs_differ(const Snapshot *a, const Snapshot *b, int check_extra)
{
    if (a->af != b->af || a->bc != b->bc || a->de != b->de || a->hl != b->hl) return 1;
    if (a->af_ != b->af_ || a->bc_ != b->bc_ || a->de_ != b->de_ || a->hl_ != b->hl_) return 1;
    if (a->ix != b->ix || a->iy != b->iy || a->sp != b->sp || a->pc != b->pc) return 1;
    if (a->i != b->i || a->r != b->r || a->iff1 != b->iff1 || a->iff2 != b->iff2) return 1;
    if (a->im != b->im || a->border != b->border) return 1;
    if (check_extra && (a->tstates != b->tstates || a->issue2 != b->issue2 ||
                        a->joystick != b->joystick)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 1. Arquivos de exemplo                                              */

static void test_sample_files(const char *dir)
{
    static const char *files[] = { "int_skip.sna", "int_skip_v1.z80", "int_skip_v3.z80",
                                   "libspectrum_empty.z80" };
    Snapshot *s = (Snapshot *)malloc(sizeof(Snapshot) * 4);
    char path[1024], err[256];
    int i;
    printf("[1] Carga dos arquivos de exemplo\n");
    for (i = 0; i < 4; i++) {
        join(path, sizeof path, dir, files[i]);
        int r = snapshot_load_file(path, &s[i], err, sizeof err);
        CHECK(r == 0, "%s: %s", files[i], err);
        if (r == 0) print_snap(files[i], &s[i]);
    }
    /* int_skip: PC=0x9000 SP=0x8FFC IM1 DI border 7, mesma RAM nos tres */
    for (i = 0; i < 3; i++) {
        CHECK(s[i].pc == 0x9000 && s[i].sp == 0x8FFC && s[i].im == 1 && s[i].iff1 == 0 &&
              s[i].iff2 == 0 && s[i].border == 7, "%s: registros inesperados", files[i]);
        CHECK(memcmp(s[i].ram, s[0].ram, 49152) == 0, "%s: RAM difere do SNA", files[i]);
        CHECK(regs_differ(&s[i], &s[0], 0) == 0, "%s: registros diferem do SNA", files[i]);
    }
    /* v3 com contador 0/0: t = 2*Q - 1 (formula do Fuse) */
    CHECK(s[2].tstates == 2 * 17472 - 1, "int_skip_v3 tstates=%lu", (unsigned long)s[2].tstates);
    CHECK(s[0].tstates == 0 && s[1].tstates == 0, "SNA/v1 tstates != 0");
    /* libspectrum_empty: PC=0x1234 SP=0x8000, contador lo=0xDF hi=2 -> 69664 */
    CHECK(s[3].pc == 0x1234 && s[3].sp == 0x8000, "empty.z80: PC/SP");
    CHECK(s[3].tstates == 69664, "empty.z80: tstates=%lu", (unsigned long)s[3].tstates);
    free(s);
}

/* ------------------------------------------------------------------ */
/* 2. Vetores do formats.md e casos de borda de carga                  */

static void test_vectors(void)
{
    uint8_t *f = (uint8_t *)calloc(1, 200000);
    Snapshot *s = (Snapshot *)malloc(sizeof *s);
    char err[256];
    int r;
    printf("[2] Vetores de teste e casos de borda\n");

    /* SNA (formats.md 6.2) */
    {
        static const uint8_t h[27] = { 0x3F,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                       0x04,0x2A,0x44,0x00,0x40,0xFF,0x01,0x07 };
        memcpy(f, h, 27);
        f[0xBF5B] = 0x34; f[0xBF5C] = 0x12;
        r = snapshot_load_sna(f, SNA_SIZE, s, err, sizeof err);
        CHECK(r == 0 && s->i == 0x3F && s->iff1 == 1 && s->iff2 == 1 && s->r == 0x2A &&
              s->af == 0x0044 && s->sp == 0xFF42 && s->pc == 0x1234 && s->im == 1 &&
              s->border == 7 && s->memptr == 0x1234, "vetor SNA (%s)", err);
        /* SP=0xFFFE: PC em 0xFFFE/0xFFFF, SP volta para 0 */
        f[0x17] = 0xFE; f[0x18] = 0xFF;
        f[SNA_HDR + 0xBFFE] = 0xCD; f[SNA_HDR + 0xBFFF] = 0xAB;
        r = snapshot_load_sna(f, SNA_SIZE, s, err, sizeof err);
        CHECK(r == 0 && s->pc == 0xABCD && s->sp == 0x0000, "SNA SP=FFFE");
        /* SP na ROM ou 0xFFFF: recusa */
        f[0x17] = 0xFF; f[0x18] = 0x3F;
        r = snapshot_load_sna(f, SNA_SIZE, s, err, sizeof err);
        CHECK(r == -1, "SNA SP=3FFF aceito");
        printf("  SP=0x3FFF -> \"%s\"\n", err);
        f[0x17] = 0xFF; f[0x18] = 0xFF;
        CHECK(snapshot_load_sna(f, SNA_SIZE, s, err, sizeof err) == -1, "SNA SP=FFFF aceito");
        f[0x17] = 0x00; f[0x18] = 0x00;
        CHECK(snapshot_load_sna(f, SNA_SIZE, s, err, sizeof err) == -1, "SNA SP=0000 aceito");
        f[0x17] = 0x00; f[0x18] = 0x40;
        CHECK(snapshot_load_sna(f, SNA_SIZE, s, err, sizeof err) == 0, "SNA SP=4000 recusado");
        CHECK(snapshot_load_sna(f, 131103, s, err, sizeof err) == -1, "SNA 128K aceito");
        printf("  SNA 128K -> \"%s\"\n", err);
        CHECK(snapshot_load_sna(f, SNA_SIZE - 1, s, err, sizeof err) == -1, "SNA curto aceito");
    }

    /* Z80 v1 (formats.md 7.2): cabecalho + RAM comprimida + 00 ED ED 00 */
    {
        static const uint8_t h[30] = { 0x44,0x00,0x00,0x00,0x00,0x5C,0x00,0x80,0x40,0xFF,
                                       0x3F,0x05,0x25,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1 };
        size_t n = 30, k;
        memset(f, 0, 200000);
        memcpy(f, h, 30);
        for (k = 0; k < 192; k++) { f[n++] = 0xED; f[n++] = 0xED; f[n++] = 0xFF; f[n++] = 0x00; }
        f[n++] = 0xED; f[n++] = 0xED; f[n++] = 192; f[n++] = 0x77;     /* 192*255+192 = 49152 */
        f[n++] = 0x00; f[n++] = 0xED; f[n++] = 0xED; f[n++] = 0x00;
        r = snapshot_load_z80(f, n, s, err, sizeof err);
        CHECK(r == 0 && s->af == 0x4400 && s->hl == 0x5C00 && s->pc == 0x8000 &&
              s->sp == 0xFF40 && s->i == 0x3F && s->r == 0x85 && s->border == 2 &&
              s->iff1 == 1 && s->iff2 == 1 && s->im == 1 && s->ram[49151] == 0x77 &&
              s->ram[49152 - 193] == 0x00 && s->ram[49152 - 192] == 0x77,
              "vetor Z80 v1 (%s)", err);
        /* faltando dados -> erro */
        CHECK(snapshot_load_z80(f, n - 8, s, err, sizeof err) == -1, "Z80 v1 truncado aceito");
        /* byte 12 = 255 -> 1: R bit 7 = 1, borda 0, sem compressao */
        memset(f, 0, 200000);
        memcpy(f, h, 30);
        f[12] = 0xFF;
        f[30 + 100] = 0x99;
        r = snapshot_load_z80(f, 30 + 49152, s, err, sizeof err);
        CHECK(r == 0 && s->r == 0x85 && s->border == 0 && s->ram[100] == 0x99,
              "Z80 byte12=255 (%s)", err);
    }

    /* Z80 v2/v3: modos de hardware */
    {
        struct { unsigned extra; int hw; int ok; } cases[] = {
            { 23, 0, 1 }, { 23, 1, 1 }, { 23, 2, 1 }, { 23, 3, 0 }, { 23, 4, 0 },
            { 54, 0, 1 }, { 54, 3, 1 }, { 54, 4, 0 }, { 55, 5, 0 }, { 54, 6, 0 },
            { 54, 7, 0 }, { 54, 9, 0 }, { 54, 12, 0 }, { 54, 14, 0 }, { 54, 128, 0 },
            { 54, 77, 0 }, { 30, 0, 0 }
        };
        size_t c;
        for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
            size_t n, k;
            memset(f, 0, 200000);
            wr16(f + 8, 0x8000);
            wr16(f + 30, (uint16_t)cases[c].extra);
            wr16(f + 32, 0x1234);
            f[34] = (uint8_t)cases[c].hw;
            n = 32 + cases[c].extra;
            /* pagina 8 do formats.md: 04 01 08 + (ED ED FF 00)x64 + ED ED 40 00 */
            f[n++] = 0x04; f[n++] = 0x01; f[n++] = 0x08;
            for (k = 0; k < 64; k++) { f[n++] = 0xED; f[n++] = 0xED; f[n++] = 0xFF; f[n++] = 0; }
            f[n++] = 0xED; f[n++] = 0xED; f[n++] = 0x40; f[n++] = 0;
            /* pagina 5 crua (0xFFFF) */
            f[n++] = 0xFF; f[n++] = 0xFF; f[n++] = 5;
            memset(f + n, 0xA5, 16384); n += 16384;
            r = snapshot_load_z80(f, n, s, err, sizeof err);
            CHECK((r == 0) == cases[c].ok, "Z80 extra=%u hw=%d: r=%d (%s)",
                  cases[c].extra, cases[c].hw, r, err);
            if (r == 0)
                CHECK(s->pc == 0x1234 && s->ram[0] == 0 && s->ram[0x3FFF] == 0 &&
                      s->ram[0x8000] == 0xA5 && s->ram[0xBFFF] == 0xA5 && s->ram[0x4000] == 0,
                      "Z80 paginas extra=%u hw=%d", cases[c].extra, cases[c].hw);
            else if (cases[c].hw == 3 || cases[c].hw == 14)
                printf("  extra=%u hw=%d -> \"%s\"\n", cases[c].extra, cases[c].hw, err);
            if (c == 0) {
                /* pagina repetida -> erro */
                memcpy(f + n, f + 32 + 23, 3 + 260); n += 3 + 260;
                CHECK(snapshot_load_z80(f, n, s, err, sizeof err) == -1, "pagina repetida aceita");
            }
        }
    }

    /* T-states v3: hi=3, low=Q-1 -> 0; hi=0, low=Q-1 -> Q; fora da faixa -> limitado */
    {
        static const struct { unsigned low, hi; uint32_t t; } tv[] = {
            { 17471, 3, 0 }, { 17471, 0, 17472 }, { 0, 2, 69887 }, { 100, 3, 17371 },
            { 65535, 3, 0 }, { 0, 200, 34943 }
        };
        size_t c;
        for (c = 0; c < sizeof tv / sizeof tv[0]; c++) {
            size_t n = 32 + 54;
            memset(f, 0, 200000);
            wr16(f + 30, 54);
            wr16(f + 32, 0x1234);
            wr16(f + 55, (uint16_t)tv[c].low);
            f[57] = (uint8_t)tv[c].hi;
            f[n++] = 0xFF; f[n++] = 0xFF; f[n++] = 8; n += 16384;
            r = snapshot_load_z80(f, n, s, err, sizeof err);
            CHECK(r == 0 && s->tstates == tv[c].t, "tstates low=%u hi=%u -> %lu (esperado %lu)",
                  tv[c].low, tv[c].hi, (unsigned long)s->tstates, (unsigned long)tv[c].t);
        }
    }
    free(f);
    free(s);
}

/* ------------------------------------------------------------------ */
/* 3. Compressao                                                       */

static int roundtrip_compress(const uint8_t *src, size_t n, const char *label)
{
    uint8_t *c = (uint8_t *)malloc(2 * n + 8), *d = (uint8_t *)malloc(n + 8);
    size_t cn, used = 0;
    long dn;
    int ok;
    cn = z80_compress(src, n, c);
    dn = z80_decompress(c, cn, d, n, &used);
    ok = cn <= 2 * n + 4 && dn == (long)n && used == cn && memcmp(src, d, n) == 0;
    if (!ok) printf("  compressao falhou: %s (n=%lu cn=%lu dn=%ld used=%lu)\n", label,
                    (unsigned long)n, (unsigned long)cn, dn, (unsigned long)used);
    free(c); free(d);
    return ok;
}

static int vec_compress(const uint8_t *in, size_t n, const uint8_t *exp, size_t en)
{
    uint8_t c[64];
    size_t cn = z80_compress(in, n, c);
    return cn == en && memcmp(c, exp, en) == 0;
}

static void test_compression(void)
{
    uint8_t *b = (uint8_t *)malloc(16384);
    int i, k, cnt = 0, okc = 0;
    printf("[3] Compressao Z80\n");
    {
        static const uint8_t i1[] = { 0xED,0,0,0,0,0,0 }, e1[] = { 0xED,0,0xED,0xED,5,0 };
        static const uint8_t i2[] = { 0xED,0xED }, e2[] = { 0xED,0xED,2,0xED };
        static const uint8_t i3[] = { 1,2,3,3,3,3,3,3,4 }, e3[] = { 1,2,0xED,0xED,6,3,4 };
        static const uint8_t i4[] = { 0,0,0,0 };
        static const uint8_t i5[] = { 0xED,0xED,0xED,0xED,0xED,0xED }, e5[] = { 0xED,0xED,6,0xED };
        static const uint8_t i6[] = { 7,0xED }, e6[] = { 7,0xED };
        static const uint8_t i7[] = { 0xED,1,1,1,1,1,1,0xED }, e7[] = { 0xED,1,0xED,0xED,5,1,0xED };
        CHECK(vec_compress(i1, 7, e1, 6), "vetor ED 00x6");
        CHECK(vec_compress(i2, 2, e2, 4), "vetor ED ED");
        CHECK(vec_compress(i3, 9, e3, 7), "vetor 03x6");
        CHECK(vec_compress(i4, 4, i4, 4), "vetor 00x4");
        CHECK(vec_compress(i5, 6, e5, 4), "vetor EDx6");
        CHECK(vec_compress(i6, 2, e6, 2), "vetor ED no fim");
        CHECK(vec_compress(i7, 8, e7, 7), "vetor ED + run + ED");
    }
    /* padroes adversarios */
    memset(b, 0xED, 16384);                       cnt++; okc += roundtrip_compress(b, 16384, "todo ED");
    for (i = 0; i < 16384; i++) b[i] = (i % 3 == 2) ? 0 : 0xED;
    cnt++; okc += roundtrip_compress(b, 16384, "ED ED 00");
    for (i = 0; i < 16384; i++) b[i] = (i % 2) ? 0 : 0xED;
    cnt++; okc += roundtrip_compress(b, 16384, "ED 00");
    for (i = 0; i < 16384; i++) b[i] = (i % 8 == 0) ? 0xED : 0x11;
    cnt++; okc += roundtrip_compress(b, 16384, "ED isolado + 7 repeticoes");
    for (i = 0; i < 16384; i++) b[i] = (i % 7 == 0) ? 0xED : (i % 7 == 1) ? 0x01 : 0xED;
    cnt++; okc += roundtrip_compress(b, 16384, "ED 01 ED ED ED ED ED");
    for (i = 0; i < 16384; i++) b[i] = (i % 6 < 1) ? 0xED : 0x00;
    cnt++; okc += roundtrip_compress(b, 16384, "ED 00x5");
    memset(b, 0, 16384);                          cnt++; okc += roundtrip_compress(b, 16384, "zeros");
    for (i = 0; i < 16384; i++) b[i] = (uint8_t)i;
    cnt++; okc += roundtrip_compress(b, 16384, "rampa");
    /* runs de todos os comprimentos 1..600 de ED / 00 / outros, alternando */
    for (k = 1; k <= 600; k++) {
        static const uint8_t vals[] = { 0xED, 0x00, 0x42 };
        int v, pos = 0;
        for (v = 0; v < 3; v++) {
            int nrun = 0;
            pos = 0;
            while (pos < 16384) {
                int len = (nrun++ % 2) ? k : 1 + (nrun % 3);
                uint8_t val = (nrun % 2) ? vals[v] : (uint8_t)(nrun % 5 == 0 ? 0xED : nrun);
                while (len-- && pos < 16384) b[pos++] = val;
            }
            cnt++; okc += roundtrip_compress(b, 16384, "runs");
        }
    }
    /* aleatorio com muito ED, tamanhos variados (inclui fim em ED / ED ED) */
    for (k = 0; k < 4000; k++) {
        size_t n = 1 + rnd() % 2000;
        size_t j;
        for (j = 0; j < n; j++) {
            uint32_t r = rnd() % 10;
            b[j] = r < 5 ? 0xED : r < 8 ? 0x00 : (uint8_t)rnd();
        }
        cnt++; okc += roundtrip_compress(b, n, "aleatorio");
    }
    CHECK(okc == cnt, "compressao: %d de %d padroes ok", okc, cnt);
    printf("  %d padroes de compressao ida-e-volta ok\n", okc);

    /* decodificador: entradas corrompidas nao estouram */
    {
        static const uint8_t bad1[] = { 1, 2, 0xED, 0xED, 9 };     /* ED ED truncado */
        static const uint8_t bad2[] = { 0xED, 0xED, 0xFF, 1 };     /* estoura dst de 10 */
        uint8_t d[10];
        CHECK(z80_decompress(bad1, sizeof bad1, d, 10, NULL) == -1, "ED ED truncado");
        CHECK(z80_decompress(bad2, sizeof bad2, d, 10, NULL) == -1, "estouro de dst");
    }
    free(b);
}

/* ------------------------------------------------------------------ */
/* 4. Gravar e ler de volta                                            */

static void random_snap(Snapshot *s, int pattern)
{
    size_t i;
    memset(s, 0, sizeof *s);
    s->af = (uint16_t)rnd(); s->bc = (uint16_t)rnd(); s->de = (uint16_t)rnd(); s->hl = (uint16_t)rnd();
    s->af_ = (uint16_t)rnd(); s->bc_ = (uint16_t)rnd(); s->de_ = (uint16_t)rnd(); s->hl_ = (uint16_t)rnd();
    s->ix = (uint16_t)rnd(); s->iy = (uint16_t)rnd();
    s->sp = (uint16_t)(0x4002 + rnd() % (0x10000 - 0x4002));
    s->pc = (uint16_t)rnd();
    s->memptr = s->pc;
    s->i = (uint8_t)rnd(); s->r = (uint8_t)rnd();
    s->iff1 = s->iff2 = (uint8_t)(rnd() & 1);
    s->im = (uint8_t)(rnd() % 3);
    s->border = (uint8_t)(rnd() & 7);
    s->tstates = rnd() % 69888;
    s->issue2 = (uint8_t)(rnd() & 1);
    s->joystick = (uint8_t)(rnd() & 3);
    for (i = 0; i < 49152; i++) {
        switch (pattern) {
        case 0: s->ram[i] = (uint8_t)rnd(); break;
        case 1: s->ram[i] = (rnd() % 4) ? 0xED : 0; break;
        case 2: s->ram[i] = (uint8_t)((i / 7) & 1 ? 0xED : i >> 9); break;
        default: s->ram[i] = 0xED; break;
        }
    }
}

static void test_roundtrip(const char *dir, const char *outdir)
{
    Snapshot *a = (Snapshot *)malloc(sizeof *a), *b = (Snapshot *)malloc(sizeof *b);
    char path_z80[1024], path_sna[1024], path_bad[1024], err[256], path[1024];
    int it, ok_z = 0, ok_s = 0, n = 0;
    printf("[4] Ida e volta (gravar/ler)\n");
    join(path_z80, sizeof path_z80, outdir, "snapshot_test_tmp.z80");
    join(path_sna, sizeof path_sna, outdir, "snapshot_test_tmp.SNA");
    join(path_bad, sizeof path_bad, outdir, "snapshot_test_tmp.xyz");

    /* a partir do SNA de exemplo */
    join(path, sizeof path, dir, "int_skip.sna");
    if (snapshot_load_file(path, a, err, sizeof err) == 0) {
        CHECK(snapshot_save_file(path_z80, a, err, sizeof err) == 0, "gravar z80: %s", err);
        CHECK(snapshot_load_file(path_z80, b, err, sizeof err) == 0, "ler z80: %s", err);
        CHECK(regs_differ(a, b, 1) == 0 && memcmp(a->ram, b->ram, 49152) == 0, "int_skip -> z80");
        CHECK(snapshot_save_file(path_sna, a, err, sizeof err) == 0, "gravar sna: %s", err);
        {
            size_t len = 0;
            uint8_t *orig = tk_read_file(path, &len, 0), *mine;
            size_t len2 = 0;
            mine = tk_read_file(path_sna, &len2, 0);
            CHECK(orig && mine && len == len2 && memcmp(orig, mine, len) == 0,
                  "SNA regravado nao e identico ao original");
            free(orig); free(mine);
        }
    }

    for (it = 0; it < 64; it++) {
        random_snap(a, it % 4);
        n++;
        /* Z80: tudo deve voltar igual */
        if (snapshot_save_file(path_z80, a, err, sizeof err) == 0 &&
            snapshot_load_file(path_z80, b, err, sizeof err) == 0 &&
            regs_differ(a, b, 1) == 0 && memcmp(a->ram, b->ram, 49152) == 0 &&
            b->memptr == a->pc && b->halted == 0)
            ok_z++;
        else printf("  z80 ida-e-volta %d falhou (%s)\n", it, err);
        /* SNA: perde tstates/issue2/joystick; RAM ganha o PC empilhado */
        if (snapshot_save_file(path_sna, a, err, sizeof err) == 0 &&
            snapshot_load_file(path_sna, b, err, sizeof err) == 0) {
            uint16_t sp2 = (uint16_t)(a->sp - 2);
            uint8_t lo = a->ram[sp2 - 0x4000], hi = a->ram[sp2 + 1 - 0x4000];
            int same;
            a->ram[sp2 - 0x4000] = (uint8_t)a->pc;
            a->ram[sp2 + 1 - 0x4000] = (uint8_t)(a->pc >> 8);
            same = regs_differ(a, b, 0) == 0 && memcmp(a->ram, b->ram, 49152) == 0;
            a->ram[sp2 - 0x4000] = lo; a->ram[sp2 + 1 - 0x4000] = hi;
            if (same) ok_s++;
            else printf("  sna ida-e-volta %d falhou\n", it);
        } else printf("  sna ida-e-volta %d falhou (%s)\n", it, err);
    }
    CHECK(ok_z == n, "z80: %d/%d", ok_z, n);
    CHECK(ok_s == n, "sna: %d/%d", ok_s, n);
    printf("  %d/%d ida-e-volta .z80 ok, %d/%d .sna ok\n", ok_z, n, ok_s, n);

    /* SNA com SP de borda */
    random_snap(a, 0);
    a->sp = 0x0000;                                  /* PC vai para 0xFFFE/0xFFFF */
    CHECK(snapshot_save_file(path_sna, a, err, sizeof err) == 0 &&
          snapshot_load_file(path_sna, b, err, sizeof err) == 0 && b->sp == 0 && b->pc == a->pc,
          "SNA SP=0000 (%s)", err);
    a->sp = 0x4002;
    CHECK(snapshot_save_file(path_sna, a, err, sizeof err) == 0 &&
          snapshot_load_file(path_sna, b, err, sizeof err) == 0 && b->sp == 0x4002 && b->pc == a->pc,
          "SNA SP=4002 (%s)", err);
    a->sp = 0x4001;
    CHECK(snapshot_save_file(path_sna, a, err, sizeof err) == -1, "SNA SP=4001 gravado");
    printf("  SP=0x4001 -> \"%s\"\n", err);
    a->sp = 0x0001;
    CHECK(snapshot_save_file(path_sna, a, err, sizeof err) == -1, "SNA SP=0001 gravado");
    /* .z80 aceita qualquer SP; tstates >= 69888 e limitado */
    a->sp = 0x0001; a->tstates = 71000;
    CHECK(snapshot_save_file(path_z80, a, err, sizeof err) == 0 &&
          snapshot_load_file(path_z80, b, err, sizeof err) == 0 && b->sp == 1 &&
          b->tstates == 69887, "Z80 SP=0001 / tstates limitado (%lu)", (unsigned long)b->tstates);
    CHECK(snapshot_save_file(path_bad, a, err, sizeof err) == -1, "extensao desconhecida gravada");
    /* cabecalho v3: ROM em 0-16383 (bytes 61/62) e Sinclair 2 com o mapa da IF2
     * esquerda (bytes 63-82), como o libspectrum espera */
    {
        static const uint16_t map[5] = { 0x0F03, 0x0803, 0x0403, 0x0203, 0x0103 };
        size_t zl = 0;
        uint8_t *z;
        int k, ok;
        a->joystick = 2;
        z = build_z80(a, &zl, err, sizeof err);
        ok = z && z[61] == 0xFF && z[62] == 0xFF && (z[29] >> 6) == 2;
        for (k = 0; ok && k < 5; k++)
            ok = (z[63 + 2 * k] | (z[64 + 2 * k] << 8)) == map[k] &&
                 z[73 + 2 * k] == '1' + k && z[74 + 2 * k] == 0;
        CHECK(ok, "Z80 v3: bytes 61/62 ou mapa do Sinclair 2 errados");
        free(z);
        a->joystick = 1;
        z = build_z80(a, &zl, err, sizeof err);
        ok = z && z[61] == 0xFF && z[62] == 0xFF;
        for (k = 63; ok && k < 83; k++) ok = z[k] == 0;
        CHECK(ok, "Z80 v3 Kempston: bytes 61-82 errados");
        free(z);
        CHECK(snapshot_save_file(path_z80, a, err, sizeof err) == 0 &&
              snapshot_load_file(path_z80, b, err, sizeof err) == 0 && b->joystick == 1,
              "Z80 Kempston ida e volta");
    }
    CHECK(snapshot_format_from_name("A.Z80") == SNAP_Z80 && snapshot_format_from_name("x.sNa") == SNAP_SNA &&
          snapshot_format_from_name("dir.z80/x") == SNAP_UNKNOWN &&
          snapshot_format_from_name("x.tap") == SNAP_UNKNOWN, "snapshot_format_from_name");
    remove(path_z80);
    remove(path_sna);
    free(a); free(b);
}

/* ------------------------------------------------------------------ */
/* 5. Fita virtual                                                     */

/* Valida a cadeia TAP e devolve o numero de blocos (-1 se invalida).
 * blk[i]/blen[i] apontam para flag..checksum. */
static int parse_tap(const uint8_t *t, size_t n, const uint8_t **blk, size_t *blen, int max)
{
    size_t pos = 0;
    int nb = 0;
    while (pos < n) {
        size_t len, i;
        uint8_t x = 0;
        if (pos + 2 > n) return -1;
        len = rd16(t + pos);
        pos += 2;
        if (len < 2 || pos + len > n || nb >= max) return -1;
        for (i = 0; i < len; i++) x ^= t[pos + i];
        if (x != 0) return -1;
        blk[nb] = t + pos;
        blen[nb] = len;
        nb++;
        pos += len;
    }
    return nb;
}

static int hex_eq(const uint8_t *got, size_t gn, const uint8_t *exp, size_t en, const char *label)
{
    size_t i;
    if (gn == en && memcmp(got, exp, en) == 0) return 1;
    printf("  %s: esperado (%lu):", label, (unsigned long)en);
    for (i = 0; i < en; i++) printf(" %02X", exp[i]);
    printf("\n  %s: obtido   (%lu):", label, (unsigned long)gn);
    for (i = 0; i < gn; i++) printf(" %02X", got[i]);
    printf("\n");
    return 0;
}

/* Confere uma fita de 4 blocos: cabecalhos, BASIC byte a byte e dados. */
static void check_vtape(const char *label, const uint8_t *tap, size_t n,
                        const uint8_t *exp_name, const uint8_t *exp_prog, size_t prog_len,
                        uint16_t start, const uint8_t *data, size_t dlen)
{
    const uint8_t *blk[8];
    size_t blen[8];
    uint8_t h0[19], h2[19];
    int nb = parse_tap(tap, n, blk, blen, 8);
    Tape *t;
    char err[256];

    CHECK(nb == 4, "%s: TAP com %d blocos", label, nb);
    if (nb != 4) return;
    /* cabecalho Program: tipo 0, nome, len, autostart 10, vars = len */
    h0[0] = 0x00; h0[1] = 0x00; memcpy(h0 + 2, exp_name, 10);
    wr16(h0 + 12, (uint16_t)prog_len); wr16(h0 + 14, 10); wr16(h0 + 16, (uint16_t)prog_len);
    h0[18] = 0; { int i; for (i = 0; i < 18; i++) h0[18] ^= h0[i]; }
    CHECK(hex_eq(blk[0], blen[0], h0, 19, "cabecalho Program"), "%s: cabecalho Program", label);
    /* BASIC */
    CHECK(blen[1] == prog_len + 2 && blk[1][0] == 0xFF &&
          hex_eq(blk[1] + 1, blen[1] - 2, exp_prog, prog_len, "BASIC"), "%s: BASIC", label);
    /* cabecalho Bytes */
    h2[0] = 0x00; h2[1] = 0x03; memcpy(h2 + 2, exp_name, 10);
    wr16(h2 + 12, (uint16_t)dlen); wr16(h2 + 14, start); wr16(h2 + 16, 32768);
    h2[18] = 0; { int i; for (i = 0; i < 18; i++) h2[18] ^= h2[i]; }
    CHECK(hex_eq(blk[2], blen[2], h2, 19, "cabecalho Bytes"), "%s: cabecalho Bytes", label);
    /* dados */
    CHECK(blen[3] == dlen + 2 && blk[3][0] == 0xFF && memcmp(blk[3] + 1, data, dlen) == 0,
          "%s: bloco de dados", label);

    /* o gravador (tape.c) tambem precisa aceitar a fita */
    t = tape_new();
    CHECK(t != NULL, "tape_new");
    if (t) {
        int r = tape_load_tap_mem(t, tap, n, err, sizeof err), i;
        CHECK(r == 0 && tape_block_count(t) == 4, "%s: tape_load_tap_mem r=%d blocos=%d (%s)",
              label, r, tape_block_count(t), err);
        if (r == 0) {
            printf("  %s:", label);
            for (i = 0; i < tape_block_count(t); i++) printf(" [%s]", tape_block_desc(t, i));
            printf("\n");
            CHECK(strncmp(tape_block_desc(t, 0), "Program:", 8) == 0 &&
                  strncmp(tape_block_desc(t, 2), "Bytes:", 6) == 0, "%s: descricoes", label);
        }
        tape_free(t);
    }
}

static void test_vtape(void)
{
    Snapshot *s = (Snapshot *)malloc(sizeof *s);
    uint8_t *tap = NULL, *bin = (uint8_t *)malloc(49152), scr[6912];
    size_t n = 0, i;
    printf("[5] Fita virtual\n");

    random_snap(s, 0);
    /* 10 LOAD ""CODE */
    {
        static const uint8_t prog[] = { 0x00,0x0A, 0x05,0x00, 0xEF,0x22,0x22,0xAF,0x0D };
        CHECK(vtape_from_snapshot(s, "TESTE", &tap, &n) == 0, "vtape_from_snapshot");
        check_vtape("snapshot", tap, n, (const uint8_t *)"TESTE     ", prog, sizeof prog,
                    16384, s->ram, 49152);
        CHECK(n == (2 + 19) + (2 + 11) + (2 + 19) + (2 + 49154), "tamanho da fita do snapshot");
        free(tap); tap = NULL;
    }
    /* nomes: truncar, '?', UTF-8 */
    {
        const uint8_t *blk[8]; size_t blen[8];
        CHECK(vtape_from_snapshot(s, "Ac\xC3\xA3o demo!longo", &tap, &n) == 0, "vtape nome UTF-8");
        CHECK(parse_tap(tap, n, blk, blen, 8) == 4 && memcmp(blk[0] + 2, "Ac?o demo!", 10) == 0,
              "nome UTF-8 -> \"Ac?o demo!\"");
        free(tap); tap = NULL;
        CHECK(vtape_from_snapshot(s, "\x01" "a\x7F", &tap, &n) == 0, "vtape nome controle");
        CHECK(parse_tap(tap, n, blk, blen, 8) == 4 && memcmp(blk[0] + 2, "?a?       ", 10) == 0,
              "nome com controle");
        free(tap); tap = NULL;
        CHECK(vtape_from_snapshot(s, NULL, &tap, &n) == 0, "vtape nome NULL");
        CHECK(parse_tap(tap, n, blk, blen, 8) == 4 && memcmp(blk[2] + 2, "          ", 10) == 0,
              "nome NULL -> espacos");
        free(tap); tap = NULL;
    }

    for (i = 0; i < 49152; i++) bin[i] = (uint8_t)(i * 7);
    /* 10 CLEAR 32767: LOAD ""CODE: RANDOMIZE USR 32768 */
    {
        static const uint8_t prog[] = {
            0x00,0x0A, 0x20,0x00,
            0xFD, '3','2','7','6','7', 0x0E,0x00,0x00,0xFF,0x7F,0x00, 0x3A,
            0xEF,0x22,0x22,0xAF, 0x3A,
            0xF9,0xC0, '3','2','7','6','8', 0x0E,0x00,0x00,0x00,0x80,0x00, 0x0D };
        CHECK(vtape_from_binary(bin, 1000, 32768, 32768, "jogo.bin", &tap, &n) == 0, "vtape_from_binary");
        check_vtape("binario 32768", tap, n, (const uint8_t *)"jogo.bin  ", prog, sizeof prog,
                    32768, bin, 1000);
        free(tap); tap = NULL;
    }
    /* 10 CLEAR 24999: LOAD ""CODE: RANDOMIZE USR 65535, ocupando ate o fim da RAM */
    {
        static const uint8_t prog[] = {
            0x00,0x0A, 0x20,0x00,
            0xFD, '2','4','9','9','9', 0x0E,0x00,0x00,0xA7,0x61,0x00, 0x3A,
            0xEF,0x22,0x22,0xAF, 0x3A,
            0xF9,0xC0, '6','5','5','3','5', 0x0E,0x00,0x00,0xFF,0xFF,0x00, 0x0D };
        CHECK(vtape_from_binary(bin, 65536 - 25000, 25000, 65535, "X", &tap, &n) == 0, "binario ate 65535");
        check_vtape("binario 25000", tap, n, (const uint8_t *)"X         ", prog, sizeof prog,
                    25000, bin, 65536 - 25000);
        free(tap); tap = NULL;
    }
    /* org baixo (buffer da impressora): sem CLEAR -> 10 LOAD ""CODE: RANDOMIZE USR 23300 */
    {
        static const uint8_t prog[] = {
            0x00,0x0A, 0x13,0x00,
            0xEF,0x22,0x22,0xAF, 0x3A,
            0xF9,0xC0, '2','3','3','0','0', 0x0E,0x00,0x00,0x04,0x5B,0x00, 0x0D };
        CHECK(vtape_from_binary(bin, 200, 23296, 23300, "rotina", &tap, &n) == 0, "binario 23296");
        check_vtape("binario 23296", tap, n, (const uint8_t *)"rotina    ", prog, sizeof prog,
                    23296, bin, 200);
        free(tap); tap = NULL;
    }
    CHECK(vtape_from_binary(bin, 2, 65535, 0, "x", &tap, &n) == -1, "binario passa de 65535");
    CHECK(vtape_from_binary(bin, 10, 16383, 0, "x", &tap, &n) == -1, "binario abaixo de 16384");
    CHECK(vtape_from_binary(bin, 0, 32768, 0, "x", &tap, &n) == -1, "binario vazio");
    CHECK(vtape_from_binary(bin, 49152, 16384, 16384, "x", &tap, &n) == 0, "binario 48K inteiro");
    free(tap); tap = NULL;

    /* 10 LOAD ""SCREEN$: PAUSE 0 */
    for (i = 0; i < 6912; i++) scr[i] = (uint8_t)(i ^ (i >> 8));
    {
        static const uint8_t prog[] = {
            0x00,0x0A, 0x0E,0x00,
            0xEF,0x22,0x22,0xAA, 0x3A, 0xF2, '0', 0x0E,0x00,0x00,0x00,0x00,0x00, 0x0D };
        uint8_t scr2[6912];
        CHECK(vtape_from_screen(scr, 6912, "tela", &tap, &n) == 0, "vtape_from_screen");
        check_vtape("tela", tap, n, (const uint8_t *)"tela      ", prog, sizeof prog,
                    16384, scr, 6912);
        free(tap); tap = NULL;
        memcpy(scr2, scr, 6144);
        memset(scr2 + 6144, 0x38, 768);
        CHECK(vtape_from_screen(scr, 6144, "tela", &tap, &n) == 0, "tela 6144");
        check_vtape("tela 6144", tap, n, (const uint8_t *)"tela      ", prog, sizeof prog,
                    16384, scr2, 6912);
        free(tap); tap = NULL;
        CHECK(vtape_from_screen(scr, 6000, "tela", &tap, &n) == -1, "tela de tamanho errado");
    }
    free(bin);
    free(s);
}

/* ------------------------------------------------------------------ */
/* 6. Fuzz                                                             */

static void fuzz_one(const uint8_t *orig, size_t len, int iters, int *runs)
{
    uint8_t *m = (uint8_t *)malloc(len + 1);
    Snapshot *s = (Snapshot *)malloc(sizeof *s);
    char err[256];
    int it;
    for (it = 0; it < iters; it++) {
        size_t n = len, k, flips;
        memcpy(m, orig, len);
        switch (rnd() % 4) {
        case 0: n = rnd() % (len + 1); break;                        /* truncamento */
        case 1: flips = 1 + rnd() % 8;                                /* bits trocados */
                for (k = 0; k < flips; k++) m[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8));
                break;
        case 2: flips = 1 + rnd() % 8;                                /* bits no cabecalho */
                for (k = 0; k < flips; k++) m[rnd() % (len < 100 ? len : 100)] ^= (uint8_t)(1u << (rnd() % 8));
                n = len - rnd() % (len < 64 ? len : 64);
                break;
        default: flips = 1 + rnd() % 32;                              /* bytes ED / FF */
                 for (k = 0; k < flips; k++) m[rnd() % len] = (rnd() & 1) ? 0xED : 0xFF;
                 if (rnd() & 1) n = rnd() % (len + 1);
                 break;
        }
        (void)snapshot_load_sna(m, n, s, err, sizeof err);
        (void)snapshot_load_z80(m, n, s, err, sizeof err);
        if (n > 7 && (rnd() & 1)) {                                   /* forca v2/v3 */
            m[6] = m[7] = 0;
            if (n > 31) { m[30] = (rnd() & 1) ? 23 : 54; m[31] = 0; }
            if (n > 34) m[34] = (uint8_t)(rnd() % 4);
            (void)snapshot_load_z80(m, n, s, err, sizeof err);
        }
        *runs += 1;
    }
    free(m);
    free(s);
}

static void test_fuzz(const char *dir)
{
    static const char *files[] = { "int_skip.sna", "int_skip_v1.z80", "int_skip_v3.z80",
                                   "libspectrum_empty.z80" };
    char path[1024];
    int i, runs = 0;
    uint8_t *rbuf = (uint8_t *)malloc(4096);
    Snapshot *s = (Snapshot *)malloc(sizeof *s);
    char err[256];
    printf("[6] Fuzz\n");
    for (i = 0; i < 4; i++) {
        size_t len = 0;
        uint8_t *d;
        join(path, sizeof path, dir, files[i]);
        d = tk_read_file(path, &len, 0);
        CHECK(d != NULL, "ler %s", path);
        if (!d) continue;
        fuzz_one(d, len, i == 0 ? 600 : 5000, &runs);
        free(d);
    }
    /* entradas totalmente aleatorias */
    for (i = 0; i < 20000; i++) {
        size_t n = rnd() % 4096, k;
        for (k = 0; k < n; k++) rbuf[k] = (rnd() % 3) ? 0xED : (uint8_t)rnd();
        if (n > 7 && (i & 1)) { rbuf[6] = rbuf[7] = 0; }
        if (n > 31 && (i & 2)) { rbuf[30] = 54; rbuf[31] = 0; rbuf[34] = 0; }
        (void)snapshot_load_z80(rbuf, n, s, err, sizeof err);
        (void)snapshot_load_sna(rbuf, n, s, err, sizeof err);
        runs++;
    }
    printf("  %d casos de fuzz sem falha\n", runs);
    CHECK(1, "fuzz");
    free(rbuf);
    free(s);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/data";
    const char *outdir = argc > 2 ? argv[2] : "build";

    test_sample_files(dir);
    test_vectors();
    test_compression();
    test_roundtrip(dir, outdir);
    test_vtape();
    test_fuzz(dir);

    printf("snapshot_test: %d verificacoes ok, %d falhas\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
