/* osd.c - Texto sobreposto com a fonte da ROM: status, toasts, ajuda,
 * navegador de arquivos e confirmacao de saida.
 *
 * Dois layouts: o classico (grade 40x30 desenhada sobre o framebuffer de
 * 320x240) e o de toque (osd_set_touch), desenhado numa tela transparente do
 * tamanho da janela dividida por uma escala inteira, com linhas grandes,
 * botoes e rolagem por arraste. */
#include "osd.h"
#include "machine.h"   /* FB_W, FB_H */
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cores (ARGB8888) */
#define C_TEXT   0xFFE8E8E8u
#define C_TITLE  0xFFFFFF40u
#define C_DIM    0xFF9090A0u
#define C_DIR    0xFF80D0FFu
#define C_PANEL  0xFF101C50u
#define C_FRAME  0xFF4060C0u
#define C_SEL_BG 0xFF00B0B0u
#define C_SEL_FG 0xFF000000u
#define C_BLACK  0xFF000000u
#define C_ERR    0xFFFF7070u
#define C_TOAST  0xFF202020u
/* so no layout de toque */
#define C_SHADE  0xB8000000u   /* fundo escurecido (translucido) */
#define C_ROW    0xFF16266Au   /* linha alternada da lista */
#define C_BTN    0xFF2A3C90u
#define C_PRESS  0xFF60E0E0u
#define C_PICK   0xFFFFD060u   /* entrada "abrir arquivo do aparelho" */

#define LIST_ROW0 3     /* primeira linha da lista do navegador */
#define LIST_ROWS 24    /* linhas visiveis */

#define TROW_H    28    /* toque: altura de uma linha da lista (pixels da tela; ~7 mm num celular) */
#define TDRAG     5     /* toque: deslocamento que transforma o toque em arraste */

/* Tela de desenho: o framebuffer (320x240) ou a tela de toque (com alfa) */
typedef struct Cv {
    uint32_t *px;
    int w, h;
    int y0, y1;        /* recorte vertical: linhas desenhaveis [y0, y1) */
    int overlay;       /* 1 = tela de toque transparente: escurecer = preto translucido */
} Cv;

static Cv cv_make(uint32_t *px, int w, int h, int overlay)
{
    Cv c;
    c.px = px; c.w = w; c.h = h; c.y0 = 0; c.y1 = h; c.overlay = overlay;
    return c;
}

/* ---------------- texto ---------------- */

/* Converte UTF-8 para ASCII imprimivel (32..126), sem acentos (compat.h). */
static void to_ascii(const char *in, char *out, size_t outlen)
{
    const unsigned char *p = (const unsigned char *)in;
    size_t n = 0;
    if (!outlen) return;
    while (*p && n + 1 < outlen) out[n++] = tk_ascii_fold(tk_utf8_next(&p));
    out[n] = 0;
}

/* Caractere 8x8 ampliado sc vezes */
static void put_char_sc(const Osd *o, const Cv *cv, int x, int y, int c,
                        uint32_t fg, uint32_t bg, int opaque, int sc)
{
    if (c < 32 || c > 127) c = '?';
    const uint8_t *g = o->font + (c - 32) * 8;
    for (int r = 0; r < 8 * sc; r++) {
        int yy = y + r;
        if (yy < cv->y0 || yy >= cv->y1 || yy < 0 || yy >= cv->h) continue;
        uint8_t bits = g[r / sc];
        uint32_t *row = cv->px + yy * cv->w;
        for (int b = 0; b < 8 * sc; b++) {
            int xx = x + b;
            if (xx < 0 || xx >= cv->w) continue;
            if (bits & (0x80 >> (b / sc))) row[xx] = fg;
            else if (opaque) row[xx] = bg;
        }
    }
}

static void put_char(const Osd *o, const Cv *cv, int x, int y, int c,
                     uint32_t fg, uint32_t bg, int opaque)
{
    put_char_sc(o, cv, x, y, c, fg, bg, opaque, 1);
}

/* Desenha texto ja em ASCII; no maximo maxc caracteres. */
static void put_text_sc(const Osd *o, const Cv *cv, int x, int y, const char *s, int maxc,
                        uint32_t fg, int sc)
{
    for (int i = 0; s[i] && i < maxc; i++)
        put_char_sc(o, cv, x + i * 8 * sc, y, (unsigned char)s[i], fg, 0, 0, sc);
}

static void put_text(const Osd *o, const Cv *cv, int x, int y, const char *s, int maxc,
                     uint32_t fg, uint32_t bg, int opaque)
{
    for (int i = 0; s[i] && i < maxc; i++)
        put_char(o, cv, x + i * 8, y, (unsigned char)s[i], fg, bg, opaque);
}

/* Texto UTF-8 na celula (col,row) da grade 40x30 */
static void cell_text(const Osd *o, const Cv *cv, int col, int row, const char *utf8,
                      int maxc, uint32_t fg, uint32_t bg, int opaque)
{
    char a[OSD_TEXT_MAX * 2];
    to_ascii(utf8, a, sizeof a);
    put_text(o, cv, col * 8, row * 8, a, maxc, fg, bg, opaque);
}

/* Texto UTF-8 em pixels, escala sc */
static void px_text(const Osd *o, const Cv *cv, int x, int y, const char *utf8, int maxc,
                    uint32_t fg, int sc)
{
    char a[OSD_TEXT_MAX * 2];
    to_ascii(utf8, a, sizeof a);
    put_text_sc(o, cv, x, y, a, maxc, fg, sc);
}

static void fill_rect(const Cv *cv, int x, int y, int w, int h, uint32_t c)
{
    for (int yy = y; yy < y + h; yy++) {
        if (yy < cv->y0 || yy >= cv->y1 || yy < 0 || yy >= cv->h) continue;
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < cv->w) cv->px[yy * cv->w + xx] = c;
    }
}

/* Escurece (1/4 do brilho) um retangulo; na tela de toque, preto translucido */
static void dim_rect(const Cv *cv, int x, int y, int w, int h)
{
    if (cv->overlay) { fill_rect(cv, x, y, w, h, C_SHADE); return; }
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= cv->h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= cv->w) continue;
            uint32_t p = cv->px[yy * cv->w + xx];
            cv->px[yy * cv->w + xx] = 0xFF000000u | ((p >> 2) & 0x3F3F3Fu);
        }
    }
}

static void frame_rect(const Cv *cv, int x, int y, int w, int h, uint32_t c)
{
    fill_rect(cv, x, y, w, 1, c);
    fill_rect(cv, x, y + h - 1, w, 1, c);
    fill_rect(cv, x, y, 1, h, c);
    fill_rect(cv, x + w - 1, y, 1, h, c);
}

static void copy_str(char *out, size_t outlen, const char *s)
{
    if (!outlen) return;
    size_t n = strlen(s);
    if (n >= outlen) n = outlen - 1;
    memmove(out, s, n);
    out[n] = 0;
}

static int inside(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* ---------------- estado ---------------- */

void osd_set_font(Osd *o, const uint8_t *rom)
{
    memcpy(o->font, rom + 0x3D00, sizeof o->font);
}

void osd_init(Osd *o, const uint8_t *rom)
{
    memset(o, 0, sizeof *o);
    osd_set_font(o, rom);
    o->ptr_row = -1;
    o->mouse_item = -1;
}

static void browser_clear(Osd *o)
{
    plat_free_entries(o->entries, o->count);
    o->entries = NULL;
    o->count = o->sel = o->top = 0;
}

void osd_free(Osd *o)
{
    browser_clear(o);
}

void osd_set_status(Osd *o, const char *text)
{
    copy_str(o->status, sizeof o->status, text ? text : "");
}

void osd_set_tape_info(Osd *o, const char *text, const char *block)
{
    copy_str(o->tape_info, sizeof o->tape_info, text ? text : "");
    copy_str(o->tape_block, sizeof o->tape_block, block ? block : "");
}

void osd_toast(Osd *o, uint32_t now_ms, uint32_t ms, const char *text)
{
    copy_str(o->toast, sizeof o->toast, text ? text : "");
    o->toast_until = now_ms + ms;
    if (!o->toast_until) o->toast_until = 1;
}

void osd_set_info(Osd *o, const char *text)
{
    copy_str(o->info, sizeof o->info, text ? text : "");
}

static void ptr_reset(Osd *o)
{
    o->ptr_down = o->ptr_drag = 0;
    o->ptr_row = -1;
    o->ptr_btn = 0;
}

/* Ao abrir um painel, o aviso que estava na tela (por exemplo a dica
 * "F1 = menu e ajuda" do inicio) ja cumpriu o papel e sairia por cima do
 * titulo do painel; os avisos gerados com o painel aberto aparecem normalmente. */
static void drop_toast(Osd *o)
{
    o->toast[0] = 0;
}

void osd_open_help(Osd *o, int from_menu)
{
    drop_toast(o);
    o->mode = OSD_HELP;
    o->help_from_menu = from_menu ? 1 : 0;
    if (o->help_page < 0) o->help_page = 0;    /* reabre na ultima pagina vista */
    o->mouse_item = -1;
    ptr_reset(o);
}

void osd_open_quit(Osd *o) { o->mode = OSD_QUIT; o->mouse_item = -1; ptr_reset(o); }

void osd_open_menu(Osd *o)
{
    drop_toast(o);
    o->mode = OSD_MENU;
    o->mouse_item = -1;
    if (o->menu_sel < 0 || o->menu_sel >= o->menu_n) o->menu_sel = 0;
    ptr_reset(o);
}

void osd_menu_set(Osd *o, const OsdMenuItem *items, int n)
{
    if (n > OSD_MENU_MAX) n = OSD_MENU_MAX;
    if (n < 0) n = 0;
    memcpy(o->menu, items, sizeof *items * (size_t)n);
    o->menu_n = n;
    if (o->menu_sel >= n) o->menu_sel = n - 1;
    if (o->menu_sel < 0) o->menu_sel = 0;
}

void osd_close(Osd *o)
{
    o->mode = OSD_NONE;
    ptr_reset(o);
}

int osd_active(const Osd *o) { return o->mode != OSD_NONE; }

void osd_set_touch(Osd *o, int on) { o->touch = on ? 1 : 0; }

void osd_set_picker_entry(Osd *o, const char *label)
{
    copy_str(o->picker_label, sizeof o->picker_label, label ? label : "");
}

int osd_supported_file(const char *name)
{
    static const char *exts[] = { "tap", "tzx", "sna", "z80", "scr", "bin", "cod" };
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++)
        if (!plat_strcasecmp(dot + 1, exts[i])) return 1;
    return 0;
}

