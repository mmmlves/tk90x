/*
 * machine_test.c - Testes da maquina (sem SDL):
 *   boot dos 3 modelos x 50/60 Hz (faixa na tela, rom_ready, PNG),
 *   digitacao automatica (linha de edicao LOAD ""), INT por segundo,
 *   carga de fita real e instantanea (dogmole.tap), fita virtual de
 *   snapshot (.sna/.z80), snapshot instantaneo, contencao e barramento flutuante.
 *
 * Uso: machine_test <dir_dados> <dir_saida>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0777)
#endif

#include "machine.h"
#include "profiles.h"
#include "png.h"
#include "compat.h"

static int g_fail, g_pass;
static const char *g_data = "tests/data", *g_out = "build/test-out";

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FALHOU: "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static double now_sec(void)
{
#if defined(CLOCK_MONOTONIC) && !defined(_WIN32)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

static Machine *new_machine(Model model, int hz60, int flash)
{
    static uint8_t rom[16384];
    MachineConfig cfg;
    Machine *m = (Machine *)calloc(1, sizeof(Machine));
    if (!m) { printf("sem memoria\n"); exit(2); }
    memset(&cfg, 0, sizeof cfg);
    cfg.model = model;
    cfg.hz60 = hz60;
    cfg.issue2 = 1;
    cfg.joymode = JOYMODE_SINCLAIR1;
    cfg.flash_load = flash;
    machine_model_rom(model, NULL, rom);
    if (machine_init(m, rom, &cfg) != 0) { printf("machine_init falhou\n"); exit(2); }
    return m;
}

static void del_machine(Machine *m)
{
    machine_free(m);
    free(m);
}

static void out_path(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", g_out, name);
}

static void save_png(const Machine *m, const char *name)
{
    char p[1024];
    out_path(p, sizeof p, name);
    if (png_write_argb(p, m->fb, FB_W, FB_H) != 0) printf("  (nao gravou %s)\n", p);
    else printf("  PNG: %s\n", p);
}

/* Le a linha de texto 'row' (0..23) da tela comparando com a fonte da ROM. */
static void screen_text(const Machine *m, int row, char *out)
{
    for (int col = 0; col < 32; col++) {
        uint8_t cell[8];
        for (int k = 0; k < 8; k++) {
            int y = row * 8 + k;
            cell[k] = m->mem[0x4000 | ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | col];
        }
        char ch = '?';
        for (int c = 32; c < 128; c++) {
            const uint8_t *f = m->rom + 0x3D00 + (c - 32) * 8;
            int eq = 1, inv = 1;
            for (int k = 0; k < 8; k++) {
                if (cell[k] != f[k]) eq = 0;
                if (cell[k] != (uint8_t)~f[k]) inv = 0;
            }
            if (eq || inv) { ch = (char)c; break; }
        }
        out[col] = ch;
    }
    out[32] = 0;
}

static int screen_find(const Machine *m, const char *s, int *row_found)
{
    char line[33];
    for (int r = 0; r < 24; r++) {
        screen_text(m, r, line);
        if (strstr(line, s)) { if (row_found) *row_found = r; return 1; }
    }
    return 0;
}

static double emu_seconds(const Machine *m, uint32_t frames)
{
    return (double)frames * m->frame_tstates / (double)TK_CPU_HZ;
}

/* ------------------------------------------------------------------ */

static void test_boot(Model model, int hz60)
{
    char name[64], line[33];
    Machine *m = new_machine(model, hz60, 0);
    uint32_t ready_frame = 0;
    printf("[boot] %s %d Hz\n", machine_model_name(model), hz60 ? 60 : 50);

    CHECK(m->frame_tstates == (hz60 ? 59736u : 71136u), "frame_tstates %u", m->frame_tstates);
    CHECK(m->line_tstates == 228 && m->frame_lines == (hz60 ? 262 : 312), "linhas %d x %d",
          m->frame_lines, m->line_tstates);

    for (int f = 0; f < 400 && !m->rom_ready; f++) machine_run_frame(m);
    ready_frame = m->frame_count;
    CHECK(m->rom_ready, "rom_ready nao ligou (PC 0x12A9) em 400 quadros");
    printf("  rom_ready no quadro %u (%.2f s)\n", ready_frame, emu_seconds(m, ready_frame));
    for (int f = 0; f < 30; f++) machine_run_frame(m);

    int row = -1;
    int found = screen_find(m, "TK90X - Color Computer", &row) ||
                screen_find(m, "TK Color Computer", &row);
    screen_text(m, row < 0 ? 0 : row, line);
    printf("  linha %d: \"%s\"\n", row, line);
    CHECK(found, "faixa de abertura nao encontrada");
    CHECK(m->border == 7, "borda %d (esperado branca)", m->border);

    /* INT por quadro / por segundo e FRAMES (23672) */
    uint32_t i0 = m->int_count;
    unsigned fr0 = m->mem[23672] | (m->mem[23673] << 8);
    int n = hz60 ? 300 : 250;
    for (int f = 0; f < n; f++) machine_run_frame(m);
    uint32_t ints = m->int_count - i0;
    unsigned fr1 = m->mem[23672] | (m->mem[23673] << 8);
    double rate = ints / emu_seconds(m, (uint32_t)n);
    printf("  %u INT em %d quadros = %.3f INT/s emulado; FRAMES +%u\n", ints, n, rate,
           (fr1 - fr0) & 0xFFFF);
    CHECK(ints == (uint32_t)n, "INT %u != quadros %d", ints, n);
    CHECK(((fr1 - fr0) & 0xFFFF) == (unsigned)n, "FRAMES nao contou 1 por quadro");
    if (hz60) CHECK(rate > 59.8 && rate < 59.9, "taxa 60 Hz %.3f", rate);
    else      CHECK(rate > 50.2 && rate < 50.3, "taxa 50 Hz %.3f", rate);

    snprintf(name, sizeof name, "boot_%s_%d.png",
             model == MODEL_TK95 ? "tk95" : model == MODEL_TK90X ? "tk90x" : "tk90x-v1",
             hz60 ? 60 : 50);
    save_png(m, name);
    del_machine(m);
}

