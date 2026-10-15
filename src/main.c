/*
 * main.c - Front-end SDL2 do emulador TK90X / TK95: linha de comando,
 * janela (escala inteira, 4:3, tela cheia), audio por fila (SDL_QueueAudio),
 * teclado PC -> Spectrum (inclusive pontuacao de qualquer layout via
 * SDL_TEXTINPUT), controles de jogo, turbo durante a carga da fita,
 * OSD (ajuda, navegador de arquivos) e modo headless para testes.
 *
 * Controles de toque (touch.h): sempre no Android; no Linux/Windows com
 * --touch, pelo mouse (e com --touch-script, por toques sinteticos, para
 * testar a interface sem aparelho).
 */
#include <SDL.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "machine.h"
#include "osd.h"
#include "platform.h"
#include "png.h"
#include "profiles.h"
#include "snapshot.h"
#include "tape.h"
#include "touch.h"
#ifdef __ANDROID__
#include <setjmp.h>
#include "android_bridge.h"
#endif

#define APP_NAME     "TK90X/TK95"
#define APP_VERSION  "1.3"

#define AUDIO_RATE   44100
#define AUDIO_VOLUME 9000
#define TURBO_BUDGET 0.015          /* segundos de emulacao por quadro exibido */
#define TAP_HOLD     3              /* quadros com a tecla pressionada (toque) */
#define TAP_GAP      6              /* quadros soltos (a ROM precisa de 5 p/ repetir tecla) */
#define TAPQ_SIZE    256
#define UNTIL_LOADED_CAP 200000L
#define TOAST_MS     2500
#define PROFILE_TOAST_MS 5000      /* aviso "Controles: ..." de um jogo com perfil */
#define STATUS_MS    3000           /* status da fita visivel apos cada mudanca de bloco */
#define LOAD_SPEED_DEFAULT 20       /* carga nostalgica: som e listras originais, 20x */
#define LOAD_LP_HZ   7000.0         /* passa-baixa suave no som da fita durante a carga */

/* ------------------------------------------------------------------ */
/* Opcoes                                                              */
/* ------------------------------------------------------------------ */

typedef struct Options {
    Model       model;
    const char *rom_path;
    int         hz60;
    int         scale;
    int         fullscreen;
    int         instant;
    int         flash;
    int         load_speed;
    uint16_t    org, exec;
    JoyMode     joymode;
    int         no_sound;
    int         headless;
    long        frames;
    int         frames_set;
    int         until_loaded;
    const char *screenshot;
    const char *type_text;
    const char *file;
    const char *wav_out;    /* (depuracao) grava em WAV o audio entregue ao SDL */
    int         contention;
    int         touch;          /* controles de toque (--touch; no Android, sempre) */
    const char *touch_script;   /* (testes) toques sinteticos por quadro */
    const char *window_shot;    /* (testes) grava a janela renderizada ao sair */
    int         win_w, win_h;   /* (testes) --window-size LxA */
    int         touch_picker;   /* (testes) mostra a entrada do seletor do Android */
    int         safe_set;       /* (testes) --safe-insets: recorte simulado da camera */
    int         safe_ins[4];    /* esquerda, alto, direita, baixo (pixels) */
} Options;

/* --touch-script: evento sintetico agendado para um quadro exibido */
typedef struct ScriptEv {
    unsigned long tick;
    SDL_Event     ev;
} ScriptEv;
#define SCRIPT_Q 512

/* Estado de uma tecla PC (por scancode) */
typedef struct HeldKey {
    uint64_t keys;          /* teclas Spectrum (bit = ZXKey) */
    uint8_t  joy;           /* bits JOY_* */
    uint8_t  active;
    uint8_t  seen;          /* ja passou por pelo menos um quadro emulado */
    uint8_t  release_pending;
} HeldKey;

typedef struct PendKey {
    int      sc;
    uint64_t keys;
    uint8_t  joy;
} PendKey;

typedef struct App {
    Options   opt;
    Machine  *m;
    Osd       osd;
    uint8_t   rom[16384];
    int       hz60;
    char      file_name[PLAT_PATH_MAX];   /* nome base do arquivo atual */
    const GameProfile *profile;           /* perfil de controle do jogo aberto (NULL = nenhum) */
    JoyMode   user_joymode;               /* modo do usuario, restaurado quando o perfil sai */
    int       profile_toast;              /* mostrar "Controles: ..." quando a carga terminar */

    /* Teclado */
    HeldKey   held[SDL_NUM_SCANCODES];
    int       any_unseen;
    int       phys[ZXK_COUNT];            /* quantas teclas PC seguram cada tecla Spectrum */
    int       joy_cnt[8];                 /* idem para os bits de joystick */
    uint8_t   applied[ZXK_COUNT];         /* estado ja entregue a maquina */
    PendKey   pend[16];
    int       npend;
    int       arrows_mode;                /* F4: ARROWS_AUTO (padrao), ARROWS_JOY, ARROWS_CURSOR */
    int       suppress_text;              /* tecla foi para um menu: ignora o texto dela */
    int       last_down_sc;               /* scancode da ultima tecla pressionada (-1 = consumido) */
    int       last_down_repeat;           /* a ultima pressao foi auto-repeticao do sistema */

    /* Fila de toques (pontuacao, --type) */
    uint8_t   tapq[TAPQ_SIZE][3];
    int       tap_head, tap_count;
    int       tap_phase, tap_timer;       /* 0 livre, 1 pressionando, 2 intervalo */
    uint8_t   tap_cur[3];
    size_t    type_pos;                   /* posicao em opt.type_text */
    int       type_done;
    int       type_quote;                 /* --type: dentro de aspas (texto literal) */
    int       type_rem;                   /* --type: depois de REM ate o fim da linha */
    int       type_wait;                  /* --type: quadros esperando o modo K */
    int       type_prev;                  /* --type: ultimo caractere entregue */

    /* Joystick */
    uint8_t   pad_joy, last_joy;
    SDL_GameController *pad;
    SDL_Joystick *joy;
    SDL_JoystickID pad_id;

    /* Turbo */
    int       turbo_forced;
    int       turbo_load_only;            /* F7 durante a carga: turbo so ate a carga acabar */
    char      status_key[OSD_TEXT_MAX];   /* status da fita: mostra por alguns segundos ao mudar */
    Uint32    status_since;
    int       auto_suppressed;

    /* SDL */
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    SDL_AudioDeviceID audio;
    double    audio_acc;
    float     lp_y;                       /* estado do passa-baixa da carga */
    FILE     *wav;                        /* --wav-out */
    uint32_t  wav_bytes;
    int       speed_now;                  /* quadros emulados por exibido (0 = maximo) */
    int       fullscreen;
    char      title[512];
    int       quit;

    unsigned long stat_frames, stat_shown;   /* TK90X_STATS=1: estatisticas no stderr */

    /* Toque */
    TouchUI  *touch;                      /* NULL sem --touch (e fora do Android) */
    SDL_Texture *ui_tex;                  /* tela dos menus de toque (osd_render_canvas) */
    uint32_t *ui_px;
    int       ui_w, ui_h, ui_s;           /* tamanho da tela e escala inteira na janela */
    SDL_Rect  pic;                        /* onde vai a imagem do emulador */
    int       out_w, out_h;               /* area de desenho em pixels */
    int       osd_ptr;                    /* ha um dedo/mouse nos menus do OSD */
    SDL_TouchID osd_ptr_dev;
    SDL_FingerID osd_ptr_id;
    int       bg_paused;                  /* app em segundo plano (Android) */
    int       diag;                       /* diagnostico: estado do joystick no canto */
    char      save_dir[PLAT_PATH_MAX];    /* snapshots e capturas ("" = diretorio atual) */

    /* --touch-script */
    char     *script_buf;                 /* texto do script (NULL = sem script) */
    int       script_n, script_pos;
    ScriptEv  sq[SCRIPT_Q];
    int       sq_n;
    unsigned long tick;                   /* quadros exibidos (render) */
    int       next_auto_id;
    char      shot_path[PLAT_PATH_MAX];
    int       shot_pending;

    uint32_t  disp[FB_W * FB_H];
    int16_t   abuf[4096];
} App;

static int g_headless = 0;      /* sem janela: erros so no stderr */

/* ------------------------------------------------------------------ */
/* Utilitarios                                                         */
/* ------------------------------------------------------------------ */

#ifdef __ANDROID__
/* Android: fatal() volta para o main (o SDL pede que SDL_main retorne em vez de
 * chamar exit(), que derrubaria o processo junto com a Activity) */
static jmp_buf g_fatal_jmp;
static int g_fatal_armed;
#endif

static void show_box(int error, const char *msg)
{
#if defined(_WIN32) || defined(__ANDROID__)
    if (!g_headless)
        SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
                                 APP_NAME, msg, NULL);
#else
    (void)error; (void)msg;
#endif
}

static void fatal(const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "tk90x: %s\n", msg);
#ifdef __ANDROID__
    SDL_Log("tk90x: %s", msg);
#endif
    show_box(1, msg);
#ifdef __ANDROID__
    if (g_fatal_armed) longjmp(g_fatal_jmp, 1);
#endif
    exit(1);
}

static void copy_str(char *out, size_t outlen, const char *s)
{
    if (!outlen) return;
    size_t n = strlen(s);
    if (n >= outlen) n = outlen - 1;
    memmove(out, s, n);
    out[n] = 0;
}

static const char *usage_text =
    "Uso: tk90x [opcoes] [arquivo]\n"
    "Arquivos: .tap .tzx .sna .z80 .scr .bin .cod\n"
    "  --model tk95|tk90x|tk90x-v1   modelo (padrao tk95)\n"
    "  --rom ARQUIVO          ROM alternativa de 16 KB\n"
    "  --50hz | --60hz        frequencia do video (padrao 50 Hz)\n"
    "  --scale N              escala da janela (padrao 3)\n"
    "  --fullscreen           tela cheia\n"
    "  --instant              snapshots sem fita virtual\n"
    "  --flash                carga instantanea (armadilha na ROM)\n"
    "  --load-speed N         carga: N vezes (padrao 20: som e listras originais),\n"
    "                         1 = tempo real, 0 = maxima\n"
    "  --org END --exec END   para .bin/.cod (padrao org 32768 ou o do +3DOS, exec = org)\n"
    "  --joystick kempston|sinclair1|sinclair2|cursor|qaop|keys  (padrao sinclair1;\n"
    "                         os jogos conhecidos escolhem o seu ao abrir)\n"
    "  --no-contention        desliga a contencao de memoria da ULA (padrao: ligada)\n"
    "  --no-sound             sem som\n"
    "  --headless             sem janela/audio (testes)\n"
    "  --frames N             (headless) roda N quadros (padrao 500)\n"
    "  --until-loaded         (headless) roda ate a carga terminar (+150 quadros)\n"
    "  --screenshot ARQ.png   grava a tela ao sair\n"
    "  --type \"TEXTO\"         digita o texto apos o boot (\\n = ENTER; PRINT, GOTO... viram tokens)\n"
    "  --wav-out ARQ.wav      (depuracao) grava o audio entregue a placa de som\n"
    "  --touch                controles de toque na tela (mouse), como no Android\n"
    "  --touch-script ARQ (ou --key-script), --window-shot ARQ.png, --window-size LxA\n"
    "                         (testes) toques/teclas sinteticos e captura da janela\n"
    "  --safe-insets E,A,D,B  (testes) recorte da camera simulado: pixels cobertos a\n"
    "                         esquerda, no alto, a direita e embaixo (modo toque)\n"
    "Na janela: F1 = menu e ajuda, F2 = abrir jogo, F3 = fita play/pausa,\n"
    "  F4 = setas (automatico/joystick/cursores), F5 = reset, F6 = salvar estado,\n"
    "  F7 = turbo, F8 = 50/60 Hz, F9 = tipo de joystick, F10 = modelo,\n"
    "  F11 = tela cheia, F12 = captura, Esc = sair. Setas = joystick nos jogos e\n"
    "  cursores no BASIC; Alt = fogo. Arraste um arquivo para a janela para abrir.\n"
    "  --help, --version\n";

static long parse_num(const char *opt, const char *s, long min, long max)
{
    char *end = NULL;
    long v;
    if (!s || !*s) fatal("opcao %s precisa de um valor", opt);
    size_t n = strlen(s);
    if (s[0] == '$' || s[0] == '#') {
        v = strtol(s + 1, &end, 16);
        if (end == s + 1) end = NULL;           /* "$" sozinho nao e numero */
    } else if (n > 1 && (s[n - 1] == 'h' || s[n - 1] == 'H')) {
        v = strtol(s, &end, 16);                /* 8000h */
        if (end == s + n - 1) end++;
    } else {
        v = strtol(s, &end, 0);
    }
    if (!end || *end || v < min || v > max)
        fatal("valor invalido para %s: '%s' (esperado %ld..%ld)", opt, s, min, max);
    return v;
}

static void parse_args(Options *o, int argc, char **argv)
{
    memset(o, 0, sizeof *o);
    o->model = MODEL_TK95;
    o->scale = 3;
    o->org = 0;             /* 0 = padrao: cabecalho +3DOS, senao 32768 */
    o->joymode = JOYMODE_SINCLAIR1;
    o->contention = 1;      /* a ULA Microdigital tem contencao (VHDL do clone, hardware 2.1) */
    o->load_speed = LOAD_SPEED_DEFAULT; /* carga nostalgica: som e listras originais, 20x */
    o->frames = 500;

    int only_files = 0;                     /* depois de "--": tudo e arquivo */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fatal("opcao %s precisa de um valor", a), (char *)NULL))
        if (only_files || a[0] != '-' || !a[1]) {
            if (o->file) fatal("mais de um arquivo na linha de comando: '%s'", a);
            o->file = a;
            continue;
        }
        if (a[1] == '-' && !a[2]) {         /* "--": o resto e arquivo */
            only_files = 1;
            continue;
        }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            printf("%s", usage_text);
            show_box(0, usage_text);
            exit(0);
        } else if (!strcmp(a, "--version")) {
            char v[64];
            snprintf(v, sizeof v, "%s %s\n", APP_NAME, APP_VERSION);
            printf("%s", v);
            show_box(0, v);                 /* -mwindows sem console: senao nada aparece */
            exit(0);
        } else if (!strcmp(a, "--model")) {
            const char *v = NEXT();
            if (!plat_strcasecmp(v, "tk95")) o->model = MODEL_TK95;
            else if (!plat_strcasecmp(v, "tk90x")) o->model = MODEL_TK90X;
            else if (!plat_strcasecmp(v, "tk90x-v1") || !plat_strcasecmp(v, "tk90xv1"))
                o->model = MODEL_TK90X_V1;
            else fatal("modelo desconhecido '%s' (use tk95, tk90x ou tk90x-v1)", v);
        } else if (!strcmp(a, "--rom")) {
            o->rom_path = NEXT();
        } else if (!strcmp(a, "--50hz")) {
            o->hz60 = 0;
        } else if (!strcmp(a, "--60hz")) {
            o->hz60 = 1;
        } else if (!strcmp(a, "--scale")) {
            o->scale = (int)parse_num(a, NEXT(), 1, 10);
        } else if (!strcmp(a, "--fullscreen")) {
            o->fullscreen = 1;
        } else if (!strcmp(a, "--instant")) {
            o->instant = 1;
        } else if (!strcmp(a, "--flash")) {
            o->flash = 1;
        } else if (!strcmp(a, "--load-speed")) {
            o->load_speed = (int)parse_num(a, NEXT(), 0, 1000);
        } else if (!strcmp(a, "--org")) {
            o->org = (uint16_t)parse_num(a, NEXT(), 0, 65535);
        } else if (!strcmp(a, "--exec")) {
            o->exec = (uint16_t)parse_num(a, NEXT(), 0, 65535);
        } else if (!strcmp(a, "--joystick")) {
            const char *v = NEXT();
            if (!plat_strcasecmp(v, "kempston")) o->joymode = JOYMODE_KEMPSTON;
            else if (!plat_strcasecmp(v, "sinclair1") || !plat_strcasecmp(v, "sinclair"))
                o->joymode = JOYMODE_SINCLAIR1;
            else if (!plat_strcasecmp(v, "sinclair2")) o->joymode = JOYMODE_SINCLAIR2;
            else if (!plat_strcasecmp(v, "cursor") || !plat_strcasecmp(v, "protek"))
                o->joymode = JOYMODE_CURSOR;
            else if (!plat_strcasecmp(v, "qaop")) o->joymode = JOYMODE_QAOP;
            else if (!plat_strcasecmp(v, "keys") || !plat_strcasecmp(v, "jogo"))
                o->joymode = JOYMODE_KEYS;
            else fatal("joystick desconhecido '%s' (kempston, sinclair1, sinclair2, cursor, qaop, keys)", v);
        } else if (!strcmp(a, "--contention")) {
            o->contention = 1;
        } else if (!strcmp(a, "--no-contention")) {
            o->contention = 0;
        } else if (!strcmp(a, "--no-sound")) {
            o->no_sound = 1;
        } else if (!strcmp(a, "--headless")) {
            o->headless = 1;
        } else if (!strcmp(a, "--frames")) {
            o->frames = parse_num(a, NEXT(), 1, 100000000L);
            o->frames_set = 1;
        } else if (!strcmp(a, "--until-loaded")) {
            o->until_loaded = 1;
        } else if (!strcmp(a, "--screenshot")) {
            o->screenshot = NEXT();
        } else if (!strcmp(a, "--type")) {
            o->type_text = NEXT();
        } else if (!strcmp(a, "--wav-out")) {
            o->wav_out = NEXT();
        } else if (!strcmp(a, "--touch")) {
            o->touch = 1;
        } else if (!strcmp(a, "--touch-script") || !strcmp(a, "--key-script")) {
            o->touch_script = NEXT();
        } else if (!strcmp(a, "--touch-picker")) {
            o->touch_picker = 1;
        } else if (!strcmp(a, "--window-shot")) {
            o->window_shot = NEXT();
        } else if (!strcmp(a, "--window-size")) {
            const char *v = NEXT();
            if (sscanf(v, "%dx%d", &o->win_w, &o->win_h) != 2 || o->win_w < 160 ||
                o->win_h < 120 || o->win_w > 16384 || o->win_h > 16384)
                fatal("valor invalido para --window-size: '%s' (ex.: 2400x1080)", v);
        } else if (!strcmp(a, "--safe-insets")) {
            const char *v = NEXT();
            int *ins = o->safe_ins;
            if (sscanf(v, "%d,%d,%d,%d", &ins[0], &ins[1], &ins[2], &ins[3]) != 4 ||
                ins[0] < 0 || ins[1] < 0 || ins[2] < 0 || ins[3] < 0 ||
                ins[0] > 4096 || ins[1] > 4096 || ins[2] > 4096 || ins[3] > 4096)
                fatal("valor invalido para --safe-insets: '%s' (ex.: 136,0,0,0)", v);
            o->safe_set = 1;
        } else {
            fatal("opcao desconhecida '%s' (veja --help)", a);
        }
