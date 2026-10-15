/* touch.c - Controles de toque: joystick virtual, FOGO, teclado do TK na
 * tela, botoes MENU / TECLADO / LOAD "" e menu de toque. Desenho com o
 * SDL_Renderer (SDL_RenderGeometry para circulos e cantos arredondados) e a
 * fonte 8x8 da ROM numa textura (sempre em escala inteira, nitida).
 *
 * Coordenadas internas: pixels do renderizador (touch_layout). Dedos chegam
 * normalizados (0..1); o mouse, em pixels da janela (convertidos). */
#include "touch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PTRS   10
#define MOUSE_DEV  ((SDL_TouchID)-0x7F00)  /* "dispositivo" do mouse (desktop --touch) */
#define K(x)       ((uint64_t)1 << (x))
#define MOD_MASK   (K(ZXK_CAPS) | K(ZXK_SYM))
/* Toque rapido no FOGO ou numa tecla da tela: fica pressionado por pelo menos
 * TAP_MIN quadros emulados (200 ms a 50 Hz). Varios jogos so leem o teclado de
 * vez em quando (entre as notas da musica do titulo, por exemplo) e perdiam um
 * toque de 3 a 5 quadros. Um toque novo na mesma tecla enquanto o anterior
 * ainda esta sendo prolongado solta antes a tecla por KEY_GAP quadros (a ROM so
 * aceita a mesma tecla de novo depois de 5 quadros solta) ou o FOGO por
 * FIRE_GAP, para o jogo ver dois toques (tiro a tiro) e nao um so. */
#define TAP_MIN    10
#define KEY_GAP    5
#define FIRE_GAP   2

/* Cores ARGB (alfa no byte alto) */
#define C_WHITE    0xFFFFFFFFu
#define C_TITLE    0xFFFFFF40u
#define C_VALUE    0xFF80E0FFu
#define C_KEY_BG   0xB4202028u
#define C_KEY_PR   0xE800B8B8u
#define C_KEY_LT   0xE8D8B800u
#define C_KEY_SUB  0xFFC0C0C8u
#define C_KEY_SYM  0xFFFF6464u
#define C_KB_BG    0x70000000u
#define C_BTN_BG   0xA0283040u
#define C_BTN_ON   0xD02868B0u
#define C_BTN_PR   0xE800B8B8u
#define C_BTN_LINE 0xC06080C0u
#define C_PANEL    0xF4101C50u
#define C_FRAME    0xFF4060C0u
#define C_ITEM     0xFF1C2C70u
#define C_ITEM_PR  0xFF60E0E0u
#define C_SHADE    0xA0000000u

enum { P_NONE = 0, P_JOY, P_FIRE, P_KEY, P_BTN, P_MENU };
enum { BTN_MENU = 0, BTN_KBD, BTN_LOAD, BTN_COUNT };
/* idx de P_MENU fora dos itens */
enum { MI_NONE = -1, MI_CLOSE = -2, MI_OUTSIDE = -3 };

typedef struct Ptr {
    int          used;
    SDL_TouchID  dev;
    SDL_FingerID id;
    int          kind, idx;
    float        x, y;          /* pixels */
    uint64_t     mods;          /* modificadores travados aplicados a esta tecla */
    int          other;         /* (CAPS/SYM) outra tecla pressionada enquanto segurava */
    int          nolatch;       /* (CAPS/SYM) este toque nao trava */
} Ptr;

/* Tecla: codigo, rotulo principal, palavra sob a letra (modo K / funcao com
 * CAPS), simbolo com SYMBOL SHIFT (vermelho, como no teclado) e nome p/ testes */
typedef struct KeyDef {
    uint8_t     zx;
    const char *main, *sub, *sym, *name;
} KeyDef;

/* As 40 teclas nas 4 fileiras do TK90X/TK95 (mesma disposicao do 48K). Nomes
 * do TK: GOTO, GOSUB, CONT, RAND; SYMBOL+X da UDG no lugar da libra.
 * '^' e a seta para cima na fonte da ROM. */
static const KeyDef kb_def[4][10] = {
    { { ZXK_1, "1", "EDIT", "!", "1" }, { ZXK_2, "2", "CAPS", "@", "2" },
      { ZXK_3, "3", "TRUE", "#", "3" }, { ZXK_4, "4", "INV.", "$", "4" },
      { ZXK_5, "5", "<", "%", "5" },    { ZXK_6, "6", "v", "&", "6" },
      { ZXK_7, "7", "^", "'", "7" },    { ZXK_8, "8", ">", "(", "8" },
      { ZXK_9, "9", "GRAF", ")", "9" }, { ZXK_0, "0", "DEL", "_", "0" } },
    { { ZXK_Q, "Q", "PLOT", "<=", "Q" },  { ZXK_W, "W", "DRAW", "<>", "W" },
      { ZXK_E, "E", "REM", ">=", "E" },   { ZXK_R, "R", "RUN", "<", "R" },
      { ZXK_T, "T", "RAND", ">", "T" },   { ZXK_Y, "Y", "RETURN", "AND", "Y" },
      { ZXK_U, "U", "IF", "OR", "U" },    { ZXK_I, "I", "INPUT", "AT", "I" },
      { ZXK_O, "O", "POKE", ";", "O" },   { ZXK_P, "P", "PRINT", "\"", "P" } },
    { { ZXK_A, "A", "NEW", "STOP", "A" }, { ZXK_S, "S", "SAVE", "NOT", "S" },
      { ZXK_D, "D", "DIM", "STEP", "D" }, { ZXK_F, "F", "FOR", "TO", "F" },
      { ZXK_G, "G", "GOTO", "THEN", "G" }, { ZXK_H, "H", "GOSUB", "^", "H" },
      { ZXK_J, "J", "LOAD", "-", "J" },   { ZXK_K, "K", "LIST", "+", "K" },
      { ZXK_L, "L", "LET", "=", "L" },    { ZXK_ENTER, "ENTER", NULL, NULL, "ENTER" } },
    { { ZXK_CAPS, "CAPS", "SHIFT", NULL, "CAPS" }, { ZXK_Z, "Z", "COPY", ":", "Z" },
      { ZXK_X, "X", "CLEAR", "UDG", "X" },  { ZXK_C, "C", "CONT", "?", "C" },
      { ZXK_V, "V", "CLS", "/", "V" },      { ZXK_B, "B", "BORDER", "*", "B" },
      { ZXK_N, "N", "NEXT", ",", "N" },     { ZXK_M, "M", "PAUSE", ".", "M" },
      { ZXK_SYM, "SYMBOL", "SHIFT", NULL, "SYM" }, { ZXK_SPACE, "SPACE", "BREAK", NULL, "SPACE" } },
};

typedef struct MenuItem {
    int  act;
    const char *label;
    int  visible;
    char value[32];
} MenuItem;

static const MenuItem menu_init[] = {
    { TOUCH_ACT_OPEN_FILE,  "Abrir jogo",        1, "" },
    { TOUCH_ACT_PICKER,     "Abrir do aparelho", 0, "" },
    { TOUCH_ACT_TAPE,       "Fita",              1, "" },
    { TOUCH_ACT_REWIND,     "Rebobinar fita",    1, "" },
    { TOUCH_ACT_RESET,      "Reiniciar",         1, "" },
    { TOUCH_ACT_TURBO,      "Turbo",             1, "" },
    { TOUCH_ACT_SNAPSHOT,   "Salvar estado",     1, "" },
    { TOUCH_ACT_SCREENSHOT, "Captura de tela",   1, "" },
    { TOUCH_ACT_HZ,         "50/60 Hz",          1, "" },
    { TOUCH_ACT_MODEL,      "Modelo",            1, "" },
    { TOUCH_ACT_JOYMODE,    "Joystick",          1, "" },
    { TOUCH_ACT_DIAG,       "Diagnostico",       1, "" },
    { TOUCH_ACT_HELP,       "Ajuda",             1, "" },
    { TOUCH_ACT_QUIT,       "Sair",              1, "" },
};
#define MENU_N ((int)(sizeof menu_init / sizeof menu_init[0]))