/* Digita J, SYM+P, SYM+P (sem ENTER) e confere a linha de edicao em E_LINE. */
static void test_autotype(Model model, int hz60)
{
    Machine *m = new_machine(model, hz60, 0);
    printf("[autotype] %s %d Hz\n", machine_model_name(model), hz60 ? 60 : 50);
    machine_autoload(m, 0, 0);
    m->autotype.count = 3;     /* sem o ENTER */
    for (int f = 0; f < 600 && m->autotype.active; f++) machine_run_frame(m);
    for (int f = 0; f < 10; f++) machine_run_frame(m);
    unsigned eline = m->mem[23641] | (m->mem[23642] << 8);
    const uint8_t *e = m->mem + eline;
    printf("  E_LINE %04X: %02X %02X %02X %02X %02X\n", eline, e[0], e[1], e[2], e[3], e[4]);
    CHECK(e[0] == 0xEF && e[1] == '"' && e[2] == '"' && e[3] == 0x0D,
          "linha de edicao nao e LOAD \"\"");
    /* agora ENTER: a ROM deve entrar no LD-BYTES esperando a fita */
    machine_key(m, ZXK_ENTER, 1);
    for (int f = 0; f < 3; f++) machine_run_frame(m);
    machine_key(m, ZXK_ENTER, 0);
    for (int f = 0; f < 20; f++) machine_run_frame(m);
    printf("  apos ENTER: PC=%04X\n", m->cpu.pc);
    CHECK(m->cpu.pc >= 0x0556 && m->cpu.pc < 0x0605, "nao esta no LD-BYTES (PC %04X)", m->cpu.pc);
    del_machine(m);
}

static int run_until_loaded(Machine *m, uint32_t max_frames, uint32_t extra, double *wall)
{
    double t0 = now_sec();
    uint32_t f = 0;
    while (f < max_frames) {
        machine_run_frame(m);
        f++;
        if (!m->autotype.active && tape_at_end(m->tape) && !tape_is_playing(m->tape) &&
            !m->pending_snap && m->flash_idle < 0)
            break;
    }
    for (uint32_t i = 0; i < extra; i++) machine_run_frame(m);
    *wall = now_sec() - t0;
    return (int)(f < max_frames);
}

static void test_tape(int flash, int hz60)
{
    char path[1024], err[256], line[33];
    double wall;
    Machine *m = new_machine(MODEL_TK95, hz60, flash);
    printf("[fita] dogmole.tap %s, %d Hz\n", flash ? "carga instantanea" : "pulsos reais", hz60 ? 60 : 50);
    snprintf(path, sizeof path, "%s/dogmole.tap", g_data);
    int r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0, "machine_open_file: %s", err);
    if (r) { del_machine(m); return; }
    int ok = run_until_loaded(m, 20000, 300, &wall);
    uint32_t frames = m->frame_count;
    double es = emu_seconds(m, frames);
    printf("  %u quadros (%.1f s emulados) em %.3f s reais = %.1fx tempo real\n",
           frames, es, wall, wall > 0 ? es / wall : 0.0);
    CHECK(ok, "fita nao terminou em 20000 quadros (bloco %d)", tape_current_block(m->tape));
    int err_found = 0;
    for (int row = 0; row < 24; row++) {
        screen_text(m, row, line);
        if (strstr(line, "Erro") || strstr(line, "rro de leitura")) { err_found = 1; printf("  tela: %s\n", line); }
    }
    CHECK(!err_found, "erro de carga na tela");
    /* o jogo roda fora da ROM e a tela tem varios atributos */
    int attrs[256] = {0}, distinct = 0;
    for (int i = 0; i < 768; i++) if (!attrs[m->mem[0x5800 + i]]++) distinct++;
    printf("  PC=%04X, %d atributos distintos\n", m->cpu.pc, distinct);
    CHECK(distinct >= 4, "tela do jogo parece vazia");
    /* velocidade depende da carga do computador (outros processos, sanitizers):
     * so reprova com TK90X_PERF_CHECK=1; senao apenas avisa */
    if (!flash && !(es / wall >= 50.0 || wall < 0.05)) {
        if (getenv("TK90X_PERF_CHECK")) CHECK(0, "velocidade %.1fx < 50x", es / wall);
        else printf("  aviso: velocidade %.1fx < 50x (computador ocupado?)\n", es / wall);
    }
    save_png(m, flash ? (hz60 ? "dogmole_flash_60.png" : "dogmole_flash.png")
                      : (hz60 ? "dogmole_real_60.png" : "dogmole_real.png"));
    del_machine(m);
}

/* Fita virtual de snapshot: confere registradores e RAM no instante da instalacao. */
typedef struct { const Snapshot *ref; int called, regs_ok, ram_ok; uint32_t frame; } HookCheck;