#undef NEXT
    }
}

static const char *joymode_short(JoyMode j)
{
    switch (j) {
    case JOYMODE_KEMPSTON:  return "Kempston";
    case JOYMODE_SINCLAIR1: return "Sinclair 1";
    case JOYMODE_SINCLAIR2: return "Sinclair 2";
    case JOYMODE_CURSOR:    return "Cursor";
    case JOYMODE_QAOP:      return "Teclas QAOP";
    case JOYMODE_KEYS:      return "Teclas do jogo";
    default:                return "?";
    }
}

static const char *joymode_name(JoyMode j)
{
    switch (j) {
    case JOYMODE_KEMPSTON:  return "Kempston (porta 31)";
    case JOYMODE_SINCLAIR1: return "Sinclair 1 (6-0, embutido)";
    case JOYMODE_SINCLAIR2: return "Sinclair 2 (1-5)";
    case JOYMODE_CURSOR:    return "Cursor (5-8, 0)";
    case JOYMODE_QAOP:      return "Teclas QAOP (espaco = fogo)";
    case JOYMODE_KEYS:      return "Teclas do jogo";
    default:                return "?";
    }
}

/* Setas do PC (F4). Automatico: joystick do modo escolhido (F9; Sinclair 1 =
 * teclas 6-0 do TK) enquanto um programa roda, e cursores do BASIC (CAPS+5..8)
 * so com a ROM esperando tecla no editor. Quase todos os jogos leem o joystick
 * do TK (6 esq., 7 dir., 8 baixo, 9 cima): como cursores, as setas cairiam em
 * 5-8 (seta para baixo = 6 = esquerda no jogo). */
enum { ARROWS_AUTO = 0, ARROWS_JOY, ARROWS_CURSOR, ARROWS_COUNT };
#define EDITOR_FRAMES 10    /* ROM no editor em algum dos ultimos N quadros */

static const char *arrows_name(int mode)
{
    switch (mode) {
    case ARROWS_AUTO:   return "Automatico";
    case ARROWS_JOY:    return "Joystick";
    case ARROWS_CURSOR: return "Cursores";
    default:            return "?";
    }
}

/* Carrega a ROM do modelo (arquivo roms/ ao lado do executavel, ./roms ou embutida) */
static void model_rom(Model model, uint8_t *out)
{
    char *base = SDL_GetBasePath();
    machine_model_rom(model, base, out);
    if (base) SDL_free(base);
}

static double frame_rate(const App *a)
{
    return (double)TK_CPU_HZ / (double)a->m->frame_tstates;
}

/* ------------------------------------------------------------------ */
/* Teclado: estado fisico, fila de toques                              */
/* ------------------------------------------------------------------ */

static void sync_keys(App *a)
{
    uint8_t want[ZXK_COUNT];
    int tapping = a->tap_phase == 1;
    uint64_t tk = a->touch ? touch_keys(a->touch) : 0;   /* teclado da tela */
    for (int k = 0; k < ZXK_COUNT; k++) {
        int on = a->phys[k] > 0;
        /* durante um toque, CAPS/SYM fisicos (ex.: Shift do PC usado para
         * digitar a pontuacao) ficam de fora */
        if (tapping && (k == ZXK_CAPS || k == ZXK_SYM)) on = 0;
        if (tk & ((uint64_t)1 << k)) on = 1;
        want[k] = (uint8_t)on;
    }
    if (tapping)
        for (int i = 0; i < 3; i++)
            if (a->tap_cur[i] < ZXK_COUNT) want[a->tap_cur[i]] = 1;
    for (int k = 0; k < ZXK_COUNT; k++) {
        if (want[k] != a->applied[k]) {
            machine_key(a->m, (ZXKey)k, want[k]);
            a->applied[k] = want[k];
        }
    }
    uint8_t joy = a->pad_joy;
    if (a->touch) joy |= touch_joy(a->touch);
    for (int b = 0; b < 5; b++)
        if (a->joy_cnt[b] > 0) joy |= (uint8_t)(1u << b);
    if (joy != a->last_joy) {
        machine_joy(a->m, joy);
        a->last_joy = joy;
    }
}

static void press_sc(App *a, int sc, uint64_t keys, uint8_t joy)
{
    HeldKey *h = &a->held[sc];
    if (h->active) return;
    h->keys = keys;
    h->joy = joy;
    h->active = 1;
    h->seen = 0;
    h->release_pending = 0;
    a->any_unseen = 1;
    for (int k = 0; k < ZXK_COUNT; k++)
        if (keys & ((uint64_t)1 << k)) a->phys[k]++;
    for (int b = 0; b < 5; b++)
        if (joy & (1u << b)) a->joy_cnt[b]++;
}

static void do_release(App *a, int sc)
{
    HeldKey *h = &a->held[sc];
    for (int k = 0; k < ZXK_COUNT; k++)
        if ((h->keys & ((uint64_t)1 << k)) && a->phys[k] > 0) a->phys[k]--;
    for (int b = 0; b < 5; b++)
        if ((h->joy & (1u << b)) && a->joy_cnt[b] > 0) a->joy_cnt[b]--;
    memset(h, 0, sizeof *h);
}

static void release_sc(App *a, int sc)
{
    HeldKey *h = &a->held[sc];
    if (!h->active) return;
    /* toque rapido demais: garante ao menos um quadro (uma interrupcao) pressionada */
    if (!h->seen) { h->release_pending = 1; return; }
    do_release(a, sc);
}

/* Depois de cada quadro emulado */
static void keys_after_frame(App *a)
{
    if (!a->any_unseen) return;
    a->any_unseen = 0;
    for (int sc = 0; sc < SDL_NUM_SCANCODES; sc++) {
        HeldKey *h = &a->held[sc];
        if (!h->active || h->seen) continue;
        h->seen = 1;
        if (h->release_pending) do_release(a, sc);
    }
}

static void release_all_keep(App *a, int keep_joy);

static void release_all(App *a)
{
    release_all_keep(a, 0);
}

/* Solta tudo; keep_joy = 1 mantem o polegar que segura o joystick de toque
 * (abrir o menu ou o teclado com outro dedo) */
static void release_all_keep(App *a, int keep_joy)
{
    memset(a->held, 0, sizeof a->held);
    memset(a->phys, 0, sizeof a->phys);
    memset(a->joy_cnt, 0, sizeof a->joy_cnt);
    a->npend = 0;
    a->last_down_sc = -1;
    /* os toques do --type sao sinteticos e se soltam sozinhos: mante-los (o
     * type_pos ja passou deles; descartar perderia o caractere) */
    if (!a->opt.type_text || a->type_done) {
        a->tap_count = a->tap_head = 0;
        a->tap_phase = 0;
    }
    a->any_unseen = 0;
    if (a->touch) {
        if (keep_joy) touch_release_keep_joy(a->touch);
        else touch_release_all(a->touch);
    }
    machine_release_all_keys(a->m);
    memset(a->applied, 0, sizeof a->applied);
    machine_joy(a->m, 0);
    a->last_joy = 0;
    sync_keys(a);
}

static int tap_push(App *a, const uint8_t k[3])
{
    if (a->tap_count >= TAPQ_SIZE) return -1;
    int i = (a->tap_head + a->tap_count) % TAPQ_SIZE;
    memcpy(a->tapq[i], k, 3);
    a->tap_count++;
    return 0;
}

/* Caractere -> passos de teclas (ate 2 passos de ate 3 teclas). 0 = sem mapeamento. */
static int char_steps(int c, uint8_t st[2][3])
{
    static const struct { int c; uint8_t k; } sym[] = {
        { '!', ZXK_1 }, { '@', ZXK_2 }, { '#', ZXK_3 }, { '$', ZXK_4 }, { '%', ZXK_5 },
        { '&', ZXK_6 }, { '\'', ZXK_7 }, { '(', ZXK_8 }, { ')', ZXK_9 }, { '_', ZXK_0 },
        { '<', ZXK_R }, { '>', ZXK_T }, { '=', ZXK_L }, { '+', ZXK_K }, { '-', ZXK_J },
        { '*', ZXK_B }, { '/', ZXK_V }, { ';', ZXK_O }, { ':', ZXK_Z }, { '"', ZXK_P },
        { '?', ZXK_C }, { ',', ZXK_N }, { '.', ZXK_M }, { '^', ZXK_H }, { 0xA3, ZXK_X },
    };
    /* modo E (CAPS+SYM) e depois SYM+tecla */
    static const struct { int c; uint8_t k; } ext[] = {
        { '~', ZXK_A }, { '|', ZXK_S }, { '\\', ZXK_D }, { '{', ZXK_F }, { '}', ZXK_G },
        { '[', ZXK_Y }, { ']', ZXK_U }, { 0xA9, ZXK_P },
    };
    static const uint8_t letters[26] = {
        ZXK_A, ZXK_B, ZXK_C, ZXK_D, ZXK_E, ZXK_F, ZXK_G, ZXK_H, ZXK_I, ZXK_J, ZXK_K, ZXK_L, ZXK_M,
        ZXK_N, ZXK_O, ZXK_P, ZXK_Q, ZXK_R, ZXK_S, ZXK_T, ZXK_U, ZXK_V, ZXK_W, ZXK_X, ZXK_Y, ZXK_Z,
    };
    static const uint8_t digits[10] = {
        ZXK_0, ZXK_1, ZXK_2, ZXK_3, ZXK_4, ZXK_5, ZXK_6, ZXK_7, ZXK_8, ZXK_9,
    };
    memset(st, ZXK_COUNT, 6);
    if (c >= 'a' && c <= 'z') { st[0][0] = letters[c - 'a']; return 1; }
    if (c >= 'A' && c <= 'Z') { st[0][0] = ZXK_CAPS; st[0][1] = letters[c - 'A']; return 1; }
    if (c >= '0' && c <= '9') { st[0][0] = digits[c - '0']; return 1; }
    if (c == ' ') { st[0][0] = ZXK_SPACE; return 1; }
    if (c == '\n' || c == '\r') { st[0][0] = ZXK_ENTER; return 1; }
    for (size_t i = 0; i < sizeof sym / sizeof sym[0]; i++)
        if (sym[i].c == c) { st[0][0] = ZXK_SYM; st[0][1] = sym[i].k; return 1; }
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++)
        if (ext[i].c == c) {
            st[0][0] = ZXK_CAPS; st[0][1] = ZXK_SYM;
            st[1][0] = ZXK_SYM;  st[1][1] = ext[i].k;
            return 2;
        }
    return 0;
}

static int queue_char(App *a, int c)
{
    uint8_t st[2][3];
    int n = char_steps(c, st);
    if (!n || a->tap_count + n > TAPQ_SIZE) return 0;
    for (int i = 0; i < n; i++) tap_push(a, st[i]);
    return n;
}

/* Decodifica um caractere UTF-8; devolve o codigo e avanca *p */
static int utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++;
    if (c >= 0x80) {
        int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        c &= extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
        for (int i = 0; i < extra && (*s & 0xC0) == 0x80; i++) c = (c << 6) | (*s++ & 0x3F);
    }
    *p = (const char *)s;
    return c;
}

static int is_loading(const App *a)
{
    const Machine *m = a->m;
    if (!m->tape) return m->pending_snap;
    if (tape_is_playing(m->tape)) return 1;
    if (m->flash_idle >= 0) return 1;   /* carga instantanea entre blocos */
    return m->pending_snap && tape_at_end(m->tape);
}

/* Velocidade da carga nostalgica: --load-speed N (com 0 = maximo, F7 desligando
 * o maximo volta aos 20x padrao) */
static int load_factor(const App *a)
{
    return a->opt.load_speed > 0 ? a->opt.load_speed : LOAD_SPEED_DEFAULT;
}

/* Turbo maximo agora? (F7 forcado, ou --load-speed 0 durante a carga sem F7) */
static int turbo_active(const App *a, int loading)
{
    return a->turbo_forced || (loading && a->opt.load_speed == 0 && !a->auto_suppressed);
}

/* Palavras-chave do BASIC para o --type: como cada uma e digitada.
 *   KW_K    letra no modo K (comandos: so no inicio de instrucao)
 *   KW_SYM  SYMBOL + tecla (qualquer modo)
 *   KW_E    modo E (CAPS+SYMBOL) e depois a tecla
 *   KW_ESYM modo E e depois SYMBOL + tecla
 * Nomes do TK (GOTO, GOSUB, CONT, RAND, SOUND) e do 48K sao aceitos. */
