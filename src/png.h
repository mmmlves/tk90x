/* png.h - Gravador PNG minimo (RGB 8 bits, zlib sem compressao). */
#ifndef TK_PNG_H
#define TK_PNG_H

#include <stdint.h>

/* Grava pixels ARGB8888 (w*h, linha a linha) como PNG RGB. 0 = ok, -1 = erro. */
int png_write_argb(const char *path, const uint32_t *pixels, int w, int h);

#endif
