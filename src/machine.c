/*
 * machine.c - TK90X / TK95: memoria, ULA Microdigital (video por T-state,
 * contencao, porta 0xFE, barramento flutuante), teclado/joystick, gravador,
 * armadilhas na ROM (auto-play, carga instantanea, instalacao do snapshot
 * da fita virtual), digitacao automatica e machine_open_file.
 *
 * Temporizacao (docs/research/tk90x-hardware.md, secoes 1, 2 e Verification):
 *   228 T/linha; 312 linhas (50 Hz, 71136 T) ou 262 (60 Hz, 59736 T);
 *   T = 0 no inicio do INT (32 T). display_start S = 14687 (50 Hz) / 8759 (60 Hz)
 *   na convencao do 48K (14335): S e o 1o T-state contido da linha 0 do papel
 *   (atraso 6) e o 1o pixel do papel e desenhado em S+1, como no 48K
 *   (contencao comeca 1 T antes do 1o pixel). Linha L do papel: contencao
 *   em [S + 228L, S + 228L + 128) com o padrao 6,5,4,3,2,1,0,0.
 *   Barramento flutuante: a ULA le bitmap/atributo em S+3 + 228L + 8k +
 *   {0,1,2,3} (instante do callback de E/S, no T3 do ciclo; = ESPectrum S+4
 *   no fim do ciclo; = Fuse 14338 no 48K); fora disso 0xFF.
 *   No papel a ULA trava bitmap+atributo e mostra as 8 colunas de uma celula
 *   de uma vez (SRegister/AttrOut em ula.vhd): uma escrita na tela nunca
 *   divide uma celula.
 */
#include "machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "roms_embedded.h"

/* Paleta: normal 0xC0, BRIGHT 0xFF por canal (preto continua preto). */
const uint32_t tk_palette[16] = {
    0xFF000000u, 0xFF0000C0u, 0xFFC00000u, 0xFFC000C0u,
    0xFF00C000u, 0xFF00C0C0u, 0xFFC0C000u, 0xFFC0C0C0u,
    0xFF000000u, 0xFF0000FFu, 0xFFFF0000u, 0xFFFF00FFu,
    0xFF00FF00u, 0xFF00FFFFu, 0xFFFFFF00u, 0xFFFFFFFFu
};

#define LINE_T        228
#define PAPER_T       128
#define FB_ROW_T      (FB_W / 2)          /* 160 T por linha visivel */
#define FB_PAPER_X0   (FB_BORDER_X / 2)   /* 16 T de borda esquerda */
#define FB_PAPER_X1   (FB_PAPER_X0 + PAPER_T)

#define AUD_CAP ((int)(sizeof(((Machine *)0)->aud_ev) / sizeof(((Machine *)0)->aud_ev[0])))
#define AUD_MIC  1
#define AUD_SPK  2
#define AUD_EAR  4

/* Enderecos na ROM (iguais nas 3 ROMs, formats.md secao 5 e hardware 9.2) */
#define ROM_SA_LD_RET  0x053F
#define ROM_LD_BYTES   0x0556
#define ROM_LD_BREAK   0x056B
#define ROM_LD_RET     0x05E2
#define ROM_MAIN_1     0x12A9
#define ROM_KEY_IN_RET 0x10B4   /* KEY-INPUT: RET Z quando nao ha tecla nova (FLAGS bit 5) */

/* Digitacao automatica: quadros com a tecla apertada / solta. O KEYBOARD da
 * ROM so aceita a mesma tecla de novo depois que o contador de 5 interrupcoes
 * do KSTATE zera, por isso o intervalo entre teclas iguais (SYM+P, SYM+P)
 * precisa de pelo menos 6 quadros. */
#define AUTOTYPE_HOLD 3
#define AUTOTYPE_GAP  6

/* Quadros sem nova chamada ao LD-BYTES depois de uma carga instantanea
 * antes de dar play na fita (para carregadores turbo proprios do jogo). */
#define FLASH_RESUME_FRAMES 50

static const uint8_t cont_pattern[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };

/* ------------------------------------------------------------------ */
/* ROMs                                                                */
/* ------------------------------------------------------------------ */

static const char *model_rom_file(Model model)
{
    switch (model) {
    case MODEL_TK90X:    return "tk90x.rom";
    case MODEL_TK90X_V1: return "tk90x-v1.rom";
    default:             return "tk95.rom";
    }
}

static int try_rom(const char *path, uint8_t *out)
{
    size_t len = 0;
    uint8_t *d = tk_read_file(path, &len, 16384);
    if (!d) return -1;
    if (len != 16384) { free(d); return -1; }
    memcpy(out, d, 16384);
    free(d);
    return 0;
}

int machine_model_rom(Model model, const char *exe_dir, uint8_t *out)
{
    const char *file = model_rom_file(model);
    char path[2048];
    if (exe_dir && exe_dir[0]) {
        size_t n = strlen(exe_dir);
        int sep = exe_dir[n - 1] == '/' || exe_dir[n - 1] == '\\';
        snprintf(path, sizeof path, "%s%sroms/%s", exe_dir, sep ? "" : "/", file);
        if (try_rom(path, out) == 0) return 0;
    }
    snprintf(path, sizeof path, "roms/%s", file);
    if (try_rom(path, out) == 0) return 0;
    switch (model) {
    case MODEL_TK90X:    memcpy(out, rom_tk90x, 16384); break;
    case MODEL_TK90X_V1: memcpy(out, rom_tk90x_v1, 16384); break;
    default:             memcpy(out, rom_tk95, 16384); break;
    }
    return 1;
}

const char *machine_model_name(Model model)
{
    switch (model) {
    case MODEL_TK90X:    return "TK90X";
    case MODEL_TK90X_V1: return "TK90X v1";
    case MODEL_TK95:     return "TK95";
    }
    return "TK95";
}

/* ------------------------------------------------------------------ */
/* Video                                                               */
/* ------------------------------------------------------------------ */

static void fill32(uint32_t *p, int n, uint32_t v)
{
    for (int i = 0; i < n; i++) p[i] = v;
}