/* ---------------- navegador ---------------- */

/* is_dir == 2 marca a entrada extra do seletor do aparelho (picker_label) */
#define ENTRY_PICKER 2

static int entry_cmp(const void *a, const void *b)
{
    const PlatEntry *x = (const PlatEntry *)a, *y = (const PlatEntry *)b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;   /* diretorios primeiro */
    int c = plat_strcasecmp(x->name, y->name);
    return c ? c : strcmp(x->name, y->name);
}

/* Geometria da lista no layout de toque (pixels da tela de toque) */
typedef struct BrGeom {
    int px, py, pw, ph;        /* painel */
    int cx, cy, cw, ch;        /* botao fechar */
    int lx, lw, ly0, ly1;      /* area da lista */
} BrGeom;

static void br_geom(const Osd *o, BrGeom *g)
{
    int W = o->cv_w > 0 ? o->cv_w : FB_W, H = o->cv_h > 0 ? o->cv_h : FB_H;
    g->px = 3; g->py = 3; g->pw = W - 6; g->ph = H - 6;
    g->cw = 44; g->ch = 24;
    g->cx = g->px + g->pw - g->cw - 3; g->cy = g->py + 3;
    g->lx = g->px + 4; g->lw = g->pw - 8 - 6;          /* 6 = barra de rolagem */
    g->ly0 = g->py + 40; g->ly1 = g->py + g->ph - 4;
}

static int br_max_scroll(const Osd *o)
{
    BrGeom g;
    br_geom(o, &g);
    int m = o->count * TROW_H - (g.ly1 - g.ly0);
    return m > 0 ? m : 0;
}

static void br_clamp_scroll(Osd *o)
{
    int m = br_max_scroll(o);
    if (o->scroll > m) o->scroll = m;
    if (o->scroll < 0) o->scroll = 0;
}

/* Toque: rola o minimo para a selecao ficar visivel */
static void br_show_sel(Osd *o)
{
    BrGeom g;
    br_geom(o, &g);
    int view = g.ly1 - g.ly0;
    int top = o->sel * TROW_H, bot = top + TROW_H;
    if (o->scroll < 0) o->scroll = top - (view - TROW_H) / 2;   /* recem-aberto: centraliza */
    else if (top < o->scroll) o->scroll = top;
    else if (bot > o->scroll + view) o->scroll = bot - view;
    br_clamp_scroll(o);
}

/* Selecao dentro da lista e visivel; a lista nunca rola alem do fim (sem
 * linhas vazias embaixo e com a barra de rolagem dentro do trilho). */
static void browser_fix_scroll(Osd *o)
{
    if (o->sel < 0) o->sel = 0;
    if (o->sel >= o->count) o->sel = o->count - 1;
    if (o->sel < 0) o->sel = 0;
    if (o->sel < o->top) o->top = o->sel;
    if (o->sel >= o->top + LIST_ROWS) o->top = o->sel - LIST_ROWS + 1;
    if (o->top > o->count - LIST_ROWS) o->top = o->count - LIST_ROWS;
    if (o->top < 0) o->top = 0;
    if (o->touch && o->cv_h > 0 && o->scroll >= 0) br_show_sel(o);
}

/* Le o diretorio; select_name (opcional) escolhe a entrada inicial. 0 ok, -1 erro. */
static int browser_load(Osd *o, const char *dir, const char *select_name)
{
    PlatEntry *list = NULL;
    int n = 0;
    if (plat_list_dir(dir, &list, &n) != 0) return -1;

    /* filtra: diretorios + extensoes suportadas */
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].is_dir || osd_supported_file(list[i].name)) list[k++] = list[i];
        else free(list[i].name);
    }
    n = k;
    qsort(list, (size_t)n, sizeof *list, entry_cmp);

    /* ".." no topo, se houver pai; antes dele, a entrada do seletor (Android) */
    char parent[PLAT_PATH_MAX];
    int has_parent = plat_parent_dir(dir, parent, sizeof parent) == 0;
    int has_picker = o->picker_label[0] != 0;
    PlatEntry *all = (PlatEntry *)malloc(sizeof(PlatEntry) * (size_t)(n + 2));
    char *dd = has_parent ? (char *)malloc(3) : NULL;
    char *pk = has_picker ? (char *)malloc(strlen(o->picker_label) + 1) : NULL;
    if (!all || (has_parent && !dd) || (has_picker && !pk)) {
        free(all);
        free(dd);
        free(pk);
        plat_free_entries(list, n);
        return -1;
    }
    int m = 0;
    if (has_picker) {
        memcpy(pk, o->picker_label, strlen(o->picker_label) + 1);
        all[m].name = pk;
        all[m].is_dir = ENTRY_PICKER;
        m++;
    }
    if (has_parent) {
        memcpy(dd, "..", 3);
        all[m].name = dd;
        all[m].is_dir = 1;
        m++;
    }
    int first = m;          /* primeira entrada de verdade */
    if (n) memcpy(all + m, list, sizeof(PlatEntry) * (size_t)n);
    m += n;
    free(list);

    browser_clear(o);
    o->entries = all;
    o->count = m;
    if (o->dir != dir) copy_str(o->dir, sizeof o->dir, dir);
    o->sel = (first < m) ? first : (has_parent ? first - 1 : 0);
    if (o->sel < 0) o->sel = 0;
    if (select_name) {
        for (int i = 0; i < m; i++)
            if (all[i].is_dir != ENTRY_PICKER && !strcmp(all[i].name, select_name)) {
                o->sel = i;
                break;
            }
    }
    o->top = 0;
    if (o->sel >= LIST_ROWS) o->top = o->sel - LIST_ROWS / 2;
    o->scroll = -1;         /* toque: centraliza a selecao no proximo desenho */
    browser_fix_scroll(o);
    o->browse_err[0] = 0;
    return 0;
}

void osd_browser_remember(Osd *o, const char *path)
{
    char abs[PLAT_PATH_MAX];
    plat_abspath(path, abs, sizeof abs);
    plat_dirname(abs, o->dir, sizeof o->dir);
    copy_str(o->last_name, sizeof o->last_name, plat_basename(abs));
}

int osd_open_browser(Osd *o, const char *dir)
{
    char d[PLAT_PATH_MAX];
    const char *pick = NULL;
    if (dir && dir[0]) plat_abspath(dir, d, sizeof d);
    else if (o->dir[0]) { copy_str(d, sizeof d, o->dir); pick = o->last_name[0] ? o->last_name : NULL; }
    else if (plat_getcwd(d, sizeof d) != 0) d[0] = 0;

    drop_toast(o);
    if (browser_load(o, d, pick) != 0) {
        char cwd[PLAT_PATH_MAX];
        if (plat_getcwd(cwd, sizeof cwd) != 0 || browser_load(o, cwd, NULL) != 0) {
#ifdef _WIN32
            if (browser_load(o, "", NULL) != 0) return -1;
#else
            if (browser_load(o, "/", NULL) != 0) return -1;
#endif
        }
    }
    o->mode = OSD_BROWSER;
    ptr_reset(o);
    return 0;
}