static const char *btn_label[BTN_COUNT] = { "MENU", "TECLADO", "LOAD \"\"" };

struct TouchUI {
    SDL_Renderer *r;
    SDL_Texture  *font;
    int       enabled;
    int       w, h;              /* area de desenho (pixels do renderizador) */
    float     u;                 /* unidade de tamanho (~ um botao) */
    float     pad;
    float     dpi;               /* 0 = desconhecido */
    SDL_Rect  pic;

    SDL_Rect  btn[BTN_COUNT];
    int       btn_ts;

    float     jx, jy, jr;        /* joystick */
    float     fx, fy, fr;        /* FOGO */
    int       lbl_ts;
    int       lbl_x0, lbl_room;  /* faixa onde cabe o rotulo do joystick (pixels) */
    char      joy_label[32];
    char      menu_note[96];     /* linha no titulo do menu ("Controles: ...") */

    int       kb_on;
    SDL_Rect  kb;                /* area do teclado */
    SDL_Rect  key[4][10];
    int       key_gap;

    int       menu_on;
    MenuItem  items[MENU_N];
    SDL_Rect  item_rc[MENU_N];
    SDL_Rect  panel, close_rc;
    int       menu_ts, title_ts;

    Ptr       p[MAX_PTRS];
    uint64_t  latched;           /* CAPS/SYMBOL travados para a proxima tecla */
    uint64_t  pend_keys;         /* pressionadas desde o ultimo quadro emulado */
    uint8_t   pend_joy;
    /* toque minimo (TAP_MIN, KEY_GAP, FIRE_GAP): quadros restantes por tecla e do FOGO */
    uint8_t   key_min[ZXK_COUNT], key_rel[ZXK_COUNT];
    uint8_t   fire_min, fire_gap;
};

/* ------------------------------------------------------------------ */
/* Desenho basico                                                      */
/* ------------------------------------------------------------------ */

static SDL_Color col(uint32_t argb)
{
    SDL_Color c;
    c.a = (Uint8)(argb >> 24); c.r = (Uint8)(argb >> 16); c.g = (Uint8)(argb >> 8); c.b = (Uint8)argb;
    return c;
}

#define GEO_MAX 256
static SDL_Vertex g_v[GEO_MAX];
static int        g_i[GEO_MAX * 3];

static void vtx(SDL_Vertex *v, float x, float y, SDL_Color c)
{
    v->position.x = x; v->position.y = y; v->color = c;
    v->tex_coord.x = 0; v->tex_coord.y = 0;
}

/* Poligono convexo (n pontos) como leque a partir do centro */
static void fill_poly(TouchUI *t, float cx, float cy, const float *xs, const float *ys, int n,
                      uint32_t argb)
{
    if (n < 3 || n + 1 > GEO_MAX) return;
    SDL_Color c = col(argb);
    vtx(&g_v[0], cx, cy, c);
    for (int i = 0; i < n; i++) vtx(&g_v[i + 1], xs[i], ys[i], c);
    for (int i = 0; i < n; i++) {
        g_i[i * 3] = 0; g_i[i * 3 + 1] = 1 + i; g_i[i * 3 + 2] = 1 + (i + 1) % n;
    }
    SDL_RenderGeometry(t->r, NULL, g_v, n + 1, g_i, n * 3);
}

/* Faixa entre dois contornos com o mesmo numero de pontos (anel, moldura) */
static void fill_band(TouchUI *t, const float *ox, const float *oy, const float *ix,
                      const float *iy, int n, uint32_t argb)
{
    if (n < 3 || 2 * n > GEO_MAX) return;
    SDL_Color c = col(argb);
    for (int i = 0; i < n; i++) {
        vtx(&g_v[2 * i], ox[i], oy[i], c);
        vtx(&g_v[2 * i + 1], ix[i], iy[i], c);
    }
    for (int i = 0; i < n; i++) {
        int a = 2 * i, b = 2 * i + 1, c2 = 2 * ((i + 1) % n), d = c2 + 1;
        g_i[i * 6 + 0] = a; g_i[i * 6 + 1] = b; g_i[i * 6 + 2] = c2;
        g_i[i * 6 + 3] = b; g_i[i * 6 + 4] = d; g_i[i * 6 + 5] = c2;
    }
    SDL_RenderGeometry(t->r, NULL, g_v, 2 * n, g_i, n * 6);
}

#define CIRC_N 48

static void circle_pts(float cx, float cy, float r, float *xs, float *ys)
{
    for (int i = 0; i < CIRC_N; i++) {
        float a = (float)i * 6.2831853f / CIRC_N;
        xs[i] = cx + r * cosf(a);
        ys[i] = cy + r * sinf(a);
    }
}

static void fill_circle(TouchUI *t, float cx, float cy, float r, uint32_t argb)
{
    float xs[CIRC_N], ys[CIRC_N];
    circle_pts(cx, cy, r, xs, ys);
    fill_poly(t, cx, cy, xs, ys, CIRC_N, argb);
}

static void ring(TouchUI *t, float cx, float cy, float r0, float r1, uint32_t argb)
{
    float ox[CIRC_N], oy[CIRC_N], ix[CIRC_N], iy[CIRC_N];
    circle_pts(cx, cy, r1, ox, oy);
    circle_pts(cx, cy, r0, ix, iy);
    fill_band(t, ox, oy, ix, iy, CIRC_N, argb);
}

/* Fatia de circulo entre os angulos a0 e a1 (radianos) */
static void sector(TouchUI *t, float cx, float cy, float r, float a0, float a1, uint32_t argb)
{
    float xs[18], ys[18];
    int n = 0;
    xs[n] = cx; ys[n] = cy; n++;
    for (int i = 0; i <= 12; i++) {
        float a = a0 + (a1 - a0) * (float)i / 12.0f;
        xs[n] = cx + r * cosf(a); ys[n] = cy + r * sinf(a); n++;
    }
    float mx = cx + 0.5f * r * cosf((a0 + a1) / 2), my = cy + 0.5f * r * sinf((a0 + a1) / 2);
    fill_poly(t, mx, my, xs, ys, n, argb);
}

#define RR_N 6      /* segmentos por canto */

static int round_pts(SDL_Rect rc, float rad, float inset, float *xs, float *ys)
{
    float x0 = (float)rc.x + inset, y0 = (float)rc.y + inset;
    float x1 = (float)(rc.x + rc.w) - inset, y1 = (float)(rc.y + rc.h) - inset;
    float r = rad - inset;
    float maxr = ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 2;
    if (r > maxr) r = maxr;
    if (r < 0) r = 0;
    const float cxs[4] = { x1 - r, x1 - r, x0 + r, x0 + r };
    const float cys[4] = { y0 + r, y1 - r, y1 - r, y0 + r };
    int n = 0;
    for (int c = 0; c < 4; c++) {
        float base = -1.5707963f + (float)c * 1.5707963f;
        for (int i = 0; i <= RR_N; i++) {
            float a = base + 1.5707963f * (float)i / RR_N;
            xs[n] = cxs[c] + r * cosf(a);
            ys[n] = cys[c] + r * sinf(a);
            n++;
        }
    }
    return n;
}

static void fill_round(TouchUI *t, SDL_Rect rc, float rad, uint32_t argb)
{
    float xs[4 * (RR_N + 1)], ys[4 * (RR_N + 1)];
    int n = round_pts(rc, rad, 0, xs, ys);
    fill_poly(t, rc.x + rc.w / 2.0f, rc.y + rc.h / 2.0f, xs, ys, n, argb);
}

static void outline_round(TouchUI *t, SDL_Rect rc, float rad, float th, uint32_t argb)
{
    float ox[4 * (RR_N + 1)], oy[4 * (RR_N + 1)], ix[4 * (RR_N + 1)], iy[4 * (RR_N + 1)];
    int n = round_pts(rc, rad, 0, ox, oy);
    round_pts(rc, rad, th, ix, iy);
    fill_band(t, ox, oy, ix, iy, n, argb);
}

