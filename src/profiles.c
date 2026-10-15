/*
 * profiles.c - Perfis de controle dos jogos de games/ e games/extras/.
 *
 * Cada perfil foi levantado sem mexer no menu do jogo: as telas de titulo e
 * de opcoes, as instrucoes (Spectrum Computing / ZXDB) e testes no emulador
 * (teclas seguradas e posicao do jogador lida na memoria; nos casos
 * duvidosos, as portas lidas pela rotina de teclado do jogo). O resumo
 * completo esta em games/LEIAME.md, secao "Controles".
 *
 * Indice das teclas em JoyKeys: direita, esquerda, baixo, cima, fogo (ordem
 * dos bits JOY_*). NK = nenhuma.
 */
#include "profiles.h"

#include <stdlib.h>

#include "compat.h"

#define NK ZXK_COUNT
#define NOKEYS { { { NK, NK }, { NK, NK }, { NK, NK }, { NK, NK }, { NK, NK } } }
/* KEYS(dir, esq, baixo, cima, fogo, fogo2) */
#define KEYS(r, l, d, u, f, f2) { { { r, NK }, { l, NK }, { d, NK }, { u, NK }, { f, f2 } } }

static const GameProfile g_profiles[] = {
    /* 01 Deep Blue (3D Game Maker): menu "1 CURSOR KEYS" ja escolhido; 0 = RUN.
     * Esquerda/direita giram, cima anda. */
    { 0x78eb972bu, "Deep Blue", JOYMODE_CURSOR, NOKEYS,
      "Cursor 5-8 (cima anda)", "FOGO (0) inicia" },
    /* 02 Frank N Stein Re-booted: "Z LEFT, X RIGHT, M ACTIVATE/JUMP", S comeca.
     * O jogo so le Z, X e M (sem cima/baixo: os postes descem sozinhos). */
    { 0x1cbcdc75u, "Frank N Stein Re-booted", JOYMODE_KEYS,
      KEYS(ZXK_X, ZXK_Z, NK, ZXK_M, ZXK_M, NK),
      "Z X, cima/fogo = M pula", "TECLADO > S para jogar" },
    /* 03 Lala Prologue: Q A O P (Q pula) e espaco; qualquer tecla comeca. */
    { 0x8bd73d02u, "Lala Prologue", JOYMODE_QAOP, NOKEYS,
      "Q A O P (cima pula)", "FOGO inicia" },
    /* 04 A Fuga do Castelo Corvoalto: le o Sinclair (6-0) sem escolher nada. */
    { 0x7e07d785u, "Castelo Corvoalto", JOYMODE_SINCLAIR1, NOKEYS,
      "Sinclair 6-0 do TK", "FOGO inicia" },
    /* 05 Bruce Lee: teclado ja escolhido; Q A O P, Z a M soco/chute, ENTER
     * comeca (e pausa no jogo, por isso nao e o fogo). */
    { 0x31ee64aau, "Bruce Lee", JOYMODE_KEYS,
      KEYS(ZXK_P, ZXK_O, ZXK_A, ZXK_Q, ZXK_M, NK),
      "Q A O P, fogo = M (golpe)", "TECLADO > ENTER para jogar" },
    /* 06 A Selva de Verdemor: le o Sinclair (6-0) sem escolher nada. */
    { 0x16e905f6u, "Selva de Verdemor", JOYMODE_SINCLAIR1, NOKEYS,
      "Sinclair 6-0 do TK", "FOGO inicia" },
    /* 06 extra Doomskulle: "2 KEYBOARD" ja escolhido, teclas O P A Q e
     * espaco (tabela do jogo em FEB9); 0 comeca. */
    { 0x568dd515u, "Doomskulle", JOYMODE_QAOP, NOKEYS,
      "Q A O P, fogo = espaco", "TECLADO > 0 para jogar" },
    /* 07 Sgt. Helmet: 1 = teclado (Q A O P, espaco atira) e comeca. */
    { 0xaed97757u, "Sgt. Helmet", JOYMODE_QAOP, NOKEYS,
      "Q A O P, fogo = espaco", "TECLADO > 1 para jogar" },
    /* 08 Dizzy: Z esq, X dir, espaco pula (e comeca), ENTER pega/usa. */
    { 0x2c09a879u, "Dizzy", JOYMODE_KEYS,
      KEYS(ZXK_X, ZXK_Z, NK, ZXK_SPACE, ZXK_ENTER, NK),
      "Z X, cima pula, fogo pega", "CIMA (pular) inicia" },
    /* 09 Green Beret: o teclado nao tem teclas padrao (pede para definir);
     * 4 = Sinclair (6-0) e FOGO comeca. */
    { 0x75f28a0au, "Green Beret", JOYMODE_SINCLAIR1, NOKEYS,
      "Sinclair 6-0 do TK", "TECLADO > 4, depois FOGO" },
    /* 10 Boulder Dash: o teclado padrao inclui 5-8 e 0 (cursor). */
    { 0x05e192a7u, "Boulder Dash", JOYMODE_CURSOR, NOKEYS,
      "Cursor 5-8, fogo 0", "FOGO inicia (2 vezes)" },
};
#define PROFILES_N ((int)(sizeof g_profiles / sizeof g_profiles[0]))

uint32_t profile_crc32(const uint8_t *data, size_t len)
{
    static uint32_t tab[256];
    static int ready;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[n] = c;
        }
        ready = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = tab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

const GameProfile *profile_find(uint32_t crc)
{
    for (int i = 0; i < PROFILES_N; i++)
        if (g_profiles[i].crc == crc) return &g_profiles[i];
    return NULL;
}

const GameProfile *profile_for_file(const char *path)
{
    size_t len = 0;
    if (!path) return NULL;
    uint8_t *d = tk_read_file(path, &len, 1u << 24);
    if (!d) return NULL;
    uint32_t crc = profile_crc32(d, len);
    free(d);
    return profile_find(crc);
}

int profile_has_keys(const GameProfile *p)
{
    if (!p) return 0;
    for (int b = 0; b < JOYKEYS_DIRS; b++)
        for (int i = 0; i < 2; i++)
            if (p->keys.key[b][i] < ZXK_COUNT) return 1;
    return 0;
}

int profile_count(void) { return PROFILES_N; }

const GameProfile *profile_at(int i)
{
    return i >= 0 && i < PROFILES_N ? &g_profiles[i] : NULL;
}