enum { KW_K, KW_SYM, KW_E, KW_ESYM };
typedef struct BasicKw { const char *name; uint8_t kind; uint8_t key; } BasicKw;
static const BasicKw basic_kw[] = {
    { "NEW", KW_K, ZXK_A }, { "BORDER", KW_K, ZXK_B }, { "CONTINUE", KW_K, ZXK_C },
    { "CONT", KW_K, ZXK_C }, { "DIM", KW_K, ZXK_D }, { "REM", KW_K, ZXK_E },
    { "FOR", KW_K, ZXK_F }, { "GO TO", KW_K, ZXK_G }, { "GOTO", KW_K, ZXK_G },
    { "GO SUB", KW_K, ZXK_H }, { "GOSUB", KW_K, ZXK_H }, { "INPUT", KW_K, ZXK_I },
    { "LOAD", KW_K, ZXK_J }, { "LIST", KW_K, ZXK_K }, { "LET", KW_K, ZXK_L },
    { "PAUSE", KW_K, ZXK_M }, { "NEXT", KW_K, ZXK_N }, { "POKE", KW_K, ZXK_O },
    { "PRINT", KW_K, ZXK_P }, { "PLOT", KW_K, ZXK_Q }, { "RUN", KW_K, ZXK_R },
    { "SAVE", KW_K, ZXK_S }, { "RANDOMIZE", KW_K, ZXK_T }, { "RAND", KW_K, ZXK_T },
    { "IF", KW_K, ZXK_U }, { "CLS", KW_K, ZXK_V }, { "DRAW", KW_K, ZXK_W },
    { "CLEAR", KW_K, ZXK_X }, { "RETURN", KW_K, ZXK_Y }, { "COPY", KW_K, ZXK_Z },
    { "<=", KW_SYM, ZXK_Q }, { "<>", KW_SYM, ZXK_W }, { ">=", KW_SYM, ZXK_E },
    { "STOP", KW_SYM, ZXK_A }, { "NOT", KW_SYM, ZXK_S }, { "STEP", KW_SYM, ZXK_D },
    { "TO", KW_SYM, ZXK_F }, { "THEN", KW_SYM, ZXK_G }, { "AND", KW_SYM, ZXK_Y },
    { "OR", KW_SYM, ZXK_U }, { "AT", KW_SYM, ZXK_I },
    { "READ", KW_E, ZXK_A }, { "BIN", KW_E, ZXK_B }, { "LPRINT", KW_E, ZXK_C },
    { "DATA", KW_E, ZXK_D }, { "TAN", KW_E, ZXK_E }, { "SGN", KW_E, ZXK_F },
    { "ABS", KW_E, ZXK_G }, { "SQR", KW_E, ZXK_H }, { "CODE", KW_E, ZXK_I },
    { "VAL", KW_E, ZXK_J }, { "LEN", KW_E, ZXK_K }, { "USR", KW_E, ZXK_L },
    { "PI", KW_E, ZXK_M }, { "INKEY$", KW_E, ZXK_N }, { "PEEK", KW_E, ZXK_O },
    { "TAB", KW_E, ZXK_P }, { "SIN", KW_E, ZXK_Q }, { "INT", KW_E, ZXK_R },
    { "RESTORE", KW_E, ZXK_S }, { "RND", KW_E, ZXK_T }, { "CHR$", KW_E, ZXK_U },
    { "LLIST", KW_E, ZXK_V }, { "COS", KW_E, ZXK_W }, { "EXP", KW_E, ZXK_X },
    { "STR$", KW_E, ZXK_Y }, { "LN", KW_E, ZXK_Z },
    { "ASN", KW_ESYM, ZXK_Q }, { "ACS", KW_ESYM, ZXK_W }, { "ATN", KW_ESYM, ZXK_E },
    { "VERIFY", KW_ESYM, ZXK_R }, { "MERGE", KW_ESYM, ZXK_T }, { "IN", KW_ESYM, ZXK_I },
    { "OUT", KW_ESYM, ZXK_O }, { "CIRCLE", KW_ESYM, ZXK_H }, { "VAL$", KW_ESYM, ZXK_J },
    { "SCREEN$", KW_ESYM, ZXK_K }, { "ATTR", KW_ESYM, ZXK_L }, { "BEEP", KW_ESYM, ZXK_Z },
    { "SOUND", KW_ESYM, ZXK_Z }, { "INK", KW_ESYM, ZXK_X }, { "PAPER", KW_ESYM, ZXK_C },
    { "FLASH", KW_ESYM, ZXK_V }, { "BRIGHT", KW_ESYM, ZXK_B }, { "OVER", KW_ESYM, ZXK_N },
    { "INVERSE", KW_ESYM, ZXK_M }, { "DEF FN", KW_ESYM, ZXK_1 }, { "FN", KW_ESYM, ZXK_2 },
    { "LINE", KW_ESYM, ZXK_3 }, { "OPEN #", KW_ESYM, ZXK_4 }, { "CLOSE #", KW_ESYM, ZXK_5 },
    { "MOVE", KW_ESYM, ZXK_6 }, { "ERASE", KW_ESYM, ZXK_7 }, { "POINT", KW_ESYM, ZXK_8 },
    { "CAT", KW_ESYM, ZXK_9 }, { "FORMAT", KW_ESYM, ZXK_0 },
};

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_alnum(int c) { return is_alpha(c) || (c >= '0' && c <= '9'); }

/* Palavra-chave mais longa que comeca em p (sem distinguir maiusculas), respeitando
 * os limites de palavra; prev = caractere anterior. NULL se nenhuma. */
static const BasicKw *match_keyword(const char *p, int prev, size_t *len)
{
    const BasicKw *best = NULL;
    size_t blen = 0;
    if (is_alpha(p[0]) && (is_alpha(prev) || prev == '$')) return NULL;
    for (size_t i = 0; i < sizeof basic_kw / sizeof basic_kw[0]; i++) {
        const char *n = basic_kw[i].name;
        size_t k = 0;
        while (n[k]) {
            int c = (unsigned char)p[k];
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c != n[k]) break;
            k++;
        }
        if (n[k] || k <= blen) continue;
        if (is_alpha(n[k - 1]) && is_alnum((unsigned char)p[k])) continue;
        best = &basic_kw[i];
        blen = k;
    }
    if (best) *len = blen;
    return best;
}

/* Enfileira os toques de uma palavra-chave (modo E: CAPS+SYM e depois a tecla) */
static void queue_keyword(App *a, const BasicKw *kw)
{
    uint8_t st[3];
    memset(st, ZXK_COUNT, 3);
    if (kw->kind == KW_E || kw->kind == KW_ESYM) {
        st[0] = ZXK_CAPS; st[1] = ZXK_SYM;
        tap_push(a, st);
        memset(st, ZXK_COUNT, 3);
    }
    if (kw->kind == KW_SYM || kw->kind == KW_ESYM) { st[0] = ZXK_SYM; st[1] = kw->key; }
    else st[0] = kw->key;
    tap_push(a, st);
}

/* --type: entrega o proximo item (caractere ou palavra-chave do BASIC) quando a
 * ROM esta pronta, a carga terminou e o toque anterior ja foi processado.
 * Fora de aspas e de REM, palavras-chave viram tokens (PRINT no modo K = tecla P;
 * INT = modo E + R; TO = SYMBOL+F ...); o modo K/L e lido das variaveis do sistema. */
static void type_feed(App *a)
{
    const char *t = a->opt.type_text;
    if (!t || a->type_done || a->tap_count || a->tap_phase) return;
    const Machine *m = a->m;
    /* ROM pronta (ou snapshot instantaneo, que nunca passa pelo editor) */
    if (!(m->rom_ready || m->frame_count >= 250) || m->autotype.active) return;
    if (a->opt.file && is_loading(a)) return;
    /* a ROM guarda uma tecla so e descarta as que chegam enquanto processa ENTER
     * (listagem, execucao): so digita depois de ver a ROM esperando tecla
     * (KEY-INPUT sem tecla nova) no ultimo quadro. Ate 1 s, para programas que
     * nao usam o teclado da ROM. */
    int rom_idle = m->key_wait_frame == m->frame_count && !(m->mem[0x5C3B] & 0x20);
    if (!rom_idle && a->type_wait < 50) { a->type_wait++; return; }
    const char *p = t + a->type_pos;
    size_t kwlen = 0;
    const BasicKw *kw = NULL;
    if (!a->type_quote && !a->type_rem) {
        /* espacos antes de uma palavra-chave: a ROM ja os mostra */
        const char *q = p;
        while (*q == ' ') q++;
        if (q != p && match_keyword(q, ' ', &kwlen)) p = q;
        kw = match_keyword(p, p != t + a->type_pos ? ' ' : a->type_prev, &kwlen);
        if (kw && kw->kind == KW_K) {
            /* comando so existe no modo K: FLAGS bit 3 = 0 e MODE = 0. Depois de
             * ENTER a ROM pode demorar: espera ate 1 s pelo modo K, senao digita
             * as letras. */
            int kmode = !(m->mem[0x5C3B] & 0x08) && m->mem[0x5C41] == 0;
            if (!kmode && a->type_wait < 50) { a->type_wait++; return; }
            if (!kmode) kw = NULL;
        }
    }
    a->type_wait = 0;
    if (kw) {
        queue_keyword(a, kw);
        p += kwlen;
        a->type_prev = (unsigned char)p[-1];
        while (*p == ' ') { p++; a->type_prev = ' '; }   /* idem depois */
        if (!strcmp(kw->name, "REM")) a->type_rem = 1;
        a->type_pos = (size_t)(p - t);
        return;
    }
    if (!*p) { a->type_done = 1; return; }
    int c;
    if (p[0] == '\\' && p[1] == 'n') { c = '\n'; p += 2; }
    else if (p[0] == '\\' && p[1] == '\\') { c = '\\'; p += 2; }
    else c = utf8_next(&p);
    a->type_pos = (size_t)(p - t);
    a->type_prev = c;
    if (c == '\n' || c == '\r') a->type_quote = a->type_rem = 0;
    else if (c == '"' && !a->type_rem) a->type_quote = !a->type_quote;
    queue_char(a, c);
}

static void tap_before_frame(App *a)
{
    type_feed(a);
    if (a->tap_phase == 0 && a->tap_count && !a->m->autotype.active) {
        memcpy(a->tap_cur, a->tapq[a->tap_head], 3);
        a->tap_head = (a->tap_head + 1) % TAPQ_SIZE;
        a->tap_count--;
        a->tap_phase = 1;
        a->tap_timer = TAP_HOLD;
    }
}

static void tap_after_frame(App *a)
{
    if (a->tap_phase == 1) {
        if (--a->tap_timer <= 0) { a->tap_phase = 2; a->tap_timer = TAP_GAP; }
    } else if (a->tap_phase == 2) {
        if (--a->tap_timer <= 0) a->tap_phase = 0;
    }
}

static int typing_busy(const App *a)
{
    if (a->tap_phase || a->tap_count) return 1;
    return a->opt.type_text && !a->type_done;
}

/* Um quadro emulado com teclado em dia */
static void emu_frame(App *a)
{
    tap_before_frame(a);
    sync_keys(a);
    machine_run_frame(a->m);
    a->stat_frames++;
    tap_after_frame(a);
    keys_after_frame(a);
    if (a->touch) touch_frame_done(a->touch);   /* toques rapidos ja vistos pela maquina */
}

/* ------------------------------------------------------------------ */
/* Arquivos, snapshot, captura                                         */
/* ------------------------------------------------------------------ */