static void fill_rect(TouchUI *t, SDL_Rect rc, uint32_t argb)
{
    SDL_SetRenderDrawColor(t->r, (Uint8)(argb >> 16), (Uint8)(argb >> 8), (Uint8)argb,
                           (Uint8)(argb >> 24));
    SDL_RenderFillRect(t->r, &rc);
}

static void triangle(TouchUI *t, float x1, float y1, float x2, float y2, float x3, float y3,
                     uint32_t argb)
{
    SDL_Color c = col(argb);
    SDL_Vertex v[3];
    vtx(&v[0], x1, y1, c); vtx(&v[1], x2, y2, c); vtx(&v[2], x3, y3, c);
    SDL_RenderGeometry(t->r, NULL, v, 3, NULL, 0);
}

/* Texto ASCII com a fonte da ROM, escala inteira sc */
static int text_w(const char *s, int sc) { return (int)strlen(s) * 8 * sc; }

static void draw_text(TouchUI *t, int x, int y, int sc, uint32_t argb, const char *s)
{
    if (!t->font || sc < 1) return;
    SDL_SetTextureColorMod(t->font, (Uint8)(argb >> 16), (Uint8)(argb >> 8), (Uint8)argb);
    SDL_SetTextureAlphaMod(t->font, (Uint8)(argb >> 24));
    for (int i = 0; s[i]; i++) {
        int c = (unsigned char)s[i];
        if (c < 32 || c > 127) c = '?';
        SDL_Rect src = { (c - 32) * 8, 0, 8, 8 };
        SDL_Rect dst = { x + i * 8 * sc, y, 8 * sc, 8 * sc };
        SDL_RenderCopy(t->r, t->font, &src, &dst);
    }
}

static void text_center(TouchUI *t, SDL_Rect rc, int sc, uint32_t argb, const char *s)
{
    draw_text(t, rc.x + (rc.w - text_w(s, sc)) / 2, rc.y + (rc.h - 8 * sc) / 2, sc, argb, s);
}

/* Maior escala <= sc em que s cabe em w pixels (0 = nao cabe nem em 1) */
static int fit_scale(const char *s, int sc, int w)
{
    while (sc > 0 && text_w(s, sc) > w) sc--;
    return sc;
}

static int iround(float v) { return (int)floorf(v + 0.5f); }

/* ------------------------------------------------------------------ */
/* Criacao                                                             */
/* ------------------------------------------------------------------ */

void touch_set_font(TouchUI *t, const uint8_t *rom_font)
{
    if (!t || !rom_font) return;
    if (!t->font) {
        t->font = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                    96 * 8, 8);
        if (!t->font) return;
        SDL_SetTextureBlendMode(t->font, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(t->font, SDL_ScaleModeNearest);
#endif
    }
    static uint32_t px[96 * 8 * 8];
    for (int c = 0; c < 96; c++)
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                px[r * 96 * 8 + c * 8 + b] = (rom_font[c * 8 + r] & (0x80 >> b)) ? 0xFFFFFFFFu
                                                                                : 0x00FFFFFFu;
    SDL_UpdateTexture(t->font, NULL, px, 96 * 8 * 4);
}

TouchUI *touch_create(SDL_Renderer *r, const uint8_t *rom_font)
{
    TouchUI *t = (TouchUI *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->r = r;
    t->enabled = 1;
    memcpy(t->items, menu_init, sizeof t->items);
    snprintf(t->joy_label, sizeof t->joy_label, "SINCLAIR 1");
    touch_set_font(t, rom_font);
    if (!t->font) { free(t); return NULL; }
    float ddpi = 0;
    if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0 && ddpi >= 100.0f && ddpi <= 1000.0f)
        t->dpi = ddpi;
    return t;
}

int touch_set_renderer(TouchUI *t, SDL_Renderer *r, const uint8_t *rom_font)
{
    if (!t) return -1;
    if (t->font) SDL_DestroyTexture(t->font);
    t->font = NULL;
    t->r = r;
    if (!r) return 0;
    touch_set_font(t, rom_font);
    return t->font ? 0 : -1;
}

void touch_destroy(TouchUI *t)
{
    if (!t) return;
    if (t->font) SDL_DestroyTexture(t->font);
    free(t);
}

void touch_set_enabled(TouchUI *t, int on)
{
    if (!t) return;
    t->enabled = on ? 1 : 0;
    if (!on) { touch_release_all(t); t->menu_on = 0; }
}

int touch_enabled(const TouchUI *t) { return t && t->enabled; }

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

static void layout_menu(TouchUI *t)
{
    float u = t->u, pad = t->pad;
    int gap = iround(u * 0.08f);
    if (gap < 3) gap = 3;
    int n = 0;
    for (int i = 0; i < MENU_N; i++) if (t->items[i].visible) n++;
    int cols = t->w >= t->h ? 2 : 1;
    int rows = (n + cols - 1) / cols;
    int title_h = iround(u * 0.7f);
    int ih = iround(u * 0.8f);
    int maxih = (int)(((float)t->h * 0.94f - (float)title_h - 2 * pad - (float)(rows - 1) * gap)
                      / (float)rows);
    if (ih > maxih) ih = maxih;
    if (ih < 16) ih = 16;
    int avail_w = (int)(((float)t->w * 0.96f - 2 * pad - (float)(cols - 1) * gap) / (float)cols);

    /* escala do texto: a maior em que rotulo + valor cabem */
    int ts = iround((float)ih * 0.38f / 8.0f);
    if (ts < 1) ts = 1;
    int iw = 0;
    for (; ts >= 1; ts--) {
        int need = 0;
        for (int i = 0; i < MENU_N; i++) {
            if (!t->items[i].visible) continue;
            int chars = (int)strlen(t->items[i].label) + 1;
            int vlen = (int)strlen(t->items[i].value);
            /* espaco para o valor mais longo que o item pode ter */
            if (t->items[i].act == TOUCH_ACT_JOYMODE && vlen < 14) vlen = 14;
            if (t->items[i].act == TOUCH_ACT_MODEL && vlen < 8) vlen = 8;
            chars += vlen;
            int wpx = chars * 8 * ts + 2 * (int)(pad + 0.5f);
            if (wpx > need) need = wpx;
        }
        iw = need;
        if (iw < iround(u * 4)) iw = iround(u * 4);
        if (iw <= avail_w || ts == 1) break;
    }
    if (iw > avail_w) iw = avail_w;
    t->menu_ts = ts;
    t->title_ts = fit_scale("MENU", ts + 1, iw);

    int pw = cols * iw + (cols - 1) * gap + 2 * (int)pad;
    int ph = title_h + rows * ih + (rows - 1) * gap + 2 * (int)pad;
    t->panel.x = (t->w - pw) / 2;
    t->panel.y = (t->h - ph) / 2;
    t->panel.w = pw;
    t->panel.h = ph;
    int cs = title_h - gap;
    t->close_rc.x = t->panel.x + pw - (int)pad - cs;
    t->close_rc.y = t->panel.y + (int)pad / 2;
    t->close_rc.w = cs;
    t->close_rc.h = cs;
    int k = 0;
    for (int i = 0; i < MENU_N; i++) {
        if (!t->items[i].visible) { memset(&t->item_rc[i], 0, sizeof t->item_rc[i]); continue; }
        int c = k % cols, rr = k / cols;
        t->item_rc[i].x = t->panel.x + (int)pad + c * (iw + gap);
        t->item_rc[i].y = t->panel.y + (int)pad + title_h + rr * (ih + gap);
        t->item_rc[i].w = iw;
        t->item_rc[i].h = ih;
        k++;
    }
}

/* Retangulo da imagem entre as linhas top e bottom da janela w x h. Sem o
 * teclado: a imagem inteira (borda inclusa), centralizada. Com o teclado
 * (kb): o papel de 192 linhas cabe entre top e bottom; a borda pode sair. */