/* Desenha a linha y do framebuffer, colunas [x0, x1) em T-states (2 px por T). */
static void render_span(Machine *m, int y, int x0, int x1)
{
    uint32_t *row = m->fb + y * FB_W;
    uint32_t bc = tk_palette[m->border & 7];
    int L = y - FB_BORDER_Y;
    int x = x0;

    if (L < 0 || L >= 192) {
        fill32(row + 2 * x0, 2 * (x1 - x0), bc);
        return;
    }
    if (x < FB_PAPER_X0) {
        int e = x1 < FB_PAPER_X0 ? x1 : FB_PAPER_X0;
        fill32(row + 2 * x, 2 * (e - x), bc);
        x = e;
    }
    if (x < x1 && x < FB_PAPER_X1) {
        int e = x1 < FB_PAPER_X1 ? x1 : FB_PAPER_X1;
        const uint8_t *bm = m->mem + (0x4000 | ((L & 0xC0) << 5) | ((L & 7) << 8) | ((L & 0x38) << 2));
        const uint8_t *at = m->mem + 0x5800 + (L >> 3) * 32;
        uint32_t *out = row + 2 * x;
        while (x < e) {
            int p = x - FB_PAPER_X0, col = p >> 2, sub = p & 3;
            uint8_t a = at[col], b = bm[col];
            int br = (a & 0x40) >> 3;
            uint32_t ink = tk_palette[(a & 7) | br];
            uint32_t pap = tk_palette[((a >> 3) & 7) | br];
            if ((a & 0x80) && m->flash_phase) { uint32_t t = ink; ink = pap; pap = t; }
            if (sub == 0 && x + 4 <= e) {
                out[0] = (b & 0x80) ? ink : pap; out[1] = (b & 0x40) ? ink : pap;
                out[2] = (b & 0x20) ? ink : pap; out[3] = (b & 0x10) ? ink : pap;
                out[4] = (b & 0x08) ? ink : pap; out[5] = (b & 0x04) ? ink : pap;
                out[6] = (b & 0x02) ? ink : pap; out[7] = (b & 0x01) ? ink : pap;
                out += 8; x += 4;
            } else {
                b = (uint8_t)(b << (sub * 2));
                out[0] = (b & 0x80) ? ink : pap;
                out[1] = (b & 0x40) ? ink : pap;
                out += 2; x++;
            }
        }
    }
    if (x < x1) fill32(row + 2 * x, 2 * (x1 - x), bc);
}