static void toast_ms(App *a, Uint32 ms, const char *fmt, ...)
{
    char msg[OSD_TEXT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (g_headless) fprintf(stderr, "%s\n", msg);
    else osd_toast(&a->osd, SDL_GetTicks(), ms, msg);
}

static void toast(App *a, const char *fmt, ...)
{
    char msg[OSD_TEXT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (g_headless) fprintf(stderr, "%s\n", msg);
    else osd_toast(&a->osd, SDL_GetTicks(), TOAST_MS, msg);
}

/* ---------------- Perfis de controle (profiles.h) ----------------
 * Abrir um jogo conhecido (CRC32 do arquivo) troca o joystick da sessao para o
 * modo que funciona com os controles padrao dele (Q A O P, teclas do jogo,
 * Cursor ou o Sinclair do TK) e mostra "Controles: ..." quando a carga
 * termina. Abrir outro arquivo sem perfil (ou trocar de modelo, que tira a
 * fita) devolve o modo que o usuario tinha antes. */

/* Modo que vale sem perfil: "teclas do jogo" so existe com um perfil */
static JoyMode plain_mode(JoyMode j)
{
    return j == JOYMODE_KEYS ? JOYMODE_SINCLAIR1 : j;
}

/* Troca o modo do joystick; o estado e reenviado no proximo quadro */
static void set_joymode(App *a, JoyMode j)
{
    machine_joy(a->m, 0);
    a->m->cfg.joymode = j;
    a->last_joy = 0xFF;
}

/* "Controles: <resumo>" e a dica para comecar (menus, ajuda e aviso) */
static void profile_lines(const App *a, char *l1, size_t n1, char *l2, size_t n2)
{
    const GameProfile *p = a->profile;
    l1[0] = l2[0] = 0;
    if (!p) return;
    snprintf(l1, n1, "Controles: %s", p->summary);
    if (p->hint) snprintf(l2, n2, "%s", p->hint);
}

static void profile_toast_now(App *a, Uint32 ms)
{
    char l1[OSD_TEXT_MAX], l2[OSD_TEXT_MAX];
    profile_lines(a, l1, sizeof l1, l2, sizeof l2);
    if (!l1[0]) return;
    if (l2[0]) toast_ms(a, ms, "%s\n%s", l1, l2);
    else toast_ms(a, ms, "%s", l1);
}

static void apply_profile(App *a, const GameProfile *p)
{
    Machine *m = a->m;
    if (p) {
        if (!a->profile) a->user_joymode = m->cfg.joymode;
        a->profile = p;
        machine_set_joy_keys(m, &p->keys);
        set_joymode(a, p->mode);
        a->profile_toast = 1;           /* quando a carga terminar (profile_tick) */
    } else if (a->profile) {
        a->profile = NULL;
        a->profile_toast = 0;
        machine_set_joy_keys(m, NULL);
        set_joymode(a, plain_mode(a->user_joymode));
    }
}

/* Depois de cada passo: carga terminada -> aviso dos controles */
static void profile_tick(App *a)
{
    if (!a->profile_toast || !a->profile) return;
    if (a->m->autotype.active || is_loading(a)) return;
    a->profile_toast = 0;
    profile_toast_now(a, PROFILE_TOAST_MS);
}

static int open_file(App *a, const char *path)
{
    char err[256] = "";
    if (machine_open_file(a->m, path, a->opt.org, a->opt.exec, a->opt.instant,
                          err, sizeof err) != 0) {
        fprintf(stderr, "tk90x: %s: %s\n", path, err[0] ? err : "erro ao abrir");
        if (!g_headless) osd_toast(&a->osd, SDL_GetTicks(), 4000, err[0] ? err : "Erro ao abrir");
        return -1;
    }
    copy_str(a->file_name, sizeof a->file_name, plat_basename(path));
    osd_browser_remember(&a->osd, path);
    apply_profile(a, profile_for_file(path));
    a->auto_suppressed = 0;
    a->npend = 0;
    a->tap_count = a->tap_head = a->tap_phase = 0;
    if (!g_headless) {
        release_all(a);
        toast(a, "Carregando %s", a->file_name);
    }
    return 0;
}

/* Nome "tk90x-AAAAMMDD-HHMMSS.ext" no diretorio atual (no Android, no
 * armazenamento do app: save_dir); se ja existir (duas capturas no mesmo
 * segundo), "-2", "-3"... para nao sobrescrever. */
static void unique_name(const App *a, char *out, size_t outlen, const char *ext)
{
    char ts[32], base[64];
    plat_timestamp(ts, sizeof ts);
    snprintf(base, sizeof base, "tk90x-%.20s.%.4s", ts, ext);
    for (int k = 2; k < 100; k++) {
        if (a->save_dir[0]) plat_path_join(a->save_dir, base, out, outlen);
        else copy_str(out, outlen, base);
        FILE *f = tk_fopen(out, "rb");
        if (!f) return;
        fclose(f);
        snprintf(base, sizeof base, "tk90x-%.20s-%d.%.4s", ts, k, ext);
    }
}

static void save_screenshot(App *a)
{
    char name[PLAT_PATH_MAX];
    unique_name(a, name, sizeof name, "png");
    if (png_write_argb(name, a->m->fb, FB_W, FB_H) == 0)
        toast(a, "Salvo: %s", plat_basename(name));
    else toast(a, "Erro ao gravar %s", plat_basename(name));
}

static void save_snapshot(App *a)
{
    char path[PLAT_PATH_MAX], err[256] = "";
    Snapshot *s = (Snapshot *)calloc(1, sizeof *s);
    if (!s) { toast(a, "Sem memoria"); return; }
    unique_name(a, path, sizeof path, "z80");
    const char *name = plat_basename(path);
    /* durante a carga da fita virtual de um snapshot grava o snapshot sendo
     * carregado (o estado do carregador da ROM nao serviria para nada) */
    int loading_snap = a->m->pending_snap && a->m->snap;
    machine_take_snapshot(a->m, s);
    if (snapshot_save_file(path, s, err, sizeof err) == 0)
        toast(a, loading_snap ? "Carga salva: %s" : "Salvo: %s", name);
    else toast(a, "Erro: %s", err[0] ? err : name);
    free(s);
}

static MachineConfig base_config(const App *a, Model model)
{
    MachineConfig c;
    memset(&c, 0, sizeof c);
    c.model = model;
    c.hz60 = a->hz60;
    c.issue2 = 1;           /* TK: EAR (bit 6 da porta FE) em repouso le 1 */
    c.joymode = plain_mode(a->opt.joymode);
    c.flash_load = a->opt.flash;
    c.no_contention = !a->opt.contention;
    return c;
}

/* F10: proximo modelo (reinicia com a ROM nova) */
static void cycle_model(App *a)
{
    static const Model order[] = { MODEL_TK95, MODEL_TK90X, MODEL_TK90X_V1 };
    int cur = 0;
    for (int i = 0; i < 3; i++)
        if (order[i] == a->m->cfg.model) cur = i;
    Model next = order[(cur + 1) % 3];
    MachineConfig c = a->m->cfg;
    c.model = next;
    if (a->profile) {           /* a fita sai, o perfil tambem: volta o modo do usuario */
        c.joymode = plain_mode(a->user_joymode);
        a->profile = NULL;
        a->profile_toast = 0;
    }
    model_rom(next, a->rom);
    machine_free(a->m);
    memset(a->m, 0, sizeof *a->m);
    if (machine_init(a->m, a->rom, &c) != 0) fatal("falha ao iniciar a maquina");
    a->m->tape_sound = 1;
    a->opt.joymode = a->m->cfg.joymode;
    osd_set_font(&a->osd, a->rom);
    if (a->touch) touch_set_font(a->touch, a->rom + 0x3D00);
    a->file_name[0] = 0;
    a->last_joy = 0xFF;
    release_all(a);
    a->tap_count = a->tap_head = a->tap_phase = 0;
    toast(a, "Modelo: %s (reset, sem fita)", machine_model_name(next));
}

/* --wav-out: WAV mono 16 bits com exatamente o que vai para a fila do SDL */
static void put_le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void wav_header(uint8_t h[44], uint32_t data_bytes)
{
    memcpy(h, "RIFF", 4);  put_le(h + 4, 36 + data_bytes, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4); put_le(h + 20, 1, 2);            /* PCM */
    put_le(h + 22, 1, 2);  put_le(h + 24, AUDIO_RATE, 4);   /* mono */
    put_le(h + 28, AUDIO_RATE * 2, 4); put_le(h + 32, 2, 2); put_le(h + 34, 16, 2);
    memcpy(h + 36, "data", 4); put_le(h + 40, data_bytes, 4);
}

static void wav_open(App *a, const char *path)
{
    uint8_t h[44];
    a->wav = tk_fopen(path, "wb");
    if (!a->wav) fatal("nao foi possivel criar '%s'", path);
    wav_header(h, 0);
    fwrite(h, 1, sizeof h, a->wav);
    a->wav_bytes = 0;
}

static void wav_write(App *a, const int16_t *buf, int n)
{
    uint8_t b[2];
    if (!a->wav) return;
    for (int i = 0; i < n; i++) {
        put_le(b, (uint16_t)buf[i], 2);
        fwrite(b, 1, 2, a->wav);
    }
    a->wav_bytes += (uint32_t)n * 2;
}

static void wav_close(App *a)
{
    uint8_t h[44];
    if (!a->wav) return;
    wav_header(h, a->wav_bytes);
    fseek(a->wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, a->wav);
    if (fclose(a->wav) != 0) fprintf(stderr, "tk90x: erro ao gravar o WAV\n");
    a->wav = NULL;
}

static int emu_step(App *a, long max_frames);

/* ------------------------------------------------------------------ */
/* Headless                                                            */
/* ------------------------------------------------------------------ */

static int run_headless(App *a)
{
    Machine *m = a->m;
    double t0 = plat_seconds();
    long max = a->opt.until_loaded ? UNTIL_LOADED_CAP : a->opt.frames;
    long extra = a->opt.frames_set ? a->opt.frames : 150;
    long idle = 0, f = 0;
    int loaded = 0;

    /* mesma cadencia da janela (passos de "quadro exibido": N quadros emulados
     * durante a carga), para o --wav-out gravar o mesmo audio; --frames conta
     * quadros emulados */
    for (f = 0; f < max;) {
        unsigned long before = a->stat_frames;
        emu_step(a, max - f);
        a->stat_shown++;
        long ran = (long)(a->stat_frames - before);
        f += ran;
        profile_tick(a);
        if (a->opt.until_loaded) {
            int busy = m->autotype.active || is_loading(a) || typing_busy(a)
                       || !(m->rom_ready || f >= 300);
            if (busy) idle = 0;
            else if ((idle += (ran > 1 ? 1 : ran)) >= extra) { loaded = 1; break; }
        }
    }
    double wall = plat_seconds() - t0;
    if (!a->opt.until_loaded)
        loaded = !(m->autotype.active || is_loading(a) || typing_busy(a));
    else if (!loaded)
        fprintf(stderr, "tk90x: aviso: limite de %ld quadros atingido sem terminar a carga\n",
                UNTIL_LOADED_CAP);

    int rc = 0;
    if (a->opt.screenshot) {
        if (png_write_argb(a->opt.screenshot, m->fb, FB_W, FB_H) != 0) {
            fprintf(stderr, "tk90x: erro ao gravar %s\n", a->opt.screenshot);
            rc = 1;
        }
    }
    double emu = (double)f * (double)m->frame_tstates / (double)TK_CPU_HZ;
    int tb = 0, tn = 0, tp = 0, playing = 0;
    if (m->tape && tape_is_loaded(m->tape)) {
        tb = tape_current_block(m->tape);
        tn = tape_block_count(m->tape);
        tp = tape_progress_permille(m->tape) / 10;
        playing = tape_is_playing(m->tape);
    }
    printf("frames=%ld emu_seconds=%.2f wall_seconds=%.3f speed=%.1fx model=%s hz=%d "
           "tape_block=%d/%d tape_pos=%d%% tape_playing=%d loaded=%d pc=0x%04X "
           "shown_seconds=%.1f\n",
           f, emu, wall, wall > 0 ? emu / wall : 0.0, machine_model_name(m->cfg.model),
           a->hz60 ? 60 : 50, tb, tn, tp, playing, loaded, m->cpu.pc,
           (double)a->stat_shown * (double)m->frame_tstates / (double)TK_CPU_HZ);
    if (a->opt.screenshot && !rc) printf("screenshot=%s\n", a->opt.screenshot);
    fflush(stdout);
    return rc;
}

/* ------------------------------------------------------------------ */
/* SDL: video, audio, controles                                        */
/* ------------------------------------------------------------------ */

static void update_title(App *a)
{
    char t[512];
    if (a->file_name[0])
        snprintf(t, sizeof t, "%s - %.300s - %d Hz", machine_model_name(a->m->cfg.model),
                 a->file_name, a->hz60 ? 60 : 50);
    else
        snprintf(t, sizeof t, "%s - %d Hz", machine_model_name(a->m->cfg.model), a->hz60 ? 60 : 50);
    if (strcmp(t, a->title)) {
        copy_str(a->title, sizeof a->title, t);
        SDL_SetWindowTitle(a->win, t);
    }
}

static void update_info(App *a)
{
    char t[OSD_TEXT_MAX];
    if (a->touch)       /* toque: sem a linha das setas do teclado do PC */
        snprintf(t, sizeof t, "%s %d Hz  Joystick: %s", machine_model_name(a->m->cfg.model),
                 a->hz60 ? 60 : 50, joymode_short(a->m->cfg.joymode));
    else
        snprintf(t, sizeof t, "%s %d Hz  Joy: %s  Setas: %s", machine_model_name(a->m->cfg.model),
                 a->hz60 ? 60 : 50, joymode_short(a->m->cfg.joymode),
                 a->arrows_mode == ARROWS_AUTO ? "auto" : a->arrows_mode == ARROWS_JOY ? "joy" : "cur");
    osd_set_info(&a->osd, t);
    {
        char l1[OSD_TEXT_MAX], l2[OSD_TEXT_MAX];
        profile_lines(a, l1, sizeof l1, l2, sizeof l2);
        osd_set_controls(&a->osd, l1, l2);
    }

    /* segunda linha: fita (progresso tambem fica aqui, ja que o status some) */
    const Machine *m = a->m;
    if (m->tape && tape_is_loaded(m->tape)) {
        int cur = tape_current_block(m->tape), cnt = tape_block_count(m->tape);
        const char *d = tape_block_desc(m->tape, cur < cnt ? cur : cnt - 1);
        /* velocidade efetiva da carga (com F7), a mesma regra do emu_step */
        char sp[16];
        if (turbo_active(a, 1)) copy_str(sp, sizeof sp, "max");
        else snprintf(sp, sizeof sp, "%dx", load_factor(a));
        snprintf(t, sizeof t, "Fita %d%% bl %d/%d %s  carga %s",
                 tape_progress_permille(m->tape) / 10, cur < cnt ? cur + 1 : cnt, cnt,
                 tape_is_playing(m->tape) ? "play" : "pausa", sp);
        osd_set_tape_info(&a->osd, t, d);
    } else {
        osd_set_tape_info(&a->osd, "Fita: nenhuma", NULL);
    }
}

/* O SDL do Android apresenta o acelerometro como joystick (desligado pela dica
 * SDL_HINT_ACCELEROMETER_AS_JOYSTICK; isto e a segunda protecao): inclinar o
 * aparelho seguraria uma direcao */
static int is_accel_joystick(int i)
{
    const char *n = SDL_JoystickNameForIndex(i);
    return n && !strcmp(n, "Android Accelerometer");
}

/* Abre o primeiro controle de jogo; sem nenhum, o primeiro joystick generico.
 * Um controle de jogo conectado depois substitui o joystick generico aberto. */
static void open_pad(App *a)
{
    int n = SDL_NumJoysticks();
    if (a->pad) return;
    for (int i = 0; i < n; i++) {
        if (is_accel_joystick(i) || !SDL_IsGameController(i)) continue;
        SDL_GameController *gc = SDL_GameControllerOpen(i);
        if (!gc) continue;
        if (a->joy) { SDL_JoystickClose(a->joy); a->joy = NULL; a->pad_joy = 0; }
        a->pad = gc;
        a->pad_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
        toast(a, "Controle: %s", SDL_GameControllerName(gc) ? SDL_GameControllerName(gc)
                                                            : "conectado");
        return;
    }
    if (a->joy) return;
    for (int i = 0; i < n; i++) {
        if (is_accel_joystick(i) || SDL_IsGameController(i)) continue;
        a->joy = SDL_JoystickOpen(i);
        if (a->joy) {
            a->pad_id = SDL_JoystickInstanceID(a->joy);
            toast(a, "Joystick: %s", SDL_JoystickName(a->joy) ? SDL_JoystickName(a->joy)
                                                              : "conectado");
            return;
        }
    }
}

static void close_pad(App *a, SDL_JoystickID id)
{
    if (a->pad && a->pad_id == id) { SDL_GameControllerClose(a->pad); a->pad = NULL; }
    else if (a->joy && a->pad_id == id) { SDL_JoystickClose(a->joy); a->joy = NULL; }
    else return;
    a->pad_joy = 0;
    open_pad(a);
}

static void poll_pad(App *a)
{
    const int dz = 16000;
    uint8_t j = 0;
    if (a->pad) {
        SDL_GameController *p = a->pad;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP)) j |= JOY_UP;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) j |= JOY_DOWN;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) j |= JOY_LEFT;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) j |= JOY_RIGHT;
        int x = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
        if (x < -dz) j |= JOY_LEFT;
        if (x > dz) j |= JOY_RIGHT;
        if (y < -dz) j |= JOY_UP;
        if (y > dz) j |= JOY_DOWN;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) ||
            SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B)) j |= JOY_FIRE;
    } else if (a->joy) {
        SDL_Joystick *p = a->joy;
        if (SDL_JoystickNumHats(p) > 0) {
            Uint8 h = SDL_JoystickGetHat(p, 0);
            if (h & SDL_HAT_UP) j |= JOY_UP;
            if (h & SDL_HAT_DOWN) j |= JOY_DOWN;
            if (h & SDL_HAT_LEFT) j |= JOY_LEFT;
            if (h & SDL_HAT_RIGHT) j |= JOY_RIGHT;
        }
        if (SDL_JoystickNumAxes(p) >= 2) {
            int x = SDL_JoystickGetAxis(p, 0), y = SDL_JoystickGetAxis(p, 1);
            if (x < -dz) j |= JOY_LEFT;
            if (x > dz) j |= JOY_RIGHT;
            if (y < -dz) j |= JOY_UP;
            if (y > dz) j |= JOY_DOWN;
        }
        int nb = SDL_JoystickNumButtons(p);
        for (int b = 0; b < nb && b < 2; b++)
            if (SDL_JoystickGetButton(p, b)) j |= JOY_FIRE;
    }
    a->pad_joy = j;
}

/* As setas (e o Alt) sao joystick agora? Decidido ao apertar a tecla. */
static int arrows_as_joy(const App *a)
{
    if (a->arrows_mode == ARROWS_JOY) return 1;
    if (a->arrows_mode == ARROWS_CURSOR) return 0;
    return !machine_editor_active(a->m, EDITOR_FRAMES);
}

/* Scancode -> teclas Spectrum / joystick. Devolve 1 se mapeado. */
static int map_scancode(const App *a, SDL_Scancode sc, uint64_t *keys, uint8_t *joy)
{
    static const uint8_t letters[26] = {
        ZXK_A, ZXK_B, ZXK_C, ZXK_D, ZXK_E, ZXK_F, ZXK_G, ZXK_H, ZXK_I, ZXK_J, ZXK_K, ZXK_L, ZXK_M,
        ZXK_N, ZXK_O, ZXK_P, ZXK_Q, ZXK_R, ZXK_S, ZXK_T, ZXK_U, ZXK_V, ZXK_W, ZXK_X, ZXK_Y, ZXK_Z,
    };
    static const uint8_t digits19[9] = {
        ZXK_1, ZXK_2, ZXK_3, ZXK_4, ZXK_5, ZXK_6, ZXK_7, ZXK_8, ZXK_9,
    };
#define K(x) ((uint64_t)1 << (x))
    *keys = 0;
    *joy = 0;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) { *keys = K(letters[sc - SDL_SCANCODE_A]); return 1; }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) { *keys = K(digits19[sc - SDL_SCANCODE_1]); return 1; }
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) { *keys = K(digits19[sc - SDL_SCANCODE_KP_1]); return 1; }
    switch (sc) {
    case SDL_SCANCODE_0: case SDL_SCANCODE_KP_0: *keys = K(ZXK_0); return 1;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: *keys = K(ZXK_ENTER); return 1;
    case SDL_SCANCODE_SPACE: *keys = K(ZXK_SPACE); return 1;
    case SDL_SCANCODE_LSHIFT: *keys = K(ZXK_CAPS); return 1;
    case SDL_SCANCODE_RSHIFT: case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL:
        *keys = K(ZXK_SYM); return 1;
    case SDL_SCANCODE_BACKSPACE: *keys = K(ZXK_CAPS) | K(ZXK_0); return 1;
    case SDL_SCANCODE_TAB: *keys = K(ZXK_CAPS) | K(ZXK_SYM); return 1;        /* modo E */
    case SDL_SCANCODE_CAPSLOCK: *keys = K(ZXK_CAPS) | K(ZXK_2); return 1;     /* CAPS LOCK */
    case SDL_SCANCODE_HOME: *keys = K(ZXK_CAPS) | K(ZXK_1); return 1;         /* EDIT */
    case SDL_SCANCODE_INSERT: *keys = K(ZXK_CAPS) | K(ZXK_9); return 1;       /* GRAPHICS */
    case SDL_SCANCODE_PAGEUP: *keys = K(ZXK_CAPS) | K(ZXK_3); return 1;       /* TRUE VIDEO */
    case SDL_SCANCODE_PAGEDOWN: *keys = K(ZXK_CAPS) | K(ZXK_4); return 1;     /* INV. VIDEO */
    case SDL_SCANCODE_END: *keys = K(ZXK_CAPS) | K(ZXK_SPACE); return 1;      /* BREAK */
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_UP:
    case SDL_SCANCODE_RIGHT: {
        static const uint8_t jb[4] = { JOY_LEFT, JOY_DOWN, JOY_UP, JOY_RIGHT };
        static const uint8_t ck[4] = { ZXK_5, ZXK_6, ZXK_7, ZXK_8 };
        int i = sc == SDL_SCANCODE_LEFT ? 0 : sc == SDL_SCANCODE_DOWN ? 1
              : sc == SDL_SCANCODE_UP ? 2 : 3;
        /* joystick: so o bit do joystick, sem CAPS (a maquina o converte nas
         * teclas do modo: 6-0, 1-5, 5-8 ou porta Kempston) */
        if (arrows_as_joy(a)) *joy = jb[i];
        else *keys = K(ZXK_CAPS) | K(ck[i]);
        return 1;
    }
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT:
        if (arrows_as_joy(a)) { *joy = JOY_FIRE; return 1; }
        return 0;
    default:
        return 0;
    }
#undef K
}

/* Teclas que tambem geram texto: a pressao espera o SDL_TEXTINPUT do mesmo
 * evento para saber se o layout do PC produziu pontuacao (Shift+1 = '!',
 * AltGr+Q = '/' no ABNT2, tecla morta + espaco = '~'...). */
static int text_capable(SDL_Scancode sc)
{
    return (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_0) || sc == SDL_SCANCODE_SPACE ||
           (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_0);
}

static void flush_pending(App *a, int only_sc)
{
    int k = 0;
    for (int i = 0; i < a->npend; i++) {
        PendKey *p = &a->pend[i];
        if (only_sc < 0 || p->sc == only_sc) press_sc(a, p->sc, p->keys, p->joy);
        else a->pend[k++] = *p;
    }
    a->npend = k;
}

