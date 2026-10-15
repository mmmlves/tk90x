/*
 * machine.h - A maquina TK90X / TK95 (clones brasileiros do ZX Spectrum 48K,
 * Microdigital). Memoria, ULA (video com borda por T-state, porta 0xFE,
 * teclado, beeper), joystick, gravador (tape.h), armadilhas na ROM
 * (carga instantanea opcional e instalacao do snapshot da fita virtual)
 * e digitacao automatica (LOAD "" apos o boot).
 *
 * Independente de SDL: o front-end (main.c) so le o framebuffer e o audio.
 */
#ifndef TK_MACHINE_H
#define TK_MACHINE_H

#include <stddef.h>
#include <stdint.h>
#include "z80.h"
#include "tape.h"
#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Framebuffer: tela 256x192 + borda visivel de 32 px (esq/dir) e 24 linhas (cima/baixo). */
#define FB_BORDER_X 32
#define FB_BORDER_Y 24
#define FB_W (256 + 2 * FB_BORDER_X)   /* 320 */
#define FB_H (192 + 2 * FB_BORDER_Y)   /* 240 */

/* Modelos (todos 48K, mesma placa/ULA Microdigital):
 *   MODEL_TK95    roms/tk95.rom    (CRC32 17368e07, ROM de 2a geracao, mais compativel) - padrao
 *   MODEL_TK90X   roms/tk90x.rom   (CRC32 3e785f6f, mesma ROM do TK95 com faixa "TK90X")
 *   MODEL_TK90X_V1 roms/tk90x-v1.rom (CRC32 d1b2f352, ROM original de 1985)
 * Temporizacao medida no TK90X real (ver docs/research/tk95.md):
 *   clock 3.575611 MHz, 228 T-states/linha; 60 Hz: 262 linhas (59736 T);
 *   50 Hz: 312 linhas (71136 T). */
typedef enum { MODEL_TK90X = 0, MODEL_TK95 = 1, MODEL_TK90X_V1 = 2 } Model;
#define TK_CPU_HZ 3575611u
/* Maior quadro (50 Hz) e folga para a ultima instrucao passar do fim do quadro */
#define TK_MAX_FRAME_TSTATES 71136u
#define TK_CONT_TAB_LEN (TK_MAX_FRAME_TSTATES + 512u)

/* Teclas do teclado Spectrum: indice = meia-linha*5 + bit.
 * Meias-linhas (porta 0xFE, byte alto com 1 bit zerado):
 *   0 (FEFE): CAPS Z X C V      1 (FDFE): A S D F G
 *   2 (FBFE): Q W E R T         3 (F7FE): 1 2 3 4 5
 *   4 (EFFE): 0 9 8 7 6         5 (DFFE): P O I U Y
 *   6 (BFFE): ENTER L K J H     7 (7FFE): SPACE SYM M N B */
typedef enum {
    ZXK_CAPS = 0, ZXK_Z, ZXK_X, ZXK_C, ZXK_V,
    ZXK_A, ZXK_S, ZXK_D, ZXK_F, ZXK_G,
    ZXK_Q, ZXK_W, ZXK_E, ZXK_R, ZXK_T,
    ZXK_1, ZXK_2, ZXK_3, ZXK_4, ZXK_5,
    ZXK_0, ZXK_9, ZXK_8, ZXK_7, ZXK_6,
    ZXK_P, ZXK_O, ZXK_I, ZXK_U, ZXK_Y,
    ZXK_ENTER, ZXK_L, ZXK_K, ZXK_J, ZXK_H,
    ZXK_SPACE, ZXK_SYM, ZXK_M, ZXK_N, ZXK_B,
    ZXK_COUNT
} ZXKey;

/* Bits do joystick (formato Kempston: 000FUDLR) */
#define JOY_RIGHT 0x01
#define JOY_LEFT  0x02
#define JOY_DOWN  0x04
#define JOY_UP    0x08
#define JOY_FIRE  0x10

