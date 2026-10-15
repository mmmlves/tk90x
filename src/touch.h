/*
 * touch.h - Controles de toque na tela (Android; no Linux/Windows com --touch,
 * pelo mouse), desenhados com o SDL_Renderer e a fonte da ROM:
 *   - joystick virtual (8 direcoes, arrastando o polegar) a esquerda e botao
 *     FOGO a direita; o modo do joystick (Sinclair 1 = 6-0, Kempston...) e o
 *     da maquina;
 *   - teclado do TK (40 teclas em 4 fileiras, como no aparelho), sobreposto
 *     e translucido na parte de baixo; multitoque (CAPS/SYMBOL + tecla) e
 *     CAPS/SYMBOL como trava de um toque para a proxima tecla;
 *   - botoes pequenos no alto a direita: MENU, TECLADO e LOAD "";
 *   - menu de toque (abrir jogo, reiniciar, turbo, estado, 50/60 Hz...).
 *
 * As teclas e o joystick nao vao direto para a maquina: o front-end junta
 * touch_keys()/touch_joy() ao teclado fisico (main.c, sync_keys) e chama
 * touch_frame_done() depois de cada quadro emulado, o que garante que um toque
 * rapido fique pressionado por pelo menos um quadro.
 */
#ifndef TK_TOUCH_H
#define TK_TOUCH_H

#include <stdint.h>
#include <SDL.h>
#include "machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TouchUI TouchUI;

/* Acoes pedidas pela interface de toque (touch_handle_event) */
typedef enum {
    TOUCH_ACT_NONE = 0,
    TOUCH_ACT_MENU,         /* menu de toque aberto (pausar a emulacao) */
    TOUCH_ACT_OPEN_FILE,    /* abrir o navegador de jogos */
    TOUCH_ACT_RESET,
    TOUCH_ACT_TOGGLE_KBD,   /* teclado mostrado/escondido */
    TOUCH_ACT_TURBO,
    TOUCH_ACT_SNAPSHOT,
    TOUCH_ACT_SCREENSHOT,
    TOUCH_ACT_HZ,
    TOUCH_ACT_MODEL,
    TOUCH_ACT_JOYMODE,
    TOUCH_ACT_PICKER,       /* abrir arquivo do aparelho (seletor do Android) */
    TOUCH_ACT_LOAD,         /* LOAD "" + ENTER e play na fita */
    TOUCH_ACT_TAPE,         /* play/pausa da fita */
    TOUCH_ACT_HELP,
    TOUCH_ACT_QUIT,
    TOUCH_ACT_REWIND,       /* rebobinar a fita (acrescentado no fim: "menu:N" dos scripts) */
    TOUCH_ACT_COUNT
} TouchAction;

/* rom_font = rom + 0x3D00 (caracteres 32..127, 8x8). NULL se falhar. */
TouchUI *touch_create(SDL_Renderer *r, const uint8_t *rom_font);
void touch_destroy(TouchUI *t);
void touch_set_font(TouchUI *t, const uint8_t *rom_font);   /* troca de modelo */
/* Troca de renderizador (SDL_RENDER_DEVICE_RESET) sem perder o estado (menu,
 * teclado, itens): r = NULL libera a textura da fonte (chamar antes de destruir
 * o renderizador antigo); com o novo, recria a fonte. 0 ok, -1 se falhar. */
int  touch_set_renderer(TouchUI *t, SDL_Renderer *r, const uint8_t *rom_font);

void touch_set_enabled(TouchUI *t, int on);
int  touch_enabled(const TouchUI *t);

/* Recalcula o layout para a area de desenho win_w x win_h (pixels do
 * renderizador) e devolve em screen_dst onde vai a imagem do emulador
 * (4:3, a maior possivel, escala inteira quando cabe; com o teclado aberto, a
 * area de papel fica acima dele). Barato: pode ser chamado a cada quadro. */
void touch_layout(TouchUI *t, int win_w, int win_h, SDL_Rect *screen_dst);

/* Eventos SDL_FINGER* (multitoque) e de mouse (botao esquerdo). Devolve 1 se o
 * evento foi consumido e preenche *act (TOUCH_ACT_*). m pode ser NULL. */
int  touch_handle_event(TouchUI *t, const SDL_Event *ev, Machine *m, int *act);

/* Desenha os controles (depois da imagem do emulador). */
void touch_draw(TouchUI *t);

/* Estado para a maquina: teclas (bit = ZXKey) e joystick (JOY_*). */
uint64_t touch_keys(const TouchUI *t);
uint8_t  touch_joy(const TouchUI *t);
/* Depois de cada quadro emulado: solta o que ja foi visto pela maquina. */
void touch_frame_done(TouchUI *t);
/* Solta todos os dedos (menu aberto, perda de foco). */
void touch_release_all(TouchUI *t);

/* Menu de toque */
int  touch_menu_open(const TouchUI *t);
void touch_menu_close(TouchUI *t);
/* Mostra/esconde um item do menu e o valor exibido a direita (NULL = nenhum). */
void touch_set_menu_item(TouchUI *t, int act, int visible, const char *value);

/* Teclado na tela */
int  touch_keyboard_visible(const TouchUI *t);
void touch_show_keyboard(TouchUI *t, int on);

/* Altura ocupada no alto pelos botoes em fila (0 se estao na margem direita). */
int  touch_top_height(const TouchUI *t);

/* Rotulo sob o joystick (modo atual, ex.: "SINCLAIR 1"). */
void touch_set_joy_label(TouchUI *t, const char *label);

/* Testes (--touch-script): centro do elemento em coordenadas normalizadas
 * (0..1). Nomes: "key:P", "key:CAPS", "key:SYM", "key:ENTER", "key:SPACE",
 * "btn:MENU", "btn:KBD", "btn:LOAD", "fire", "joy" (centro), "joy:u", "joy:ur",
 * "joy:r", "joy:dr", "joy:d", "joy:dl", "joy:l", "joy:ul" (direcoes a 80% do
 * raio), "menu:N" (acao TOUCH_ACT_* de numero N), "menu:close".
 * 0 ok, -1 se o elemento nao esta visivel. */
int  touch_find(const TouchUI *t, const char *name, float *nx, float *ny);

#ifdef __cplusplus
}
#endif
#endif