static void handle_text(App *a, const char *text)
{
    const char *p = text;
    /* auto-repeticao (o SDL manda TEXTINPUT a cada repeticao, sem marca): so
     * enfileira com a fila vazia, senao a digitacao continuaria por segundos
     * depois de soltar a tecla (o sistema repete mais rapido que os toques) */
    if (a->last_down_repeat && (a->tap_count || a->tap_phase)) return;
    while (*p) {
        int c = utf8_next(&p);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')
            continue;       /* ja tratados pelo scancode */
        uint8_t st[2][3];
        if (!char_steps(c, st)) continue;
        /* a tecla fisica que gerou o texto nao vale como tecla Spectrum: so a
         * ultima pressionada, se ainda pendente (pontuacao sem scancode mapeado
         * nao fica pendente e nao pode levar a letra digitada antes) */
        if (a->npend && a->pend[a->npend - 1].sc == a->last_down_sc) {
            a->npend--;
            a->last_down_sc = -1;
        }
        queue_char(a, c);
    }
}

static void set_fullscreen(App *a, int on)
{
#ifdef __ANDROID__
    /* sempre tela cheia: sair dela mostraria as barras do sistema e mudaria o
     * tamanho da superficie (TK90XActivity mantem o modo imersivo) */
    (void)a;
    (void)on;
#else
    if (SDL_SetWindowFullscreen(a->win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0)
        a->fullscreen = on;
#endif
}

/* F2 / "Abrir jogo": navegador no ultimo diretorio. No Android a primeira
 * abertura vai para os jogos que acompanham o app (extraidos dos assets). */
static void open_browser(App *a)
{
    const char *dir = NULL;
    release_all(a);
#ifdef __ANDROID__
    if (!a->osd.dir[0]) {
        dir = android_games_dir();
        if (!dir) dir = SDL_AndroidGetInternalStoragePath();
    }
#endif
    if (osd_open_browser(&a->osd, dir) != 0) toast(a, "Erro ao listar arquivos");
}

/* Seletor de documentos do aparelho (Android); o arquivo escolhido chega por
 * android_poll_picked, ja copiado para o cache. */
static void open_picker(App *a)
{
    release_all(a);
#ifdef __ANDROID__
    android_open_picker();
#else
    toast(a, "Seletor do aparelho: so no Android");
#endif
}

static void menu_activate(App *a, int id);

static void handle_osd_result(App *a, OsdResult r, const char *path)
{
    if (r == OSD_RES_OPEN) open_file(a, path);
    else if (r == OSD_RES_QUIT) a->quit = 1;
    else if (r == OSD_RES_PICKER) open_picker(a);
    else if (r == OSD_RES_ACTION) menu_activate(a, a->osd.menu_action);
}

static OsdKey osd_key_of(SDL_Keycode k, int *ch)
{
    *ch = 0;
    switch (k) {
    case SDLK_UP: return OSDK_UP;
    case SDLK_DOWN: return OSDK_DOWN;
    case SDLK_PAGEUP: return OSDK_PGUP;
    case SDLK_PAGEDOWN: return OSDK_PGDN;
    case SDLK_HOME: return OSDK_HOME;
    case SDLK_END: return OSDK_END;
    case SDLK_RETURN: case SDLK_KP_ENTER: return OSDK_ENTER;
    case SDLK_BACKSPACE: return OSDK_BACK;
    case SDLK_LEFT: return OSDK_LEFT;
    case SDLK_RIGHT: return OSDK_RIGHT;
    case SDLK_ESCAPE: return OSDK_ESC;
    default:
        if (k > 32 && k < 127) { *ch = (int)k; return OSDK_CHAR; }
        return OSDK_NONE;
    }
}

static void open_menu(App *a);

static void function_key(App *a, SDL_Keycode k)
{
    Machine *m = a->m;
    switch (k) {
    case SDLK_F1:
        /* desktop: menu principal (com a ajuda); toque: a ajuda (o MENU e o da tela) */
        release_all(a);
        update_info(a);
        if (a->touch) osd_open_help(&a->osd, 0);
        else open_menu(a);
        break;
    case SDLK_F2:
        open_browser(a);
        break;
    case SDLK_F3:
        if (!m->tape || !tape_is_loaded(m->tape)) { toast(a, "Nenhuma fita carregada"); break; }
        if (tape_is_playing(m->tape)) {
            tape_stop(m->tape);
            toast(a, "Fita: pausa");
        } else {
            if (tape_at_end(m->tape)) tape_rewind(m->tape);
            tape_play(m->tape, m->cpu.tstates);
            a->auto_suppressed = 0;
            toast(a, "Fita: play");
        }
        break;
    case SDLK_F4:
        release_all(a);
        a->arrows_mode = (a->arrows_mode + 1) % ARROWS_COUNT;
        if (a->arrows_mode == ARROWS_AUTO)
            toast(a, "Setas: automatico (joy %s)", joymode_short(m->cfg.joymode));
        else if (a->arrows_mode == ARROWS_JOY)
            toast(a, "Setas: joystick %s, Alt=fogo", joymode_short(m->cfg.joymode));
        else
            toast(a, "Setas: cursores (CAPS+5..8)");
        update_info(a);
        break;
    case SDLK_F5:
        machine_reset(m);
        release_all(a);
        a->tap_count = a->tap_head = a->tap_phase = 0;   /* nada de toques de antes do reset */
        a->auto_suppressed = 0;
        toast(a, "Reset");
        break;
    case SDLK_F6:
        save_snapshot(a);
        break;
    case SDLK_F7: {
        int turbo_now = turbo_active(a, is_loading(a));
        /* na carga: alterna entre o maximo e a velocidade configurada (20x) */
        if (turbo_now) {
            a->turbo_forced = 0;
            a->turbo_load_only = 0;
            if (is_loading(a)) a->auto_suppressed = 1;
            if (is_loading(a) && load_factor(a) > 1)
                toast(a, "Carga %dx: som e listras originais", load_factor(a));
            else
                toast(a, "Turbo desligado");
        } else {
            a->turbo_forced = 1;
            a->turbo_load_only = is_loading(a);
            toast(a, is_loading(a) ? "Carga: velocidade maxima" : "Turbo ligado");
        }
        update_info(a);                 /* o menu (F1) mostra a velocidade efetiva */
        break;
    }
    case SDLK_F8:
        a->hz60 = !a->hz60;
        machine_set_hz(m, a->hz60);
        toast(a, a->hz60 ? "60 Hz (PAL-M)" : "50 Hz");
        break;
    case SDLK_F9: {
        /* "Teclas do jogo" so com um perfil que as tenha. Com perfil, a troca
         * vale para a sessao; o modo do usuario volta ao abrir outro arquivo. */
        JoyMode j = m->cfg.joymode;
        do j = (JoyMode)((j + 1) % JOYMODE_COUNT);
        while (j == JOYMODE_KEYS && !profile_has_keys(a->profile));
        set_joymode(a, j);
        if (!a->profile) a->opt.joymode = j;
        if (j == JOYMODE_KEYS) toast(a, "Joystick: teclas do jogo (%s)", a->profile->summary);
        else toast(a, "Joystick: %s", joymode_name(j));
        update_info(a);
        break;
    }
    case SDLK_F10:
        cycle_model(a);
        break;
    case SDLK_F11:
        set_fullscreen(a, !a->fullscreen);
        break;
    case SDLK_F12:
        save_screenshot(a);
        break;
    default:
        break;
    }
}

static int is_function_key(SDL_Keycode k)
{
    return (k >= SDLK_F1 && k <= SDLK_F12) || k == SDLK_ESCAPE;
}

/* ------------------------------------------------------------------ */
/* Menu principal do desktop (F1)                                      */
/* ------------------------------------------------------------------ */

enum { MI_OPEN = 1, MI_TAPE, MI_REWIND, MI_RESET, MI_TURBO, MI_SNAP, MI_SHOT, MI_MODEL,
       MI_HZ, MI_JOY, MI_ARROWS, MI_FULL, MI_DIAG, MI_HELP, MI_QUIT };

/* Itens na ordem do menu; key = tecla de funcao que faz o mesmo (tambem
 * funciona com o menu aberto); gap = linha em branco antes */
static const struct MenuDef {
    int id;
    SDL_Keycode key;
    const char *keyname, *label;
    int gap;
} menu_def[] = {
    { MI_OPEN,   SDLK_F2,  "F2",  "Abrir jogo/programa", 0 },
    { MI_TAPE,   SDLK_F3,  "F3",  "Fita: play/pausa",    0 },
    { MI_REWIND, 0,        "",    "Fita: rebobinar",     0 },
    { MI_RESET,  SDLK_F5,  "F5",  "Reiniciar (reset)",   1 },
    { MI_TURBO,  SDLK_F7,  "F7",  "Turbo",               0 },
    { MI_SNAP,   SDLK_F6,  "F6",  "Salvar estado .z80",  0 },
    { MI_SHOT,   SDLK_F12, "F12", "Captura de tela",     0 },
    { MI_MODEL,  SDLK_F10, "F10", "Modelo",              1 },
    { MI_HZ,     SDLK_F8,  "F8",  "50/60 Hz",            0 },
    { MI_JOY,    SDLK_F9,  "F9",  "Joystick",            0 },
    { MI_ARROWS, SDLK_F4,  "F4",  "Setas",               0 },
    { MI_FULL,   SDLK_F11, "F11", "Tela cheia",          0 },
    { MI_DIAG,   0,        "",    "Diagnostico",         0 },
    { MI_HELP,   0,        "",    "Ajuda",               1 },
    { MI_QUIT,   0,        "",    "Sair",                0 },
};
#define MENU_DEF_N ((int)(sizeof menu_def / sizeof menu_def[0]))

/* Itens com o estado atual (chamado a cada quadro com o menu aberto) */
static void menu_build(App *a)
{
    const Machine *m = a->m;
    OsdMenuItem it[MENU_DEF_N];
    int has_tape = m->tape && tape_is_loaded(m->tape);
    update_info(a);
    for (int i = 0; i < MENU_DEF_N; i++) {
        OsdMenuItem *o = &it[i];
        memset(o, 0, sizeof *o);
        o->id = menu_def[i].id;
        o->gap = menu_def[i].gap;
        copy_str(o->key, sizeof o->key, menu_def[i].keyname);
        copy_str(o->label, sizeof o->label, menu_def[i].label);
        const char *v = "";
        char buf[16];
        switch (o->id) {
        case MI_TAPE:
            v = !has_tape ? "sem fita" : tape_is_playing(m->tape) ? "tocando" : "parada";
            break;
        case MI_REWIND:
            if (has_tape) {
                snprintf(buf, sizeof buf, "%d%%", tape_progress_permille(m->tape) / 10);
                v = buf;
            }
            break;
        case MI_TURBO:  v = turbo_active(a, is_loading(a)) ? "ligado" : "desligado"; break;
        case MI_MODEL:  v = machine_model_name(m->cfg.model); break;
        case MI_HZ:     v = a->hz60 ? "60 Hz" : "50 Hz"; break;
        case MI_JOY:    v = joymode_short(m->cfg.joymode); break;
        case MI_ARROWS: v = arrows_name(a->arrows_mode); break;
        case MI_FULL:   v = a->fullscreen ? "sim" : "nao"; break;
        case MI_DIAG:   v = a->diag ? "ligado" : "desligado"; break;
        default: break;
        }
        copy_str(o->value, sizeof o->value, v);
    }
    osd_menu_set(&a->osd, it, MENU_DEF_N);
}

static void open_menu(App *a)
{
    menu_build(a);
    osd_open_menu(&a->osd);
}

/* Item escolhido. Os que mudam um valor (turbo, modelo, Hz, joystick, setas,
 * tela cheia) deixam o menu aberto mostrando o valor novo. */
static void menu_activate(App *a, int id)
{
    Machine *m = a->m;
    switch (id) {
    case MI_OPEN:   open_browser(a); return;
    case MI_TAPE:   osd_close(&a->osd); function_key(a, SDLK_F3); return;
    case MI_REWIND:
        osd_close(&a->osd);
        if (!m->tape || !tape_is_loaded(m->tape)) { toast(a, "Nenhuma fita carregada"); return; }
        tape_stop(m->tape);
        tape_rewind(m->tape);
        /* a ROM da o play sozinha quando comeca a ler (rom_traps) */
        toast(a, "Fita rebobinada: LOAD \"\" carrega");
        return;
    case MI_RESET:  osd_close(&a->osd); function_key(a, SDLK_F5); return;
    case MI_SNAP:   osd_close(&a->osd); function_key(a, SDLK_F6); return;
    case MI_SHOT:   osd_close(&a->osd); function_key(a, SDLK_F12); return;
    case MI_TURBO:  function_key(a, SDLK_F7); break;
    case MI_MODEL:  function_key(a, SDLK_F10); break;
    case MI_HZ:     function_key(a, SDLK_F8); break;
    case MI_JOY:    function_key(a, SDLK_F9); break;
    case MI_ARROWS: function_key(a, SDLK_F4); break;
    case MI_FULL:   function_key(a, SDLK_F11); break;
    case MI_DIAG:
        a->diag = !a->diag;
        toast(a, a->diag ? "Diagnostico: JOY = bits do joystick" : "Diagnostico desligado");
        break;
    case MI_HELP:   osd_open_help(&a->osd, 1); return;
    case MI_QUIT:   osd_open_quit(&a->osd); return;
    default: return;
    }
    menu_build(a);
}

/* Tecla de funcao com o menu aberto: o item correspondente. 1 se tratou. */
static int menu_fkey(App *a, SDL_Keycode k)
{
    for (int i = 0; i < MENU_DEF_N; i++)
        if (menu_def[i].key == k) { menu_activate(a, menu_def[i].id); return 1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Toque                                                               */
/* ------------------------------------------------------------------ */

/* LOAD "": digita LOAD "" + ENTER (a mesma digitacao automatica do boot) e da
 * play na fita ao terminar. Sem fita no gravador, abre o navegador. */
static void quick_load(App *a)
{
    Machine *m = a->m;
    if (!m->tape || !tape_is_loaded(m->tape)) {
        open_browser(a);
        toast(a, "Nenhuma fita: escolha um jogo");
        return;
    }
    if (tape_is_playing(m->tape)) { toast(a, "A fita ja esta tocando"); return; }
    /* LOAD "" do boot (ou do toque anterior) ainda sendo digitado: recomecar
     * digitaria o J de novo no modo L (LOAD "j"") */
    if (m->autotype.active) { toast(a, "Carregando..."); return; }
    /* so digita direto com a ROM parada no editor, no modo K (o mesmo teste do
     * --type); com um jogo rodando, reinicia antes, como ao abrir uma fita */
    int editor = m->rom_ready && m->key_wait_frame == m->frame_count &&
                 !(m->mem[0x5C3B] & 0x08) && m->mem[0x5C41] == 0;
    if (!editor) machine_reset(m);
    if (tape_at_end(m->tape)) tape_rewind(m->tape);
    release_all(a);
    a->tap_count = a->tap_head = a->tap_phase = 0;
    a->auto_suppressed = 0;
    machine_autoload(m, 0, 1);
    toast(a, editor ? "LOAD \"\" e play na fita" : "Reinicio, LOAD \"\" e play na fita");
}

static void touch_action(App *a, int act)
{
    switch (act) {
    case TOUCH_ACT_MENU:       release_all_keep(a, 1); update_info(a); break;
    case TOUCH_ACT_OPEN_FILE:  open_browser(a); break;
    case TOUCH_ACT_PICKER:     open_picker(a); break;
    case TOUCH_ACT_RESET:      function_key(a, SDLK_F5); break;
    case TOUCH_ACT_TOGGLE_KBD: release_all_keep(a, 1); break;
    case TOUCH_ACT_TURBO:      function_key(a, SDLK_F7); break;
    case TOUCH_ACT_SNAPSHOT:   function_key(a, SDLK_F6); break;
    case TOUCH_ACT_SCREENSHOT: function_key(a, SDLK_F12); break;
    case TOUCH_ACT_HZ:         function_key(a, SDLK_F8); break;
    case TOUCH_ACT_MODEL:      function_key(a, SDLK_F10); break;
    case TOUCH_ACT_JOYMODE:    function_key(a, SDLK_F9); break;
    case TOUCH_ACT_TAPE:       function_key(a, SDLK_F3); break;
    case TOUCH_ACT_HELP:       function_key(a, SDLK_F1); break;
    case TOUCH_ACT_QUIT:       release_all(a); osd_open_quit(&a->osd); break;
    case TOUCH_ACT_LOAD:       quick_load(a); break;
    case TOUCH_ACT_REWIND:     menu_activate(a, MI_REWIND); break;
    case TOUCH_ACT_DIAG:       menu_activate(a, MI_DIAG); break;
    default: break;
    }
}

/* Valores mostrados no menu de toque e o modo sob o joystick */
static void touch_update_menu(App *a)
{
    TouchUI *t = a->touch;
    const Machine *m = a->m;
    touch_set_menu_item(t, TOUCH_ACT_TURBO, 1, turbo_active(a, is_loading(a)) ? "ligado" : "desligado");
    touch_set_menu_item(t, TOUCH_ACT_HZ, 1, a->hz60 ? "60 Hz" : "50 Hz");
    touch_set_menu_item(t, TOUCH_ACT_MODEL, 1, machine_model_name(m->cfg.model));
    touch_set_menu_item(t, TOUCH_ACT_JOYMODE, 1, joymode_short(m->cfg.joymode));
    const char *tape = "sem fita";
    if (m->tape && tape_is_loaded(m->tape)) tape = tape_is_playing(m->tape) ? "tocando" : "parada";
    touch_set_menu_item(t, TOUCH_ACT_TAPE, 1, tape);
    touch_set_menu_item(t, TOUCH_ACT_DIAG, 1, a->diag ? "ligado" : "desligado");
    char l1[OSD_TEXT_MAX], l2[OSD_TEXT_MAX];
    profile_lines(a, l1, sizeof l1, l2, sizeof l2);
    touch_set_menu_note(t, l1);
    char lbl[32];
    copy_str(lbl, sizeof lbl, joymode_short(m->cfg.joymode));
    for (char *c = lbl; *c; c++)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    touch_set_joy_label(t, lbl);
}

static int is_pointer_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_FINGERDOWN: case SDL_FINGERUP: case SDL_FINGERMOTION:
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION: case SDL_MOUSEWHEEL:
        return 1;
    default:
        return 0;
    }
}

#define OSD_MOUSE_DEV ((SDL_TouchID)-2)

/* Toque/mouse nos menus do OSD: um ponteiro so, em pixels da tela de toque */
static void osd_pointer_event(App *a, const SDL_Event *e)
{
    if (a->ui_s <= 0 || a->out_w <= 0 || a->out_h <= 0) return;
    OsdPtr kind;
    float x, y;
    SDL_TouchID dev;
    SDL_FingerID id;
    switch (e->type) {
    case SDL_MOUSEWHEEL:
        if (e->wheel.which != SDL_TOUCH_MOUSEID) osd_scroll(&a->osd, -e->wheel.y * 66);
        return;
    case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
#ifdef SDL_MOUSE_TOUCHID
        if (e->tfinger.touchId == SDL_MOUSE_TOUCHID) return;
#endif
        dev = e->tfinger.touchId;
        id = e->tfinger.fingerId;
        x = e->tfinger.x * (float)a->out_w;
        y = e->tfinger.y * (float)a->out_h;
        kind = e->type == SDL_FINGERDOWN ? OSD_PTR_DOWN
             : e->type == SDL_FINGERMOTION ? OSD_PTR_MOVE : OSD_PTR_UP;
        break;
    default: {
        int motion = e->type == SDL_MOUSEMOTION;
        if ((motion ? e->motion.which : e->button.which) == SDL_TOUCH_MOUSEID) return;
        if (!motion && e->button.button != SDL_BUTTON_LEFT) return;
        if (motion && !(e->motion.state & SDL_BUTTON_LMASK)) return;
        int ww = 0, wh = 0;
        SDL_GetWindowSize(a->win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return;
        x = (float)(motion ? e->motion.x : e->button.x) * (float)a->out_w / (float)ww;
        y = (float)(motion ? e->motion.y : e->button.y) * (float)a->out_h / (float)wh;
        dev = OSD_MOUSE_DEV;
        id = 0;
        kind = motion ? OSD_PTR_MOVE : e->type == SDL_MOUSEBUTTONDOWN ? OSD_PTR_DOWN : OSD_PTR_UP;
        break;
    }
    }
    if (kind == OSD_PTR_DOWN) {
        if (a->osd_ptr) return;                 /* um dedo so nos menus */
        a->osd_ptr = 1;
        a->osd_ptr_dev = dev;
        a->osd_ptr_id = id;
    } else if (!a->osd_ptr || a->osd_ptr_dev != dev || a->osd_ptr_id != id) {
        return;
    }
    if (kind == OSD_PTR_UP) a->osd_ptr = 0;
    char path[PLAT_PATH_MAX];
    path[0] = 0;
    OsdResult r = osd_pointer(&a->osd, kind, (int)(x / (float)a->ui_s), (int)(y / (float)a->ui_s),
                              path, sizeof path);
    handle_osd_result(a, r, path);
}

static void handle_pointer(App *a, const SDL_Event *e)
{
    if (osd_active(&a->osd)) { osd_pointer_event(a, e); return; }
    a->osd_ptr = 0;
    int act = TOUCH_ACT_NONE;
    if (touch_handle_event(a->touch, e, a->m, &act) && act != TOUCH_ACT_NONE) touch_action(a, act);
}

/* App em segundo plano (Android) ou sem foco: pausa a emulacao e o som */
static void pause_app(App *a, int on)
{
    if (a->bg_paused == on) return;
    a->bg_paused = on;
    /* so ao pausar: ao voltar, um dedo que ainda esta na tela (o SDL nao manda
     * outro FINGERDOWN para ele) continua valendo */
    if (on) release_all(a);
    if (a->audio) {
        SDL_PauseAudioDevice(a->audio, on);
        if (on) SDL_ClearQueuedAudio(a->audio);
    }
#ifdef __ANDROID__
    android_keep_screen_on(!on);
#endif
}

/* ---------------- --touch-script (testes da interface de toque) ----------------
 * Uma linha por comando: QUADRO COMANDO ARGS (QUADRO = quadros exibidos desde o
 * inicio, ou +N = N depois da linha anterior; '#' comenta). ALVO e "x,y"
 * normalizado (0..1) ou @nome de touch_find ("@key:P", "@btn:MENU", "@joy:ur",
 * "@menu:2") ou @file:PREFIXO (linha do navegador). Comandos:
 *   down ID ALVO | move ID ALVO | up ID     dedo ID (multitoque)
 *   tap ALVO [QUADROS]                      toque curto (padrao 4 quadros)
 *   hold ID ALVO QUADROS                    pressiona por N quadros
 *   drag ID ALVO1 ALVO2 QUADROS             arrasta e solta
 *   mouse down|move|up ALVO                 mouse (botao esquerdo)
 *   key NOME                                tecla (escape, back, return, f1...)
 *   keydown NOME | keyup NOME               segura / solta uma tecla (left, up, a, left_alt...)
 *   peek ENDERECO [N]                       N bytes da memoria no stdout (0x94A2...)
 *   event bg|fg|reset                       segundo plano / de volta / SDL_RENDER_DEVICE_RESET
 *   shot ARQ.png                            grava a janela renderizada
 *   print [TEXTO]                           estado no stdout
 *   quit */

typedef struct ScriptLine { unsigned long frame; char *text; } ScriptLine;
static ScriptLine *g_lines;
static float g_fx[64], g_fy[64];        /* ultima posicao de cada dedo sintetico */

static int script_load(App *a, const char *path)
{
    size_t len = 0;
    uint8_t *d = tk_read_file(path, &len, 1u << 20);
    if (!d) return -1;
    char *buf = (char *)realloc(d, len + 1);
    if (!buf) { free(d); return -1; }
    buf[len] = 0;
    int cap = 1;
    for (size_t i = 0; i < len; i++) if (buf[i] == '\n') cap++;
    g_lines = (ScriptLine *)calloc((size_t)cap, sizeof *g_lines);
    if (!g_lines) { free(buf); return -1; }
    unsigned long last = 0;
    char *p = buf;
    int n = 0;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *q = p;
        while (*q == ' ' || *q == '\t' || *q == '\r') q++;
        size_t l = strlen(q);
        while (l && (q[l - 1] == '\r' || q[l - 1] == ' ')) q[--l] = 0;
        if (*q && *q != '#') {
            char *end = NULL;
            int rel = *q == '+';
            unsigned long f = strtoul(q + rel, &end, 10);
            if (end == q + rel) fatal("--touch-script: linha sem quadro: '%s'", q);
            if (rel) f += last;
            if (f < last) fatal("--touch-script: quadros fora de ordem em '%s'", q);
            last = f;
            while (*end == ' ' || *end == '\t') end++;
            g_lines[n].frame = f;
            g_lines[n].text = end;
            n++;
        }
        p = nl ? nl + 1 : NULL;
    }
    a->script_buf = buf;        /* g_lines aponta para dentro dele */
    a->script_n = n;
    a->script_pos = 0;
    a->next_auto_id = 100;
    return 0;
}

static void sq_push(App *a, unsigned long tick, const SDL_Event *ev)
{
    if (a->sq_n >= SCRIPT_Q) { fprintf(stderr, "tk90x: fila do --touch-script cheia\n"); return; }
    a->sq[a->sq_n].tick = tick;
    a->sq[a->sq_n].ev = *ev;
    a->sq_n++;
}

static void sq_finger(App *a, unsigned long tick, Uint32 type, int id, float x, float y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.tfinger.touchId = 1;
    e.tfinger.fingerId = id;
    e.tfinger.x = x;
    e.tfinger.y = y;
    e.tfinger.pressure = type == SDL_FINGERUP ? 0.0f : 1.0f;
    g_fx[id & 63] = x;
    g_fy[id & 63] = y;
    sq_push(a, tick, &e);
}

static void sq_mouse(App *a, unsigned long tick, Uint32 type, float nx, float ny)
{
    int ww = 0, wh = 0;
    SDL_GetWindowSize(a->win, &ww, &wh);
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    int x = (int)(nx * (float)ww), y = (int)(ny * (float)wh);
    if (type == SDL_MOUSEMOTION) {
        e.motion.x = x; e.motion.y = y; e.motion.state = SDL_BUTTON_LMASK;
    } else {
        e.button.x = x; e.button.y = y; e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = 1;
    }
    if (a->win) e.button.windowID = SDL_GetWindowID(a->win);
    sq_push(a, tick, &e);
}

/* Alvo -> coordenadas normalizadas. 0 ok. */
static int script_target(App *a, const char *t, float *nx, float *ny)
{
    if (t[0] == '@') {
        if (!strncmp(t + 1, "file:", 5)) {
            int x, y;
            if (a->ui_s <= 0 || osd_browser_find(&a->osd, t + 6, &x, &y) != 0) return -1;
            *nx = ((float)x + 0.5f) * (float)a->ui_s / (float)a->out_w;
            *ny = ((float)y + 0.5f) * (float)a->ui_s / (float)a->out_h;
            return 0;
        }
        return a->touch ? touch_find(a->touch, t + 1, nx, ny) : -1;
    }
    return sscanf(t, "%f,%f", nx, ny) == 2 ? 0 : -1;
}

static void script_print(App *a, const char *text)
{
    const Machine *m = a->m;
    printf("script tick=%lu frame=%lu osd=%d menu=%d kbd=%d keys=%02X%02X%02X%02X%02X%02X%02X%02X "
           "joy=%02X joymode=%s arrows=%s editor=%d tape=%s pc=%04X perfil=%s %s\n",
           a->tick, (unsigned long)m->frame_count, (int)a->osd.mode,
           a->touch ? touch_menu_open(a->touch) : 0, a->touch ? touch_keyboard_visible(a->touch) : 0,
           m->key_rows[0], m->key_rows[1], m->key_rows[2], m->key_rows[3], m->key_rows[4],
           m->key_rows[5], m->key_rows[6], m->key_rows[7], m->joy_state,
           joymode_short(m->cfg.joymode), arrows_name(a->arrows_mode),
           machine_editor_active(m, EDITOR_FRAMES),
           m->tape && tape_is_loaded(m->tape) ? (tape_is_playing(m->tape) ? "play" : "stop") : "none",
           m->cpu.pc, a->profile ? a->profile->name : "-", text);
    fflush(stdout);
}

static void script_exec(App *a, const char *line)
{
    char cmd[16] = "", a1[256] = "", a2[256] = "", a3[256] = "", a4[64] = "";
    int n = sscanf(line, "%15s %255s %255s %255s %63s", cmd, a1, a2, a3, a4);
    unsigned long now = a->tick;
    float x = 0, y = 0, x2 = 0, y2 = 0;
    if (n < 1) return;
    if (!strcmp(cmd, "down") || !strcmp(cmd, "move")) {
        if (n < 3 || script_target(a, a2, &x, &y) != 0) goto bad;
        sq_finger(a, now, cmd[0] == 'd' ? SDL_FINGERDOWN : SDL_FINGERMOTION, atoi(a1), x, y);
    } else if (!strcmp(cmd, "up")) {
        int id = atoi(a1);
        if (n >= 3 && script_target(a, a2, &x, &y) == 0) sq_finger(a, now, SDL_FINGERUP, id, x, y);
        else sq_finger(a, now, SDL_FINGERUP, id, g_fx[id & 63], g_fy[id & 63]);
    } else if (!strcmp(cmd, "tap")) {
        if (n < 2 || script_target(a, a1, &x, &y) != 0) goto bad;
        int hold = n >= 3 ? atoi(a2) : 4;
        int id = a->next_auto_id++;
        if (a->next_auto_id > 120) a->next_auto_id = 100;
        sq_finger(a, now, SDL_FINGERDOWN, id, x, y);
        sq_finger(a, now + (unsigned long)(hold > 0 ? hold : 1), SDL_FINGERUP, id, x, y);
    } else if (!strcmp(cmd, "hold")) {
        if (n < 4 || script_target(a, a2, &x, &y) != 0) goto bad;
        int id = atoi(a1);
        sq_finger(a, now, SDL_FINGERDOWN, id, x, y);
        sq_finger(a, now + (unsigned long)atoi(a3), SDL_FINGERUP, id, x, y);
    } else if (!strcmp(cmd, "drag")) {
        if (n < 5 || script_target(a, a2, &x, &y) != 0 || script_target(a, a3, &x2, &y2) != 0)
            goto bad;
        int id = atoi(a1), steps = atoi(a4);
        if (steps < 1) steps = 1;
        sq_finger(a, now, SDL_FINGERDOWN, id, x, y);
        for (int i = 1; i <= steps; i++) {
            float f = (float)i / (float)steps;
            sq_finger(a, now + (unsigned long)i, SDL_FINGERMOTION, id, x + (x2 - x) * f,
                      y + (y2 - y) * f);
        }
        sq_finger(a, now + (unsigned long)steps + 1, SDL_FINGERUP, id, x2, y2);
    } else if (!strcmp(cmd, "mouse")) {
        if (n < 3 || script_target(a, a2, &x, &y) != 0) goto bad;
        Uint32 t = !strcmp(a1, "down") ? SDL_MOUSEBUTTONDOWN
                 : !strcmp(a1, "up") ? SDL_MOUSEBUTTONUP : SDL_MOUSEMOTION;
        sq_mouse(a, now, t, x, y);
    } else if (!strcmp(cmd, "key")) {
        for (char *c = a1; *c; c++) if (*c == '_') *c = ' ';    /* left_alt = "Left Alt" */
        SDL_Keycode k = !strcmp(a1, "back") ? SDLK_AC_BACK : SDL_GetKeyFromName(a1);
        if (k == SDLK_UNKNOWN) goto bad;
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_KEYDOWN;
        e.key.state = SDL_PRESSED;
        e.key.keysym.sym = k;
        e.key.keysym.scancode = k == SDLK_AC_BACK ? SDL_SCANCODE_AC_BACK : SDL_GetScancodeFromKey(k);
        sq_push(a, now, &e);
        e.type = SDL_KEYUP;
        e.key.state = SDL_RELEASED;
        sq_push(a, now + 2, &e);
    } else if (!strcmp(cmd, "keydown") || !strcmp(cmd, "keyup")) {
        for (char *c = a1; *c; c++) if (*c == '_') *c = ' ';
        SDL_Keycode k = SDL_GetKeyFromName(a1);
        if (k == SDLK_UNKNOWN) goto bad;
        SDL_Event e;
        SDL_zero(e);
        int down = !strcmp(cmd, "keydown");
        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.sym = k;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
        sq_push(a, now, &e);
    } else if (!strcmp(cmd, "peek")) {
        long addr = strtol(a1, NULL, 0), cnt = n >= 3 ? strtol(a2, NULL, 0) : 1;
        if (addr < 0 || addr > 65535 || cnt < 1 || cnt > 64) goto bad;
        printf("peek tick=%lu %04lX:", now, addr);
        for (long i = 0; i < cnt; i++) printf(" %02X", a->m->mem[(addr + i) & 0xFFFF]);
        printf("\n");
        fflush(stdout);
    } else if (!strcmp(cmd, "event")) {
        SDL_Event e;
        SDL_zero(e);
        if (!strcmp(a1, "bg")) e.type = SDL_APP_WILLENTERBACKGROUND;
        else if (!strcmp(a1, "fg")) e.type = SDL_APP_DIDENTERFOREGROUND;
        else if (!strcmp(a1, "reset")) e.type = SDL_RENDER_DEVICE_RESET;
        else goto bad;
        sq_push(a, now, &e);
    } else if (!strcmp(cmd, "shot")) {
        if (n < 2) goto bad;
        copy_str(a->shot_path, sizeof a->shot_path, a1);
        a->shot_pending = 1;
    } else if (!strcmp(cmd, "print")) {
        const char *rest = line + strlen(cmd);
        while (*rest == ' ') rest++;
        script_print(a, rest);
    } else if (!strcmp(cmd, "quit")) {
        a->quit = 1;
    } else {
        goto bad;
    }
    return;
bad:
    fprintf(stderr, "tk90x: --touch-script: comando invalido ou alvo invisivel (quadro %lu): %s\n",
            now, line);
}

/* Executa as linhas devidas e entrega os eventos agendados ate este quadro */
static void script_run(App *a)
{
    while (a->script_pos < a->script_n && g_lines[a->script_pos].frame <= a->tick)
        script_exec(a, g_lines[a->script_pos++].text);
    int k = 0;
    for (int i = 0; i < a->sq_n; i++) {
        if (a->sq[i].tick <= a->tick) SDL_PushEvent(&a->sq[i].ev);
        else a->sq[k++] = a->sq[i];
    }
    a->sq_n = k;
}

/* Renderizador e textura da imagem (no inicio e depois de SDL_RENDER_DEVICE_RESET) */
static void create_renderer(App *a)
{
    a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a->ren) a->ren = SDL_CreateRenderer(a->win, -1, 0);
    if (!a->ren) fatal("falha ao criar o renderizador: %s", SDL_GetError());
    if (!a->opt.touch) {
        SDL_RenderSetLogicalSize(a->ren, FB_W, FB_H);
        SDL_RenderSetIntegerScale(a->ren, SDL_TRUE);
    }
    a->tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                               FB_W, FB_H);
    if (!a->tex) fatal("falha ao criar a textura: %s", SDL_GetError());
}