/* O TK90X/TK95 tem conector de joystick embutido compativel com Interface 2,
 * teclas 6-0 (JOYMODE_SINCLAIR1) - e o padrao. Kempston e um opcional
 * (interface externa comum). */
typedef enum {
    JOYMODE_KEMPSTON = 0,  /* porta 0x1F (interface externa) */
    JOYMODE_SINCLAIR1,     /* teclas 6-0 (6 esq,7 dir,8 baixo,9 cima,0 fogo) - embutido no TK */
    JOYMODE_SINCLAIR2,     /* teclas 1-5 (1 esq,2 dir,3 baixo,4 cima,5 fogo) */
    JOYMODE_CURSOR,        /* 5 esq,6 baixo,7 cima,8 dir,0 fogo (Protek/AGF) */
    JOYMODE_QAOP,          /* teclas Q cima, A baixo, O esq, P dir, SPACE fogo */
    JOYMODE_KEYS,          /* teclas do jogo: mapa de machine_set_joy_keys (perfil do jogo) */
    JOYMODE_COUNT
} JoyMode;

/* Mapa do JOYMODE_KEYS: para cada bit do joystick (indice = posicao do bit
 * JOY_*: 0 direita, 1 esquerda, 2 baixo, 3 cima, 4 fogo) ate 2 teclas
 * pressionadas juntas (ex.: fogo = SYM + M). ZXK_COUNT = nenhuma. */
#define JOYKEYS_DIRS 5
typedef struct JoyKeys {
    uint8_t key[JOYKEYS_DIRS][2];
} JoyKeys;

typedef struct MachineConfig {
    Model    model;
    int      hz60;          /* 1 = modo 60 Hz (PAL-M), 0 = 50 Hz */
    int      issue2;        /* comportamento do bit 6 (EAR) da porta FE: 0 = issue 3 */
    JoyMode  joymode;
    int      flash_load;    /* 1 = carga instantanea por armadilha na ROM (so blocos padrao) */
    int      no_contention; /* 1 = desliga a contencao de memoria/E-S da ULA */
    int      no_floating_bus; /* 1 = portas nao decodificadas devolvem sempre 0xFF */
} MachineConfig;

/* ROM do modelo: tenta <dir_do_executavel>/roms/<arq>, depois ./roms/<arq>,
 * senao usa a copia embutida (roms_embedded.h). exe_dir pode ser NULL.
 * Copia 16384 bytes para out. Devolve 0 (arquivo) ou 1 (embutida). */
int machine_model_rom(Model model, const char *exe_dir, uint8_t *out);
const char *machine_model_name(Model model);   /* "TK95", "TK90X", "TK90X v1" */

/* Estado da digitacao automatica */
typedef struct AutoType {
    int      active;
    int      count, pos;
    uint8_t  keys[32][3];   /* ate 3 teclas simultaneas por passo (ZXK_COUNT = vazio) */
    int      hold_frames;   /* quadros com a tecla pressionada */
    int      gap_frames;    /* quadros soltos entre passos */
    int      timer;
    int      phase;         /* 0 = esperando ROM pronta, 1 = pressionando, 2 = soltando */
    int      wait_frames;   /* quadros minimos apos o reset antes de comecar (tela de abertura visivel) */
    int      play_tape_after; /* 1 = da play na fita ao terminar */
} AutoType;