static void snap_hook(void *user, Machine *m)
{
    HookCheck *h = (HookCheck *)user;
    const Snapshot *s = h->ref;
    const Z80 *c = &m->cpu;
    h->called++;
    h->frame = m->frame_count;
    h->regs_ok = c->af == s->af && c->bc == s->bc && c->de == s->de && c->hl == s->hl &&
                 c->af_ == s->af_ && c->bc_ == s->bc_ && c->de_ == s->de_ && c->hl_ == s->hl_ &&
                 c->ix == s->ix && c->iy == s->iy && c->sp == s->sp && c->pc == s->pc &&
                 c->i == s->i && c->r == s->r && c->iff1 == s->iff1 && c->iff2 == s->iff2 &&
                 c->im == s->im && m->border == s->border;
    h->ram_ok = memcmp(m->mem + 0x4000, s->ram, 49152) == 0;
    if (!h->regs_ok)
        printf("  regs: PC %04X/%04X SP %04X/%04X AF %04X/%04X IM %d/%d IFF %d/%d\n",
               c->pc, s->pc, c->sp, s->sp, c->af, s->af, c->im, s->im, c->iff1, s->iff1);
}

static void test_vtape(const char *file, int flash, const char *png)
{
    char path[1024], err[256];
    double wall;
    Snapshot *ref = (Snapshot *)malloc(sizeof(Snapshot));
    HookCheck h;
    Machine *m = new_machine(MODEL_TK95, 0, flash);
    printf("[fita virtual] %s %s\n", file, flash ? "(carga instantanea)" : "(pulsos reais)");
    snprintf(path, sizeof path, "%s/%s", g_data, file);
    if (snapshot_load_file(path, ref, err, sizeof err) != 0) {
        CHECK(0, "snapshot_load_file: %s", err);
        free(ref); del_machine(m); return;
    }
    memset(&h, 0, sizeof h);
    h.ref = ref;
    m->snap_hook = snap_hook;
    m->snap_hook_user = &h;
    int r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0, "machine_open_file: %s", err);
    if (r) { free(ref); del_machine(m); return; }
    double t0 = now_sec();
    uint32_t f = 0;
    while (!h.called && f < 20000) { machine_run_frame(m); f++; }
    wall = now_sec() - t0;
    printf("  instalado no quadro %u (%.1f s emulados, %.3f s reais)\n", h.frame,
           emu_seconds(m, h.frame), wall);
    CHECK(h.called == 1, "snapshot nao instalado (chamadas %d)", h.called);
    CHECK(h.regs_ok, "registradores diferentes do snapshot");
    CHECK(h.ram_ok, "RAM diferente do snapshot");
    CHECK(!m->pending_snap && !tape_is_playing(m->tape), "fita/pending nao limpos");
    for (int i = 0; i < 250; i++) machine_run_frame(m);
    save_png(m, png);
    free(ref);
    del_machine(m);
}

static void test_instant(const char *file)
{
    char path[1024], err[256];
    Snapshot *ref = (Snapshot *)malloc(sizeof(Snapshot));
    HookCheck h;
    Machine *m = new_machine(MODEL_TK95, 0, 0);
    printf("[snapshot instantaneo] %s\n", file);
    snprintf(path, sizeof path, "%s/%s", g_data, file);
    for (int i = 0; i < 20; i++) machine_run_frame(m);
    CHECK(snapshot_load_file(path, ref, err, sizeof err) == 0, "snapshot_load_file: %s", err);
    int r = machine_open_file(m, path, 0, 0, 1, err, sizeof err);
    CHECK(r == 0, "machine_open_file: %s", err);
    memset(&h, 0, sizeof h);
    h.ref = ref;
    snap_hook(&h, m);
    CHECK(h.regs_ok && h.ram_ok, "estado apos carga instantanea difere");
    CHECK(m->cpu.tstates == ref->tstates % m->frame_tstates, "tstates %u", m->cpu.tstates);
    /* round-trip take_snapshot */
    Snapshot *t = (Snapshot *)malloc(sizeof(Snapshot));
    machine_take_snapshot(m, t);
    CHECK(t->pc == ref->pc && t->sp == ref->sp && !memcmp(t->ram, ref->ram, 49152),
          "machine_take_snapshot difere");
    for (int i = 0; i < 250; i++) machine_run_frame(m);
    save_png(m, "int_skip_instant.png");
    free(t);
    free(ref);
    del_machine(m);
}

/* Snapshot gravado com a CPU parada em HALT (EI: HALT: JR -3 em 0x8000):
 * .Z80/.SNA nao guardam o estado HALT, entao a carga precisa trata-lo como
 * parado; a INT do 1o quadro deve empilhar o endereco seguinte ao HALT. */
static void test_snapshot_halt(void)
{
    char path[1024], err[256];
    Snapshot *s = (Snapshot *)calloc(1, sizeof(Snapshot));
    Machine *m = new_machine(MODEL_TK95, 1, 0);
    printf("[snapshot em HALT]\n");
    s->ram[0x8000 - 0x4000] = 0xFB;      /* EI */
    s->ram[0x8001 - 0x4000] = 0x76;      /* HALT */
    s->ram[0x8002 - 0x4000] = 0x18;      /* JR 0x8000 */
    s->ram[0x8003 - 0x4000] = 0xFC;
    s->pc = 0x8001; s->sp = 0xF000; s->iff1 = s->iff2 = 1; s->im = 1; s->iy = 0x5C3A;
    s->halted = 1; s->tstates = 5;
    out_path(path, sizeof path, "halt.z80");
    CHECK(snapshot_save_file(path, s, err, sizeof err) == 0, "gravar halt.z80: %s", err);
    CHECK(machine_open_file(m, path, 0, 0, 1, err, sizeof err) == 0, "abrir halt.z80: %s", err);
    CHECK(m->cpu.halted == 1 && m->cpu.pc == 0x8001, "HALT nao restaurado (halted %d pc %04X)",
          m->cpu.halted, m->cpu.pc);
    m->mem[0x0038] = 0xC9;               /* (a ROM e copiada em mem; RET direto para o teste) */
    machine_run_frame(m);
    CHECK(m->int_count == 1 && m->mem[0xEFFE] == 0x02 && m->mem[0xEFFF] == 0x80,
          "INT empilhou %02X%02X (esperado 8002)", m->mem[0xEFFF], m->mem[0xEFFE]);
    free(s);
    del_machine(m);
}