static SDL_Rect place_picture(int w, int h, int top, int bottom, int kb)
{
    float s = (float)w / FB_W;
    if ((float)h / FB_H < s) s = (float)h / FB_H;
    int avail = bottom - top;
    if (kb && (float)avail / 192.0f < s) s = (float)avail / 192.0f;
    float si = floorf(s);
    if (si >= 1.0f && si >= 0.85f * s) s = si;     /* escala inteira quando cabe */
    if (s < 0.25f) s = 0.25f;
    SDL_Rect pic;
    pic.w = iround(FB_W * s);
    pic.h = iround(FB_H * s);
    pic.x = (w - pic.w) / 2;
    if (!kb) pic.y = (h - pic.h) / 2;
    else if (pic.h <= avail) pic.y = top + (avail - pic.h) / 2;
    else pic.y = top + (avail - iround(192 * s)) / 2 - iround(FB_BORDER_Y * s);
    return pic;
}

void touch_layout(TouchUI *t, int w, int h, SDL_Rect *dst)
{
    if (!t) return;
    t->w = w;
    t->h = h;
    float u = (float)h * 0.11f;
    if (u > (float)w * 0.065f) u = (float)w * 0.065f;   /* telas 4:3 (tablets) */
    if (t->dpi > 0) {
        float cap = t->dpi * 11.0f / 25.4f;      /* no maximo ~11 mm (tablets) */
        if (u > cap) u = cap;
    }
    if (u < 28) u = 28;
    t->u = u;
    float pad = u * 0.15f;
    if (pad < 4) pad = 4;
    t->pad = pad;

    /* teclado: 4 fileiras de 10 na parte de baixo */
    int gap = iround(u * 0.06f);
    if (gap < 2) gap = 2;
    t->key_gap = gap;
    int kh = iround(u * 0.95f);
    int maxkh = (int)(((float)h * 0.46f - 3 * gap - pad) / 4);
    if (kh > maxkh) kh = maxkh;
    int kw = iround(u * 1.6f);
    int maxkw = (int)(((float)w - 2 * pad - 9 * gap) / 10);
    if (kw > maxkw) kw = maxkw;
    t->kb.w = 10 * kw + 9 * gap;
    t->kb.h = 4 * kh + 3 * gap;
    t->kb.x = (w - t->kb.w) / 2;
    t->kb.y = h - (int)pad - t->kb.h;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 10; c++) {
            SDL_Rect *k = &t->key[r][c];
            k->x = t->kb.x + c * (kw + gap);
            k->y = t->kb.y + r * (kh + gap);
            k->w = kw;
            k->h = kh;
        }

    /* botoes do alto: tamanho */
    int bh = iround(u * 0.72f);
    int ts = iround((float)bh * 0.45f / 8.0f);
    if (ts < 1) ts = 1;
    int bw = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        int wpx = text_w(btn_label[i], ts) + iround((float)bh * 0.7f);
        if (wpx > bw) bw = wpx;
    }
    t->btn_ts = ts;
    int bgap = gap * 2;

    /* imagem: 4:3 a maior possivel; com o teclado, o papel (192 linhas) fica
     * acima dele (a borda de baixo passa sob o teclado translucido) */
    int kb = t->enabled && t->kb_on;
    int bottom = kb ? t->kb.y - 2 * gap - (int)(pad / 2) : h;
    SDL_Rect pic = place_picture(w, h, 0, bottom, kb);
    int mr = w - (pic.x + pic.w);
    int stack = mr >= bw + 2 * (int)pad;      /* pilha de botoes na margem direita */
    /* com o teclado e os botoes em fila no alto, o papel fica entre os dois */
    if (kb && !stack && t->enabled)
        pic = place_picture(w, h, (int)pad + bh + gap, bottom, kb);
    t->pic = pic;
    if (dst) *dst = pic;
    if (!t->enabled) return;

    int ml = pic.x;
    mr = w - (pic.x + pic.w);
    if (stack) {
        /* na margem lateral sobra altura: botoes de ~u (alvo de dedo de ~7 mm);
         * na fila do alto ficam baixos para nao cobrir o papel */
        int sbh = iround(u * 0.9f);
        int x = w - mr / 2 - bw / 2;
        for (int i = 0; i < BTN_COUNT; i++) {
            t->btn[i].x = x;
            t->btn[i].y = (int)pad + i * (sbh + bgap);
            t->btn[i].w = bw;
            t->btn[i].h = sbh;
        }
    } else {
        int x = w - (int)pad;
        for (int i = 0; i < BTN_COUNT; i++) {
            int bwi = text_w(btn_label[i], ts) + iround((float)bh * 0.7f);
            x -= bwi;
            t->btn[i].x = x;
            t->btn[i].y = (int)pad;
            t->btn[i].w = bwi;
            t->btn[i].h = bh;
            x -= bgap;
        }
    }

    /* joystick a esquerda, FOGO a direita: no centro da margem se couber */
    /* A area de toque (1,5 raio) fica longe das faixas de gesto do Android: a
     * de baixo (inicio/apps) e as dos lados (voltar, ~6% da largura). Com
     * margem, o controle se afasta da borda sem invadir a imagem; sobre a
     * imagem (telas 4:3), so a base sai da faixa do lado. */
    float side = (float)w * 0.06f;
    t->jr = u * 1.05f;
    t->jy = h - t->jr - u * 1.1f;
    if (ml >= 2 * t->jr + 2 * pad) {
        t->jx = ml / 2.0f;
        if (t->jx < side + t->jr * 1.5f) t->jx = side + t->jr * 1.5f;
        if (t->jx > ml - t->jr - pad) t->jx = ml - t->jr - pad;
    } else {
        t->jx = pad + t->jr + u * 0.2f;
        if (t->jx < side + t->jr) t->jx = side + t->jr;
    }
    t->fr = u * 0.8f;
    if (mr >= 2 * t->fr + 2 * pad) {
        t->fx = w - mr / 2.0f;
        if (t->fx > w - side - t->fr * 1.4f) t->fx = w - side - t->fr * 1.4f;
        if (t->fx < w - mr + t->fr + pad) t->fx = w - mr + t->fr + pad;
    } else {
        t->fx = w - pad - t->fr - u * 0.2f;
        if (t->fx > w - side - t->fr) t->fx = w - side - t->fr;
    }
    t->fy = t->jy;
    t->lbl_ts = iround(u * 0.025f);
    if (t->lbl_ts < 1) t->lbl_ts = 1;
    /* rotulo do modo: dentro da margem esquerda, sem invadir a imagem; sem
     * margem (a base ja fica sobre a borda do TK), ate onde comeca o papel
     * (a borda e 1/10 da largura da imagem de cada lado) */
    t->lbl_x0 = (int)pad;
    if (ml - 2 * (int)pad >= 2 * t->jr) t->lbl_room = ml - 2 * (int)pad;
    else t->lbl_room = ml + pic.w / 10 - 2 * (int)pad;
    if (t->lbl_room < (int)(2 * t->jr)) t->lbl_room = (int)(2 * t->jr);

    layout_menu(t);
}

/* ------------------------------------------------------------------ */
/* Estado das teclas                                                   */
/* ------------------------------------------------------------------ */

uint64_t touch_keys(const TouchUI *t)
{
    if (!t || !t->enabled) return 0;
    uint64_t k = t->pend_keys;
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].used && t->p[i].kind == P_KEY) k |= K(t->p[i].idx) | t->p[i].mods;
    for (int z = 0; z < ZXK_COUNT; z++) {
        if (t->key_rel[z]) k &= ~K(z);          /* intervalo entre dois toques rapidos */
        else if (t->key_min[z]) k |= K(z);      /* toque rapido ainda prolongado */
    }
    return k;
}

static uint8_t joy_dir(const TouchUI *t, float x, float y)
{
    float dx = x - t->jx, dy = y - t->jy;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < t->jr * 0.22f) return 0;           /* zona morta */
    float c = 0.38268f * d;                    /* sen(22,5): 8 setores de 45 graus */
    uint8_t b = 0;
    if (dx > c) b |= JOY_RIGHT;
    if (dx < -c) b |= JOY_LEFT;
    if (dy > c) b |= JOY_DOWN;
    if (dy < -c) b |= JOY_UP;
    return b;
}