typedef struct Machine {
    Z80       cpu;
    MachineConfig cfg;
    uint8_t   rom[16384];
    uint8_t   mem[65536];          /* 0..16383 = copia da ROM (escritas ignoradas) */

    /* Temporizacao (dependem de hz60, definidas em machine_init/machine_set_hz) */
    int       line_tstates;        /* T-states por linha */
    int       frame_lines;         /* linhas por quadro */
    uint32_t  frame_tstates;       /* T-states por quadro */
    int       first_screen_line;   /* linha (desde o INT) onde comeca a 1a linha do papel */
    int       int_length;          /* duracao do INT em T-states */
    int       contended;           /* 1 se ha contencao de memoria da ULA */

    /* ULA */
    uint8_t   border;              /* 0..7 */
    uint8_t   ula_out;             /* ultimo byte escrito na porta FE */
    uint8_t   key_rows[8];         /* ativo baixo: bit=0 tecla pressionada (5 bits) */
    uint8_t   joy_state;           /* JOY_* do controle fisico */
    JoyKeys   joy_keys;            /* teclas do JOYMODE_KEYS (machine_set_joy_keys; o reset mantem) */
    uint32_t  frame_count;         /* quadros desde o reset */
    int       flash_phase;         /* FLASH inverte a cada 32 quadros (ULA TK, bit 5) */

    /* Video */
    uint32_t  fb[FB_W * FB_H];     /* ARGB8888 */
    uint32_t  video_tstate;        /* ate onde o framebuffer foi desenhado neste quadro */

    /* Audio: transicoes do beeper/EAR no quadro (instante, nivel combinado:
     * bit0 = MIC, bit1 = alto-falante (bit 4 da porta FE), bit2 = EAR da fita) */
    struct { uint32_t t; uint8_t level; } aud_ev[8192];
    int       aud_count;
    uint8_t   aud_level_start;     /* nivel no inicio do quadro */
    uint8_t   aud_level;           /* nivel atual */
    int       tape_sound;          /* 1 = mixa o sinal da fita no audio */

    /* Fita */
    Tape     *tape;
    int       tape_ear;            /* nivel EAR atual vindo da fita */

    /* Fita virtual de snapshot: aplica quando o bloco de 48K terminar */
    int       pending_snap;        /* 1 = aguardando fim do bloco VTAPE_SNAPSHOT_DATA_BLOCK */
    Snapshot *snap;                /* propriedade da maquina (free em machine_free/limpeza) */
    int       vtape_snap;          /* 1 = a fita no gravador e a fita virtual de snap: o reset a
                                      rebobina e um novo LOAD "" rearma a instalacao */

    AutoType  autotype;
    int       rom_ready;           /* 1 apos a ROM terminar a inicializacao e esperar tecla */

    /* --- Acrescentados por machine.c (estado interno; o front-end pode so ler) --- */
    int       display_start;       /* T (desde o INT) do 1o T-state contido do papel, convencao
                                      48K (14335): 14687 a 50 Hz, 8759 a 60 Hz; 1o pixel em +1 */
    uint32_t  fb_t0;               /* T-state em que a ULA desenha o pixel (0,0) do framebuffer */
    uint8_t   auto_rows[8];        /* teclas da digitacao automatica (ativo baixo) */
    uint32_t  int_count;           /* interrupcoes aceitas desde o reset */
    int       in_frame;            /* 1 durante machine_run_frame */
    int       snap_block_done;     /* fita virtual: bloco de 48K ja terminou */
    int       snap_wait;           /* quadros desde o fim do bloco de 48K (aplica a forca apos 2) */
    int       flash_idle;          /* quadros desde a ultima carga instantanea (-1 = inativo) */
    float     aud_dc;              /* filtro passa-alta do audio (remove nivel DC) */
    /* Chamado logo apos instalar o snapshot da fita virtual (pode ser NULL). */
    void    (*snap_hook)(void *user, struct Machine *m);
    void     *snap_hook_user;
    uint32_t  key_wait_frame;      /* frame_count+1 do ultimo quadro em que a ROM esperou tecla
                                      (KEY-INPUT 0x10B4 sem tecla nova); 0 = nunca. Usado pelo --type */
    uint8_t   cont_tab[TK_CONT_TAB_LEN]; /* atraso de contencao por T-state do quadro */
} Machine;

/* Paleta ARGB8888 (0-7 normal, 8-15 BRIGHT). */
extern const uint32_t tk_palette[16];

