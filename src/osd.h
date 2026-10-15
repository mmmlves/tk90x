/*
 * osd.h - Texto sobreposto a tela (On-Screen Display) usando a fonte 8x8
 * da propria ROM ativa (rom + 0x3D00, caracteres 32..127):
 *   - linha de status na borda inferior (ex.: "FITA 45%  Program: NOME  TURBO");
 *   - mensagens temporarias ("toasts", 2-3 s) na borda superior;
 *   - menu principal (F1, so no desktop: itens montados pelo front-end com
 *     osd_menu_set), ajuda em paginas, navegador de arquivos (F2) e
 *     confirmacao de saida (Esc); no layout classico, tambem pelo mouse
 *     (osd_mouse, em pixels do framebuffer).
 * Modo toque (osd_set_touch, --touch e Android): os menus e o toast vao para
 * uma tela propria do tamanho da janela (osd_render_canvas), com linhas
 * grandes, botoes, toque para abrir e arraste para rolar (osd_pointer).
 *
 * Independente de SDL: o front-end traduz as teclas para OsdKey e entrega
 * uma copia do framebuffer (FB_W x FB_H, ARGB8888) para osd_render.
 * Texto em UTF-8; o que nao for ASCII vira a letra sem acento ou '?'.
 */
#ifndef TK_OSD_H
#define TK_OSD_H

#include <stddef.h>
#include <stdint.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSD_COLS 40            /* FB_W / 8 */
#define OSD_ROWS 30            /* FB_H / 8 */
#define OSD_TEXT_MAX 128

typedef enum { OSD_NONE = 0, OSD_HELP, OSD_BROWSER, OSD_QUIT, OSD_MENU } OsdMode;

/* Toque/mouse nos menus (osd_pointer), em pixels da tela de toque */
typedef enum { OSD_PTR_DOWN = 0, OSD_PTR_MOVE, OSD_PTR_UP } OsdPtr;

/* Teclas entregues aos menus */
typedef enum {
    OSDK_NONE = 0, OSDK_UP, OSDK_DOWN, OSDK_PGUP, OSDK_PGDN, OSDK_HOME, OSDK_END,
    OSDK_ENTER, OSDK_BACK, OSDK_ESC, OSDK_CHAR,
    OSDK_LEFT, OSDK_RIGHT   /* ajuda: pagina anterior/proxima; navegador: sobe/abre */
} OsdKey;

/* Resultado de osd_key */
typedef enum {
    OSD_RES_NONE = 0,     /* nada a fazer */
    OSD_RES_CLOSED,       /* menu fechado */
    OSD_RES_OPEN,         /* navegador: abrir o arquivo em path */
    OSD_RES_QUIT,         /* confirmou a saida */
    OSD_RES_PICKER,       /* navegador: entrada "abrir arquivo do aparelho" (Android) */
    OSD_RES_ACTION        /* menu: item escolhido em Osd.menu_action (o menu continua aberto;
                             o front-end decide se fecha) */
} OsdResult;

/* Item do menu principal (desktop). key: tecla de funcao mostrada a esquerda
 * ("F2", "" = nenhuma); value: estado atual a direita ("50 Hz", "" = nenhum);
 * gap: linha em branco antes (separa grupos). */
#define OSD_MENU_MAX 20
typedef struct OsdMenuItem {
    int  id;
    char key[4];
    char label[24];
    char value[16];
    int  gap;
} OsdMenuItem;

typedef struct Osd {
    uint8_t  font[96 * 8];             /* caracteres 32..127 */
    OsdMode  mode;

    char     status[OSD_TEXT_MAX];     /* "" = sem linha de status */
    char     toast[OSD_TEXT_MAX];
    uint32_t toast_until;              /* ms (relogio do chamador) */
    char     info[OSD_TEXT_MAX];       /* linha de estado mostrada na ajuda */
    char     tape_info[OSD_TEXT_MAX];  /* fita na ajuda: progresso, bloco, velocidade */
    char     tape_block[OSD_TEXT_MAX]; /* descricao do bloco atual (pode ser "") */
    char     controls[OSD_TEXT_MAX];   /* perfil do jogo: "Controles: ..." ("" = nenhum) */
    char     controls2[OSD_TEXT_MAX];  /* perfil do jogo: como comecar ("" = nada) */
    int      diag;                     /* diagnostico: canto com o estado do joystick */
    char     diag_text[32];

    /* Navegador de arquivos */
    char      dir[PLAT_PATH_MAX];      /* diretorio atual (lembrado entre aberturas) */
    PlatEntry *entries;                /* entrada 0 pode ser ".." */
    int       count, sel, top;
    char      browse_err[OSD_TEXT_MAX];
    char      last_name[256];          /* ultimo arquivo aberto (selecionado ao reabrir) */

    /* Modo toque (osd_set_touch): menus grandes desenhados por osd_render_canvas
     * numa tela do tamanho da janela; osd_render so desenha o status. */
    int       touch;
    char      picker_label[64];        /* entrada extra no topo do navegador ("" = nenhuma) */
    int       cv_w, cv_h;              /* tamanho da ultima tela de toque (para o hit-test) */
    int       scroll;                  /* navegador: deslocamento da lista em pixels */
    int       ptr_down, ptr_drag;      /* ponteiro pressionado / arrastando a lista */
    int       ptr_x0, ptr_y0, ptr_scroll0;
    int       ptr_row;                 /* linha sob o dedo (-1 = nenhuma) */
    int       ptr_btn;                 /* botao sob o dedo (0 = nenhum) */
    int       toast_top;               /* toque: o toast fica abaixo desta linha da tela */

    /* Menu principal (desktop) e ajuda em paginas */
    OsdMenuItem menu[OSD_MENU_MAX];
    int       menu_n, menu_sel;
    int       menu_action;             /* id do item escolhido (OSD_RES_ACTION) */
    int       mouse_item;              /* item/linha sob o clique do mouse (-1 = nenhum) */
    int       help_page;
    int       help_from_menu;          /* Esc na ajuda volta ao menu */
} Osd;