static void browser_up_dir(Osd *o)
{
    char parent[PLAT_PATH_MAX], child[PLAT_PATH_MAX];
    if (plat_parent_dir(o->dir, parent, sizeof parent) != 0) return;
    /* nome do diretorio atual, para seleciona-lo no pai */
    char tmp[PLAT_PATH_MAX];
    copy_str(tmp, sizeof tmp, o->dir);
    size_t n = strlen(tmp);
    while (n > 1 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\')) tmp[--n] = 0;
    copy_str(child, sizeof child, plat_basename(tmp));
    if (!child[0]) copy_str(child, sizeof child, tmp);       /* ex.: "C:" */
#ifdef _WIN32
    if (!parent[0] && n >= 2 && tmp[1] == ':') { child[0] = tmp[0]; child[1] = ':'; child[2] = 0; }
#endif
    if (browser_load(o, parent, child) != 0)
        snprintf(o->browse_err, sizeof o->browse_err, "Erro ao abrir o diretorio");
}

static OsdResult browser_key(Osd *o, OsdKey key, int ch, char *path, size_t pathlen)
{
    switch (key) {
    case OSDK_UP:   o->sel--; break;
    case OSDK_DOWN: o->sel++; break;
    case OSDK_PGUP: o->sel -= LIST_ROWS - 1; break;
    case OSDK_PGDN: o->sel += LIST_ROWS - 1; break;
    case OSDK_HOME: o->sel = 0; break;
    case OSDK_END:  o->sel = o->count - 1; break;
    case OSDK_ESC:  o->mode = OSD_NONE; return OSD_RES_CLOSED;
    case OSDK_LEFT:
    case OSDK_BACK: browser_up_dir(o); break;
    case OSDK_RIGHT:
    case OSDK_ENTER: {
        if (o->count <= 0) break;
        const PlatEntry *e = &o->entries[o->sel];
        if (e->is_dir == ENTRY_PICKER) { o->mode = OSD_NONE; return OSD_RES_PICKER; }
        if (!strcmp(e->name, "..")) { browser_up_dir(o); break; }
        char full[PLAT_PATH_MAX];
        plat_path_join(o->dir, e->name, full, sizeof full);
        if (e->is_dir) {
            if (browser_load(o, full, NULL) != 0)
                snprintf(o->browse_err, sizeof o->browse_err, "Erro ao abrir o diretorio");
        } else {
            copy_str(path, pathlen, full);
            copy_str(o->last_name, sizeof o->last_name, e->name);
            o->mode = OSD_NONE;
            return OSD_RES_OPEN;
        }
        break;
    }
    case OSDK_CHAR: {
        /* pula para a proxima entrada que comeca com a letra */
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (ch <= 32 || ch >= 127 || o->count <= 0) break;
        for (int i = 1; i <= o->count; i++) {
            int j = (o->sel + i) % o->count;
            int c0 = (unsigned char)o->entries[j].name[0];
            if (c0 >= 'A' && c0 <= 'Z') c0 += 32;
            if (c0 == ch && strcmp(o->entries[j].name, "..") &&
                o->entries[j].is_dir != ENTRY_PICKER) { o->sel = j; break; }
        }
        break;
    }
    default: break;
    }
    browser_fix_scroll(o);
    return OSD_RES_NONE;
}

static OsdResult help_key(Osd *o, OsdKey key, int ch);
static OsdResult menu_key(Osd *o, OsdKey key, int ch);

OsdResult osd_key(Osd *o, OsdKey key, int ch, char *path, size_t pathlen)
{
    switch (o->mode) {
    case OSD_HELP:
        return help_key(o, key, ch);
    case OSD_MENU:
        return menu_key(o, key, ch);
    case OSD_QUIT:
        if (key == OSDK_ESC || (key == OSDK_CHAR && (ch == 's' || ch == 'S'))) {
            o->mode = OSD_NONE;
            return OSD_RES_QUIT;
        }
        if (key == OSDK_NONE) return OSD_RES_NONE;
        o->mode = OSD_NONE;
        return OSD_RES_CLOSED;
    case OSD_BROWSER:
        return browser_key(o, key, ch, path, pathlen);
    default:
        return OSD_RES_NONE;
    }
}

/* ---------------- ajuda em paginas ----------------
 * Texto em UTF-8 (os acentos caem na fonte da ROM, ver to_ascii). Cada linha
 * tem no maximo HELP_COLS caracteres e cada pagina no maximo HELP_LINES
 * linhas, o que cabe nos dois layouts: o classico (320x240) e o de toque
 * (tela de pelo menos 240 linhas). Cada pagina tem a versao do desktop
 * (teclas do PC) e a do toque (MENU, TECLADO, FOGO...). */
#define HELP_COLS  36
#define HELP_LINES 19

static const char *const help_p1_desk[] = {
    "O TK90X (1985) e o TK95 (1986) são",
    "micros brasileiros da Microdigital,",
    "compatíveis com o ZX Spectrum 48K.",
    "Os programas vinham em fita cassete.",
    "",
    "F1 abre o menu e esta ajuda.",
    "",
    "Para jogar: arraste o arquivo do",
    "jogo para esta janela, ou aperte F2",
    "e escolha na lista (Enter abre).",
    "O emulador reinicia o micro, digita",
    "LOAD \"\" sozinho e toca a fita: tem",
    "o chiado e as listras na borda,",
    "como no aparelho, só que 20 vezes",
    "mais rápido (F7 = o mais rápido).",
    "",
    "Depois, siga as instruções do jogo:",
    "setas = joystick, Alt = fogo.",
    NULL
};
static const char *const help_p1_touch[] = {
    "O TK90X (1985) e o TK95 (1986) são",
    "micros brasileiros da Microdigital,",
    "compatíveis com o ZX Spectrum 48K.",
    "Os programas vinham em fita cassete.",
    "",
    "MENU (no alto): jogos, opções e",
    "esta ajuda.",
    "Para jogar: MENU > Abrir jogo e",
    "toque no jogo da lista.",
    "O emulador reinicia o micro, digita",
    "LOAD \"\" sozinho e toca a fita: tem",
    "o chiado e as listras na borda,",
    "como no aparelho, só que 20 vezes",
    "mais rápido (MENU > Turbo = máximo).",
    "",
    "Joystick (esquerda): arraste o dedo.",
    "FOGO (direita): tiro.",
    "Voltar: fecha os menus / sair.",
    NULL
};

static const char *const help_p2_desk[] = {
    "O F2 faz tudo sozinho. Para carregar",
    "como no aparelho, no BASIC:",
    "1. Com o cursor K piscando, aperte",
    "   J: aparece LOAD (no cursor K",
    "   cada tecla é um comando).",
    "2. Digite \"\" (Ctrl+P duas vezes,",
    "   ou as aspas do teclado do PC).",
    "3. Enter: a fita toca sozinha (não",
    "   aperte F3, que a pausaria).",
    "",
    "Fita no fim, ou erro \"R Erro de",
    "leitura\"? No menu (F1), use",
    "Fita: rebobinar e LOAD \"\" de novo.",
    "",
    "Jogos com várias cargas (fases): a",
    "fita volta a tocar sozinha quando",
    "o jogo pede a próxima parte; se não",
    "voltar, aperte F3.",
    NULL
};
static const char *const help_p2_touch[] = {
    "MENU > Abrir jogo faz tudo sozinho.",
    "Para carregar como no aparelho:",
    "1. Toque TECLADO. Com o cursor K",
    "   piscando, toque J: aparece LOAD",
    "   (no cursor K cada tecla é um",
    "   comando).",
    "2. Toque SYMBOL e depois P, duas",
    "   vezes: \"\" (aspas).",
    "3. ENTER: a fita toca sozinha (não",
    "   toque em MENU > Fita: pausaria).",
    "O botão LOAD \"\" faz tudo isso.",
    "",
    "Fita no fim, ou erro \"R Erro de",
    "leitura\"? MENU > Rebobinar fita",
    "e LOAD \"\" de novo.",
    "",
    "Jogos com várias cargas (fases): a",
    "fita volta a tocar sozinha quando",
    "o jogo pede a próxima parte.",
    NULL
};

static const char *const help_p3_desk[] = {
    ".tap .tzx  fitas (quase todos os",
    "           jogos): tocam como fita.",
    ".z80 .sna  estados salvos (cópia da",
    "           memória), carregados por",
    "           uma \"fita virtual\".",
    ".scr       uma tela (6912 bytes).",
    ".bin .cod  código de máquina puro:",
    "           carrega e executa no",
    "           endereço 32768. Outro:",
    "           tk90x --org 24576",
    "           --exec 24600 rotina.bin",
    ".zip: descompacte antes de abrir.",
    "",
    "No F2: setas escolhem, Enter abre,",
    "Backspace sobe uma pasta, uma letra",
    "pula para o nome; clique duplo abre.",
    "F6 salva o estado num .z80: abra-o",
    "depois (F2) para continuar dali.",
    NULL
};
static const char *const help_p3_touch[] = {
    ".tap .tzx  fitas (quase todos os",
    "           jogos): tocam como fita.",
    ".z80 .sna  estados salvos (cópia da",
    "           memória), carregados por",
    "           uma \"fita virtual\".",
    ".scr       uma tela (6912 bytes).",
    ".bin .cod  código de máquina puro:",
    "           carrega e executa no",
    "           endereço 32768.",
    ".zip: descompacte antes de abrir.",
    "",
    "MENU > Abrir jogo: toque no arquivo",
    "ou na pasta; arraste para rolar.",
    "No Android, \"Abrir do aparelho\"",
    "abre os arquivos do celular.",
    "MENU > Salvar estado grava um .z80:",
    "abra-o depois para continuar dali.",
    NULL
};

static const char *const help_p4_desk[] = {
    "Letras e números: iguais ao PC.",
    "Shift esquerdo  = CAPS SHIFT",
    "Ctrl/Shift dir. = SYMBOL SHIFT",
    "Backspace       = DELETE (apagar)",
    "Pontuação (\" , ; + - * / = ...):",
    "  digite direto, em qualquer layout.",
    "Setas no BASIC: movem o cursor.",
    "Tab = modo E   CapsLock = CAPS LOCK",
    "Home = EDIT    End = BREAK",
    "Insert = GRAPHICS",
    "PgUp/PgDn = TRUE/INV. VIDEO",
    "",
    "BASIC do TK: no cursor K cada tecla",
    "é um comando, escrito nela no",
    "teclado do TK (P = PRINT, J = LOAD,",
    "R = RUN, K = LIST, A = NEW).",
    "Exemplo: P 2 + 2 Enter mostra 4.",
    NULL
};
static const char *const help_p4_touch[] = {
    "TECLADO (no alto): mostra/esconde",
    "as 40 teclas do TK, como no micro.",
    "CAPS SHIFT e SYMBOL SHIFT: um toque",
    "trava para a próxima tecla (fica",
    "amarela); segurar e tocar outra",
    "tecla também funciona.",
    "CAPS travado + SYMBOL = modo E.",
    "Em vermelho: SYMBOL + a tecla",
    "(SYMBOL + P = aspas).",
    "CAPS + 0 = apagar (DELETE).",
    "CAPS + 5 6 7 8 = mover o cursor.",
    "",
    "BASIC do TK: no cursor K cada tecla",
    "é um comando, escrito sob a letra",
    "(P = PRINT, J = LOAD, R = RUN,",
    "K = LIST, A = NEW). Exemplo:",
    "P 2 SYMBOL+K 2 ENTER mostra 4.",
    NULL
};

static const char *const help_p5_desk[] = {
    "Setas (automático, o padrão): no",
    "jogo são o joystick; no BASIC,",
    "movem o cursor. Alt = fogo.",
    "O joystick do TK (Sinclair 1) usa",
    "as teclas 6 esq., 7 dir., 8 baixo,",
    "9 cima e 0 fogo.",
    "Se o jogo não obedecer às setas:",
    "- F9 troca o tipo: Sinclair 1,",
    "  Kempston, Sinclair 2 ou Cursor;",
    "- veja no jogo a opção de controle",
    "  (teclado, joystick, Kempston...);",
    "- muitos jogos usam Q cima, A baixo,",
    "  O esq., P dir. e espaço = fogo.",
    "F4: setas automáticas, sempre",
    "joystick ou sempre cursores.",
    "Controle USB: funciona direto, como",
    "o joystick escolhido no F9.",
    NULL
};
static const char *const help_p5_touch[] = {
    "Joystick (esquerda): arraste o",
    "polegar a partir do centro; são 8",
    "direções. FOGO (direita): tiro.",
    "Ele age como o joystick do TK",
    "(Sinclair 1): teclas 6 esq., 7 dir.,",
    "8 baixo, 9 cima e 0 fogo.",
    "Se o jogo não obedecer:",
    "- MENU > Joystick troca o tipo:",
    "  Sinclair 1, Kempston, Sinclair 2",
    "  ou Cursor;",
    "- veja no jogo a opção de controle",
    "  (teclado, joystick, Kempston...);",
    "- muitos jogos usam Q cima, A baixo,",
    "  O esq., P dir. e espaço = fogo",
    "  (no TECLADO).",
    "Controle USB ou Bluetooth: funciona",
    "direto, como o joystick escolhido.",
    NULL
};

static const char *const help_p6_desk[] = {
    "F5   reinicia (reset): como desligar",
    "     e ligar; a fita continua lá.",
    "F10  troca o modelo: TK95 (padrão),",
    "     TK90X ou TK90X v1 (ROM de 1985,",
    "     menos compatível). Reinicia e",
    "     tira a fita: reabra o jogo.",
    "F8   50 Hz (padrão: jogos europeus",
    "     na velocidade certa) ou 60 Hz",
    "     (o TK brasileiro de fábrica).",
    "F7   turbo: velocidade máxima.",
    "F6   salva o estado (.z80).",
    "F12  captura de tela (.png).",
    "F11  tela cheia.",
    "Esc  sai (pede confirmação).",
    "",
    "Os arquivos do F6 e do F12 vão para",
    "a pasta de onde o emulador foi",
    "aberto. Mais detalhes: README.md.",
    NULL
};
static const char *const help_p6_touch[] = {
    "MENU > Reiniciar: reset, como",
    "desligar e ligar o micro.",
    "MENU > Modelo: TK95 (padrão), TK90X",
    "ou TK90X v1 (ROM de 1985, menos",
    "compatível). Reinicia e tira a",
    "fita: abra o jogo de novo.",
    "MENU > 50/60 Hz: 50 Hz (padrão,",
    "jogos europeus na velocidade certa)",
    "ou 60 Hz (o TK brasileiro).",
    "MENU > Turbo: velocidade máxima.",
    "MENU > Salvar estado: grava .z80.",
    "MENU > Captura de tela: grava .png.",
    "",
    "No Android os arquivos vão para a",
    "pasta do app (Android/data/...,",
    "acessível pelo cabo USB).",
    "",
    "Voltar: fecha os menus; sem menu,",
    "pergunta se quer sair.",
    NULL
};

typedef struct HelpPage {
    const char *title;
    const char *const *desk;
    const char *const *touch;
} HelpPage;

static const HelpPage help_pages[] = {
    { "PRIMEIROS PASSOS",      help_p1_desk, help_p1_touch },
    { "CARREGAR UMA FITA",     help_p2_desk, help_p2_touch },
    { "FORMATOS DE ARQUIVO",   help_p3_desk, help_p3_touch },
    { "TECLADO",               help_p4_desk, help_p4_touch },
    { "JOGOS E JOYSTICK",      help_p5_desk, help_p5_touch },
    { "RESET, MODELOS E MAIS", help_p6_desk, help_p6_touch },
};
#define HELP_PAGES ((int)(sizeof help_pages / sizeof help_pages[0]))

int osd_help_pages(void) { return HELP_PAGES; }

static const char *const *help_text(const Osd *o, int page)
{
    return o->touch ? help_pages[page].touch : help_pages[page].desk;
}

static int help_count_lines(const char *const *l)
{
    int n = 0;
    while (l[n] && n < HELP_LINES) n++;
    return n;
}

static int help_clamp(int page)
{
    return page < 0 ? 0 : page >= HELP_PAGES ? HELP_PAGES - 1 : page;
}

/* Esc na ajuda: volta ao menu (se veio dele) ou fecha */
static OsdResult help_back(Osd *o)
{
    o->mouse_item = -1;
    ptr_reset(o);
    if (o->help_from_menu) { o->mode = OSD_MENU; return OSD_RES_NONE; }
    o->mode = OSD_NONE;
    return OSD_RES_CLOSED;
}

static OsdResult help_key(Osd *o, OsdKey key, int ch)
{
    int pg = help_clamp(o->help_page);
    switch (key) {
    case OSDK_LEFT: case OSDK_PGUP: case OSDK_UP:
        if (pg > 0) pg--;
        break;
    case OSDK_RIGHT: case OSDK_PGDN: case OSDK_DOWN:
        if (pg < HELP_PAGES - 1) pg++;
        break;
    case OSDK_ENTER:            /* Enter: proxima pagina; na ultima, sai */
        if (pg == HELP_PAGES - 1) return help_back(o);
        pg++;
        break;
    case OSDK_HOME: pg = 0; break;
    case OSDK_END:  pg = HELP_PAGES - 1; break;
    case OSDK_CHAR:
        if (ch >= '1' && ch < '1' + HELP_PAGES) pg = ch - '1';
        break;
    case OSDK_ESC: case OSDK_BACK:
        return help_back(o);
    default:
        break;
    }
    o->help_page = pg;
    return OSD_RES_NONE;
}

/* ---------------- menu principal (desktop) ---------------- */

#define MENU_Y0    40   /* y (pixels) do primeiro item */
#define MENU_PITCH 10   /* distancia entre itens: 8 da fonte + 2 de folga */
#define MENU_GAP   6    /* espaco extra entre grupos */

static int menu_y(const Osd *o, int i)
{
    int y = MENU_Y0;
    for (int j = 1; j <= i && j < o->menu_n; j++) y += MENU_PITCH + (o->menu[j].gap ? MENU_GAP : 0);
    return y;
}

/* Item sob o ponto (pixels do framebuffer); -1 nenhum */
static int menu_item_at(const Osd *o, int x, int y)
{
    if (x < 2 * 8 - 2 || x >= 38 * 8 + 2) return -1;
    for (int i = 0; i < o->menu_n; i++) {
        int ry = menu_y(o, i);
        if (y >= ry - 1 && y < ry + MENU_PITCH - 1) return i;
    }
    return -1;
}

static int fold_lower(int c)
{
    if (c >= 'A' && c <= 'Z') c += 32;
    return c;
}

static OsdResult menu_key(Osd *o, OsdKey key, int ch)
{
    int n = o->menu_n;
    if (n <= 0) {
        if (key == OSDK_ESC || key == OSDK_BACK) { o->mode = OSD_NONE; return OSD_RES_CLOSED; }
        return OSD_RES_NONE;
    }
    switch (key) {
    case OSDK_UP:   o->menu_sel = (o->menu_sel + n - 1) % n; break;
    case OSDK_DOWN: o->menu_sel = (o->menu_sel + 1) % n; break;
    case OSDK_HOME: case OSDK_PGUP: o->menu_sel = 0; break;
    case OSDK_END:  case OSDK_PGDN: o->menu_sel = n - 1; break;
    case OSDK_ENTER: case OSDK_RIGHT:
        o->menu_action = o->menu[o->menu_sel].id;
        return OSD_RES_ACTION;
    case OSDK_ESC: case OSDK_BACK: case OSDK_LEFT:
        o->mode = OSD_NONE;
        return OSD_RES_CLOSED;
    case OSDK_CHAR: {
        /* letra inicial: se so um item comeca com ela, executa; se varios,
         * a selecao passa para o proximo deles */
        char a[64];
        int c = fold_lower(ch), hits = 0, first = -1, next = -1;
        for (int i = 0; i < n; i++) {
            to_ascii(o->menu[i].label, a, sizeof a);
            if (fold_lower((unsigned char)a[0]) != c) continue;
            hits++;
            if (first < 0) first = i;
            if (next < 0 && i > o->menu_sel) next = i;
        }
        if (!hits) break;
        o->menu_sel = next >= 0 ? next : first;
        if (hits == 1) {
            o->menu_action = o->menu[o->menu_sel].id;
            return OSD_RES_ACTION;
        }
        break;
    }
    default:
        break;
    }
    return OSD_RES_NONE;
}

/* ---------------- desenho (layout classico, grade 40x30) ---------------- */

static void draw_panel(const Cv *cv, int col, int row, int cols, int rows)
{
    int x = col * 8 - 4, y = row * 8 - 4, w = cols * 8 + 8, h = rows * 8 + 8;
    fill_rect(cv, x, y, w, h, C_PANEL);
    frame_rect(cv, x, y, w, h, C_FRAME);
}

/* Texto centralizado na linha (grade de 40 colunas) */
static void center_text(const Osd *o, const Cv *cv, int row, const char *utf8, uint32_t fg)
{
    char a[OSD_TEXT_MAX * 2];
    to_ascii(utf8, a, sizeof a);
    int n = (int)strlen(a);
    if (n > OSD_COLS) n = OSD_COLS;
    put_text(o, cv, (FB_W - n * 8) / 2, row * 8, a, n, fg, 0, 0);
}

static void draw_menu(const Osd *o, const Cv *cv)
{
    dim_rect(cv, 0, 0, FB_W, FB_H);
    draw_panel(cv, 1, 1, 38, 28);
    cell_text(o, cv, 2, 1, "MENU - TK90X / TK95", 36, C_TITLE, 0, 0);
    cell_text(o, cv, 30, 1, "F1 fecha", 8, C_DIM, 0, 0);
    /* fita: progresso e bloco atual (a linha de status da carga some apos 3 s) */
    cell_text(o, cv, 2, 2, o->tape_info, 36, C_DIR, 0, 0);
    if (o->tape_block[0]) cell_text(o, cv, 3, 3, o->tape_block, 35, C_DIR, 0, 0);
    for (int i = 0; i < o->menu_n; i++) {
        const OsdMenuItem *it = &o->menu[i];
        int y = menu_y(o, i);
        int sel = i == o->menu_sel;
        uint32_t fg = C_TEXT, kc = C_DIM, vc = C_DIR;
        char a[64];
        if (sel) {
            fill_rect(cv, 2 * 8 - 2, y - 1, 36 * 8 + 4, MENU_PITCH, C_SEL_BG);
            fg = kc = vc = C_SEL_FG;
        }
        to_ascii(it->key, a, sizeof a);
        put_text(o, cv, 2 * 8, y, a, 3, kc, 0, 0);
        to_ascii(it->label, a, sizeof a);
        put_text(o, cv, 6 * 8, y, a, 22, fg, 0, 0);
        if (it->value[0]) {
            to_ascii(it->value, a, sizeof a);
            int n = (int)strlen(a);
            if (n > 12) n = 12;
            put_text(o, cv, (38 - n) * 8, y, a, n, vc, 0, 0);
        }
    }
    /* rodape (a ultima linha da grade fica para os avisos) */
    int y = (o->menu_n ? menu_y(o, o->menu_n - 1) : MENU_Y0) + 14;
    if (y < 202) y = 202;
    put_text(o, cv, 2 * 8, y, "Setas/mouse escolhem, Enter usa;", 36, C_DIM, 0, 0);
    put_text(o, cv, 2 * 8, y + 10, "letra inicial = atalho. Esc fecha.", 36, C_DIM, 0, 0);
    put_text(o, cv, 2 * 8, y + 20, "Dica: arraste um jogo p/ a janela.", 36, C_TITLE, 0, 0);
}

#define HELP_FOOT_Y 218   /* rodape da ajuda (pixels) */

static void draw_help(const Osd *o, const Cv *cv)
{
    int pg = help_clamp(o->help_page);
    const char *const *l = help_text(o, pg);
    int n = help_count_lines(l);
    dim_rect(cv, 0, 0, FB_W, FB_H);
    draw_panel(cv, 1, 1, 38, 28);
    cell_text(o, cv, 2, 1, help_pages[pg].title, 27, C_TITLE, 0, 0);
    char ind[32];
    snprintf(ind, sizeof ind, "Pag %d/%d", pg + 1, HELP_PAGES);
    cell_text(o, cv, 38 - (int)strlen(ind), 1, ind, 9, C_DIM, 0, 0);
    fill_rect(cv, 2 * 8, 2 * 8 + 3, 36 * 8, 1, C_FRAME);
    /* linhas com 2 px de folga entre elas quando cabem; o rodape fica acima da
     * ultima linha da grade, onde aparecem os avisos (toasts) */
    int y0 = 3 * 8, avail = HELP_FOOT_Y - 6 - y0;
    int pitch = n ? avail / n : 10;
    if (pitch > 10) pitch = 10;
    if (pitch < 8) pitch = 8;
    for (int i = 0; i < n; i++) {
        char a[OSD_TEXT_MAX];
        to_ascii(l[i], a, sizeof a);
        put_text(o, cv, 2 * 8, y0 + i * pitch, a, HELP_COLS, C_TEXT, 0, 0);
    }
    fill_rect(cv, 2 * 8, HELP_FOOT_Y - 4, 36 * 8, 1, C_FRAME);
    put_text(o, cv, 2 * 8, HELP_FOOT_Y, "< anterior", 10, pg > 0 ? C_TITLE : C_DIM, 0, 0);
    const char *esc = o->help_from_menu ? "Esc volta" : "Esc fecha";
    put_text(o, cv, (FB_W - 9 * 8) / 2, HELP_FOOT_Y, esc, 9, C_DIM, 0, 0);
    put_text(o, cv, 29 * 8, HELP_FOOT_Y, "proxima >", 9, pg < HELP_PAGES - 1 ? C_TITLE : C_DIM, 0, 0);
}

/* Dialogo de saida: botoes [ Sim ] e [ Nao ] na linha QUIT_BTN_ROW */
#define QUIT_BTN_ROW 18
#define QUIT_SIM_COL 12
#define QUIT_NAO_COL 21
#define QUIT_BTN_W   7

static void draw_quit(const Osd *o, const Cv *cv)
{
    dim_rect(cv, 0, 0, FB_W, FB_H);
    draw_panel(cv, 5, 11, 30, 9);
    center_text(o, cv, 12, "Sair do emulador?", C_TITLE);
    center_text(o, cv, 14, "Esc ou S = sim   N = nao", C_TEXT);
    center_text(o, cv, 16, "F1 = menu e ajuda", C_DIR);
    for (int b = 0; b < 2; b++) {
        int col = b ? QUIT_NAO_COL : QUIT_SIM_COL;
        int hot = o->mouse_item == b;
        fill_rect(cv, col * 8 - 2, QUIT_BTN_ROW * 8 - 2, QUIT_BTN_W * 8 + 4, 12,
                  hot ? C_PRESS : C_BTN);
        frame_rect(cv, col * 8 - 2, QUIT_BTN_ROW * 8 - 2, QUIT_BTN_W * 8 + 4, 12, C_FRAME);
        cell_text(o, cv, col + 1, QUIT_BTN_ROW, b ? " Nao " : " Sim ", 5,
                  hot ? C_SEL_FG : C_TEXT, 0, 0);
    }
}

/* Nome cabendo em maxc colunas: corta o meio e mantem a extensao */
static void fit_name(const char *ascii, char *out, size_t outlen, int maxc)
{
    int n = (int)strlen(ascii);
    if (n <= maxc) { copy_str(out, outlen, ascii); return; }
    int tail = 6;
    int head = maxc - tail - 1;
    if (head < 1) head = 1;
    char tmp[OSD_TEXT_MAX];
    snprintf(tmp, sizeof tmp, "%.*s~%s", head, ascii, ascii + n - tail);
    copy_str(out, outlen, tmp);
}

/* Nome exibido de uma entrada (ASCII, com separador nos diretorios) */
static void entry_label(const PlatEntry *e, char *name, size_t namelen)
{
    char a[PLAT_PATH_MAX];
    if (e->is_dir == ENTRY_PICKER) { to_ascii(e->name, name, namelen); return; }
    if (!strcmp(e->name, "..")) { copy_str(name, namelen, ".. (voltar)"); return; }
    to_ascii(e->name, a, sizeof a);
    if (e->is_dir) {
        size_t l = strlen(a);
        if (l + 1 < sizeof a && !(l && (a[l - 1] == '\\' || a[l - 1] == '/'))) {
            a[l] = PLAT_SEP;
            a[l + 1] = 0;
        }
    }
    copy_str(name, namelen, a);
}

/* Entradas de verdade na lista (sem ".." e sem a do seletor) */
static int browser_files(const Osd *o)
{
    int n = 0;
    for (int i = 0; i < o->count; i++)
        if (o->entries[i].is_dir != ENTRY_PICKER && strcmp(o->entries[i].name, "..")) n++;
    return n;
}

/* Caminho do diretorio cabendo em maxc colunas (mostra o final) */
static void dir_label(const Osd *o, char *out, size_t outlen, int maxc)
{
    char a[PLAT_PATH_MAX];
    to_ascii(o->dir[0] ? o->dir : "Computador", a, sizeof a);
    int len = (int)strlen(a);
    if (maxc < 8) maxc = 8;
    if (len > maxc) snprintf(out, outlen, "...%s", a + len - (maxc - 3));
    else copy_str(out, outlen, a);
}

static void draw_browser(const Osd *o, const Cv *cv)
{
    dim_rect(cv, 0, 0, FB_W, FB_H);
    draw_panel(cv, 1, 1, 38, 28);
    cell_text(o, cv, 2, 1, "ABRIR ARQUIVO", 36, C_TITLE, 0, 0);

    /* caminho: mostra o final se for longo */
    char shown[64];
    dir_label(o, shown, sizeof shown, 36);
    put_text(o, cv, 2 * 8, 2 * 8, shown, 36, C_DIM, 0, 0);

    if (o->browse_err[0])
        cell_text(o, cv, 2, 27, o->browse_err, 36, C_ERR, 0, 0);
    if (!browser_files(o))
        cell_text(o, cv, 2, LIST_ROW0 + 1, "(nenhum arquivo suportado aqui)", 36, C_DIM, 0, 0);

    for (int i = 0; i < LIST_ROWS; i++) {
        int idx = o->top + i;
        if (idx >= o->count) break;
        const PlatEntry *e = &o->entries[idx];
        char name[OSD_TEXT_MAX * 2], line[OSD_TEXT_MAX];
        entry_label(e, name, sizeof name);
        fit_name(name, line, sizeof line, 35);
        int row = LIST_ROW0 + i;
        uint32_t fg = e->is_dir == ENTRY_PICKER ? C_PICK : e->is_dir ? C_DIR : C_TEXT;
        if (idx == o->sel) {
            fill_rect(cv, 2 * 8 - 2, row * 8, 36 * 8 + 4, 8, C_SEL_BG);
            fg = C_SEL_FG;
        }
        put_text(o, cv, 2 * 8 + 4, row * 8, line, 35, fg, 0, 0);
    }

    /* barra de rolagem simples */
    if (o->count > LIST_ROWS) {
        int x = 38 * 8 + 1, y0 = LIST_ROW0 * 8, h = LIST_ROWS * 8;
        fill_rect(cv, x, y0, 2, h, C_FRAME);
        int th = h * LIST_ROWS / o->count;
        if (th < 4) th = 4;
        int ty = y0 + (h - th) * o->top / (o->count - LIST_ROWS);
        fill_rect(cv, x, ty, 2, th, C_TEXT);
    }
    if (!o->browse_err[0]) {
        char pos[32];
        snprintf(pos, sizeof pos, "%d/%d", o->count ? o->sel + 1 : 0, o->count);
        cell_text(o, cv, 38 - (int)strlen(pos), 1, pos, 12, C_DIM, 0, 0);
    }
    cell_text(o, cv, 2, 28, "Enter abre  Backspace sobe  Esc", 36, C_DIM, 0, 0);
}

/* Faixa escura + texto centralizado numa linha da grade */
static void draw_band(const Osd *o, const Cv *cv, int row, const char *utf8, uint32_t fg)
{
    char a[OSD_TEXT_MAX * 2];
    to_ascii(utf8, a, sizeof a);
    int n = (int)strlen(a);
    int maxn = cv->w / 8 - 1;
    if (n > maxn) n = maxn;
    int w = n * 8;
    int x = (cv->w - w) / 2;
    fill_rect(cv, x - 3, row * 8 - 2, w + 6, 12, C_TOAST);
    put_text(o, cv, x, row * 8, a, n, fg, 0, 0);
}

/* Toast: some quando vence. Devolve 1 se ainda deve ser desenhado. */
static int toast_live(Osd *o, uint32_t now_ms)
{
    if (!o->toast[0]) return 0;
    if ((int32_t)(now_ms - o->toast_until) >= 0) { o->toast[0] = 0; return 0; }
    return 1;
}

void osd_render(Osd *o, uint32_t *fb, uint32_t now_ms)
{
    Cv cv = cv_make(fb, FB_W, FB_H, 0);
    /* status na borda inferior (sob os menus) */
    if (o->status[0] && o->mode == OSD_NONE) {
        char a[OSD_TEXT_MAX * 2];
        to_ascii(o->status, a, sizeof a);
        int n = (int)strlen(a);
        if (n > OSD_COLS - 2) n = OSD_COLS - 2;
        /* so a largura do texto, na borda de baixo: o resto da borda (listras) fica livre */
        dim_rect(&cv, 4, FB_H - 12, n * 8 + 8, 10);
        put_text(o, &cv, 8, FB_H - 11, a, n, C_TEXT, 0, 0);
    }
    /* modo toque: menus e toast vao para a tela de toque (osd_render_canvas) */
    if (o->touch) return;

    switch (o->mode) {
    case OSD_HELP:    draw_help(o, &cv); break;
    case OSD_BROWSER: draw_browser(o, &cv); break;
    case OSD_QUIT:    draw_quit(o, &cv); break;
    case OSD_MENU:    draw_menu(o, &cv); break;
    default: break;
    }

    /* sem menu: borda superior. Com um painel grande (menu, ajuda, navegador)
     * o aviso ocupa a linha do titulo, por dentro da moldura, sem cobrir o
     * rodape nem a borda; com o dialogo de saida, a ultima linha da grade. */
    if (toast_live(o, now_ms)) {
        if (o->mode == OSD_MENU || o->mode == OSD_HELP || o->mode == OSD_BROWSER) {
            char a[OSD_TEXT_MAX * 2];
            to_ascii(o->toast, a, sizeof a);
            int n = (int)strlen(a);
            if (n > 36) n = 36;
            fill_rect(&cv, 2 * 8 - 2, 1 * 8 - 1, 36 * 8 + 4, 9, C_TOAST);
            put_text(o, &cv, (FB_W - n * 8) / 2, 1 * 8, a, n, C_TITLE, 0, 0);
        } else {
            draw_band(o, &cv, o->mode == OSD_NONE ? 1 : OSD_ROWS - 1, o->toast, C_TITLE);
        }
    }
}

/* ---------------- mouse no layout classico ---------------- */

OsdResult osd_mouse(Osd *o, OsdPtr ev, int x, int y, int clicks, char *path, size_t pathlen)
{
    /* painel grande (menu, ajuda, navegador): colunas 1..38, linhas 1..28 */
    int in_panel = inside(x, y, 4, 4, FB_W - 8, FB_H - 8);
    switch (o->mode) {
    case OSD_MENU: {
        int it = menu_item_at(o, x, y);
        if (ev == OSD_PTR_MOVE) {                 /* destaca o item sob o mouse */
            if (it >= 0) o->menu_sel = it;
            return OSD_RES_NONE;
        }
        if (ev == OSD_PTR_DOWN) {
            o->mouse_item = it >= 0 ? it : in_panel ? -1 : -2;
            if (it >= 0) o->menu_sel = it;
            return OSD_RES_NONE;
        }
        int was = o->mouse_item;
        o->mouse_item = -1;
        if (was >= 0 && it == was) {
            o->menu_action = o->menu[it].id;
            return OSD_RES_ACTION;
        }
        if (was == -2 && !in_panel) { o->mode = OSD_NONE; return OSD_RES_CLOSED; }
        return OSD_RES_NONE;
    }
    case OSD_HELP: {
        /* rodape: "< anterior" | "Esc" | "proxima >"; no texto: metade
         * esquerda volta, direita avanca */
        int zone;
        if (!in_panel) zone = 2;
        else if (y >= HELP_FOOT_Y - 4) zone = x < 13 * 8 ? 0 : x >= 27 * 8 ? 1 : 2;
        else zone = x < FB_W / 2 ? 0 : 1;
        if (ev == OSD_PTR_MOVE) return OSD_RES_NONE;
        if (ev == OSD_PTR_DOWN) { o->mouse_item = zone; return OSD_RES_NONE; }
        int was = o->mouse_item;
        o->mouse_item = -1;
        if (was != zone) return OSD_RES_NONE;
        if (zone == 2) return help_back(o);
        return help_key(o, zone ? OSDK_RIGHT : OSDK_LEFT, 0);
    }
    case OSD_QUIT: {
        int b = -1;
        if (y >= QUIT_BTN_ROW * 8 - 2 && y < QUIT_BTN_ROW * 8 + 10) {
            if (x >= QUIT_SIM_COL * 8 - 2 && x < (QUIT_SIM_COL + QUIT_BTN_W) * 8 + 2) b = 0;
            else if (x >= QUIT_NAO_COL * 8 - 2 && x < (QUIT_NAO_COL + QUIT_BTN_W) * 8 + 2) b = 1;
        }
        int outside = !inside(x, y, 5 * 8 - 4, 11 * 8 - 4, 30 * 8 + 8, 9 * 8 + 8);
        if (ev == OSD_PTR_MOVE) { o->mouse_item = b; return OSD_RES_NONE; }
        if (ev == OSD_PTR_DOWN) { o->mouse_item = b >= 0 ? b : outside ? 2 : -1; return OSD_RES_NONE; }
        int was = o->mouse_item;
        o->mouse_item = -1;
        if (was == 0 && b == 0) { o->mode = OSD_NONE; return OSD_RES_QUIT; }
        if ((was == 1 && b == 1) || (was == 2 && outside)) { o->mode = OSD_NONE; return OSD_RES_CLOSED; }
        return OSD_RES_NONE;
    }
    case OSD_BROWSER: {
        /* clique escolhe a linha; clique duplo abre (ou entra no diretorio) */
        if (ev != OSD_PTR_DOWN) return OSD_RES_NONE;
        if (y < LIST_ROW0 * 8 || y >= (LIST_ROW0 + LIST_ROWS) * 8 || x < 2 * 8 - 2 || x >= 38 * 8 + 2)
            return OSD_RES_NONE;
        int idx = o->top + (y - LIST_ROW0 * 8) / 8;
        if (idx < 0 || idx >= o->count) return OSD_RES_NONE;
        o->sel = idx;
        if (clicks >= 2) return browser_key(o, OSDK_ENTER, 0, path, pathlen);
        return OSD_RES_NONE;
    }
    default:
        return OSD_RES_NONE;
    }
}

/* ---------------- layout de toque ---------------- */

/* Botao com moldura e rotulo centralizado (escala sc) */
static void t_button(const Osd *o, const Cv *cv, int x, int y, int w, int h, const char *label,
                     int pressed, int sc)
{
    fill_rect(cv, x, y, w, h, pressed ? C_PRESS : C_BTN);
    frame_rect(cv, x, y, w, h, C_FRAME);
    int n = (int)strlen(label);
    while (sc > 1 && n * 8 * sc > w - 4) sc--;
    px_text(o, cv, x + (w - n * 8 * sc) / 2, y + (h - 8 * sc) / 2, label, n,
            pressed ? C_SEL_FG : C_TEXT, sc);
}

static void t_panel(const Cv *cv, int x, int y, int w, int h)
{
    fill_rect(cv, x, y, w, h, C_PANEL);
    frame_rect(cv, x, y, w, h, C_FRAME);
}

static int br_row_at(const Osd *o, int x, int y)
{
    BrGeom g;
    br_geom(o, &g);
    if (!inside(x, y, g.lx, g.ly0, g.lw, g.ly1 - g.ly0)) return -1;
    int i = (y - g.ly0 + (o->scroll > 0 ? o->scroll : 0)) / TROW_H;
    return i < o->count ? i : -1;
}

static void t_draw_browser(Osd *o, const Cv *cv0)
{
    BrGeom g;
    br_geom(o, &g);
    if (o->scroll < 0) br_show_sel(o);
    else br_clamp_scroll(o);

    dim_rect(cv0, 0, 0, cv0->w, cv0->h);
    t_panel(cv0, g.px, g.py, g.pw, g.ph);
    int tsc = (g.pw >= 13 * 16 + 120) ? 2 : 1;
    px_text(o, cv0, g.px + 6, g.py + 6 + (tsc == 1 ? 4 : 0), "ABRIR ARQUIVO", 13, C_TITLE, tsc);
    t_button(o, cv0, g.cx, g.cy, g.cw, g.ch, "X", o->ptr_down && o->ptr_btn == 1, 2);
    char pos[32];
    snprintf(pos, sizeof pos, "%d/%d", o->count ? o->sel + 1 : 0, o->count);
    px_text(o, cv0, g.cx - 6 - (int)strlen(pos) * 8, g.py + 11, pos, 12, C_DIM, 1);

    int cols = (g.pw - 12) / 8;
    char shown[PLAT_PATH_MAX];
    dir_label(o, shown, sizeof shown, cols);
    put_text(o, cv0, g.px + 6, g.py + 29, shown, cols, C_DIM, 0, 0);

    /* lista recortada na sua area: linhas parciais em cima e embaixo */
    Cv cv = *cv0;
    cv.y0 = g.ly0;
    cv.y1 = g.ly1;
    int ncols = (g.lw - 12) / 8;
    if (ncols > OSD_TEXT_MAX - 1) ncols = OSD_TEXT_MAX - 1;
    int first = o->scroll / TROW_H;
    for (int idx = first; idx < o->count; idx++) {
        int y = g.ly0 + idx * TROW_H - o->scroll;
        if (y >= g.ly1) break;
        const PlatEntry *e = &o->entries[idx];
        char name[OSD_TEXT_MAX * 2], line[OSD_TEXT_MAX];
        entry_label(e, name, sizeof name);
        fit_name(name, line, sizeof line, ncols);
        uint32_t fg = e->is_dir == ENTRY_PICKER ? C_PICK : e->is_dir ? C_DIR : C_TEXT;
        uint32_t bg = (idx & 1) ? C_ROW : C_PANEL;
        if (o->ptr_down && idx == o->ptr_row) { bg = C_PRESS; fg = C_SEL_FG; }
        else if (idx == o->sel) { bg = C_SEL_BG; fg = C_SEL_FG; }
        fill_rect(&cv, g.lx, y, g.lw, TROW_H - 1, bg);
        put_text(o, &cv, g.lx + 6, y + (TROW_H - 8) / 2, line, ncols, fg, 0, 0);
    }
    if (!browser_files(o))
        px_text(o, &cv, g.lx + 6, g.ly0 + TROW_H + 7, "(nenhum arquivo suportado aqui)", ncols,
                C_DIM, 1);
    if (o->browse_err[0]) {
        fill_rect(&cv, g.lx, g.ly1 - 14, g.lw, 14, C_PANEL);
        px_text(o, &cv, g.lx + 6, g.ly1 - 11, o->browse_err, ncols, C_ERR, 1);
    }

    /* barra de rolagem */
    int view = g.ly1 - g.ly0, total = o->count * TROW_H;
    if (total > view) {
        int x = g.lx + g.lw + 2;
        fill_rect(cv0, x, g.ly0, 3, view, C_FRAME);
        int th = view * view / total;
        if (th < 10) th = 10;
        int ty = g.ly0 + (view - th) * o->scroll / (total - view);
        fill_rect(cv0, x, ty, 3, th, C_TEXT);
    }
}

/* Ajuda no layout de toque: painel, texto e botoes "<", FECHAR, ">" embaixo */
typedef struct HelpGeom {
    int x, y, w, h;
    int by, bh;                /* linha dos botoes */
    int px, nx, cx, sw, cw;    /* "<" em px, ">" em nx (largura sw), FECHAR em cx (largura cw) */
} HelpGeom;

static void t_help_geom(const Osd *o, HelpGeom *g)
{
    g->w = o->cv_w - 8;
    if (g->w > 44 * 8) g->w = 44 * 8;
    g->h = o->cv_h - 8;
    g->x = (o->cv_w - g->w) / 2;
    g->y = 4;
    g->bh = 26;
    g->by = g->y + g->h - g->bh - 4;
    g->sw = 56;
    g->cw = 96;
    g->px = g->x + 6;
    g->nx = g->x + g->w - 6 - g->sw;
    g->cx = g->x + (g->w - g->cw) / 2;
}

static void t_draw_help(const Osd *o, const Cv *cv)
{
    HelpGeom g;
    t_help_geom(o, &g);
    int pg = help_clamp(o->help_page);
    const char *const *l = help_text(o, pg);
    int n = help_count_lines(l);
    dim_rect(cv, 0, 0, cv->w, cv->h);
    t_panel(cv, g.x, g.y, g.w, g.h);
    int cols = (g.w - 16) / 8;
    int tx = g.x + 8, ty = g.y + 6;
    char ind[32];
    snprintf(ind, sizeof ind, "Pag %d/%d", pg + 1, HELP_PAGES);
    int il = (int)strlen(ind);
    px_text(o, cv, tx, ty, help_pages[pg].title, cols - il - 1, C_TITLE, 1);
    px_text(o, cv, g.x + g.w - 8 - il * 8, ty, ind, il, C_DIM, 1);
    fill_rect(cv, tx, ty + 11, g.w - 16, 1, C_FRAME);
    int y0 = ty + 16, avail = g.by - 4 - y0;
    int pitch = n ? avail / n : 10;
    if (pitch > 11) pitch = 11;
    if (pitch < 8) pitch = 8;
    for (int i = 0; i < n && y0 + i * pitch + 8 <= g.by - 2; i++)
        px_text(o, cv, tx, y0 + i * pitch, l[i], cols, C_TEXT, 1);
    int pr = o->ptr_down ? o->ptr_btn : 0;
    t_button(o, cv, g.px, g.by, g.sw, g.bh, "<", pr == 1, 2);
    t_button(o, cv, g.cx, g.by, g.cw, g.bh, "FECHAR", pr == 2, 1);
    t_button(o, cv, g.nx, g.by, g.sw, g.bh, ">", pr == 3, 2);
}

typedef struct QuitGeom { int x, y, w, h, bw, bh, sx, nx, by; } QuitGeom;

static void t_quit_geom(const Osd *o, QuitGeom *q)
{
    q->w = o->cv_w - 16;
    if (q->w > 220) q->w = 220;
    q->h = 84;
    q->x = (o->cv_w - q->w) / 2;
    q->y = (o->cv_h - q->h) / 2;
    q->bw = (q->w - 30) / 2;
    q->bh = 32;
    q->sx = q->x + 10;
    q->nx = q->x + q->w - 10 - q->bw;
    q->by = q->y + q->h - 10 - q->bh;
}

static void t_draw_quit(const Osd *o, const Cv *cv)
{
    QuitGeom q;
    t_quit_geom(o, &q);
    dim_rect(cv, 0, 0, cv->w, cv->h);
    t_panel(cv, q.x, q.y, q.w, q.h);
    const char *t = "Sair do emulador?";
    int n = (int)strlen(t);
    px_text(o, cv, q.x + (q.w - n * 8) / 2, q.y + 14, t, n, C_TITLE, 1);
    t_button(o, cv, q.sx, q.by, q.bw, q.bh, "SIM", o->ptr_down && o->ptr_btn == 1, 2);
    t_button(o, cv, q.nx, q.by, q.bw, q.bh, "NAO", o->ptr_down && o->ptr_btn == 2, 2);
}

int osd_canvas_busy(const Osd *o, uint32_t now_ms)
{
    if (o->mode != OSD_NONE) return 1;
    return o->toast[0] && (int32_t)(now_ms - o->toast_until) < 0;
}

void osd_render_canvas(Osd *o, uint32_t *px, int w, int h, uint32_t now_ms)
{
    Cv cv = cv_make(px, w, h, 1);
    o->cv_w = w;
    o->cv_h = h;
    switch (o->mode) {
    case OSD_HELP:    t_draw_help(o, &cv); break;
    case OSD_BROWSER: t_draw_browser(o, &cv); break;
    case OSD_QUIT:    t_draw_quit(o, &cv); break;
    default: break;
    }
    /* na ajuda o aviso cobriria os botoes de pagina: espera ela fechar */
    if (o->mode != OSD_HELP && toast_live(o, now_ms)) {
        /* faixa no alto (sem menu) ou embaixo (com menu), largura do texto */
        char a[OSD_TEXT_MAX * 2];
        to_ascii(o->toast, a, sizeof a);
        int n = (int)strlen(a), maxn = w / 8 - 2;
        if (n > maxn) n = maxn;
        int tw = n * 8, x = (w - tw) / 2;
        int y = o->mode == OSD_NONE ? (o->toast_top + 6 > 6 ? o->toast_top + 6 : 6) : h - 18;
        fill_rect(&cv, x - 5, y - 4, tw + 10, 16, C_TOAST);
        put_text(o, &cv, x, y, a, n, C_TITLE, 0, 0);
    }
}

OsdResult osd_pointer(Osd *o, OsdPtr ev, int x, int y, char *path, size_t pathlen)
{
    if (o->mode == OSD_NONE) { ptr_reset(o); return OSD_RES_NONE; }
    if (ev == OSD_PTR_DOWN) {
        o->ptr_down = 1;
        o->ptr_drag = 0;
        o->ptr_x0 = x;
        o->ptr_y0 = y;
        o->ptr_btn = 0;
        o->ptr_row = -1;
    } else if (!o->ptr_down) {
        return OSD_RES_NONE;        /* movimento sem toque, ou toque que comecou fora */
    }

    switch (o->mode) {
    case OSD_HELP: {
        /* botoes "<" (1), FECHAR (2) e ">" (3); no texto, metade esquerda
         * volta (4) e direita avanca (5); fora do painel fecha (6) */
        HelpGeom g;
        t_help_geom(o, &g);
        int b;
        if (inside(x, y, g.px, g.by, g.sw, g.bh)) b = 1;
        else if (inside(x, y, g.cx, g.by, g.cw, g.bh)) b = 2;
        else if (inside(x, y, g.nx, g.by, g.sw, g.bh)) b = 3;
        else if (!inside(x, y, g.x, g.y, g.w, g.h)) b = 6;
        else if (y >= g.by) b = 0;
        else b = x < g.x + g.w / 2 ? 4 : 5;
        if (ev == OSD_PTR_DOWN) { o->ptr_btn = b; return OSD_RES_NONE; }
        if (ev == OSD_PTR_MOVE) return OSD_RES_NONE;
        int was = o->ptr_btn;
        ptr_reset(o);
        if (!was || was != b) return OSD_RES_NONE;
        if (b == 2 || b == 6) return help_back(o);
        return help_key(o, (b == 1 || b == 4) ? OSDK_LEFT : OSDK_RIGHT, 0);
    }

    case OSD_QUIT: {
        QuitGeom q;
        t_quit_geom(o, &q);
        if (ev == OSD_PTR_DOWN) {
            if (inside(x, y, q.sx, q.by, q.bw, q.bh)) o->ptr_btn = 1;
            else if (inside(x, y, q.nx, q.by, q.bw, q.bh)) o->ptr_btn = 2;
            else if (!inside(x, y, q.x, q.y, q.w, q.h)) o->ptr_btn = 3;
            return OSD_RES_NONE;
        }
        if (ev == OSD_PTR_MOVE) return OSD_RES_NONE;
        int b = o->ptr_btn;
        ptr_reset(o);
        if (b == 1 && inside(x, y, q.sx, q.by, q.bw, q.bh)) { o->mode = OSD_NONE; return OSD_RES_QUIT; }
        if ((b == 2 && inside(x, y, q.nx, q.by, q.bw, q.bh)) ||
            (b == 3 && !inside(x, y, q.x, q.y, q.w, q.h))) {
            o->mode = OSD_NONE;
            return OSD_RES_CLOSED;
        }
        return OSD_RES_NONE;
    }

    case OSD_BROWSER: {
        BrGeom g;
        br_geom(o, &g);
        if (o->scroll < 0) br_show_sel(o);
        if (ev == OSD_PTR_DOWN) {
            o->ptr_scroll0 = o->scroll;
            if (inside(x, y, g.cx, g.cy, g.cw, g.ch)) o->ptr_btn = 1;
            else o->ptr_row = br_row_at(o, x, y);
            return OSD_RES_NONE;
        }
        if (ev == OSD_PTR_MOVE) {
            int dy = y - o->ptr_y0, dx = x - o->ptr_x0;
            if (!o->ptr_drag && !o->ptr_btn &&
                (dy > TDRAG || dy < -TDRAG || dx > 3 * TDRAG || dx < -3 * TDRAG)) {
                o->ptr_drag = 1;
                o->ptr_row = -1;
            }
            if (o->ptr_drag) {
                o->scroll = o->ptr_scroll0 - dy;
                br_clamp_scroll(o);
            }
            return OSD_RES_NONE;
        }
        /* soltou */
        int btn = o->ptr_btn, row = o->ptr_row, drag = o->ptr_drag;
        ptr_reset(o);
        if (drag) return OSD_RES_NONE;
        if (btn == 1) {
            if (inside(x, y, g.cx, g.cy, g.cw, g.ch)) { o->mode = OSD_NONE; return OSD_RES_CLOSED; }
            return OSD_RES_NONE;
        }
        /* toque curto na mesma linha: abre (arquivo) ou entra (diretorio) */
        if (row >= 0 && row == br_row_at(o, x, y)) {
            o->sel = row;
            return browser_key(o, OSDK_ENTER, 0, path, pathlen);
        }
        return OSD_RES_NONE;
    }
    default:
        return OSD_RES_NONE;
    }
}

void osd_scroll(Osd *o, int dy)
{
    if (o->mode != OSD_BROWSER) return;
    if (o->scroll < 0) br_show_sel(o);
    o->scroll += dy;
    br_clamp_scroll(o);
}

int osd_browser_find(Osd *o, const char *prefix, int *x, int *y)
{
    if (o->mode != OSD_BROWSER || o->cv_h <= 0) return -1;
    size_t n = strlen(prefix);
    for (int i = 0; i < o->count; i++) {
        if (strncmp(o->entries[i].name, prefix, n)) continue;
        BrGeom g;
        br_geom(o, &g);
        if (o->scroll < 0) br_show_sel(o);
        int top = i * TROW_H, view = g.ly1 - g.ly0;
        if (top < o->scroll || top + TROW_H > o->scroll + view) {
            o->scroll = top - (view - TROW_H) / 2;
            br_clamp_scroll(o);
        }
        *x = g.lx + g.lw / 2;
        *y = g.ly0 + top - o->scroll + TROW_H / 2;
        return 0;
    }
    return -1;
}