/* Inicializa com a ROM (16384 bytes). Faz reset. Devolve 0 ok. */
int  machine_init(Machine *m, const uint8_t *rom, const MachineConfig *cfg);
void machine_free(Machine *m);
void machine_reset(Machine *m);                 /* reset a frio: RAM zerada, CPU reset, autotype limpo */
void machine_set_hz(Machine *m, int hz60);      /* troca 50/60 Hz (refaz a temporizacao) */

/* Roda exatamente um quadro: INT no inicio, CPU ate frame_tstates, desenha
 * o framebuffer (borda/papel com precisao de T-state), avanca a fita, coleta
 * eventos de audio, processa autotype e armadilhas. */
void machine_run_frame(Machine *m);

/* Gera nsamples amostras mono int16 do ultimo quadro rodado (som do beeper,
 * e da fita se tape_sound), distribuindo o quadro inteiro em nsamples
 * amostras (media por amostra = filtro passa-baixa simples, sem aliasing
 * grosseiro). O chamador escolhe nsamples = taxa / quadros_por_segundo.
 * volume 0..32767. Devolve nsamples. */
int  machine_audio(Machine *m, int16_t *out, int nsamples, int volume);

/* Teclado / joystick */
void machine_key(Machine *m, ZXKey k, int pressed);
void machine_release_all_keys(Machine *m);
void machine_joy(Machine *m, uint8_t joy_bits);  /* estado completo JOY_* */
/* Teclas do JOYMODE_KEYS (copiadas; NULL = nenhuma). machine_init comeca vazio. */
void machine_set_joy_keys(Machine *m, const JoyKeys *k);
/* Teclas que um modo de teclado (SINCLAIR1/2, CURSOR, QAOP, KEYS) aperta para
 * cada bit do joystick; KEMPSTON (porta 31) devolve tudo vazio. */
void machine_joy_keys_of(const Machine *m, JoyMode mode, JoyKeys *out);
/* 1 se a ROM esperou tecla no editor (KEY-INPUT sem tecla nova, ver
 * key_wait_frame) em algum dos ultimos `frames` quadros: cursor K/L/C/E/G do
 * BASIC, INPUT, "scroll?". Fica 0 com um programa em codigo de maquina rodando
 * (jogos leem o teclado direto), durante a carga da fita e em PAUSE/INKEY$.
 * O front-end usa para decidir se as setas do PC sao cursores ou joystick. */
int  machine_editor_active(const Machine *m, uint32_t frames);

/* Digitacao automatica de LOAD "" + ENTER (J, SYM+P, SYM+P, ENTER) apos a
 * ROM ficar pronta e no minimo wait_frames quadros desde o reset.
 * Se play_tape for 1, da play na fita ao terminar de digitar. */
void machine_autoload(Machine *m, int wait_frames, int play_tape);

/* Snapshots */
void machine_apply_snapshot(Machine *m, const Snapshot *s);     /* aplica ja */
/* Estado atual; durante a carga da fita virtual (pending_snap), o snapshot sendo carregado. */
void machine_take_snapshot(const Machine *m, Snapshot *s);
/* Entrega um snapshot a ser instalado quando a fita virtual terminar o bloco de 48K
 * (a maquina copia o snapshot). */
void machine_set_pending_snapshot(Machine *m, const Snapshot *s);

/* Carrega qualquer arquivo suportado (.tap .tzx .sna .z80 .scr .bin .cod)
 * do jeito "original": reset, tela de abertura, LOAD "" digitado e fita tocando
 * (snapshots e binarios viram fita virtual). org/exec usados para .bin/.cod
 * (org=0 -> o do cabecalho +3DOS, se houver, senao 32768; exec=0 -> exec=org).
 * Outras extensoes (ou nenhuma) sao reconhecidas pelo conteudo/tamanho (TZX,
 * SNA de 49179 bytes, cadeia TAP, +3DOS, tela de 6912 bytes); o resto e recusado.
 * instant=1 aplica snapshots direto, sem fita.
 * Devolve 0 ok, -1 erro com mensagem em err. */
int  machine_open_file(Machine *m, const char *path, uint16_t org, uint16_t exec,
                       int instant, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
#endif