/* O SDL perdeu o dispositivo grafico (contexto EGL ao voltar ao Android, D3D no
 * Windows): as texturas antigas nao valem mais. Recria renderizador e texturas;
 * o estado do toque (menu, teclado) continua. */
static void reset_renderer(App *a)
{
    touch_set_renderer(a->touch, NULL, NULL);
    if (a->ui_tex) SDL_DestroyTexture(a->ui_tex);
    a->ui_tex = NULL;
    a->ui_w = a->ui_h = 0;              /* render_touch recria no proximo quadro */
    if (a->tex) SDL_DestroyTexture(a->tex);
    a->tex = NULL;
    if (a->ren) SDL_DestroyRenderer(a->ren);
    a->ren = NULL;
    create_renderer(a);
    if (a->touch && touch_set_renderer(a->touch, a->ren, a->rom + 0x3D00) != 0)
        fatal("falha ao recriar os controles de toque: %s", SDL_GetError());
    SDL_Log("tk90x: renderizador recriado (SDL_RENDER_DEVICE_RESET)");
}

/* Desktop (sem --touch): mouse nos menus do OSD. Com o tamanho logico do
 * renderizador (320x240), o SDL ja entrega as coordenadas nessa escala. */
static void classic_mouse(App *a, const SDL_Event *e)
{
    char path[PLAT_PATH_MAX];
    OsdResult r = OSD_RES_NONE;
    path[0] = 0;
    switch (e->type) {
    case SDL_MOUSEWHEEL:
        if (a->osd.mode == OSD_BROWSER || a->osd.mode == OSD_MENU || a->osd.mode == OSD_HELP) {
            int up = e->wheel.y > 0;
            if (e->wheel.y == 0) return;
            OsdKey k = a->osd.mode == OSD_HELP ? (up ? OSDK_LEFT : OSDK_RIGHT) : (up ? OSDK_UP : OSDK_DOWN);
            for (int i = 0; i < (a->osd.mode == OSD_BROWSER ? 3 : 1); i++) osd_key(&a->osd, k, 0, path, sizeof path);
        }
        return;
    case SDL_MOUSEMOTION:
        r = osd_mouse(&a->osd, OSD_PTR_MOVE, e->motion.x, e->motion.y, 0, path, sizeof path);
        break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT) return;
        r = osd_mouse(&a->osd, e->type == SDL_MOUSEBUTTONDOWN ? OSD_PTR_DOWN : OSD_PTR_UP,
                      e->button.x, e->button.y, e->button.clicks, path, sizeof path);
        break;
    default:
        return;
    }
    handle_osd_result(a, r, path);
}