/* Desenha tudo o que a ULA mostraria ate o instante t_now (exclusive). */
static void video_update(Machine *m, uint32_t t_now)
{
    uint32_t t = m->video_tstate;
    if (t_now > m->frame_tstates) t_now = m->frame_tstates;
    if (t_now <= t) return;
    m->video_tstate = t_now;
    if (t < m->fb_t0) t = m->fb_t0;
    while (t < t_now) {
        uint32_t rel = t - m->fb_t0;
        uint32_t y = rel / LINE_T, x = rel % LINE_T;
        if (y >= FB_H) break;
        if (x >= FB_ROW_T) { t += LINE_T - x; continue; }
        uint32_t e = x + (t_now - t);
        if (e > FB_ROW_T) e = FB_ROW_T;
        render_span(m, (int)y, (int)x, (int)e);
        t += e - x;
    }
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

static uint8_t audio_level_now(const Machine *m)
{
    uint8_t l = 0;
    if (m->ula_out & 0x08) l |= AUD_MIC;
    if (m->ula_out & 0x10) l |= AUD_SPK;
    if (m->tape_sound && m->tape_ear && m->tape && tape_is_playing(m->tape)) l |= AUD_EAR;
    return l;
}

static void audio_event(Machine *m)
{
    uint8_t l = audio_level_now(m);
    if (l == m->aud_level) return;
    m->aud_level = l;
    if (m->aud_count < AUD_CAP) {
        m->aud_ev[m->aud_count].t = m->cpu.tstates;
        m->aud_ev[m->aud_count].level = l;
        m->aud_count++;
    } else {
        /* cheio: junta com o ultimo evento (nivel final continua certo) */
        m->aud_ev[AUD_CAP - 1].level = l;
    }
}

int machine_audio(Machine *m, int16_t *out, int nsamples, int volume)
{
    /* amplitudes relativas: alto-falante grande, fita media, MIC pequeno */
    static const int amp_part[3] = { 3, 16, 8 };   /* MIC, SPK, EAR */
    int amp[8];
    const int amp_max = 3 + 16 + 8;
    if (nsamples <= 0 || !out) return 0;
    for (int l = 0; l < 8; l++)
        amp[l] = ((l & AUD_MIC) ? amp_part[0] : 0) + ((l & AUD_SPK) ? amp_part[1] : 0) +
                 ((l & AUD_EAR) ? amp_part[2] : 0);
    if (volume < 0) volume = 0;
    if (volume > 32767) volume = 32767;

    uint32_t ft = m->frame_tstates;
    uint8_t lvl = m->aud_level_start;
    int e = 0;
    uint32_t t = 0;
    float dc = m->aud_dc;
    for (int i = 0; i < nsamples; i++) {
        uint32_t t0 = t;
        uint32_t t1 = (uint32_t)(((uint64_t)(i + 1) * ft) / (uint32_t)nsamples);
        int64_t area = 0;
        while (e < m->aud_count && m->aud_ev[e].t < t1) {
            uint32_t et = m->aud_ev[e].t;
            if (et > t) { area += (int64_t)amp[lvl] * (et - t); t = et; }
            lvl = m->aud_ev[e].level & 7;
            e++;
        }
        area += (int64_t)amp[lvl] * (t1 - t);
        t = t1;
        float v = (t1 > t0) ? (float)area / (float)(t1 - t0) : (float)amp[lvl];
        v = v / (float)amp_max;             /* 0..1 */
        dc += (v - dc) * 0.0025f;            /* passa-alta de 1 polo (~17 Hz a 44,1 kHz) */
        float s = (v - dc) * (float)volume;
        if (s > 32767.0f) s = 32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        out[i] = (int16_t)s;
    }
    m->aud_dc = dc;
    return nsamples;
}

/* ------------------------------------------------------------------ */
/* Barramento                                                          */
/* ------------------------------------------------------------------ */

static uint8_t bus_read(void *ctx, uint16_t addr)
{
    return ((Machine *)ctx)->mem[addr];
}

/* Ate onde desenhar antes de uma escrita na tela no instante t: no papel de
 * uma linha so celulas inteiras (a ULA trava os 8 px de uma celula juntos),
 * nunca o meio de uma; o resto da celula sai depois, com o byte novo. */
static uint32_t paper_catchup(const Machine *m, uint32_t t)
{
    uint32_t p0 = (uint32_t)m->display_start + 1;   /* 1o pixel do papel */
    if (t < p0) return t;
    uint32_t rel = t - p0, L = rel / LINE_T, o = rel % LINE_T;
    if (L >= 192 || o >= PAPER_T) return t;
    return t - (o & 3);
}

static void bus_write(void *ctx, uint16_t addr, uint8_t v)
{
    Machine *m = (Machine *)ctx;
    if (addr < 0x4000) return;
    /* tela: desenha ate agora antes de mudar (multicor, efeitos de raster) */
    if (addr < 0x5B00 && m->mem[addr] != v) video_update(m, paper_catchup(m, m->cpu.tstates));
    m->mem[addr] = v;
}

static unsigned cont_at(const Machine *m, uint32_t t)
{
    return t < TK_CONT_TAB_LEN ? m->cont_tab[t] : 0;
}

static unsigned bus_mem_contention(void *ctx, uint16_t addr, uint32_t t)
{
    const Machine *m = (const Machine *)ctx;
    if ((addr & 0xC000) != 0x4000) return 0;
    return cont_at(m, t);
}

/* Regras do Fuse para o ciclo de E/S de 4 T:
 *   alto contido, A0=0: C:1, C:3      alto contido, A0=1: C:1 C:1 C:1 C:1
 *   alto livre,   A0=0: N:1, C:3      alto livre,   A0=1: N:4 */
static unsigned bus_io_contention(void *ctx, uint16_t port, uint32_t t)
{
    const Machine *m = (const Machine *)ctx;
    uint32_t tt = t;
    int hi = (port & 0xC000) == 0x4000;
    int ula = (port & 1) == 0;
    if (hi) {
        if (ula) {
            tt += cont_at(m, tt); tt += 1;
            tt += cont_at(m, tt); tt += 3;
        } else {
            for (int i = 0; i < 4; i++) { tt += cont_at(m, tt); tt += 1; }
        }
    } else if (ula) {
        tt += 1; tt += cont_at(m, tt); tt += 3;
    } else {
        tt += 4;
    }
    return (unsigned)(tt - t - 4);
}

/* Barramento flutuante: byte que a ULA esta lendo da tela em t, ou 0xFF. */
static uint8_t floating_bus(const Machine *m, uint32_t t)
{
    uint32_t base = (uint32_t)m->display_start + 3;
    if (t < base) return 0xFF;
    uint32_t rel = t - base;
    uint32_t L = rel / LINE_T, o = rel % LINE_T;
    if (L >= 192 || o >= PAPER_T) return 0xFF;
    uint32_t col = (o >> 3) * 2;
    switch (o & 7) {
    case 2: col++; /* fallthrough */
    case 0: return m->mem[0x4000 | ((L & 0xC0) << 5) | ((L & 7) << 8) | ((L & 0x38) << 2) | col];
    case 3: col++; /* fallthrough */
    case 1: return m->mem[0x5800 + (L >> 3) * 32 + col];
    default: return 0xFF;
    }
}

/* Linhas do teclado vistas pela ULA (teclado fisico + autotype + joystick). */
static uint8_t key_row(const Machine *m, int row)
{
    uint8_t v = (uint8_t)(m->key_rows[row] & m->auto_rows[row]);
    uint8_t j = m->joy_state;
    if (!j) return v;
    switch (m->cfg.joymode) {
    case JOYMODE_SINCLAIR1:   /* EFFE: bit0 fogo(0) 1 cima(9) 2 baixo(8) 3 dir(7) 4 esq(6) */
        if (row == 4) {
            if (j & JOY_FIRE)  v &= (uint8_t)~0x01;
            if (j & JOY_UP)    v &= (uint8_t)~0x02;
            if (j & JOY_DOWN)  v &= (uint8_t)~0x04;
            if (j & JOY_RIGHT) v &= (uint8_t)~0x08;
            if (j & JOY_LEFT)  v &= (uint8_t)~0x10;
        }
        break;
    case JOYMODE_SINCLAIR2:   /* F7FE: bit0 esq(1) 1 dir(2) 2 baixo(3) 3 cima(4) 4 fogo(5) */
        if (row == 3) {
            if (j & JOY_LEFT)  v &= (uint8_t)~0x01;
            if (j & JOY_RIGHT) v &= (uint8_t)~0x02;
            if (j & JOY_DOWN)  v &= (uint8_t)~0x04;
            if (j & JOY_UP)    v &= (uint8_t)~0x08;
            if (j & JOY_FIRE)  v &= (uint8_t)~0x10;
        }
        break;
    case JOYMODE_CURSOR:      /* 5 esq (F7FE b4); 6 baixo, 7 cima, 8 dir, 0 fogo (EFFE b4,b3,b2,b0) */
        if (row == 3 && (j & JOY_LEFT)) v &= (uint8_t)~0x10;
        if (row == 4) {
            if (j & JOY_DOWN)  v &= (uint8_t)~0x10;
            if (j & JOY_UP)    v &= (uint8_t)~0x08;
            if (j & JOY_RIGHT) v &= (uint8_t)~0x04;
            if (j & JOY_FIRE)  v &= (uint8_t)~0x01;
        }
        break;
    default:
        break;
    }
    return v;
}

static uint8_t bus_in(void *ctx, uint16_t port)
{
    Machine *m = (Machine *)ctx;
    if ((port & 1) == 0) {
        uint8_t hi = (uint8_t)(port >> 8), v = 0x1F;
        for (int r = 0; r < 8; r++)
            if (!(hi & (1 << r))) v &= key_row(m, r);
        v |= 0x20;
        /* bit 6 = EAR: sinal da fita tocando; parado, o TK le 1 (=issue 2);
         * issue2 = 0 simula o issue 3 (bit 4 da ultima escrita). */
        int ear;
        if (m->tape && tape_is_playing(m->tape)) ear = m->tape_ear;
        else ear = m->cfg.issue2 ? 1 : ((m->ula_out >> 4) & 1);
        if (ear) v |= 0x40;
        /* bit 7: diodo D1 do TK90X o prende em 0; no TK95 (sem D1) le 1 */
        if (m->cfg.model == MODEL_TK95) v |= 0x80;
        return v;
    }
    if (m->cfg.joymode == JOYMODE_KEMPSTON && (port & 0xE0) == 0)
        return (uint8_t)(m->joy_state & 0x1F);
    if (m->cfg.no_floating_bus) return 0xFF;
    return floating_bus(m, m->cpu.tstates);
}

static void bus_out(void *ctx, uint16_t port, uint8_t v)
{
    Machine *m = (Machine *)ctx;
    if (port & 1) return;                 /* 0xDF (boot) e outras: ignoradas */
    if ((v & 7) != m->border) {
        video_update(m, m->cpu.tstates);  /* borda exata por T-state */
        m->border = v & 7;
    }
    m->ula_out = v;
    audio_event(m);
}

/* ------------------------------------------------------------------ */
/* Temporizacao                                                        */
/* ------------------------------------------------------------------ */

static void build_timing(Machine *m)
{
    int S;
    m->line_tstates = LINE_T;
    m->frame_lines = m->cfg.hz60 ? 262 : 312;
    m->frame_tstates = (uint32_t)(LINE_T * m->frame_lines);
    m->int_length = 32;
    S = m->cfg.hz60 ? 8759 : 14687;
    m->display_start = S;
    m->first_screen_line = (S + 1) / LINE_T;          /* 64 (50 Hz) / 38 (60 Hz) */
    m->fb_t0 = (uint32_t)(S + 1 - FB_PAPER_X0 - FB_BORDER_Y * LINE_T);

    memset(m->cont_tab, 0, sizeof m->cont_tab);
    for (int L = 0; L < 192; L++) {
        uint32_t b = (uint32_t)(S + L * LINE_T);
        for (int o = 0; o < PAPER_T; o++) m->cont_tab[b + (uint32_t)o] = cont_pattern[o & 7];
    }
}

static void apply_contention_cfg(Machine *m)
{
    m->contended = !m->cfg.no_contention;
    m->cpu.bus.mem_contention = m->contended ? bus_mem_contention : NULL;
    m->cpu.bus.io_contention = m->contended ? bus_io_contention : NULL;
}

void machine_set_hz(Machine *m, int hz60)
{
    m->cfg.hz60 = hz60 ? 1 : 0;
    build_timing(m);
    if (m->cpu.tstates >= m->frame_tstates) m->cpu.tstates %= m->frame_tstates;
    if (m->video_tstate > m->frame_tstates) m->video_tstate = m->frame_tstates;
}

/* ------------------------------------------------------------------ */
/* Inicializacao                                                       */
/* ------------------------------------------------------------------ */

int machine_init(Machine *m, const uint8_t *rom, const MachineConfig *cfg)
{
    uint8_t tmp[16384];
    Z80Bus bus;
    if (!m || !rom) return -1;
    memcpy(tmp, rom, 16384);              /* rom pode apontar para dentro de m */
    memset(m, 0, sizeof *m);
    if (cfg) m->cfg = *cfg;
    else { m->cfg.model = MODEL_TK95; m->cfg.issue2 = 1; m->cfg.joymode = JOYMODE_SINCLAIR1; }
    memcpy(m->rom, tmp, 16384);

    memset(&bus, 0, sizeof bus);
    bus.ctx = m;
    bus.read = bus_read;
    bus.write = bus_write;
    bus.in = bus_in;
    bus.out = bus_out;
    z80_init(&m->cpu, &bus);

    m->tape = tape_new();
    if (!m->tape) return -1;
    tape_set_cpu_hz(m->tape, TK_CPU_HZ);
    m->flash_idle = -1;
    build_timing(m);
    machine_reset(m);
    return 0;
}

void machine_free(Machine *m)
{
    if (!m) return;
    if (m->tape) { tape_free(m->tape); m->tape = NULL; }
    free(m->snap);
    m->snap = NULL;
    m->pending_snap = 0;
}

void machine_release_all_keys(Machine *m)
{
    for (int i = 0; i < 8; i++) m->key_rows[i] = 0x1F;
}

static void autotype_release(Machine *m)
{
    for (int i = 0; i < 8; i++) m->auto_rows[i] = 0x1F;
}

void machine_reset(Machine *m)
{
    memset(m->mem, 0, sizeof m->mem);
    memcpy(m->mem, m->rom, 16384);
    z80_reset(&m->cpu);
    m->cpu.tstates = 0;
    m->cpu.ei_delay = 0;
    apply_contention_cfg(m);
    build_timing(m);

    m->border = 0;
    m->ula_out = 0;
    machine_release_all_keys(m);
    autotype_release(m);
    m->frame_count = 0;
    m->flash_phase = 0;
    m->int_count = 0;
    m->video_tstate = 0;
    for (int i = 0; i < FB_W * FB_H; i++) m->fb[i] = tk_palette[0];

    m->aud_count = 0;
    m->aud_level = 0;
    m->aud_level_start = 0;

    if (m->tape) {
        tape_stop(m->tape);
        /* fita virtual de snapshot: volta ao inicio para um novo LOAD ""
         * (a armadilha do LD-BYTES rearma a instalacao, ver rom_traps) */
        if (m->vtape_snap) tape_rewind(m->tape);
    }
    m->tape_ear = 0;

    m->pending_snap = 0;
    m->snap_block_done = 0;
    m->snap_wait = 0;
    m->flash_idle = -1;
    memset(&m->autotype, 0, sizeof m->autotype);
    m->rom_ready = 0;
    m->key_wait_frame = 0;
}

/* ------------------------------------------------------------------ */
/* Teclado / joystick                                                  */
/* ------------------------------------------------------------------ */

void machine_key(Machine *m, ZXKey k, int pressed)
{
    if ((int)k < 0 || k >= ZXK_COUNT) return;
    int row = (int)k / 5, bit = (int)k % 5;
    if (pressed) m->key_rows[row] &= (uint8_t)~(1u << bit);
    else         m->key_rows[row] |= (uint8_t)(1u << bit);
}

void machine_joy(Machine *m, uint8_t joy_bits)
{
    m->joy_state = joy_bits & 0x1F;
}

int machine_editor_active(const Machine *m, uint32_t frames)
{
    uint32_t k = m->key_wait_frame;
    return k != 0 && k <= m->frame_count && m->frame_count - k < frames;
}

/* ------------------------------------------------------------------ */
/* Digitacao automatica                                                */
/* ------------------------------------------------------------------ */

static void autotype_press(Machine *m, int step)
{
    autotype_release(m);
    for (int i = 0; i < 3; i++) {
        int k = m->autotype.keys[step][i];
        if (k >= 0 && k < ZXK_COUNT)
            m->auto_rows[k / 5] &= (uint8_t)~(1u << (k % 5));
    }
}

void machine_autoload(Machine *m, int wait_frames, int play_tape)
{
    AutoType *a = &m->autotype;
    memset(a, 0, sizeof *a);
    for (int i = 0; i < 32; i++) a->keys[i][0] = a->keys[i][1] = a->keys[i][2] = ZXK_COUNT;
    a->keys[0][0] = ZXK_J;
    a->keys[1][0] = ZXK_SYM; a->keys[1][1] = ZXK_P;
    a->keys[2][0] = ZXK_SYM; a->keys[2][1] = ZXK_P;
    a->keys[3][0] = ZXK_ENTER;
    a->count = 4;
    a->hold_frames = AUTOTYPE_HOLD;
    a->gap_frames = AUTOTYPE_GAP;
    a->wait_frames = wait_frames < 0 ? 0 : wait_frames;
    a->play_tape_after = play_tape ? 1 : 0;
    a->phase = 0;
    a->active = 1;
    autotype_release(m);
}

/* Chamado no fim de cada quadro. */
static void autotype_frame(Machine *m)
{
    AutoType *a = &m->autotype;
    if (!a->active) return;
    switch (a->phase) {
    case 0:
        if (m->rom_ready && m->frame_count >= (uint32_t)a->wait_frames) {
            a->pos = 0;
            if (a->count <= 0) { a->active = 0; break; }
            autotype_press(m, 0);
            a->timer = a->hold_frames > 0 ? a->hold_frames : 1;
            a->phase = 1;
        }
        break;
    case 1:
        if (--a->timer <= 0) {
            autotype_release(m);
            a->timer = a->gap_frames > 0 ? a->gap_frames : 1;
            a->phase = 2;
        }
        break;
    case 2:
        if (--a->timer <= 0) {
            a->pos++;
            if (a->pos >= a->count) {
                a->active = 0;
                /* play ja; com carga instantanea a armadilha do LD-BYTES cuida disso */
                if (a->play_tape_after && m->tape && tape_is_loaded(m->tape) &&
                    !tape_is_playing(m->tape) && !tape_at_end(m->tape) && !m->cfg.flash_load)
                    tape_play(m->tape, m->cpu.tstates);
            } else {
                autotype_press(m, a->pos);
                a->timer = a->hold_frames > 0 ? a->hold_frames : 1;
                a->phase = 1;
            }
        }
        break;
    default:
        a->active = 0;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Snapshots                                                           */
/* ------------------------------------------------------------------ */

void machine_apply_snapshot(Machine *m, const Snapshot *s)
{
    Z80 *c = &m->cpu;
    /* no meio do quadro: desenha ate agora com a RAM/borda antigas */
    if (m->in_frame) video_update(m, c->tstates);
    memcpy(m->mem + 0x4000, s->ram, 49152);
    c->af = s->af; c->bc = s->bc; c->de = s->de; c->hl = s->hl;
    c->af_ = s->af_; c->bc_ = s->bc_; c->de_ = s->de_; c->hl_ = s->hl_;
    c->ix = s->ix; c->iy = s->iy; c->sp = s->sp; c->pc = s->pc;
    c->memptr = s->memptr;
    c->i = s->i; c->r = s->r;
    c->iff1 = s->iff1 ? 1 : 0; c->iff2 = s->iff2 ? 1 : 0;
    c->im = s->im > 2 ? 1 : s->im;
    c->halted = s->halted ? 1 : 0;
    /* .Z80 e .SNA nao guardam o estado HALT; pela convencao do Fuse (e deste
     * nucleo) a CPU parada fica com o PC no proprio HALT. Sem isto, a INT que
     * chega logo apos a carga empilharia o endereco do HALT em vez de PC+1 e o
     * programa perderia um quadro inteiro parado de novo no HALT (visto em
     * Green Beret, 60 Hz, gravado na tela de titulo). Para uma CPU que ainda
     * ia executar o HALT, marcar parada e equivalente (HALT = NOP repetido). */
    if (!c->halted && m->mem[c->pc] == 0x76) c->halted = 1;
    c->ei_delay = 0;
    c->q = 0;
    c->iff2_read = 0;
    /* prefixo DD/FD pendente de antes da carga nao vale para o programa novo
     * (o PC do snapshot aponta o proprio prefixo, que sera lido de novo) */
    c->pending_prefix = 0;
    if (!m->in_frame) {
        /* entre quadros: posicao do quadro do snapshot (Z80 v3), senao 0 */
        c->tstates = s->tstates < m->frame_tstates ? s->tstates : s->tstates % m->frame_tstates;
        m->video_tstate = 0;
    }
    m->border = s->border & 7;
    m->ula_out = (uint8_t)((m->ula_out & 0x18) | m->border);
}

void machine_take_snapshot(const Machine *m, Snapshot *s)
{
    const Z80 *c = &m->cpu;
    /* durante a carga da fita virtual o estado que importa e o do snapshot
     * sendo carregado, nao o do carregador da ROM no meio do caminho */
    if (m->pending_snap && m->snap) { memcpy(s, m->snap, sizeof *s); return; }
    memset(s, 0, sizeof *s);
    memcpy(s->ram, m->mem + 0x4000, 49152);
    s->af = c->af; s->bc = c->bc; s->de = c->de; s->hl = c->hl;
    s->af_ = c->af_; s->bc_ = c->bc_; s->de_ = c->de_; s->hl_ = c->hl_;
    s->ix = c->ix; s->iy = c->iy; s->sp = c->sp; s->pc = c->pc;
    s->memptr = c->memptr;
    s->i = c->i; s->r = c->r;
    s->iff1 = c->iff1; s->iff2 = c->iff2; s->im = c->im;
    s->border = m->border & 7;
    s->halted = c->halted;
    s->tstates = c->tstates < m->frame_tstates ? c->tstates : 0;
    s->issue2 = m->cfg.issue2 ? 1 : 0;
    switch (m->cfg.joymode) {
    case JOYMODE_KEMPSTON:  s->joystick = 1; break;
    case JOYMODE_SINCLAIR2: s->joystick = 2; break;
    case JOYMODE_SINCLAIR1: s->joystick = 3; break;
    default:                s->joystick = 0; break;
    }
}

void machine_set_pending_snapshot(Machine *m, const Snapshot *s)
{
    if (!m->snap) {
        m->snap = (Snapshot *)malloc(sizeof(Snapshot));
        if (!m->snap) return;
    }
    memcpy(m->snap, s, sizeof(Snapshot));
    m->pending_snap = 1;
    m->snap_block_done = 0;
    m->snap_wait = 0;
}

static void install_pending(Machine *m)
{
    m->pending_snap = 0;
    m->snap_block_done = 0;
    m->snap_wait = 0;
    m->flash_idle = -1;
    if (m->tape) tape_stop(m->tape);
    if (!m->snap) return;
    machine_apply_snapshot(m, m->snap);
    if (m->snap_hook) m->snap_hook(m->snap_hook_user, m);
}

/* ------------------------------------------------------------------ */
/* Carga instantanea (armadilha em LD-BREAK 0x056B, formats.md 5.3)    */
/* ------------------------------------------------------------------ */

static uint8_t sz53p(uint8_t v)
{
    uint8_t f = (uint8_t)(v & (Z80_FLAG_S | Z80_FLAG_3 | Z80_FLAG_5));
    uint8_t p = v;
    p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
    if (!v) f |= Z80_FLAG_Z;
    if (!(p & 1)) f |= Z80_FLAG_PV;
    return f;
}

/* Flags de CP 1 com A = a */
static uint8_t cp1_flags(uint8_t a)
{
    uint8_t r = (uint8_t)(a - 1);
    uint8_t f = Z80_FLAG_N;
    f |= (uint8_t)(r & Z80_FLAG_S);
    if (!r) f |= Z80_FLAG_Z;
    if ((a & 0x0F) < 1) f |= Z80_FLAG_H;
    if (a == 0x80) f |= Z80_FLAG_PV;
    if (a < 1) f |= Z80_FLAG_C;
    return f;                       /* F3/F5 vem do operando (1): zero */
}

static int flash_load(Machine *m)
{
    Z80 *c = &m->cpu;
    const uint8_t *blk = NULL;
    size_t len = 0;
    if (m->rom[ROM_LD_BREAK] != 0xC0 || m->rom[ROM_LD_RET] != 0xC9) return 0;  /* ROM estranha */
    int idx = tape_current_block(m->tape);
    if (!tape_take_rom_block(m->tape, &blk, &len)) return 0;
    if (m->pending_snap && idx == VTAPE_SNAPSHOT_DATA_BLOCK) m->snap_block_done = 1;

    uint8_t want = (uint8_t)(c->af_ >> 8);
    int load = c->af_ & 1;
    uint16_t de = c->de, ix = c->ix;
    uint8_t A = Z80_A(c), F = Z80_F(c), B = Z80_B(c), H = Z80_H(c), L = Z80_L(c);
    size_t done = 0;

    if (len == 0) {
        L = 1;
        c->af_ = (uint16_t)((c->af_ & 0xFF00) | 0x01);
        F &= (uint8_t)~Z80_FLAG_C;
        goto out_regs;
    }
    uint8_t parity = blk[0];
    L = blk[0];
    A = 0;
    if (de == 0) {                       /* bug da ROM com tamanho 0 */
        B = 0xB0; A = parity; F = cp1_flags(A); H = parity;
        goto out_regs;
    }
    c->af_ = 0x0145;
    if ((uint8_t)(want ^ blk[0])) {
        A = (uint8_t)(want ^ blk[0]);
        F = sz53p(A);                    /* XOR: H=N=C=0 */
        H = parity;
        goto out_regs;
    }
    {
        size_t n = len - 1 < de ? len - 1 : de;
        int ok = 1;
        if (load) video_update(m, c->tstates);
        for (size_t i = 0; i < n; i++) {
            uint8_t b = blk[1 + i];
            uint16_t a = (uint16_t)(ix + i);
            if (!load) {
                if (m->mem[a] != b) { L = b; ok = 0; done = i; break; }
            } else if (a >= 0x4000) {
                m->mem[a] = b;
            }
            parity ^= b;
            L = b;
            done = i + 1;
        }
        if (!ok) {
            F = 0; H = parity;
        } else if (n == de && len > (size_t)de + 1) {
            uint8_t ck = blk[de + 1];
            parity ^= ck;
            L = ck;
            A = parity; F = cp1_flags(A); B = 0xB0; H = parity;
        } else {
            B = 0; L = 1; F = 0; H = parity;      /* fita acabou antes: timeout */
        }
    }
    c->de = (uint16_t)(de - done);
    c->ix = (uint16_t)(ix + done);
out_regs:
    c->af = (uint16_t)((A << 8) | F);
    c->bc = (uint16_t)((B << 8) | 0x01);
    c->hl = (uint16_t)((H << 8) | L);
    c->pc = ROM_LD_RET;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Laco principal                                                      */
/* ------------------------------------------------------------------ */

static void tape_edges(Machine *m)
{
    int lvl = tape_advance(m->tape, m->cpu.tstates);
    if (lvl != m->tape_ear) {
        m->tape_ear = lvl;
        if (m->tape_sound) audio_event(m);
    }
    /* a fita pode ter parado sozinha: o nivel do audio deixa de incluir o EAR */
    if (m->tape_sound && !tape_is_playing(m->tape)) audio_event(m);
}

/* Armadilhas por PC (so enderecos da ROM). Devolve 1 se mexeu na fita. */
static int rom_traps(Machine *m, uint16_t pc)
{
    Tape *t = m->tape;
    switch (pc) {
    case ROM_LD_BYTES:
        if (m->flash_idle >= 0) m->flash_idle = 0;
        /* fita virtual de snapshot lida de novo (reset + LOAD ""): rearma a
         * instalacao enquanto a ROM le os blocos antes do de 48K */
        if (m->vtape_snap && m->snap && !m->pending_snap) {
            int b = tape_current_block(t);
            if (b >= 0 && b < VTAPE_SNAPSHOT_DATA_BLOCK) {
                m->pending_snap = 1;
                m->snap_block_done = 0;
                m->snap_wait = 0;
            }
        }
        if (tape_is_loaded(t) && !tape_is_playing(t) && !tape_at_end(t)) {
            tape_play(t, m->cpu.tstates);
            return 1;
        }
        return 0;
    case ROM_LD_BREAK:
        if (m->cfg.flash_load && tape_is_loaded(t)) {
            if (flash_load(m)) {
                tape_stop(t);
                m->flash_idle = tape_at_end(t) ? -1 : 0;
                return 1;
            }
            /* bloco nao padrao: carga real; garante a fita tocando */
            if (!tape_is_playing(t) && !tape_at_end(t)) { tape_play(t, m->cpu.tstates); return 1; }
        }
        return 0;
    case ROM_LD_RET:
        /* fim do bloco de 48K da fita virtual: IX deu a volta (0x4000+0xC000) e DE = 0 */
        if (m->pending_snap && m->cpu.ix == 0 && m->cpu.de == 0) {
            install_pending(m);
            return 1;
        }
        return 0;
    case ROM_SA_LD_RET:
        if (m->pending_snap && (m->snap_block_done ||
                                tape_block_finished(t, VTAPE_SNAPSHOT_DATA_BLOCK))) {
            install_pending(m);
            return 1;
        }
        return 0;
    case ROM_MAIN_1:
        m->rom_ready = 1;
        return 0;
    case ROM_KEY_IN_RET:
        if (m->cpu.af & Z80_FLAG_Z) m->key_wait_frame = m->frame_count + 1;
        return 0;
    default:
        return 0;
    }
}

void machine_run_frame(Machine *m)
{
    Z80 *c = &m->cpu;
    Tape *tp = m->tape;
    const uint32_t ft = m->frame_tstates;
    int irq = 1;

    apply_contention_cfg(m);
    m->in_frame = 1;
    m->video_tstate = 0;
    m->aud_count = 0;
    m->aud_level_start = m->aud_level;

    uint32_t edge = tape_next_edge(tp);
    for (;;) {
        uint32_t t = c->tstates;
        if (t >= ft) break;
        if (irq) {
            if (t < (uint32_t)m->int_length) {
                if (z80_interrupt(c, 0xFF)) { irq = 0; m->int_count++; continue; }
            } else {
                irq = 0;
            }
        }
        if (t >= edge) {
            tape_edges(m);
            edge = tape_next_edge(tp);
        }
        uint16_t pc = c->pc;
        if ((uint16_t)(pc - ROM_SA_LD_RET) <= (uint16_t)(ROM_MAIN_1 - ROM_SA_LD_RET)) {
            if (rom_traps(m, pc)) edge = tape_next_edge(tp);
            if (c->pc != pc) continue;   /* armadilha desviou o PC */
        }
        z80_step(c);
    }

    video_update(m, ft);
    m->in_frame = 0;

    /* rebaseia o relogio */
    c->tstates -= ft;
    for (int i = 0; i < m->aud_count; i++)
        if (m->aud_ev[i].t > ft) m->aud_ev[i].t = ft;
    tape_frame_end(tp, ft);

    m->frame_count++;
    /* FLASH: contador de quadros de 6 bits, bit 5 (VHDL do clone da ULA): 32 quadros por fase */
    if ((m->frame_count & 31) == 0) m->flash_phase ^= 1;

    autotype_frame(m);

    /* seguranca: bloco de 48K terminou mas a CPU nao passou por 0x053F/0x05E2 */
    if (m->pending_snap) {
        if (!m->snap_block_done && tape_block_finished(tp, VTAPE_SNAPSHOT_DATA_BLOCK))
            m->snap_block_done = 1;
        if (m->snap_block_done && ++m->snap_wait > 2) install_pending(m);
    }

    /* carga instantanea: se ninguem chamou o LD-BYTES por um tempo, da play
     * (o jogo pode ter um carregador turbo proprio para os blocos seguintes) */
    if (m->flash_idle >= 0) {
        if (!tape_is_loaded(tp) || tape_at_end(tp) || tape_is_playing(tp)) {
            m->flash_idle = -1;
        } else if (++m->flash_idle >= FLASH_RESUME_FRAMES) {
            m->flash_idle = -1;
            tape_play(tp, c->tstates);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Abrir arquivos                                                      */
/* ------------------------------------------------------------------ */

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) snprintf(err, errlen, "%s", msg);
}

/* Nome da fita: nome do arquivo sem diretorio e extensao, maiusculo, ate 10
 * caracteres. Letras acentuadas perdem o acento (como no OSD); outros
 * caracteres nao ASCII viram '?' (um por caractere). */
static void tape_name_from_path(const char *path, char *out)
{
    const char *b = path, *p;
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    const char *dot = strrchr(b, '.');
    const unsigned char *q = (const unsigned char *)b;
    const unsigned char *qe = q + (dot && dot > b ? (size_t)(dot - b) : strlen(b));
    size_t k = 0;
    while (q < qe && k < 10) {
        unsigned cp = tk_utf8_next(&q);
        unsigned char ch = (unsigned char)(cp < 0x20 || cp == 0x7F ? '_' : tk_ascii_fold(cp));
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
        out[k++] = (char)ch;
    }
    out[k] = 0;
    if (!k) strcpy(out, "PROGRAMA");
}

static int load_vtape(Machine *m, uint8_t *tap, size_t taplen, char *err, size_t errlen)
{
    char e2[256];
    int r = tape_load_tap_mem(m->tape, tap, taplen, e2, sizeof e2);
    free(tap);
    if (r != 0) { set_err(err, errlen, e2); return -1; }
    m->vtape_snap = 0;
    return 0;
}

/* Fita nova no gravador: reset, tela de abertura e LOAD "" digitado */
static int start_tape(Machine *m)
{
    m->vtape_snap = 0;
    machine_reset(m);
    machine_autoload(m, 0, 1);
    return 0;
}

/* Snapshot ja lido: aplica direto (instant) ou monta a fita virtual. */
static int open_snapshot(Machine *m, const Snapshot *s, const char *name, int instant,
                         char *err, size_t errlen)
{
    uint8_t *tap = NULL;
    size_t taplen = 0;
    if (instant) {
        /* a fita no gravador (e m->snap) nao mudam: vtape_snap continua valendo */
        if (m->tape) tape_stop(m->tape);
        m->autotype.active = 0;
        autotype_release(m);
        m->pending_snap = 0;
        m->snap_block_done = 0;
        m->flash_idle = -1;
        machine_apply_snapshot(m, s);
        return 0;
    }
    if (vtape_from_snapshot(s, name, &tap, &taplen) != 0) {
        set_err(err, errlen, "Falha ao montar a fita virtual do snapshot");
        return -1;
    }
    if (load_vtape(m, tap, taplen, err, errlen) != 0) return -1;
    machine_reset(m);
    machine_set_pending_snapshot(m, s);
    m->vtape_snap = 1;          /* a fita no gravador e a deste snapshot */
    machine_autoload(m, 0, 1);
    return 0;
}

/* Cadeia de comprimentos de um TAP (len, dados[len], ...) termina exatamente no fim? */
static int tap_chain_ok(const uint8_t *d, size_t len)
{
    size_t pos = 0;
    while (pos + 2 <= len) {
        size_t bl = (size_t)(d[pos] | (d[pos + 1] << 8));
        if (bl < 2 || pos + 2 + bl > len) return 0;
        pos += 2 + bl;
    }
    return len > 0 && pos == len;
}

/* Binario cru (.bin/.cod ou cabecalho +3DOS) -> fita virtual com carregador BASIC.
 * Cabecalho +3DOS (formats.md secao 8): 128 bytes, tipo 3 = CODE com org e
 * tamanho; o --org do usuario prevalece. */
static int open_binary(Machine *m, const uint8_t *data, size_t len, uint16_t org, uint16_t exec,
                       const char *name, char *err, size_t errlen)
{
    const uint8_t *payload = data;
    size_t plen = len;
    uint8_t *tap = NULL;
    size_t taplen = 0;
    if (len >= 128 && !memcmp(data, "PLUS3DOS\x1A", 9)) {
        unsigned sum = 0;
        for (int i = 0; i < 127; i++) sum += data[i];
        if ((sum & 0xFF) != data[127]) {
            set_err(err, errlen, "Cabecalho +3DOS com checksum invalido");
            return -1;
        }
        if (data[15] != 3) {
            set_err(err, errlen, "Arquivo +3DOS nao e de codigo (tipo 3); programas BASIC e "
                                 "matrizes nao sao suportados");
            return -1;
        }
        size_t hlen = (size_t)(data[16] | (data[17] << 8));
        plen = len - 128;
        if (hlen < plen) plen = hlen;
        payload = data + 128;
        if (!org) org = (uint16_t)(data[18] | (data[19] << 8));
        if (!plen) { set_err(err, errlen, "Arquivo +3DOS sem dados"); return -1; }
    }
    if (!org) org = 32768;
    if (!exec) exec = org;
    if ((uint32_t)org < 0x4000 || (uint32_t)org + plen > 0x10000) {
        set_err(err, errlen, "O binario nao cabe na RAM (verifique --org)");
        return -1;
    }
    if (vtape_from_binary(payload, plen, org, exec, name, &tap, &taplen) != 0) {
        set_err(err, errlen, "Falha ao montar a fita virtual do binario");
        return -1;
    }
    if (load_vtape(m, tap, taplen, err, errlen) != 0) return -1;
    return start_tape(m);
}

/* Tela .SCR -> fita virtual */
static int open_screen(Machine *m, const uint8_t *data, size_t len, const char *name,
                       char *err, size_t errlen)
{
    uint8_t *tap = NULL;
    size_t taplen = 0;
    if (vtape_from_screen(data, len, name, &tap, &taplen) != 0) {
        set_err(err, errlen, "Tela .SCR invalida (esperados 6912 bytes)");
        return -1;
    }
    if (load_vtape(m, tap, taplen, err, errlen) != 0) return -1;
    return start_tape(m);
}

/* Arquivo de extensao desconhecida (ou sem extensao): pelo conteudo e pelo
 * tamanho (formats.md secao 9); o que nao for reconhecido e recusado (codigo
 * cru so com .bin/.cod/.raw/.obj, para nao executar lixo). */
static int open_by_content(Machine *m, const uint8_t *data, size_t len, uint16_t org,
                           uint16_t exec, const char *name, int instant, char *err, size_t errlen)
{
    char e2[256];
    if (len >= 10 && !memcmp(data, "ZXTape!\x1A", 8)) {
        if (tape_load_tzx_mem(m->tape, data, len, e2, sizeof e2) != 0) {
            set_err(err, errlen, e2);
            return -1;
        }
        return start_tape(m);
    }
    if (tape_sniff_unsupported(data, len, err, errlen)) return -1;
    if (len >= 4 && !memcmp(data, "ZXST", 4)) {
        set_err(err, errlen, "Formato SZX nao suportado (use .z80 ou .sna)");
        return -1;
    }
    if (len >= 128 && !memcmp(data, "PLUS3DOS\x1A", 9))
        return open_binary(m, data, len, org, exec, name, err, errlen);
    if (len == 49179 || len == 131103 || len == 147487) {      /* SNA (128K e recusado) */
        Snapshot *s = (Snapshot *)malloc(sizeof(Snapshot));
        int r;
        if (!s) { set_err(err, errlen, "Memoria insuficiente"); return -1; }
        r = snapshot_load_sna(data, len, s, err, errlen);
        if (r == 0) r = open_snapshot(m, s, name, instant, err, errlen);
        free(s);
        return r;
    }
    if (tap_chain_ok(data, len)) {
        if (tape_load_tap_mem(m->tape, data, len, e2, sizeof e2) != 0) {
            set_err(err, errlen, e2);
            return -1;
        }
        return start_tape(m);
    }
    if (len == 6912) return open_screen(m, data, len, name, err, errlen);
    set_err(err, errlen, "Formato de arquivo nao reconhecido (use .tap .tzx .sna .z80 .scr .bin .cod)");
    return -1;
}

int machine_open_file(Machine *m, const char *path, uint16_t org, uint16_t exec,
                      int instant, char *err, size_t errlen)
{
    char ext[16], name[16];
    uint8_t *data = NULL;
    size_t len = 0;
    int r;

    if (err && errlen) err[0] = 0;
    if (!path || !path[0]) { set_err(err, errlen, "Nenhum arquivo informado"); return -1; }
    tk_file_ext(path, ext, sizeof ext);
    tape_name_from_path(path, name);

    if (!strcmp(ext, "tap") || !strcmp(ext, "tzx")) {
        if (tape_load_file(m->tape, path, err, errlen) != 0) return -1;
        return start_tape(m);
    }

    if (!strcmp(ext, "sna") || !strcmp(ext, "z80")) {
        Snapshot *s = (Snapshot *)malloc(sizeof(Snapshot));
        if (!s) { set_err(err, errlen, "Memoria insuficiente"); return -1; }
        r = snapshot_load_file(path, s, err, errlen);
        if (r == 0) r = open_snapshot(m, s, name, instant, err, errlen);
        free(s);
        return r;
    }

    if (!strcmp(ext, "rom")) {
        set_err(err, errlen, "Arquivo de ROM: use a opcao --rom para troca-la");
        return -1;
    }

    data = tk_read_file(path, &len, 1u << 24);
    if (!data) { set_err(err, errlen, "Nao foi possivel ler o arquivo"); return -1; }
    if (len == 0) { free(data); set_err(err, errlen, "Arquivo vazio"); return -1; }

    if (!strcmp(ext, "scr"))
        r = open_screen(m, data, len, name, err, errlen);
    else if (!strcmp(ext, "bin") || !strcmp(ext, "cod") || !strcmp(ext, "raw") || !strcmp(ext, "obj"))
        r = open_binary(m, data, len, org, exec, name, err, errlen);
    else
        r = open_by_content(m, data, len, org, exec, name, instant, err, errlen);
    free(data);
    return r;
}
