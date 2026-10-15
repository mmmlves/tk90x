/* compat.h - Portabilidade Linux/Windows para os modulos do nucleo.
 * Caminhos sao sempre UTF-8; no Windows sao convertidos para UTF-16 (_wfopen),
 * assim nomes com acento funcionam nos dois sistemas. */
#ifndef TK_COMPAT_H
#define TK_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static inline FILE *tk_fopen(const char *path, const char *mode)
{
    wchar_t wpath[1024], wmode[16];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16)) return NULL;
    return _wfopen(wpath, wmode);
}
#else
static inline FILE *tk_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}
#endif

/* Le o arquivo inteiro para a memoria (malloc). Devolve NULL em erro.
 * max_len limita o tamanho aceito (0 = sem limite). */
static inline uint8_t *tk_read_file(const char *path, size_t *len, size_t max_len)
{
    FILE *f = tk_fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 65536;
            if (max_len && ncap > max_len + 1) ncap = max_len + 1;
            if (ncap <= cap) { free(buf); fclose(f); return NULL; }
            uint8_t *nb = (uint8_t *)realloc(buf, ncap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb; cap = ncap;
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (r == 0) break;
        if (max_len && n > max_len) { free(buf); fclose(f); return NULL; }
    }
    fclose(f);
    *len = n;
    return buf;
}

/* Decodifica um caractere UTF-8 em *p (avanca *p); devolve o codigo. */
static inline unsigned tk_utf8_next(const unsigned char **p)
{
    const unsigned char *s = *p;
    unsigned c = *s++, cp = c;
    int extra = 0;
    if (c >= 0x80) {
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else cp = 0xFFFD;                  /* byte invalido como inicio */
        for (int i = 0; i < extra && (*s & 0xC0) == 0x80; i++, s++)
            cp = (cp << 6) | (*s & 0x3F);
    }
    *p = s;
    return cp;
}

/* Codigo Unicode -> ASCII imprimivel (32..126): Latin-1 U+00C0..U+00FF vira a
 * letra sem acento (A de A-agudo, c de c-cedilha...); o resto que nao e ASCII
 * imprimivel vira '?'. */
static inline char tk_ascii_fold(unsigned cp)
{
    static const char latin1_fold[] =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUY?s"
        "aaaaaaaceeeeiiiidnooooo/ouuuuy?y";
    if (cp >= 32 && cp < 127) return (char)cp;
    if (cp >= 0xC0 && cp <= 0xFF) return latin1_fold[cp - 0xC0];
    return '?';
}

/* Extensao em minusculas (sem o ponto), ou "" */
static inline void tk_file_ext(const char *path, char *out, size_t outlen)
{
    const char *dot = strrchr(path, '.');
    const char *sep1 = strrchr(path, '/'), *sep2 = strrchr(path, '\\');
    const char *sep = sep1 > sep2 ? sep1 : sep2;
    out[0] = 0;
    if (!dot || (sep && dot < sep) || outlen == 0) return;
    size_t i = 0;
    for (dot++; *dot && i + 1 < outlen; dot++, i++)
        out[i] = (char)((*dot >= 'A' && *dot <= 'Z') ? *dot + 32 : *dot);
    out[i] = 0;
}

#endif