static void handle_event(App *a, const SDL_Event *e)
{
    char path[PLAT_PATH_MAX];
    if (a->touch && is_pointer_event(e)) { handle_pointer(a, e); return; }
    if (!a->touch && osd_active(&a->osd) && is_pointer_event(e)) { classic_mouse(a, e); return; }
    switch (e->type) {
    case SDL_QUIT:
        a->quit = 1;
        break;
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
        pause_app(a, 1);
        break;
    case SDL_APP_DIDENTERFOREGROUND:
        pause_app(a, 0);
        break;
    case SDL_KEYDOWN: {
        SDL_Keycode k = e->key.keysym.sym;
        SDL_Scancode sc = e->key.keysym.scancode;
        if (k == SDLK_AC_BACK) k = SDLK_ESCAPE;     /* voltar do Android = Esc */
        /* toque: Esc/voltar fecha primeiro o menu de toque e o teclado da tela */
        if (a->touch && k == SDLK_ESCAPE && !e->key.repeat && !osd_active(&a->osd)) {
            if (touch_menu_open(a->touch)) { touch_menu_close(a->touch); break; }
            if (touch_keyboard_visible(a->touch)) {
                touch_show_keyboard(a->touch, 0);
                release_all(a);
                break;
            }
        }
        if (osd_active(&a->osd)) {
            a->suppress_text = 1;
            /* auto-repeticao so para navegar na lista; Esc/F1/F2/Enter e o dialogo
             * de saida exigem um toque novo (segurar Esc nao confirma a saida) */
            if (e->key.repeat && (a->osd.mode == OSD_QUIT ||
                (k != SDLK_UP && k != SDLK_DOWN && k != SDLK_PAGEUP && k != SDLK_PAGEDOWN &&
                 k != SDLK_BACKSPACE && k != SDLK_LEFT)))
                break;
            /* F1 fecha o menu e a ajuda; no dialogo de saida, abre o menu */
            if (k == SDLK_F1 && (a->osd.mode == OSD_HELP || a->osd.mode == OSD_MENU)) {
                osd_close(&a->osd);
                break;
            }
            if (k == SDLK_F2 && a->osd.mode == OSD_BROWSER) { osd_close(&a->osd); break; }
            if (a->osd.mode == OSD_MENU && k >= SDLK_F2 && k <= SDLK_F12 && menu_fkey(a, k)) break;
            if (k == SDLK_F11) { set_fullscreen(a, !a->fullscreen); break; }
            if (k == SDLK_F1 || k == SDLK_F2) { function_key(a, k); break; }
            int ch;
            OsdKey ok = osd_key_of(k, &ch);
            if (ok == OSDK_NONE) break;
            path[0] = 0;
            handle_osd_result(a, osd_key(&a->osd, ok, ch, path, sizeof path), path);
            break;
        }
        a->last_down_repeat = e->key.repeat;
        if (is_function_key(k)) {
            if (e->key.repeat) break;
            if (k == SDLK_ESCAPE) { release_all(a); osd_open_quit(&a->osd); }
            else function_key(a, k);
            break;
        }
        if (e->key.repeat || (int)sc < 0 || (int)sc >= SDL_NUM_SCANCODES) break;
        a->last_down_sc = (int)sc;      /* mapeada ou nao (ver handle_text) */
        uint64_t keys;
        uint8_t joy;
        if (!map_scancode(a, sc, &keys, &joy)) break;
        if (text_capable(sc) && a->npend < (int)(sizeof a->pend / sizeof a->pend[0])) {
            PendKey *p = &a->pend[a->npend++];
            p->sc = (int)sc; p->keys = keys; p->joy = joy;
        } else {
            press_sc(a, (int)sc, keys, joy);
        }
        break;
    }
    case SDL_KEYUP: {
        int sc = (int)e->key.keysym.scancode;
        if (sc < 0 || sc >= SDL_NUM_SCANCODES) break;
        flush_pending(a, sc);
        release_sc(a, sc);
        break;
    }
    case SDL_TEXTINPUT:
        if (!osd_active(&a->osd) && !a->suppress_text) handle_text(a, e->text.text);
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) release_all(a);
#ifdef __ANDROID__
        /* o layout segue sozinho (render_touch le o tamanho de saida a cada
         * quadro); o registro so ajuda a diagnosticar trocas de superficie */
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            SDL_Log("tk90x: janela %dx%d", e->window.data1, e->window.data2);
        /* Android: sem foco (outro app, aviso do sistema) a emulacao pausa */
        /* em tela dividida o app continua visivel: so o segundo plano pausa */
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST && !android_in_multiwindow())
            pause_app(a, 1);
        else if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) pause_app(a, 0);
#endif
        break;
    case SDL_DROPFILE:
        if (e->drop.file) {
            osd_close(&a->osd);
            touch_menu_close(a->touch);     /* senao a emulacao segue pausada sob o menu */
            open_file(a, e->drop.file);
            SDL_free(e->drop.file);
        }
        break;
    case SDL_RENDER_DEVICE_RESET:
        reset_renderer(a);
        break;
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_JOYDEVICEADDED:
        open_pad(a);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        close_pad(a, e->cdevice.which);
        break;
    case SDL_JOYDEVICEREMOVED:
        close_pad(a, e->jdevice.which);
        break;
    default:
        break;
    }
}

/* Gera e enfileira o audio do ultimo quadro emulado (sempre um quadro inteiro,
 * na altura original). Durante a carga um passa-baixa suave (~7 kHz) tira a
 * aspereza das bordas da onda quadrada da fita. Com --wav-out, o mesmo audio
 * (inclusive o silencio de recomposicao) vai para o arquivo. */
static void queue_audio(App *a, int loading)
{
    if (!a->audio && !a->wav) return;
    double fps = frame_rate(a);
    a->audio_acc += (double)AUDIO_RATE / fps;
    int n = (int)a->audio_acc;
    a->audio_acc -= n;
    if (n <= 0) return;
    if (n > (int)(sizeof a->abuf / sizeof a->abuf[0])) n = (int)(sizeof a->abuf / sizeof a->abuf[0]);
    Uint32 per_frame = (Uint32)n * 2;
    if (a->audio) {
        Uint32 queued = SDL_GetQueuedAudioSize(a->audio);
        if (queued > per_frame * 5) return;         /* fila cheia: pula este quadro */
        if (queued == 0) {
            /* faltou audio (pausa, menu, lentidao): recompoe a folga com silencio */
            memset(a->abuf, 0, (size_t)n * 2);
            SDL_QueueAudio(a->audio, a->abuf, per_frame);
            SDL_QueueAudio(a->audio, a->abuf, per_frame);
            wav_write(a, a->abuf, n);
            wav_write(a, a->abuf, n);
        }
    }
    machine_audio(a->m, a->abuf, n, AUDIO_VOLUME);
    const float k = (float)(1.0 - exp(-6.283185307179586 * LOAD_LP_HZ / AUDIO_RATE));
    float y = a->lp_y;
    for (int i = 0; i < n; i++) {
        y += ((float)a->abuf[i] - y) * k;
        if (loading) a->abuf[i] = (int16_t)y;
    }
    a->lp_y = y;
    if (a->audio) SDL_QueueAudio(a->audio, a->abuf, per_frame);
    wav_write(a, a->abuf, n);
}

static void make_status(App *a)
{
    Machine *m = a->m;
    char s[OSD_TEXT_MAX];
    s[0] = 0;
    if (m->tape && tape_is_loaded(m->tape) && tape_is_playing(m->tape)) {
        int cur = tape_current_block(m->tape);
        const char *name = NULL;
        /* ultimo cabecalho tocado (Program:/Bytes:), senao o bloco atual */
        for (int i = cur; i >= 0 && !name; i--) {
            const char *d = tape_block_desc(m->tape, i);
            if (d && (!strncmp(d, "Program:", 8) || !strncmp(d, "Bytes:", 6))) name = d;
        }
        if (!name) name = tape_block_desc(m->tape, cur);
        /* a borda e das listras: o status (uma linha fina so da largura do
         * texto) aparece por alguns segundos quando o bloco ou a velocidade
         * muda e depois some; o progresso fica tambem no menu (F1) */
        char key[OSD_TEXT_MAX], speed[16];
        if (a->speed_now == 0) copy_str(speed, sizeof speed, "MAX");
        else snprintf(speed, sizeof speed, "%dx", a->speed_now);
        snprintf(key, sizeof key, "%s|%s", name ? name : "", speed);
        Uint32 now = SDL_GetTicks();
        if (strcmp(key, a->status_key) != 0) {
            copy_str(a->status_key, sizeof a->status_key, key);
            a->status_since = now;
        }
        if (now - a->status_since < STATUS_MS)
            snprintf(s, sizeof s, "FITA %d%% %s %s", tape_progress_permille(m->tape) / 10,
                     speed, name ? name : "");
    } else {
        a->status_key[0] = 0;
        if (a->speed_now == 0) snprintf(s, sizeof s, "TURBO");
    }
    /* teclado de toque aberto: a borda de baixo (onde fica o status) passa sob
     * as teclas e o texto apareceria misturado a primeira fileira */
    if (a->touch && touch_keyboard_visible(a->touch)) s[0] = 0;
    osd_set_status(&a->osd, s);
}

/* Modo toque: imagem no retangulo do layout, controles e, por cima, os
 * menus do OSD numa tela de (janela / escala inteira) pixels */
static void render_touch(App *a, Uint32 now)
{
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(a->ren, &w, &h) != 0 || w <= 0 || h <= 0) return;
    a->out_w = w;
    a->out_h = h;
    touch_update_menu(a);
    /* recorte da camera (Android 9+; --safe-insets nos testes): imagem e controles
     * ficam fora dele. Lido a cada quadro: muda ao girar 180 graus. */
    int ins[4] = { 0, 0, 0, 0 };
    if (a->opt.safe_set) memcpy(ins, a->opt.safe_ins, sizeof ins);
#ifdef __ANDROID__
    else android_safe_insets(ins);
#endif
    touch_set_safe_insets(a->touch, ins[0], ins[1], ins[2], ins[3]);
    touch_layout(a->touch, w, h, &a->pic);
    SDL_RenderCopy(a->ren, a->tex, NULL, &a->pic);
    touch_draw(a->touch);

    /* escala dos menus: tela de ~240-300 linhas (texto de 8 px da ROM) */
    int s = h / 240;
    if (s < 1) s = 1;
    int cw = (w + s - 1) / s, ch = (h + s - 1) / s;
    a->ui_s = s;
    if (!osd_canvas_busy(&a->osd, now)) return;
    if (!a->ui_tex || cw != a->ui_w || ch != a->ui_h) {
        if (a->ui_tex) SDL_DestroyTexture(a->ui_tex);
        free(a->ui_px);
        a->ui_px = (uint32_t *)malloc((size_t)cw * (size_t)ch * 4);
        a->ui_tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                      cw, ch);
        if (!a->ui_tex || !a->ui_px) {
            if (a->ui_tex) SDL_DestroyTexture(a->ui_tex);
            free(a->ui_px);
            a->ui_tex = NULL;
            a->ui_px = NULL;
            a->ui_w = a->ui_h = 0;
            return;
        }
        SDL_SetTextureBlendMode(a->ui_tex, SDL_BLENDMODE_BLEND);
        a->ui_w = cw;
        a->ui_h = ch;
    }
    memset(a->ui_px, 0, (size_t)cw * (size_t)ch * 4);
    a->osd.toast_top = (touch_top_height(a->touch) + s - 1) / s;   /* toast sob os botoes */
    osd_render_canvas(&a->osd, a->ui_px, cw, ch, now);
    SDL_UpdateTexture(a->ui_tex, NULL, a->ui_px, cw * 4);
    SDL_Rect dst = { 0, 0, cw * s, ch * s };
    SDL_RenderCopy(a->ren, a->ui_tex, NULL, &dst);
}

/* --window-shot / shot do --touch-script: grava o que foi desenhado na janela */
static void window_shot(App *a)
{
    a->shot_pending = 0;
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(a->ren, &w, &h) != 0 || w <= 0 || h <= 0) return;
    if (!a->touch) {
        /* tamanho logico 320x240: le so a area da imagem (o viewport) */
        SDL_Rect vp;
        float sx = 1, sy = 1;
        SDL_RenderGetViewport(a->ren, &vp);
        SDL_RenderGetScale(a->ren, &sx, &sy);
        if (vp.w > 0 && vp.h > 0) { w = (int)(vp.w * sx); h = (int)(vp.h * sy); }
    }
    uint32_t *px = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!px) return;
    if (SDL_RenderReadPixels(a->ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0 &&
        png_write_argb(a->shot_path, px, w, h) == 0)
        printf("window_shot=%s %dx%d tick=%lu\n", a->shot_path, w, h, a->tick);
    else
        fprintf(stderr, "tk90x: erro ao gravar %s: %s\n", a->shot_path, SDL_GetError());
    fflush(stdout);
    free(px);
}