static uint8_t joy_held(const TouchUI *t)
{
    uint8_t j = 0;
    for (int i = 0; i < MAX_PTRS; i++) {
        const Ptr *p = &t->p[i];
        if (!p->used) continue;
        if (p->kind == P_JOY) j |= joy_dir(t, p->x, p->y);
        else if (p->kind == P_FIRE) j |= JOY_FIRE;
    }
    return j;
}

/* Junta d as direcoes pendentes: uma direcao nova substitui a oposta do mesmo
 * eixo (um polegar rapido nunca da ESQ+DIR ou CIMA+BAIXO no mesmo quadro) */
static uint8_t joy_merge(uint8_t pend, uint8_t d)
{
    if (d & (JOY_LEFT | JOY_RIGHT)) pend &= (uint8_t)~(JOY_LEFT | JOY_RIGHT);
    if (d & (JOY_UP | JOY_DOWN))    pend &= (uint8_t)~(JOY_UP | JOY_DOWN);
    return (uint8_t)(pend | d);
}

uint8_t touch_joy(const TouchUI *t)
{
    if (!t || !t->enabled) return 0;
    /* a posicao atual do dedo manda no eixo que ela ocupa */
    uint8_t j = joy_merge(t->pend_joy, joy_held(t));
    if (t->fire_gap) j &= (uint8_t)~JOY_FIRE;
    else if (t->fire_min) j |= JOY_FIRE;
    return j;
}

void touch_frame_done(TouchUI *t)
{
    if (!t) return;
    t->pend_keys = 0;
    t->pend_joy = 0;
    for (int z = 0; z < ZXK_COUNT; z++) {
        if (t->key_rel[z]) t->key_rel[z]--;
        else if (t->key_min[z]) t->key_min[z]--;
    }
    if (t->fire_gap) t->fire_gap--;
    else if (t->fire_min) t->fire_min--;
}

/* Esquece os toques rapidos ainda prolongados */
static void drop_min(TouchUI *t)
{
    memset(t->key_min, 0, sizeof t->key_min);
    memset(t->key_rel, 0, sizeof t->key_rel);
    t->fire_min = t->fire_gap = 0;
}

void touch_release_all(TouchUI *t)
{
    if (!t) return;
    memset(t->p, 0, sizeof t->p);
    t->pend_keys = 0;
    t->pend_joy = 0;
    drop_min(t);
}

void touch_release_keep_joy(TouchUI *t)
{
    if (!t) return;
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].kind != P_JOY) memset(&t->p[i], 0, sizeof t->p[i]);
    t->pend_keys = 0;
    t->pend_joy = 0;
    drop_min(t);
}

int touch_menu_open(const TouchUI *t) { return t && t->enabled && t->menu_on; }

void touch_menu_close(TouchUI *t)
{
    if (!t) return;
    t->menu_on = 0;
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].kind == P_MENU) t->p[i].used = 0;
}

void touch_set_menu_item(TouchUI *t, int act, int visible, const char *value)
{
    if (!t) return;
    for (int i = 0; i < MENU_N; i++) {
        if (t->items[i].act != act) continue;
        int vis = visible ? 1 : 0;
        const char *v = value ? value : "";
        if (t->items[i].visible == vis && !strcmp(t->items[i].value, v)) return;
        t->items[i].visible = vis;
        snprintf(t->items[i].value, sizeof t->items[i].value, "%s", v);
        if (t->w > 0) layout_menu(t);
        return;
    }
}

int touch_keyboard_visible(const TouchUI *t) { return t && t->enabled && t->kb_on; }

void touch_show_keyboard(TouchUI *t, int on)
{
    if (!t) return;
    t->kb_on = on ? 1 : 0;
    /* dedos no FOGO/teclado anterior deixam de valer; o polegar que segura o
     * joystick continua (o outro dedo tocou TECLADO) */
    for (int i = 0; i < MAX_PTRS; i++) {
        int k = t->p[i].kind;
        if (k == P_FIRE || k == P_KEY) t->p[i].used = 0;
    }
    t->latched = 0;
}

int touch_top_height(const TouchUI *t)
{
    if (!t || !t->enabled || t->w <= 0) return 0;
    /* em fila: todos na mesma altura; em pilha: um embaixo do outro */
    if (t->btn[0].y != t->btn[1].y) return 0;
    return t->btn[0].y + t->btn[0].h;
}

void touch_set_menu_note(TouchUI *t, const char *note)
{
    if (t) snprintf(t->menu_note, sizeof t->menu_note, "%s", note ? note : "");
}

void touch_set_joy_label(TouchUI *t, const char *label)
{
    if (t) snprintf(t->joy_label, sizeof t->joy_label, "%s", label ? label : "");
}

/* ------------------------------------------------------------------ */
/* Eventos                                                             */
/* ------------------------------------------------------------------ */

static int in_rect(SDL_Rect rc, float x, float y)
{
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

static Ptr *find_ptr(TouchUI *t, SDL_TouchID dev, SDL_FingerID id)
{
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].used && t->p[i].dev == dev && t->p[i].id == id) return &t->p[i];
    return NULL;
}

static Ptr *new_ptr(TouchUI *t, SDL_TouchID dev, SDL_FingerID id)
{
    Ptr *p = find_ptr(t, dev, id);
    if (p) return p;
    for (int i = 0; i < MAX_PTRS; i++)
        if (!t->p[i].used) {
            memset(&t->p[i], 0, sizeof t->p[i]);
            t->p[i].dev = dev;
            t->p[i].id = id;
            return &t->p[i];
        }
    return NULL;
}

/* Tecla sob o ponto (com o vao entre teclas dividido ao meio); -1 nenhuma */
static int key_at(const TouchUI *t, float x, float y, int *row, int *colp)
{
    float g = t->key_gap / 2.0f;
    if (x < t->kb.x - g || x >= t->kb.x + t->kb.w + g || y < t->kb.y - g ||
        y >= t->kb.y + t->kb.h + g)
        return -1;
    int kw = t->key[0][0].w, kh = t->key[0][0].h;
    int c = (int)((x - t->kb.x + g) / (float)(kw + t->key_gap));
    int r = (int)((y - t->kb.y + g) / (float)(kh + t->key_gap));
    if (c < 0) c = 0;
    if (c > 9) c = 9;
    if (r < 0) r = 0;
    if (r > 3) r = 3;
    *row = r;
    *colp = c;
    return kb_def[r][c].zx;
}

static int is_mod(int zx) { return zx == ZXK_CAPS || zx == ZXK_SYM; }

/* Algum dedo (alem de p) segura esta tecla / o FOGO? */
static int key_held_by_other(const TouchUI *t, const Ptr *p, int zx)
{
    for (int i = 0; i < MAX_PTRS; i++) {
        const Ptr *q = &t->p[i];
        if (q != p && q->used && q->kind == P_KEY && (q->idx == zx || (q->mods & K(zx)))) return 1;
    }
    return 0;
}

static int fire_held_by_other(const TouchUI *t, const Ptr *p)
{
    for (int i = 0; i < MAX_PTRS; i++)
        if (&t->p[i] != p && t->p[i].used && t->p[i].kind == P_FIRE) return 1;
    return 0;
}

/* Toque novo: comeca o tempo minimo; se o toque anterior (dedo ja solto)
 * ainda esta sendo prolongado, solta antes por gap_frames quadros */
static void tap_min(uint8_t *min, uint8_t *gap, int held_by_other, int gap_frames)
{
    if (*min && !*gap && !held_by_other) *gap = (uint8_t)gap_frames;
    *min = TAP_MIN;
}