/* Contencao: le a tabela e mede uma instrucao em memoria contida. */
static void test_contention(void)
{
    printf("[contencao / barramento flutuante]\n");
    for (int hz = 0; hz < 2; hz++) {
        Machine *m = new_machine(MODEL_TK95, hz, 0);
        uint32_t S = hz ? 8759u : 14687u;
        CHECK(m->display_start == (int)S, "display_start %d", m->display_start);
        CHECK(m->cont_tab[S - 1] == 0 && m->cont_tab[S] == 6 && m->cont_tab[S + 1] == 5 &&
              m->cont_tab[S + 6] == 0 && m->cont_tab[S + 7] == 0 && m->cont_tab[S + 8] == 6 &&
              m->cont_tab[S + 127] == 0 && m->cont_tab[S + 128] == 0 &&
              m->cont_tab[S + 228] == 6 && m->cont_tab[S + 191 * 228 + 120] == 6 &&
              m->cont_tab[S + 192 * 228] == 0, "tabela de contencao (%d Hz)", hz ? 60 : 50);
        /* NOPs em 0x8000 (livre) e em 0x6000 (contida), comecando no papel */
        Z80 *c = &m->cpu;
        memset(m->mem + 0x6000, 0, 64);
        memset(m->mem + 0x8000, 0, 64);
        c->pc = 0x8000; c->tstates = S; c->iff1 = 0;
        for (int i = 0; i < 8; i++) z80_step(c);
        uint32_t free_t = c->tstates - S;
        c->pc = 0x6000; c->tstates = S;
        for (int i = 0; i < 8; i++) z80_step(c);
        uint32_t cont_t = c->tstates - S;
        printf("  %d Hz: 8 NOPs livres %u T, contidos %u T\n", hz ? 60 : 50, free_t, cont_t);
        CHECK(free_t == 32, "NOPs livres %u", free_t);
        CHECK(cont_t > 32, "sem contencao (%u)", cont_t);
        m->cfg.no_contention = 1;
        machine_run_frame(m);
        c->pc = 0x6000; c->tstates = S;
        for (int i = 0; i < 8; i++) z80_step(c);
        CHECK(c->tstates - S == 32, "no_contention nao desligou");
        m->cfg.no_contention = 0;

        /* barramento flutuante: le 0xFF na porta 0xFF fora do papel; byte da tela dentro
         * (instante do callback, T3 do ciclo de E/S: S+3 = Fuse 14338 no 48K) */
        m->mem[0x4000] = 0x5A; m->mem[0x5800] = 0xA5; m->mem[0x4001] = 0x3C; m->mem[0x5801] = 0xC3;
        uint8_t v[8];
        for (int k = 0; k < 8; k++) { c->tstates = S + 3 + (uint32_t)k; v[k] = c->bus.in(m, 0x40FF); }
        c->tstates = 100;
        uint8_t idle = c->bus.in(m, 0x40FF);
        printf("  flutuante: %02X %02X %02X %02X %02X | fora: %02X\n", v[0], v[1], v[2], v[3], v[4], idle);
        CHECK(v[0] == 0x5A && v[1] == 0xA5 && v[2] == 0x3C && v[3] == 0xC3 && v[4] == 0xFF &&
              idle == 0xFF, "barramento flutuante");
        /* ponta a ponta: IN A,($FF) com o ciclo de E/S comecando em S le o bitmap;
         * comecando em S-1, 0xFF (como Fuse 48K e ESPectrum TK) */
        for (int k = 0; k < 2; k++) {
            m->mem[0x8000] = 0xDB; m->mem[0x8001] = 0xFF;
            c->pc = 0x8000; c->af = 0; c->tstates = S - 7 - (uint32_t)k;
            z80_step(c);
            CHECK(Z80_A(c) == (k ? 0xFF : 0x5A), "IN A,($FF) com E/S em S-%d: %02X", k, Z80_A(c));
        }
        /* porta FE: bit 7 = 1 so no TK95, bit 6 = 1 sem fita, bit 5 = 1 */
        uint8_t fe = c->bus.in(m, 0xFEFE);
        CHECK(fe == 0xFF, "FE no TK95 = %02X", fe);
        del_machine(m);
    }
    Machine *m = new_machine(MODEL_TK90X, 0, 0);
    uint8_t fe = m->cpu.bus.in(m, 0xFEFE);
    CHECK(fe == 0x7F, "FE no TK90X = %02X (esperado 7F)", fe);
    machine_key(m, ZXK_Z, 1);
    fe = m->cpu.bus.in(m, 0xFEFE);
    CHECK(fe == 0x7D, "tecla Z: FE = %02X", fe);
    machine_key(m, ZXK_Z, 0);
    machine_joy(m, JOY_FIRE | JOY_LEFT);
    fe = m->cpu.bus.in(m, 0xEFFE);
    CHECK((fe & 0x1F) == 0x0E, "joystick Sinclair 1: %02X", fe);
    m->cfg.joymode = JOYMODE_KEMPSTON;
    CHECK(m->cpu.bus.in(m, 0x001F) == (JOY_FIRE | JOY_LEFT), "Kempston");
    del_machine(m);
}

