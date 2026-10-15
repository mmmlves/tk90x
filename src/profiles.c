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
     * Controles de tanque: 5/8 giram, 7 anda para a frente, 6 atira (o
     * "fireball" do 3D Game Maker) e 0 pula (para a frente se andando). */
    { 0x78eb972bu, "Deep Blue", JOYMODE_CURSOR, NOKEYS,
      "lados giram, cima anda", "baixo atira; FOGO inicia e pula" },
    /* 02 Frank N Stein Re-booted: "Z LEFT, X RIGHT, M ACTIVATE/JUMP", S comeca.
     * O jogo so le Z, X e M (sem cima/baixo: os postes descem sozinhos); M so
     * age em molas e alavancas. S no BAIXO (que o jogo nao usa) comeca a
     * partida sem abrir o teclado; no jogo, S nao faz nada. No FOGO nao da:
     * com S e M juntos o titulo nao comeca. */
    { 0x1cbcdc75u, "Frank N Stein Re-booted", JOYMODE_KEYS,
      KEYS(ZXK_X, ZXK_Z, ZXK_S, ZXK_M, ZXK_M, NK),
      "cima ou FOGO aciona molas", "BAIXO inicia; nao ha pulo livre" },
    /* 03 Lala Prologue: "O = LEFT, P = RIGHT, Q - SPACE = JUMP" (manual); A nao
     * e usado; qualquer tecla comeca. */
    { 0x8bd73d02u, "Lala Prologue", JOYMODE_QAOP, NOKEYS,
      "cima ou FOGO pula", "FOGO inicia; diagonal pula de lado" },
    /* 04 A Fuga do Castelo Corvoalto: le o Sinclair (6-0) sem escolher nada.
     * (Na tela de titulo, 2 troca o jogo para Cursor: 1 volta.) */
    { 0x7e07d785u, "Castelo Corvoalto", JOYMODE_SINCLAIR1, NOKEYS,
      "FOGO lanca faisca", "FOGO inicia" },
    /* 05 Bruce Lee: teclado ja escolhido; Q A O P, Z a M soco/chute, ENTER
     * comeca (e pausa no jogo, por isso nao e o fogo). Durante os creditos o
     * M (FOGO) pula para o menu; no menu, A (baixo) troca 1/2 jogadores. */
    { 0x31ee64aau, "Bruce Lee", JOYMODE_KEYS,
      KEYS(ZXK_P, ZXK_O, ZXK_A, ZXK_Q, ZXK_M, NK),
      "FOGO golpeia, cima pula", "FOGO, TECLADO > ENTER, TECLADO" },
    /* 06 A Selva de Verdemor: le o Sinclair (6-0) sem escolher nada. */
    { 0x16e905f6u, "Selva de Verdemor", JOYMODE_SINCLAIR1, NOKEYS,
      "FOGO sopra o dardo", "FOGO inicia" },
    /* 06 extra Doomskulle: "2 KEYBOARD" ja escolhido, teclas O P A Q e
     * espaco (tabela do jogo em FEB9); 0 comeca e no jogo nao faz nada, por
     * isso vai junto no FOGO. Espaco andando atira; parado pega/larga. */
    { 0x568dd515u, "Doomskulle", JOYMODE_KEYS,
      KEYS(ZXK_P, ZXK_O, ZXK_A, ZXK_Q, ZXK_SPACE, ZXK_0),
      "andando, FOGO atira", "FOGO inicia; parado, FOGO pega" },
    /* 07 Sgt. Helmet: 1 = teclado (Q A O P, espaco atira) e comeca. */
    { 0xaed97757u, "Sgt. Helmet", JOYMODE_QAOP, NOKEYS,
      "FOGO atira (um por toque)", "TECLADO > 1, depois TECLADO" },
    /* 08 Dizzy: Z esq, X dir, espaco pula (e comeca), ENTER pega/usa. */
    { 0x2c09a879u, "Dizzy", JOYMODE_KEYS,
      KEYS(ZXK_X, ZXK_Z, NK, ZXK_SPACE, ZXK_ENTER, NK),
      "cima pula, FOGO pega/usa", "CIMA (pular) inicia" },
    /* 09 Green Beret: o teclado nao tem teclas padrao (pede para definir);
     * 4 = Sinclair (6-0) e FOGO comeca. 0 e a faca; com joystick, a arma
     * especial (lanca-chamas, granadas, bazuca) e qualquer tecla fora da
     * fileira de cima: o espaco vai junto no FOGO. */
    { 0x75f28a0au, "Green Beret", JOYMODE_KEYS,
      KEYS(ZXK_7, ZXK_6, ZXK_8, ZXK_9, ZXK_0, ZXK_SPACE),
      "FOGO faca + arma especial", "TECLADO > 4, TECLADO, FOGO" },
    /* 10 Boulder Dash: o teclado padrao tem E/Q/7 cima, F/K/6 baixo,
     * M/X/5 esq., SymShift/C/8 dir. e N/V/B/0 fogo. Letras e nao o Cursor: na
     * tela SELECT JOYSTICK TYPE (1-6) o 5 escolheria "FULLER" e travaria o
     * joystick. FOGO + direcao cava/pega sem andar. */
    { 0x05e192a7u, "Boulder Dash", JOYMODE_KEYS,
      KEYS(ZXK_C, ZXK_X, ZXK_K, ZXK_E, ZXK_N, NK),
      "FOGO+direcao cava parado", "FOGO; em PRESS FIRE, FOGO" },
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