static void key_down(TouchUI *t, Ptr *p, int zx)
{
    uint64_t bit = K(zx);
    p->kind = P_KEY;
    p->idx = zx;
    int combo = 0;
    /* CAPS/SYMBOL seguros por outro dedo: nao viram trava ao soltar */
    for (int i = 0; i < MAX_PTRS; i++) {
        Ptr *q = &t->p[i];
        if (q != p && q->used && q->kind == P_KEY && is_mod(q->idx)) { q->other = 1; combo = 1; }
    }
    /* CAPS segurado + SYMBOL (ou o contrario) = modo E: o segundo tambem nao trava */
    if (is_mod(zx) && combo) p->nolatch = 1;
    if (is_mod(zx) && (t->latched & bit)) {
        /* tocar de novo no modificador travado: destrava */
        t->latched &= ~bit;
        p->nolatch = 1;
        p->mods = 0;
    } else {
        p->mods = t->latched & ~bit;
        if (p->mods) {
            t->latched = 0;
            if (is_mod(zx)) p->nolatch = 1;     /* CAPS travado + SYMBOL = modo E */
        }
    }
    t->pend_keys |= bit | p->mods;
    for (int z = 0; z < ZXK_COUNT; z++) {
        if ((bit | p->mods) & K(z)) {
            tap_min(&t->key_min[z], &t->key_rel[z], key_held_by_other(t, p, z), KEY_GAP);
        } else if (!key_held_by_other(t, p, z)) {
            /* outra tecla ja solta: o prolongamento acaba aqui. Digitando
             * rapido, duas teclas juntas fariam a ROM ignorar as duas. */
            t->key_min[z] = t->key_rel[z] = 0;
        }
    }
}

static void key_up(TouchUI *t, Ptr *p)
{
    if (is_mod(p->idx) && !p->other && !p->nolatch) t->latched |= K(p->idx);
}

/* Toque comecou em (x,y): decide o dono do dedo. Devolve 1 se e um controle. */
static int pointer_down(TouchUI *t, Ptr *p, float x, float y)
{
    p->x = x;
    p->y = y;
    if (t->menu_on) {
        p->kind = P_MENU;
        p->idx = MI_NONE;
        if (in_rect(t->close_rc, x, y)) p->idx = MI_CLOSE;
        else if (!in_rect(t->panel, x, y)) p->idx = MI_OUTSIDE;
        else
            for (int i = 0; i < MENU_N; i++)
                if (t->items[i].visible && in_rect(t->item_rc[i], x, y)) p->idx = i;
        return 1;
    }
    for (int i = 0; i < BTN_COUNT; i++) {
        SDL_Rect rc = t->btn[i];
        int g = t->key_gap;       /* um pouco de folga em volta */
        rc.x -= g; rc.y -= g; rc.w += 2 * g; rc.h += 2 * g;
        if (in_rect(rc, x, y)) { p->kind = P_BTN; p->idx = i; return 1; }
    }
    if (t->kb_on) {
        int r, c;
        int zx = key_at(t, x, y, &r, &c);
        if (zx >= 0) { key_down(t, p, zx); return 1; }
        return 0;
    }
    float dx = x - t->jx, dy = y - t->jy;
    if (dx * dx + dy * dy <= (t->jr * 1.5f) * (t->jr * 1.5f)) {
        p->kind = P_JOY;
        t->pend_joy = joy_merge(t->pend_joy, joy_dir(t, x, y));
        return 1;
    }
    dx = x - t->fx;
    dy = y - t->fy;
    if (dx * dx + dy * dy <= (t->fr * 1.4f) * (t->fr * 1.4f)) {
        p->kind = P_FIRE;
        t->pend_joy |= JOY_FIRE;
        tap_min(&t->fire_min, &t->fire_gap, fire_held_by_other(t, p), FIRE_GAP);
        return 1;
    }
    return 0;
}

static int pointer_move(TouchUI *t, Ptr *p, float x, float y)
{
    p->x = x;
    p->y = y;
    if (p->kind == P_JOY) t->pend_joy = joy_merge(t->pend_joy, joy_dir(t, x, y));
    return 1;
}

/* Dedo levantado: devolve a acao */
static int pointer_up(TouchUI *t, Ptr *p, float x, float y)
{
    int act = TOUCH_ACT_NONE;
    p->x = x;
    p->y = y;
    switch (p->kind) {
    case P_KEY:
        key_up(t, p);
        break;
    case P_BTN:
        if (!in_rect(t->btn[p->idx], x, y)) {
            SDL_Rect rc = t->btn[p->idx];
            int g = t->key_gap * 2;
            rc.x -= g; rc.y -= g; rc.w += 2 * g; rc.h += 2 * g;
            if (!in_rect(rc, x, y)) break;
        }
        if (p->idx == BTN_MENU) {
            touch_release_keep_joy(t);     /* o polegar no joystick continua dele */
            t->menu_on = 1;
            act = TOUCH_ACT_MENU;
            return act;            /* p ja foi liberado */
        }
        if (p->idx == BTN_KBD) {
            touch_show_keyboard(t, !t->kb_on);
            act = TOUCH_ACT_TOGGLE_KBD;
        } else if (p->idx == BTN_LOAD) {
            act = TOUCH_ACT_LOAD;
        }
        break;
    case P_MENU:
        if (p->idx >= 0 && in_rect(t->item_rc[p->idx], x, y)) {
            act = t->items[p->idx].act;
            /* turbo, Hz e joystick mudam um valor: o menu fica aberto para mostra-lo */
            if (act != TOUCH_ACT_TURBO && act != TOUCH_ACT_HZ && act != TOUCH_ACT_JOYMODE &&
                act != TOUCH_ACT_DIAG)
                t->menu_on = 0;
        } else if (p->idx == MI_CLOSE && in_rect(t->close_rc, x, y)) {
            t->menu_on = 0;
        } else if (p->idx == MI_OUTSIDE && !in_rect(t->panel, x, y)) {
            t->menu_on = 0;
        }
        break;
    default:
        break;
    }
    p->used = 0;
    return act;
}

int touch_handle_event(TouchUI *t, const SDL_Event *ev, Machine *m, int *act)
{
    (void)m;
    if (act) *act = TOUCH_ACT_NONE;
    if (!t || !t->enabled || t->w <= 0) return 0;
    SDL_TouchID dev;
    SDL_FingerID id;
    float x, y;
    int type;   /* 0 down, 1 move, 2 up */
    switch (ev->type) {
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
#ifdef SDL_MOUSE_TOUCHID
        if (ev->tfinger.touchId == SDL_MOUSE_TOUCHID) return 0;   /* sintetico do mouse */
#endif
        dev = ev->tfinger.touchId;
        id = ev->tfinger.fingerId;
        x = ev->tfinger.x * (float)t->w;
        y = ev->tfinger.y * (float)t->h;
        type = ev->type == SDL_FINGERDOWN ? 0 : ev->type == SDL_FINGERMOTION ? 1 : 2;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION: {
        if (ev->type == SDL_MOUSEMOTION ? ev->motion.which == SDL_TOUCH_MOUSEID
                                        : ev->button.which == SDL_TOUCH_MOUSEID)
            return 0;                                         /* sintetico do toque */
        if (ev->type != SDL_MOUSEMOTION && ev->button.button != SDL_BUTTON_LEFT) return 0;
        int ww = t->w, wh = t->h;
        SDL_Window *win = SDL_RenderGetWindow(t->r);
        if (win) SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return 0;
        float sx = (float)t->w / (float)ww, sy = (float)t->h / (float)wh;
        dev = MOUSE_DEV;
        id = 0;
        if (ev->type == SDL_MOUSEMOTION) {
            if (!(ev->motion.state & SDL_BUTTON_LMASK)) return 0;
            x = (float)ev->motion.x * sx; y = (float)ev->motion.y * sy; type = 1;
        } else {
            x = (float)ev->button.x * sx; y = (float)ev->button.y * sy;
            type = ev->type == SDL_MOUSEBUTTONDOWN ? 0 : 2;
        }
        break;
    }
    default:
        return 0;
    }

    if (type == 0) {
        Ptr *p = new_ptr(t, dev, id);
        if (!p) return 0;
        p->used = 1;
        if (!pointer_down(t, p, x, y)) { p->used = 0; return 0; }
        return 1;
    }
    Ptr *p = find_ptr(t, dev, id);
    if (!p && type == 1 && dev != MOUSE_DEV && !t->menu_on && !t->kb_on) {
        /* dedo que o SDL ainda acompanha mas que perdemos (menu, segundo
         * plano): ele nunca manda outro FINGERDOWN. Arrastando sobre o
         * joystick, ele volta a ser o joystick. */
        float dx = x - t->jx, dy = y - t->jy;
        if (dx * dx + dy * dy <= (t->jr * 1.5f) * (t->jr * 1.5f)) {
            p = new_ptr(t, dev, id);
            if (!p) return 0;
            p->used = 1;
            if (!pointer_down(t, p, x, y)) { p->used = 0; return 0; }
            return 1;
        }
    }
    if (!p) return 0;
    if (type == 1) return pointer_move(t, p, x, y);
    int a = pointer_up(t, p, x, y);
    if (act) *act = a;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Desenho dos controles                                               */
/* ------------------------------------------------------------------ */

static int ptr_on(const TouchUI *t, int kind, int idx)
{
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].used && t->p[i].kind == kind && (idx < 0 || t->p[i].idx == idx)) return 1;
    return 0;
}