/* A ULA mostra os 8 px de uma celula juntos: uma escrita na tela no meio da
 * celula nao a divide (antes: 2 px antigos e 6 novos). */
static void test_paper_cells(void)
{
    printf("[papel: celulas inteiras]\n");
    for (int hz = 0; hz < 2; hz++) {
        Machine *m = new_machine(MODEL_TK95, hz, 0);
        Z80 *c = &m->cpu;
        uint32_t S = (uint32_t)m->display_start;
        const uint32_t *row = m->fb + FB_BORDER_Y * FB_W + FB_BORDER_X;
        for (int k = 0; k < 2; k++) {
            machine_run_frame(m);
            memset(m->mem + 0x4000, 0, 6912);
            memset(m->mem + 0x5800, 0x10, 768);            /* papel vermelho */
            m->video_tstate = 0;
            if (k == 0) {
                /* LD (HL),A em 0x8000, M1 em S+3: escrita (contida 0) em S+10 = 1 T dentro da celula 2 */
                m->mem[0x8000] = 0x77;
                c->pc = 0x8000; c->hl = 0x5802; c->af = 0x0800; c->tstates = S + 3; c->iff1 = 0;
                z80_step(c);
                CHECK(c->tstates == S + 10, "LD (HL),A terminou em S+%d", (int)(c->tstates - S));
            } else {
                c->tstates = S + 3;                      /* escrita direta: meio da celula 0 */
                c->bus.write(m, 0x5800, 0x08);
            }
            c->tstates = m->frame_tstates;               /* termina o quadro (muda a borda) */
            c->bus.out(m, 0x00FE, (uint8_t)((m->border + 1) & 7));
            const uint32_t *cell = row + (k ? 0 : 16);
            int same = 1;
            for (int x = 1; x < 8; x++) if (cell[x] != cell[0]) same = 0;
            printf("  %d Hz, %s: %06X ... %06X\n", hz ? 60 : 50, k ? "escrita em S+3" : "LD (HL),A",
                   (unsigned)(cell[0] & 0xFFFFFF), (unsigned)(cell[7] & 0xFFFFFF));
            CHECK(same && cell[0] == tk_palette[1], "celula dividida (%d Hz, caso %d)", hz ? 60 : 50, k);
        }
        del_machine(m);
    }
}

/* Prefixo DD/FD pendente no fim do quadro nao vaza para o snapshot carregado */
static void test_snapshot_prefix(void)
{
    printf("[snapshot apos cadeia DD DD]\n");
    Machine *m = new_machine(MODEL_TK95, 0, 0);
    Snapshot *s = (Snapshot *)calloc(1, sizeof(Snapshot));
    Z80 *c = &m->cpu;
    memset(m->mem + 0x8000, 0xDD, 4);
    c->pc = 0x8000; c->iff1 = 0;
    z80_step(c);
    CHECK(c->pending_prefix == 0xDD, "DD DD nao deixou prefixo pendente (%02X)", c->pending_prefix);
    s->pc = 0x9000; s->sp = 0xF000;
    s->ram[0x9000 - 0x4000] = 0x3E; s->ram[0x9001 - 0x4000] = 0x42;   /* LD A,0x42 */
    machine_apply_snapshot(m, s);
    CHECK(c->pending_prefix == 0, "pending_prefix=%02X apos aplicar o snapshot", c->pending_prefix);
    z80_step(c);
    CHECK(Z80_A(c) == 0x42 && c->pc == 0x9002, "1a instrucao do snapshot: A=%02X PC=%04X",
          Z80_A(c), c->pc);
    free(s);
    del_machine(m);
}

/* Fita virtual de snapshot: F6 durante a carga grava o snapshot sendo carregado;
 * F5 (reset) + LOAD "" carrega a fita virtual de novo e instala o snapshot. */
static void test_vtape_reload(const char *file, int flash)
{
    char path[1024], err[256];
    Snapshot *ref = (Snapshot *)malloc(sizeof(Snapshot)), *t = (Snapshot *)malloc(sizeof(Snapshot));
    HookCheck h;
    Machine *m = new_machine(MODEL_TK95, 0, flash);
    printf("[fita virtual: F6 na carga, reset e LOAD de novo] %s%s\n", file, flash ? " (instantanea)" : "");
    snprintf(path, sizeof path, "%s/%s", g_data, file);
    CHECK(snapshot_load_file(path, ref, err, sizeof err) == 0, "snapshot_load_file: %s", err);
    CHECK(machine_open_file(m, path, 0, 0, 0, err, sizeof err) == 0, "machine_open_file: %s", err);
    memset(&h, 0, sizeof h);
    h.ref = ref;
    m->snap_hook = snap_hook;
    m->snap_hook_user = &h;
    for (int i = 0; i < 60; i++) machine_run_frame(m);
    CHECK(m->pending_snap, "carga ja terminou no quadro 60?");
    machine_take_snapshot(m, t);
    CHECK(t->pc == ref->pc && t->sp == ref->sp && !memcmp(t->ram, ref->ram, 49152),
          "F6 na carga gravou o carregador (PC=%04X)", t->pc);
    uint32_t f = 0;
    while (!h.called && f < 20000) { machine_run_frame(m); f++; }
    CHECK(h.called == 1 && h.regs_ok && h.ram_ok, "1a carga: instalado %d", h.called);
    for (int i = 0; i < 50; i++) machine_run_frame(m);
    machine_reset(m);                            /* F5 */
    machine_autoload(m, 0, 1);                   /* LOAD "" (e play, como o F3) */
    f = 0;
    while (h.called < 2 && f < 20000) { machine_run_frame(m); f++; }
    printf("  reinstalado %d vez(es) em %u quadros, PC=%04X\n", h.called, f, m->cpu.pc);
    CHECK(h.called == 2 && h.regs_ok && h.ram_ok, "2a carga apos reset: instalado %d", h.called);
    free(t);
    free(ref);
    del_machine(m);
}