/* Inicializa (fonte da ROM de 16 KB) e zera o estado. */
void osd_init(Osd *o, const uint8_t *rom);
void osd_free(Osd *o);
/* Troca a fonte (ao mudar de ROM/modelo). */
void osd_set_font(Osd *o, const uint8_t *rom);

/* Linha de status na borda inferior (NULL ou "" esconde). */
void osd_set_status(Osd *o, const char *text);
/* Mensagem temporaria por ms milissegundos, a partir de now_ms. Um '\n' divide
 * o texto em duas linhas (a segunda some nos paineis grandes do desktop). */
void osd_toast(Osd *o, uint32_t now_ms, uint32_t ms, const char *text);
/* Linha de informacao exibida no topo da ajuda (modelo, Hz, joystick...). */
void osd_set_info(Osd *o, const char *text);
/* Controles do jogo aberto (perfil): mostrados no menu (F1) e na pagina do
 * joystick da ajuda. NULL/"" = nenhum. */
void osd_set_controls(Osd *o, const char *line1, const char *line2);
/* Diagnostico: texto curto num canto da imagem (NULL/"" esconde). */
void osd_set_diag(Osd *o, const char *text);
/* Fita na ajuda: linha de progresso e descricao do bloco atual (NULL = nenhuma). */
void osd_set_tape_info(Osd *o, const char *text, const char *block);

/* Abre um menu. osd_open_browser: dir NULL/"" usa o ultimo diretorio
 * (ou o atual). Devolve 0 ok.
 * osd_open_help: ajuda em paginas (Esquerda/Direita, PgUp/PgDn, 1-9); com
 * from_menu = 1, Esc volta ao menu principal. No modo toque o texto fala de
 * toques (MENU, TECLADO...) em vez das teclas do PC. */
void osd_open_help(Osd *o, int from_menu);
/* Menu principal do desktop: osd_menu_set entrega/atualiza os itens (pode ser
 * chamado a cada quadro; a selecao e mantida). */
void osd_open_menu(Osd *o);
void osd_menu_set(Osd *o, const OsdMenuItem *items, int n);
int  osd_help_pages(void);
int  osd_open_browser(Osd *o, const char *dir);
void osd_open_quit(Osd *o);
void osd_close(Osd *o);
/* Lembra o diretorio e o nome de um arquivo aberto por outro caminho
 * (linha de comando, arrastar e soltar) para a proxima abertura do navegador. */
void osd_browser_remember(Osd *o, const char *path);
int  osd_active(const Osd *o);          /* 1 se ha menu aberto (emulacao pausada) */

/* Tecla para o menu aberto. ch e o caractere digitado (OSDK_CHAR).
 * Em OSD_RES_OPEN o caminho completo vai para path. */
OsdResult osd_key(Osd *o, OsdKey key, int ch, char *path, size_t pathlen);

/* Mouse no layout classico, em pixels do framebuffer (FB_W x FB_H; com o
 * tamanho logico do SDL_Renderer os eventos ja chegam nessa escala). ev
 * OSD_PTR_MOVE sem botao serve para destacar o item sob o mouse. clicks = 2
 * (clique duplo) abre o arquivo no navegador. */
OsdResult osd_mouse(Osd *o, OsdPtr ev, int x, int y, int clicks, char *path, size_t pathlen);

/* Desenha toast, status e menu sobre fb (FB_W x FB_H ARGB8888). */
void osd_render(Osd *o, uint32_t *fb, uint32_t now_ms);

/* Extensoes aceitas pelo navegador (.tap .tzx .sna .z80 .scr .bin .cod). */
int  osd_supported_file(const char *name);

/* ---- Modo toque (--touch, Android) ---- */

/* Liga o layout de toque: linhas grandes, botoes, rolagem por arraste. */
void osd_set_touch(Osd *o, int on);
/* Entrada extra no topo do navegador (NULL/"" = nenhuma); ao ser escolhida,
 * osd_key/osd_pointer devolvem OSD_RES_PICKER. Vale na proxima abertura. */
void osd_set_picker_entry(Osd *o, const char *label);
/* Desenha o menu aberto e o toast numa tela w x h (ARGB8888 com alfa, que o
 * chamador zera antes) a ser esticada sobre a janela inteira. */
void osd_render_canvas(Osd *o, uint32_t *px, int w, int h, uint32_t now_ms);
/* 1 se osd_render_canvas tem algo a desenhar (menu aberto ou toast). */
int  osd_canvas_busy(const Osd *o, uint32_t now_ms);
/* Toque/mouse no menu aberto, em pixels da tela de osd_render_canvas.
 * Toque curto escolhe (abre arquivo / entra no diretorio); arrastar rola. */
OsdResult osd_pointer(Osd *o, OsdPtr ev, int x, int y, char *path, size_t pathlen);
/* Rola a lista do navegador (roda do mouse), em pixels da tela de toque. */
void osd_scroll(Osd *o, int dy);
/* Testes: centro da linha do navegador cujo nome comeca com prefix (rolando a
 * lista ate ela, se preciso). 0 ok, -1 se nao ha. */
int  osd_browser_find(Osd *o, const char *prefix, int *x, int *y);

#ifdef __cplusplus
}
#endif
#endif