static void render(App *a)
{
    Uint32 now = SDL_GetTicks();
    memcpy(a->disp, a->m->fb, sizeof a->disp);
    make_status(a);
    if (a->diag) {
        /* bits do joystick que chegam a maquina (000FUDLR) e o modo */
        char d[32];
        snprintf(d, sizeof d, "JOY:%02X %s", a->m->joy_state, joymode_short(a->m->cfg.joymode));
        osd_set_diag(&a->osd, d);
    } else {
        osd_set_diag(&a->osd, NULL);
    }
    osd_render(&a->osd, a->disp, now);
    SDL_UpdateTexture(a->tex, NULL, a->disp, FB_W * 4);
    SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 255);
    SDL_RenderClear(a->ren);
    if (a->touch) render_touch(a, now);
    else SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    a->tick++;
    if (a->shot_pending) window_shot(a);
    SDL_RenderPresent(a->ren);
}

/* Roda os quadros de um passo (1 quadro exibido), no maximo max_frames.
 * Guarda em speed_now os quadros emulados por exibido (0 = turbo maximo).
 * Devolve 1 se em turbo maximo. */
static int emu_step(App *a, long max_frames)
{
    int loading = is_loading(a);
    if (!loading) {
        a->auto_suppressed = 0;               /* carga terminou: turbo automatico rearma */
        if (a->turbo_load_only) a->turbo_forced = a->turbo_load_only = 0;
    }

    int turbo = turbo_active(a, loading);
    if (turbo) {
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 t0 = SDL_GetPerformanceCounter();
        Uint64 budget = (Uint64)(TURBO_BUDGET * (double)freq);
        long n = 0;
        do {
            emu_frame(a);
            if (n++ == 0) queue_audio(a, is_loading(a));
            /* turbo automatico para assim que a carga termina */
            if (!a->turbo_forced && !is_loading(a)) break;
        } while (SDL_GetPerformanceCounter() - t0 < budget && n < 5000 && n < max_frames);
        a->speed_now = 0;
        return 1;
    }
    /* carga nostalgica (--load-speed N, padrao 20): N quadros por quadro exibido. O
     * quadro exibido e o ultimo (borda com as listras reais) e o audio e o desse
     * mesmo quadro, na altura original: o chiado da fita continua audivel e a
     * cadencia (piloto, dados, pausa) corre N vezes mais rapido. */
    long n = loading ? load_factor(a) : 1;
    if (n > max_frames) n = max_frames;
    if (n < 1) n = 1;
    a->speed_now = (int)n;
    /* na janela, N quadros por exibido no maximo dentro do mesmo orcamento de
     * tempo do turbo: N alto demais para o computador nao congela a janela
     * (eventos, audio); sem janela, sempre N exatos (--wav-out, --frames) */
    Uint64 t0 = g_headless || n == 1 ? 0 : SDL_GetPerformanceCounter();
    Uint64 budget = g_headless || n == 1 ? 0
                    : (Uint64)(TURBO_BUDGET * (double)SDL_GetPerformanceFrequency());
    for (long i = 0; i < n; i++) {
        emu_frame(a);
        int still = is_loading(a);
        int late = budget && SDL_GetPerformanceCounter() - t0 >= budget;
        if (i == n - 1 || !still || late) {   /* ultimo, a carga acabou ou sem tempo */
            queue_audio(a, loading && still);
            break;
        }
    }
    return 0;
}

static void open_audio(App *a)
{
    if (a->opt.no_sound) return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "tk90x: sem audio: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    a->audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a->audio) {
        fprintf(stderr, "tk90x: sem audio: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(a->audio, 0);
}

static int run_gui(App *a)
{
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    /* Windows com escala de 125%/150%: sem isto o DWM estica a janela em
     * bitmap (borrado) e a escala inteira se perde; tamanhos em pixels reais */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#ifdef __ANDROID__
    /* paisagem (os dois lados), voltar chega como SDLK_AC_BACK */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    /* o acelerometro nao vira joystick (antes do SDL_INIT_JOYSTICK, que le a
     * dica): com ele, segurar o aparelho inclinado apertaria uma direcao */
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#endif
    /* toque: os dedos sao tratados direto (sem mouse sintetico) */
    if (a->opt.touch) SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        fatal("falha ao iniciar o SDL: %s", SDL_GetError());
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0)
        fprintf(stderr, "tk90x: sem controles: %s\n", SDL_GetError());

    int scale = a->opt.scale;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        while (scale > 1 && (FB_W * scale > usable.w || FB_H * scale > usable.h - 40)) scale--;

    Uint32 wflags = SDL_WINDOW_RESIZABLE;
    if (a->opt.fullscreen) wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    int win_w = FB_W * scale, win_h = FB_H * scale;
    if (a->opt.win_w > 0) { win_w = a->opt.win_w; win_h = a->opt.win_h; }   /* testes */
    a->win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              win_w, win_h, wflags);
    if (!a->win) fatal("falha ao criar a janela: %s", SDL_GetError());
    a->fullscreen = a->opt.fullscreen;
    SDL_SetWindowMinimumSize(a->win, FB_W, FB_H);
    create_renderer(a);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
#ifndef __ANDROID__
    SDL_StartTextInput();       /* no Android abriria o teclado do sistema */
#endif

    if (a->opt.touch) {
        /* layout do toque: a imagem vai no retangulo calculado por touch_layout */
        a->touch = touch_create(a->ren, a->rom + 0x3D00);
        if (!a->touch) fatal("falha ao criar os controles de toque: %s", SDL_GetError());
        osd_set_touch(&a->osd, 1);
        int picker = a->opt.touch_picker;
#ifdef __ANDROID__
        picker = 1;
#endif
        if (picker) {
            osd_set_picker_entry(&a->osd, "Abrir arquivo do aparelho...");
            touch_set_menu_item(a->touch, TOUCH_ACT_PICKER, 1, NULL);
        }
    }
    if (a->opt.touch_script && script_load(a, a->opt.touch_script) != 0)
        fatal("nao foi possivel ler '%s'", a->opt.touch_script);

    open_audio(a);
    open_pad(a);
    update_title(a);
#ifdef __ANDROID__
    android_keep_screen_on(1);
#endif
    if (a->touch) {
        if (!a->opt.file) toast(a, "MENU = jogos e opcoes   LOAD \"\" = carregar");
    } else {
        /* desktop: o menu e a ajuda ficam no F1 (o toast cobre so uma faixa no alto) */
        osd_toast(&a->osd, SDL_GetTicks(), 4000,
                  a->opt.file ? "F1 = menu e ajuda" : "F1 = menu e ajuda   F2 = abrir jogo");
    }

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = SDL_GetPerformanceCounter();
    Uint64 stat_t = next;
    int stats = getenv("TK90X_STATS") != NULL;
    int turbo = 0;
    while (!a->quit) {
        SDL_Event e;
        if (a->script_buf) script_run(a);
        while (SDL_PollEvent(&e)) handle_event(a, &e);
        flush_pending(a, -1);
        a->suppress_text = 0;
        if (a->quit) break;
        poll_pad(a);
#ifdef __ANDROID__
        {
            /* arquivo escolhido no seletor do aparelho (copiado para o cache):
             * o navegador continua lembrando o diretorio dos jogos */
            char picked[PLAT_PATH_MAX], dir[PLAT_PATH_MAX], last[256];
            if (android_poll_picked(picked, sizeof picked)) {
                copy_str(dir, sizeof dir, a->osd.dir);
                copy_str(last, sizeof last, a->osd.last_name);
                osd_close(&a->osd);
                touch_menu_close(a->touch);     /* "Abrir com" com o menu aberto */
                open_file(a, picked);
                copy_str(a->osd.dir, sizeof a->osd.dir, dir);
                copy_str(a->osd.last_name, sizeof a->osd.last_name, last);
            }
        }
#endif
        if (a->bg_paused) {
            /* segundo plano: nada roda nem desenha */
            SDL_Delay(30);
            a->tick++;
            next = SDL_GetPerformanceCounter();
            continue;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        if (osd_active(&a->osd) || (a->touch && touch_menu_open(a->touch))) {
            /* menu aberto: emulacao pausada */
            if (a->osd.mode == OSD_MENU) menu_build(a);
            render(a);
            SDL_Delay(15);
            next = SDL_GetPerformanceCounter();
            update_title(a);
            continue;
        }
        double fps = frame_rate(a);
        double period = (double)freq / fps;
        if ((Sint64)(now - next) < 0) {
            double ms = (double)(next - now) * 1000.0 / (double)freq;
            SDL_Delay(ms > 2.0 ? (Uint32)(ms - 1.0) : (ms > 0.5 ? 1 : 0));
            continue;
        }
        /* fila de audio ajusta levemente o passo (relogio da placa de som manda) */
        double adj = 1.0;
        if (a->audio) {
            double q = (double)SDL_GetQueuedAudioSize(a->audio) / 2.0 / ((double)AUDIO_RATE / fps);
            if (q < 2.0) adj = 0.995;
            else if (q > 4.0) adj = 1.005;
        }
        turbo = emu_step(a, 5000);
        profile_tick(a);
        next += (Uint64)(period * adj);
        /* atrasado: roda ate 3 quadros extras sem exibir; muito atrasado: ressincroniza
         * (nao na carga nostalgica: cada passo ja tem N quadros e orcamento de tempo) */
        for (int k = 0; k < 3 && !turbo && a->speed_now <= 1; k++) {
            now = SDL_GetPerformanceCounter();
            if ((Sint64)(now - next) < (Sint64)period) break;
            turbo = emu_step(a, 5000);
            next += (Uint64)period;
        }
        now = SDL_GetPerformanceCounter();
        if ((Sint64)(now - next) > (Sint64)(period * 4)) next = now;
        update_title(a);
        render(a);
        a->stat_shown++;
        if (stats && now - stat_t >= freq) {
            double q = a->audio ? SDL_GetQueuedAudioSize(a->audio) / 2.0 * 1000.0 / AUDIO_RATE : 0;
            double dt = (double)(now - stat_t) / (double)freq;
            fprintf(stderr, "quadros/s=%.1f exibidos/s=%.1f audio_fila=%.0fms turbo=%d\n",
                    a->stat_frames / dt, a->stat_shown / dt, q, turbo);
            a->stat_frames = a->stat_shown = 0;
            stat_t = now;
        }
    }

    if (a->opt.window_shot) {
        copy_str(a->shot_path, sizeof a->shot_path, a->opt.window_shot);
        a->shot_pending = 1;
        render(a);
    }
    if (a->audio) SDL_CloseAudioDevice(a->audio);
    if (a->pad) SDL_GameControllerClose(a->pad);
    touch_destroy(a->touch);
    a->touch = NULL;
    if (a->ui_tex) SDL_DestroyTexture(a->ui_tex);
    free(a->ui_px);
    free(a->script_buf);
    free(g_lines);
    g_lines = NULL;
    if (a->joy) SDL_JoystickClose(a->joy);
    SDL_DestroyTexture(a->tex);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}

/* ------------------------------------------------------------------ */

#ifdef _WIN32
/* Executavel de janela (-mwindows): se foi chamado de um console (cmd,
 * PowerShell), escreve nesse console cada saida que nao foi redirecionada
 * (stdout e stderr separados: "> out.txt" so redireciona a primeira). No
 * Windows 8+ um console herdado ja funciona sem isto; cobre o Windows 7 e
 * handles nao herdados. */
static void win_attach_console(void)
{
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE), he = GetStdHandle(STD_ERROR_HANDLE);
    int need_out = !ho || ho == INVALID_HANDLE_VALUE;
    int need_err = !he || he == INVALID_HANDLE_VALUE;
    if (!need_out && !need_err) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (need_out && !freopen("CONOUT$", "w", stdout)) return;
    if (need_err && !freopen("CONOUT$", "w", stderr)) return;
}
#endif

#ifdef __ANDROID__
static App *g_app;      /* para fatal_cleanup */

/* Depois de um fatal(): a biblioteca continua carregada e o Android pode abrir
 * o app de novo no mesmo processo. Sem SDL_Quit o video ficaria iniciado com a
 * janela antiga viva, e a proxima abertura falharia ("Android only supports one
 * window"). */
static void fatal_cleanup(void)
{
    App *a = g_app;
    g_app = NULL;
    if (a) {
        if (a->audio) SDL_CloseAudioDevice(a->audio);
        if (a->pad) SDL_GameControllerClose(a->pad);
        if (a->joy) SDL_JoystickClose(a->joy);
        touch_destroy(a->touch);
        if (a->ui_tex) SDL_DestroyTexture(a->ui_tex);
        if (a->tex) SDL_DestroyTexture(a->tex);
        if (a->ren) SDL_DestroyRenderer(a->ren);
        if (a->win) SDL_DestroyWindow(a->win);
        free(a->ui_px);
        free(a->script_buf);
        osd_free(&a->osd);
        if (a->m) { machine_free(a->m); free(a->m); }
        free(a);
    }
    free(g_lines);
    g_lines = NULL;
    SDL_Quit();
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    win_attach_console();
#endif
#ifdef __ANDROID__
    /* Android: sem linha de comando nem headless; tela cheia com toque */
    argc = argc > 1 ? 1 : argc;
    /* erro fatal: a caixa de mensagem ja foi mostrada; SDL_main retorna e o
     * SDLActivity fecha a Activity */
    if (setjmp(g_fatal_jmp)) {
        g_fatal_armed = 0;
        fatal_cleanup();
        return 1;
    }
    g_fatal_armed = 1;
#endif
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--headless")) g_headless = 1;
    App *a = (App *)calloc(1, sizeof(App));
    if (!a) { fprintf(stderr, "tk90x: sem memoria\n"); return 1; }
#ifdef __ANDROID__
    g_app = a;
#endif
    a->last_down_sc = -1;
    parse_args(&a->opt, argc, argv);
#ifdef __ANDROID__
    a->opt.touch = 1;
    a->opt.fullscreen = 1;
    {
        /* snapshots e capturas: armazenamento externo do app (acessivel por
         * USB), senao o interno */
        const char *d = SDL_AndroidGetExternalStoragePath();
        if (!d) d = SDL_AndroidGetInternalStoragePath();
        if (d) copy_str(a->save_dir, sizeof a->save_dir, d);
    }
#endif
    if (a->opt.headless) a->opt.touch = 0;
    g_headless = a->opt.headless;
    a->hz60 = a->opt.hz60;
    if (!a->opt.headless) {
        /* --frames/--until-loaded sem --headless: nada a fazer, mas avisa */
        if (a->opt.until_loaded)
            fprintf(stderr, "tk90x: --until-loaded so vale com --headless\n");
    }

    /* ROM */
    if (a->opt.rom_path) {
        size_t len = 0;
        uint8_t *r = tk_read_file(a->opt.rom_path, &len, 65536);
        if (!r) fatal("nao foi possivel ler a ROM '%s'", a->opt.rom_path);
        if (len != 16384) { free(r); fatal("a ROM '%s' deve ter 16384 bytes (tem %u)", a->opt.rom_path, (unsigned)len); }
        memcpy(a->rom, r, 16384);
        free(r);
    } else {
        model_rom(a->opt.model, a->rom);
    }

    a->m = (Machine *)calloc(1, sizeof(Machine));
    if (!a->m) fatal("sem memoria");
    MachineConfig cfg = base_config(a, a->opt.model);
    if (machine_init(a->m, a->rom, &cfg) != 0) fatal("falha ao iniciar a maquina");
    a->m->tape_sound = 1;       /* ouve-se a fita durante a carga, como no aparelho */
    osd_init(&a->osd, a->rom);
    a->last_joy = 0;
    a->user_joymode = a->m->cfg.joymode;

    if (a->opt.file) {
        if (open_file(a, a->opt.file) != 0 && a->opt.headless) return 1;
    }

    if (a->opt.wav_out) wav_open(a, a->opt.wav_out);

    int rc;
    if (a->opt.headless) {
        rc = run_headless(a);
    } else {
        update_info(a);
        rc = run_gui(a);
        if (a->opt.screenshot && png_write_argb(a->opt.screenshot, a->m->fb, FB_W, FB_H) != 0)
            fprintf(stderr, "tk90x: erro ao gravar %s\n", a->opt.screenshot);
    }
    wav_close(a);
    osd_free(&a->osd);
    machine_free(a->m);
    free(a->m);
    free(a);
#ifdef __ANDROID__
    g_app = NULL;
#endif
    return rc;
}