static void write_file(const char *path, const void *d, size_t n)
{
    FILE *f = tk_fopen(path, "wb");
    if (f) { fwrite(d, 1, n, f); fclose(f); }
}

/* Arquivos sem extensao conhecida: reconhecidos pelo conteudo ou recusados;
 * cabecalho +3DOS; nome da fita sem acentos. */
static void test_open_detection(void)
{
    char path[1024], src[1024], err[256], desc0[64];
    size_t len = 0;
    double wall;
    int r;
    printf("[deteccao de formato / +3DOS / nome com acento]\n");
    /* TAP sem extensao: fita, nao codigo */
    snprintf(src, sizeof src, "%s/int_skip.tap", g_data);
    Machine *m = new_machine(MODEL_TK95, 0, 1);
    r = machine_open_file(m, src, 0, 0, 0, err, sizeof err);
    snprintf(desc0, sizeof desc0, "%s", r == 0 ? tape_block_desc(m->tape, 0) : "");
    uint8_t *d = tk_read_file(src, &len, 0);
    out_path(path, sizeof path, "intskip_tap");
    if (d) write_file(path, d, len);
    free(d);
    r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0 && !strcmp(tape_block_desc(m->tape, 0), desc0), "TAP sem extensao: %s / '%s'",
          err, r == 0 ? tape_block_desc(m->tape, 0) : "");
    /* SNA com extensao estranha: snapshot (fita virtual) */
    snprintf(src, sizeof src, "%s/int_skip.sna", g_data);
    d = tk_read_file(src, &len, 0);
    out_path(path, sizeof path, "jogo.sna.bak");
    if (d) write_file(path, d, len);
    free(d);
    r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0 && m->pending_snap && m->vtape_snap, "SNA .bak: %s", err);
    /* formatos nao suportados e lixo: recusados, nada executado */
    static const struct { const char *name, *data, *msg; } bad[] = {
        { "fita.pzx", "PZXT\x01\x00\x00\x00", "PZX" },
        { "fita.csw", "Compressed Square Wave\x1A\x02\x00", "CSW" },
        { "jogo.szx", "ZXST\x01\x04\x00\x00", "SZX" },
        { "notas.txt", "Ola, isto e um texto qualquer\n", "nao reconhecido" },
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        out_path(path, sizeof path, bad[i].name);
        write_file(path, bad[i].data, strlen(bad[i].data) + 1);
        err[0] = 0;
        r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
        CHECK(r == -1 && strstr(err, bad[i].msg), "%s: r=%d '%s'", bad[i].name, r, err);
    }
    del_machine(m);

    /* +3DOS tipo 3 (org 40000) com LD A,2 / OUT (FE),A / JP 40004 */
    static const uint8_t code[7] = { 0x3E, 0x02, 0xD3, 0xFE, 0xC3, 0x44, 0x9C };
    uint8_t p3[128 + 7];
    memset(p3, 0, sizeof p3);
    memcpy(p3, "PLUS3DOS\x1A\x01\x00", 11);
    p3[11] = (uint8_t)sizeof p3;
    p3[15] = 3; p3[16] = 7; p3[18] = 0x40; p3[19] = 0x9C; p3[20] = 0x00; p3[21] = 0x80;
    unsigned sum = 0;
    for (int i = 0; i < 127; i++) sum += p3[i];
    p3[127] = (uint8_t)sum;
    memcpy(p3 + 128, code, sizeof code);
    out_path(path, sizeof path, "p3.bin");
    write_file(path, p3, sizeof p3);
    m = new_machine(MODEL_TK95, 0, 1);
    r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0, "+3DOS: %s", err);
    run_until_loaded(m, 20000, 20, &wall);
    printf("  +3DOS org 40000: PC=%04X borda %d\n", m->cpu.pc, m->border);
    CHECK(m->cpu.pc == 0x9C44 && m->border == 2, "+3DOS nao rodou em 40000 (PC=%04X)", m->cpu.pc);
    p3[127] ^= 1;
    write_file(path, p3, sizeof p3);
    r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == -1 && strstr(err, "checksum"), "+3DOS checksum ruim aceito: '%s'", err);
    del_machine(m);

    /* nome com acento: a ROM mostra "PASSARO AC" (antes "P?SSARO A?") */
    out_path(path, sizeof path, "P\xC3\xA1ssaro A\xC3\xA7\xC3\xA3o.bin");
    write_file(path, code, sizeof code);
    m = new_machine(MODEL_TK95, 0, 0);
    r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0 && !strcmp(tape_block_desc(m->tape, 0), "Program: PASSARO AC"),
          "nome com acento: '%s' (%s)", r == 0 ? tape_block_desc(m->tape, 0) : "", err);
    del_machine(m);
}

