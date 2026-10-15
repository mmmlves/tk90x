/* png.c - Gravador PNG minimo: RGB 8 bits, fluxo zlib com blocos "stored". */
#include "png.h"
#include "compat.h"   /* tk_fopen: caminho UTF-8 tambem no Windows */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put32(tail, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
           fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

int png_write_argb(const char *path, const uint32_t *pixels, int w, int h)
{
    if (!crc_ready) crc_init();
    if (w <= 0 || h <= 0) return -1;

    /* Dados crus: por linha, 1 byte de filtro (0) + RGB */
    size_t row = (size_t)w * 3 + 1;
    size_t raw_len = row * (size_t)h;
    uint8_t *raw = malloc(raw_len);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *p = raw + row * (size_t)y;
        *p++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = pixels[(size_t)y * w + x];
            *p++ = (uint8_t)(c >> 16); *p++ = (uint8_t)(c >> 8); *p++ = (uint8_t)c;
        }
    }

    /* zlib: cabecalho + blocos stored de ate 65535 bytes + Adler-32 */
    size_t nblocks = raw_len / 65535 + 1;
    size_t z_len = 2 + raw_len + nblocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    if (!z) { free(raw); return -1; }
    size_t zp = 0, left = raw_len, pos = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    do {
        size_t n = left > 65535 ? 65535 : left;
        z[zp++] = (uint8_t)(left == n ? 1 : 0);
        z[zp++] = (uint8_t)n; z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)~n; z[zp++] = (uint8_t)(~n >> 8);
        memcpy(z + zp, raw + pos, n);
        zp += n; pos += n; left -= n;
    } while (left > 0);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    put32(z + zp, (b << 16) | a);
    zp += 4;
    free(raw);

    FILE *f = tk_fopen(path, "wb");
    if (!f) { free(z); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    int rc = fwrite(sig, 1, 8, f) == 8 ? 0 : -1;
    if (!rc) rc = write_chunk(f, "IHDR", ihdr, 13);
    if (!rc) rc = write_chunk(f, "IDAT", z, (uint32_t)zp);
    if (!rc) rc = write_chunk(f, "IEND", NULL, 0);
    if (fclose(f) != 0) rc = -1;
    free(z);
    return rc;
}