static void draw_buttons(TouchUI *t)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        SDL_Rect rc = t->btn[i];
        int pressed = ptr_on(t, P_BTN, i);
        uint32_t bg = pressed ? C_BTN_PR : (i == BTN_KBD && t->kb_on) ? C_BTN_ON : C_BTN_BG;
        float rad = rc.h * 0.3f;
        fill_round(t, rc, rad, bg);
        outline_round(t, rc, rad, t->u * 0.025f + 1, pressed ? C_BTN_PR : C_BTN_LINE);
        text_center(t, rc, t->btn_ts, pressed ? 0xFF000000u : C_WHITE, btn_label[i]);
    }
}

static void draw_joystick(TouchUI *t)
{
    float jx = t->jx, jy = t->jy, jr = t->jr;
    uint8_t dir = 0;
    const Ptr *jp = NULL;
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].used && t->p[i].kind == P_JOY) { jp = &t->p[i]; break; }
    if (jp) dir = joy_dir(t, jp->x, jp->y);

    fill_circle(t, jx, jy, jr, 0x2CFFFFFFu);
    /* setor da direcao atual (8 direcoes de 45 graus) */
    if (dir) {
        float dx = (dir & JOY_RIGHT) ? 1.0f : (dir & JOY_LEFT) ? -1.0f : 0.0f;
        float dy = (dir & JOY_DOWN) ? 1.0f : (dir & JOY_UP) ? -1.0f : 0.0f;
        float a = atan2f(dy, dx);
        sector(t, jx, jy, jr, a - 0.3927f, a + 0.3927f, 0x7800E0E0u);
    }
    ring(t, jx, jy, jr - (t->u * 0.035f + 1), jr, 0x90FFFFFFu);
    /* setas nas 4 direcoes principais e pontos nas diagonais */
    for (int k = 0; k < 8; k++) {
        float a = (float)k * 0.7853982f;
        float ca = cosf(a), sa = sinf(a);
        float rr = jr * 0.8f;
        static const uint8_t bits[8] = {
            JOY_RIGHT, JOY_RIGHT | JOY_DOWN, JOY_DOWN, JOY_DOWN | JOY_LEFT,
            JOY_LEFT, JOY_LEFT | JOY_UP, JOY_UP, JOY_UP | JOY_RIGHT };
        uint32_t c = dir == bits[k] ? 0xF000F0F0u : 0x90FFFFFFu;
        if (k % 2 == 0) {
            float sz = jr * 0.13f;
            float tx = jx + ca * (rr + sz * 0.5f), ty = jy + sa * (rr + sz * 0.5f);
            float bx = jx + ca * (rr - sz * 0.6f), by = jy + sa * (rr - sz * 0.6f);
            triangle(t, tx, ty, bx - sa * sz, by + ca * sz, bx + sa * sz, by - ca * sz, c);
        } else {
            fill_circle(t, jx + ca * rr * 0.97f, jy + sa * rr * 0.97f, jr * 0.04f + 1, c);
        }
    }
    /* manopla: segue o dedo, presa dentro da base */
    float kx = jx, ky = jy;
    if (jp) {
        float dx = jp->x - jx, dy = jp->y - jy, d = sqrtf(dx * dx + dy * dy);
        float lim = jr * 0.58f;
        if (d > lim) { dx *= lim / d; dy *= lim / d; }
        kx += dx;
        ky += dy;
    }
    fill_circle(t, kx, ky, jr * 0.4f, jp ? 0xB000C8C8u : 0x60FFFFFFu);
    ring(t, kx, ky, jr * 0.4f - (t->u * 0.03f + 1), jr * 0.4f, 0xA0FFFFFFu);

    /* modo do joystick sobre a base */
    if (t->joy_label[0]) {
        int ts = fit_scale(t->joy_label, t->lbl_ts, t->lbl_room);
        if (ts < 1) ts = 1;
        int tw = text_w(t->joy_label, ts);
        int lx = (int)(jx - tw / 2.0f);
        if (lx + tw > t->lbl_x0 + t->lbl_room) lx = t->lbl_x0 + t->lbl_room - tw;
        if (lx < t->lbl_x0) lx = t->lbl_x0;
        if (lx < (int)t->pad) lx = (int)t->pad;
        draw_text(t, lx, (int)(jy - jr - 8 * ts - t->u * 0.12f), ts, 0xB0FFFFFFu, t->joy_label);
    }

    /* FOGO */
    int fire = ptr_on(t, P_FIRE, -1);
    fill_circle(t, t->fx, t->fy, t->fr, fire ? 0xD0FF5050u : 0x58E03838u);
    ring(t, t->fx, t->fy, t->fr - (t->u * 0.035f + 1), t->fr, fire ? 0xF0FFFFFFu : 0x90FFFFFFu);
    int fs = fit_scale("FOGO", iround(t->fr * 0.42f / 8.0f), (int)(t->fr * 1.6f));
    if (fs < 1) fs = 1;
    SDL_Rect frc = { (int)(t->fx - t->fr), (int)(t->fy - t->fr), (int)(2 * t->fr), (int)(2 * t->fr) };
    text_center(t, frc, fs, C_WHITE, "FOGO");
}