static void test_audio(void)
{
    printf("[audio]\n");
    Machine *m = new_machine(MODEL_TK95, 0, 0);
    int16_t buf[882];
    for (int f = 0; f < 100; f++) machine_run_frame(m);
    /* BEEP: digita via teclado nao e simples; gera eventos sinteticos no quadro */
    machine_run_frame(m);
    m->aud_level_start = 0;
    m->aud_count = 0;
    for (int i = 0; i < 100; i++) {
        m->aud_ev[i].t = (uint32_t)i * 700;
        m->aud_ev[i].level = (i & 1) ? 2 : 0;
        m->aud_count++;
    }
    int n = machine_audio(m, buf, 882, 20000);
    int mn = 0, mx = 0;
    for (int i = 0; i < n; i++) { if (buf[i] < mn) mn = buf[i]; if (buf[i] > mx) mx = buf[i]; }
    printf("  onda quadrada: min %d max %d\n", mn, mx);
    CHECK(n == 882 && mx > 3000 && mn < -3000, "audio sem amplitude");
    del_machine(m);
}

/* .scr e .bin pela fita virtual (arquivos gerados no diretorio de saida) */
static void test_raw_files(void)
{
    char path[1024], err[256];
    double wall;
    size_t len = 0;
    printf("[.scr / .bin]\n");

    /* .scr: a tela (bloco 3) do dogmole.tap */
    snprintf(path, sizeof path, "%s/dogmole.tap", g_data);
    uint8_t *tap = tk_read_file(path, &len, 0);
    uint8_t scr[6912];
    int have = 0;
    for (size_t pos = 0; tap && pos + 2 <= len;) {
        size_t bl = (size_t)(tap[pos] | (tap[pos + 1] << 8));
        if (bl == 6914 && tap[pos + 2] == 0xFF) { memcpy(scr, tap + pos + 3, 6912); have = 1; break; }
        pos += 2 + bl;
    }
    free(tap);
    CHECK(have, "tela nao encontrada no dogmole.tap");
    out_path(path, sizeof path, "dogmole.scr");
    FILE *f = tk_fopen(path, "wb");
    if (f) { fwrite(scr, 1, 6912, f); fclose(f); }
    Machine *m = new_machine(MODEL_TK90X, 0, 0);
    int r = machine_open_file(m, path, 0, 0, 0, err, sizeof err);
    CHECK(r == 0, ".scr: %s", err);
    run_until_loaded(m, 20000, 50, &wall);
    printf("  .scr: %u quadros, tela %s\n", m->frame_count,
           memcmp(m->mem + 0x4000, scr, 6912) ? "DIFERENTE" : "igual ao arquivo");
    CHECK(!memcmp(m->mem + 0x4000, scr, 6912), ".scr nao ficou na tela");
    save_png(m, "dogmole_scr.png");
    del_machine(m);

    /* .bin: LD A,2 / OUT (FE),A / JR $  (borda vermelha) */
    static const uint8_t prog[6] = { 0x3E, 0x02, 0xD3, 0xFE, 0x18, 0xFE };
    out_path(path, sizeof path, "borda.bin");
    f = tk_fopen(path, "wb");
    if (f) { fwrite(prog, 1, sizeof prog, f); fclose(f); }
    for (int k = 0; k < 2; k++) {
        uint16_t org = k ? 40000 : 0;
        uint16_t pc_loop = (uint16_t)((k ? 40000 : 32768) + 4);
        m = new_machine(MODEL_TK95, 0, k);
        r = machine_open_file(m, path, org, 0, 0, err, sizeof err);
        CHECK(r == 0, ".bin: %s", err);
        run_until_loaded(m, 20000, 20, &wall);
        printf("  .bin org %u%s: %u quadros, PC=%04X borda %d\n", k ? 40000 : 32768,
               k ? " (instantanea)" : "", m->frame_count, m->cpu.pc, m->border);
        CHECK(m->cpu.pc == pc_loop && m->border == 2, ".bin nao rodou");
        del_machine(m);
    }
    /* erro em portugues */
    m = new_machine(MODEL_TK95, 0, 0);
    r = machine_open_file(m, "/nao/existe.tap", 0, 0, 0, err, sizeof err);
    printf("  inexistente: \"%s\"\n", err);
    CHECK(r == -1 && err[0], "arquivo inexistente nao deu erro");
    del_machine(m);
}

