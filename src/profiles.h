/*
 * profiles.h - Perfis de controle por jogo: para cada arquivo conhecido
 * (identificado pelo CRC32 dos bytes do arquivo), o modo de joystick que
 * funciona com os controles PADRAO do jogo, sem mexer no menu dele (as teclas
 * do jogo via JOYMODE_KEYS, QAOP, Cursor ou o Sinclair 1 do TK quando o jogo
 * ja le o joystick sem escolher nada), um resumo para o aviso "Controles: ..."
 * e uma dica de como comecar a partida so com os toques.
 *
 * Os perfis cobrem os jogos de games/ e games/extras/ (a pesquisa de cada um
 * esta em games/LEIAME.md, secao "Controles"). Independente de SDL.
 */
#ifndef TK_PROFILES_H
#define TK_PROFILES_H

#include <stddef.h>
#include <stdint.h>
#include "machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameProfile {
    uint32_t    crc;        /* CRC32 (zlib) do arquivo inteiro */
    const char *name;       /* nome curto do jogo */
    JoyMode     mode;       /* modo do joystick na sessao */
    JoyKeys     keys;       /* teclas (JOYMODE_KEYS; nos outros modos, vazio) */
    const char *summary;    /* resumo dos controles, ASCII, ate 28 caracteres (cabe no aviso) */
    const char *hint;       /* como comecar so com toques (NULL = nada a fazer) */
} GameProfile;

/* CRC32 (polinomio do zlib/PNG) de len bytes. */
uint32_t profile_crc32(const uint8_t *data, size_t len);

/* Perfil do arquivo com este CRC32, ou NULL. */
const GameProfile *profile_find(uint32_t crc);

/* Perfil do arquivo no disco (le o arquivo inteiro, ate 16 MB), ou NULL. */
const GameProfile *profile_for_file(const char *path);

/* 1 se o perfil tem teclas para o JOYMODE_KEYS. */
int profile_has_keys(const GameProfile *p);

/* Numero de perfis e acesso por indice (testes). */
int profile_count(void);
const GameProfile *profile_at(int i);

#ifdef __cplusplus
}
#endif
#endif