static void draw_key(TouchUI *t, const KeyDef *d, SDL_Rect rc)
{
    uint64_t bit = K(d->zx);
    int pressed = 0;
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].used && t->p[i].kind == P_KEY &&
            (t->p[i].idx == d->zx || (t->p[i].mods & bit)))
            pressed = 1;
    int latched = (t->latched & bit) != 0;
    uint32_t bg = pressed ? C_KEY_PR : latched ? C_KEY_LT : C_KEY_BG;
    int dark = pressed || latched;
    fill_round(t, rc, rc.h * 0.14f, bg);

    int pad = rc.h / 10;
    if (pad < 2) pad = 2;
    int ls = (int)((float)rc.h * 0.36f / 8.0f);
    if (ls < 1) ls = 1;
    /* palavras e simbolos: metade da letra, arredondada para cima (com 1/3
     * ficavam com ~1 mm de altura num celular) */
    int ss = ls >= 3 ? (ls + 1) / 2 : 1;
    uint32_t fg = dark ? 0xFF000000u : C_WHITE;
    uint32_t sub_c = dark ? 0xFF202020u : C_KEY_SUB;
    uint32_t sym_c = dark ? 0xFF800000u : C_KEY_SYM;
    int single = d->main[1] == 0;

    if (single) {
        int lx = rc.x + pad + rc.w / 14, ly = rc.y + pad;
        draw_text(t, lx, ly, ls, fg, d->main);
        if (d->sym) {
            /* simbolo de um caractere um pouco maior (legivel) */
            int sy = d->sym[1] ? ss : (ss + 1 < ls ? ss + 1 : ss);
            int sw = text_w(d->sym, sy);
            /* sem espaco ao lado da letra: escala menor (nunca some) */
            while (sy > 1 && lx + 8 * ls + pad / 2 + sw > rc.x + rc.w - pad) sw = text_w(d->sym, --sy);
            draw_text(t, rc.x + rc.w - pad - sw, ly, sy, sym_c, d->sym);
        }
        if (d->sub) {
            int s2 = fit_scale(d->sub, ss, rc.w - 2 * pad);
            if (s2 >= 1 && ly + 8 * ls + 8 * s2 <= rc.y + rc.h - pad + 2)
                draw_text(t, rc.x + (rc.w - text_w(d->sub, s2)) / 2, rc.y + rc.h - pad - 8 * s2,
                          s2, sub_c, d->sub);
        }
    } else {
        /* ENTER, CAPS SHIFT, SYMBOL SHIFT, BREAK SPACE */
        int ms = fit_scale(d->main, ls, rc.w - 2 * pad);
        if (ms < 1) ms = 1;
        int s2 = d->sub ? fit_scale(d->sub, ss, rc.w - 2 * pad) : 0;
        int total = 8 * ms + (s2 ? 8 * s2 + pad / 2 : 0);
        int y = rc.y + (rc.h - total) / 2;
        uint32_t mc = (d->zx == ZXK_SYM) ? sym_c : fg;
        draw_text(t, rc.x + (rc.w - text_w(d->main, ms)) / 2, y, ms, mc, d->main);
        if (s2)
            draw_text(t, rc.x + (rc.w - text_w(d->sub, s2)) / 2, y + 8 * ms + pad / 2, s2,
                      d->zx == ZXK_SYM ? sym_c : sub_c, d->sub);
    }
}

static void draw_keyboard(TouchUI *t)
{
    SDL_Rect bg = t->kb;
    int g = t->key_gap * 2;
    bg.x -= g; bg.y -= g; bg.w += 2 * g; bg.h += 2 * g;
    fill_round(t, bg, t->key[0][0].h * 0.2f, C_KB_BG);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 10; c++) draw_key(t, &kb_def[r][c], t->key[r][c]);
}

static void draw_menu(TouchUI *t)
{
    SDL_Rect all = { 0, 0, t->w, t->h };
    fill_rect(t, all, C_SHADE);
    float rad = t->u * 0.15f;
    fill_round(t, t->panel, rad, C_PANEL);
    outline_round(t, t->panel, rad, t->u * 0.03f + 1, C_FRAME);
    int pad = (int)t->pad;
    int title_h = t->close_rc.h;
    draw_text(t, t->panel.x + pad, t->close_rc.y + (title_h - 8 * t->title_ts) / 2, t->title_ts,
              C_TITLE, "MENU");
    if (t->menu_note[0]) {
        /* controles do jogo aberto, entre "MENU" e o X, no maior tamanho que cabe */
        int x0 = t->panel.x + pad + text_w("MENU", t->title_ts) + pad * 2;
        int room = t->close_rc.x - pad - x0;
        int ts = fit_scale(t->menu_note, t->menu_ts, room);
        if (ts >= 1 && text_w(t->menu_note, ts) <= room)
            draw_text(t, x0, t->close_rc.y + (title_h - 8 * ts) / 2, ts, C_VALUE, t->menu_note);
    }

    int close_pr = 0;
    for (int i = 0; i < MAX_PTRS; i++)
        if (t->p[i].used && t->p[i].kind == P_MENU && t->p[i].idx == MI_CLOSE) close_pr = 1;
    fill_round(t, t->close_rc, t->close_rc.h * 0.2f, close_pr ? C_ITEM_PR : C_ITEM);
    text_center(t, t->close_rc, fit_scale("X", t->title_ts, t->close_rc.w),
                close_pr ? 0xFF000000u : C_WHITE, "X");

    for (int i = 0; i < MENU_N; i++) {
        const MenuItem *it = &t->items[i];
        if (!it->visible) continue;
        SDL_Rect rc = t->item_rc[i];
        int pr = 0;
        for (int k = 0; k < MAX_PTRS; k++)
            if (t->p[k].used && t->p[k].kind == P_MENU && t->p[k].idx == i) pr = 1;
        fill_round(t, rc, rc.h * 0.18f, pr ? C_ITEM_PR : C_ITEM);
        int ts = t->menu_ts;
        int ty = rc.y + (rc.h - 8 * ts) / 2;
        draw_text(t, rc.x + pad, ty, ts, pr ? 0xFF000000u : C_WHITE, it->label);
        if (it->value[0]) {
            int vw = text_w(it->value, ts);
            draw_text(t, rc.x + rc.w - pad - vw, ty, ts, pr ? 0xFF003040u : C_VALUE, it->value);
        }
    }
}

void touch_draw(TouchUI *t)
{
    if (!t || !t->enabled || t->w <= 0) return;
    SDL_SetRenderDrawBlendMode(t->r, SDL_BLENDMODE_BLEND);
    if (t->kb_on) draw_keyboard(t);
    else draw_joystick(t);
    draw_buttons(t);
    if (t->menu_on) draw_menu(t);
    SDL_SetRenderDrawBlendMode(t->r, SDL_BLENDMODE_NONE);
}

/* ------------------------------------------------------------------ */
/* Testes                                                              */
/* ------------------------------------------------------------------ */

static int center_of(const TouchUI *t, SDL_Rect rc, float *nx, float *ny)
{
    if (rc.w <= 0 || rc.h <= 0) return -1;
    *nx = (rc.x + rc.w / 2.0f) / (float)t->w;
    *ny = (rc.y + rc.h / 2.0f) / (float)t->h;
    return 0;
}

int touch_find(const TouchUI *t, const char *name, float *nx, float *ny)
{
    if (!t || !t->enabled || t->w <= 0 || !name) return -1;
    if (!strncmp(name, "btn:", 4)) {
        static const char *names[BTN_COUNT] = { "MENU", "KBD", "LOAD" };
        for (int i = 0; i < BTN_COUNT; i++)
            if (!strcmp(name + 4, names[i])) return center_of(t, t->btn[i], nx, ny);
        return -1;
    }
    if (!strncmp(name, "key:", 4)) {
        if (!t->kb_on) return -1;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 10; c++)
                if (!strcmp(name + 4, kb_def[r][c].name)) return center_of(t, t->key[r][c], nx, ny);
        return -1;
    }
    if (!strncmp(name, "menu:", 5)) {
        if (!t->menu_on) return -1;
        if (!strcmp(name + 5, "close")) return center_of(t, t->close_rc, nx, ny);
        int a = atoi(name + 5);
        for (int i = 0; i < MENU_N; i++)
            if (t->items[i].act == a && t->items[i].visible) return center_of(t, t->item_rc[i], nx, ny);
        return -1;
    }
    if (t->kb_on || t->menu_on) return -1;
    if (!strcmp(name, "fire")) {
        *nx = t->fx / (float)t->w;
        *ny = t->fy / (float)t->h;
        return 0;
    }
    if (!strncmp(name, "joy", 3)) {
        static const struct { const char *n; float dx, dy; } dirs[] = {
            { "", 0, 0 }, { ":u", 0, -1 }, { ":ur", 0.7071f, -0.7071f }, { ":r", 1, 0 },
            { ":dr", 0.7071f, 0.7071f }, { ":d", 0, 1 }, { ":dl", -0.7071f, 0.7071f },
            { ":l", -1, 0 }, { ":ul", -0.7071f, -0.7071f } };
        for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
            if (!strcmp(name + 3, dirs[i].n)) {
                *nx = (t->jx + dirs[i].dx * t->jr * 0.8f) / (float)t->w;
                *ny = (t->jy + dirs[i].dy * t->jr * 0.8f) / (float)t->h;
                return 0;
            }
    }
    return -1;
}