/* Modos de teclado do joystick: QAOP e as teclas do jogo (perfil) */
static void test_joy_modes(void)
{
    printf("[joystick: QAOP e teclas do jogo]\n");
    Machine *m = new_machine(MODEL_TK95, 0, 0);
    Z80 *c = &m->cpu;
    m->cfg.joymode = JOYMODE_QAOP;
    machine_joy(m, JOY_UP | JOY_RIGHT | JOY_FIRE);
    CHECK((c->bus.in(m, 0xFBFE) & 0x1F) == 0x1E, "QAOP cima = Q: %02X", c->bus.in(m, 0xFBFE));
    CHECK((c->bus.in(m, 0xDFFE) & 0x1F) == 0x1E, "QAOP dir = P: %02X", c->bus.in(m, 0xDFFE));
    CHECK((c->bus.in(m, 0x7FFE) & 0x1F) == 0x1E, "QAOP fogo = SPACE: %02X", c->bus.in(m, 0x7FFE));
    CHECK((c->bus.in(m, 0xEFFE) & 0x1F) == 0x1F, "QAOP nao aperta 6-0: %02X", c->bus.in(m, 0xEFFE));
    machine_joy(m, JOY_LEFT | JOY_DOWN);
    CHECK((c->bus.in(m, 0xDFFE) & 0x1F) == 0x1D, "QAOP esq = O: %02X", c->bus.in(m, 0xDFFE));
    CHECK((c->bus.in(m, 0xFDFE) & 0x1F) == 0x1E, "QAOP baixo = A: %02X", c->bus.in(m, 0xFDFE));

    /* teclas do jogo: fogo = SYM + M juntas, esquerda = Z */
    JoyKeys k;
    memset(&k, ZXK_COUNT, sizeof k);
    k.key[4][0] = ZXK_SYM; k.key[4][1] = ZXK_M;
    k.key[1][0] = ZXK_Z;
    machine_set_joy_keys(m, &k);
    m->cfg.joymode = JOYMODE_KEYS;
    machine_joy(m, JOY_FIRE);
    CHECK((c->bus.in(m, 0x7FFE) & 0x1F) == 0x19, "teclas do jogo: fogo = SYM+M: %02X",
          c->bus.in(m, 0x7FFE));
    machine_joy(m, JOY_LEFT | JOY_RIGHT);       /* direita sem tecla: nada */
    CHECK((c->bus.in(m, 0xFEFE) & 0x1F) == 0x1D, "teclas do jogo: esq = Z: %02X", c->bus.in(m, 0xFEFE));
    CHECK((c->bus.in(m, 0x00FE) & 0x1F) == 0x1D, "teclas do jogo: todas as linhas: %02X",
          c->bus.in(m, 0x00FE));
    machine_set_joy_keys(m, NULL);
    CHECK((c->bus.in(m, 0x00FE) & 0x1F) == 0x1F, "sem teclas do jogo: nada apertado");
    /* o teclado de verdade continua valendo junto */
    machine_key(m, ZXK_X, 1);
    CHECK((c->bus.in(m, 0xFEFE) & 0x1F) == 0x1B, "tecla X com o joystick: %02X", c->bus.in(m, 0xFEFE));
    machine_key(m, ZXK_X, 0);
    JoyKeys s1;
    machine_joy_keys_of(m, JOYMODE_SINCLAIR1, &s1);
    CHECK(s1.key[4][0] == ZXK_0 && s1.key[1][0] == ZXK_6, "tabela do Sinclair 1");
    del_machine(m);
}

/* Perfis: CRC32 e um perfil para cada jogo de games/ (rodando da raiz do projeto) */
static void test_profiles(void)
{
    printf("[perfis de controle dos jogos]\n");
    CHECK(profile_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u, "CRC32 de referencia");
    static const char *const files[] = {
        "games/01-knight-lore-alt-deep-blue.tap", "games/02-manic-miner-alt-frank-n-stein-rebooted.tzx",
        "games/03-jet-set-willy-alt-lala-prologue.tzx", "games/04-atic-atac-alt-castelo-corvoalto.tap",
        "games/05-bruce-lee.tzx", "games/06-sabre-wulf-alt-selva-de-verdemor.tap",
        "games/07-commando-alt-sgt-helmet-training-day.tap", "games/08-dizzy.tzx",
        "games/09-green-beret.tzx", "games/10-boulder-dash.tzx", "games/extras/06-extra-doomskulle.tzx",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        FILE *f = fopen(files[i], "rb");
        if (!f) { printf("  (sem %s: pulado)\n", files[i]); continue; }
        fclose(f);
        const GameProfile *p = profile_for_file(files[i]);
        CHECK(p != NULL, "perfil de %s", files[i]);
        if (!p) continue;
        CHECK(p->summary && strlen(p->summary) <= 28, "resumo curto em %s", p->name);
        CHECK(p->mode != JOYMODE_KEYS || profile_has_keys(p), "teclas no perfil %s", p->name);
        printf("  %-28s %08X %s\n", p->name, p->crc, p->summary);
    }
    CHECK(profile_count() == 11, "11 perfis (%d)", profile_count());
    CHECK(profile_for_file("tests/data/dogmole.tap") == NULL, "arquivo sem perfil");
}

int main(int argc, char **argv)
{
    if (argc > 1) g_data = argv[1];
    if (argc > 2) g_out = argv[2];
    make_dir(g_out);

    static const Model models[3] = { MODEL_TK95, MODEL_TK90X, MODEL_TK90X_V1 };
    for (int i = 0; i < 3; i++)
        for (int hz = 0; hz < 2; hz++) test_boot(models[i], hz);
    for (int i = 0; i < 3; i++)
        for (int hz = 0; hz < 2; hz++) test_autotype(models[i], hz);
    test_contention();
    test_paper_cells();
    test_snapshot_prefix();
    test_audio();
    test_tape(0, 0);
    test_tape(1, 0);
    test_tape(0, 1);
    test_vtape("int_skip.sna", 0, "int_skip_vtape.png");
    test_vtape("int_skip_v3.z80", 1, "int_skip_v3_flash.png");
    test_instant("int_skip_v1.z80");
    test_instant("int_skip.sna");
    test_snapshot_halt();
    test_vtape_reload("int_skip.sna", 1);
    test_vtape_reload("int_skip_v3.z80", 0);
    test_raw_files();
    test_open_detection();
    test_joy_modes();
    test_profiles();

    printf("\nmachine_test: %d ok, %d falhas\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
