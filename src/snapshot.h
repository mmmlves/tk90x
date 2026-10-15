/*
 * snapshot.h - Snapshots 48K (.SNA e .Z80 v1/v2/v3) e a "fita virtual",
 * que transforma um snapshot ou um binario cru numa fita TAP padrao que a
 * ROM carrega de verdade (Program: / Bytes: / tela sendo desenhada).
 *
 * Fita virtual de snapshot (vtape_from_snapshot):
 *   bloco 0: cabecalho Program "<nome>" com autostart na linha 10
 *   bloco 1: programa BASIC:  10 LOAD ""CODE
 *   bloco 2: cabecalho Bytes "<nome>" start=16384 len=49152
 *   bloco 3: dados = os 48K de RAM do snapshot (16384..65535)
 * A ROM carrega os 48K por cima de si mesma (variaveis de sistema e pilha
 * inclusas); o LD-BYTES so usa registradores, entao funciona ate o fim do
 * bloco. Quando a maquina detecta que o bloco 3 terminou e a CPU chegou a
 * SA/LD-RET (0x053F), ela aplica o snapshot (snapshot_apply) e o jogo segue
 * exatamente do ponto salvo.
 */
#ifndef TK_SNAPSHOT_H
#define TK_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Snapshot {
    uint8_t  ram[49152];           /* 0x4000..0xFFFF */
    uint16_t af, bc, de, hl;
    uint16_t af_, bc_, de_, hl_;
    uint16_t ix, iy, sp, pc;
    uint16_t memptr;
    uint8_t  i, r;
    uint8_t  iff1, iff2, im;
    uint8_t  border;               /* 0..7 */
    uint8_t  halted;
    uint32_t tstates;              /* posicao no quadro, se conhecida (Z80 v3); senao 0 */
    uint8_t  issue2;               /* Z80: bit 2 do byte 29 */
    uint8_t  joystick;             /* Z80: bits 6-7 do byte 29 (0 cursor,1 kempston,2 sinclair2,3 sinclair1) */
} Snapshot;

typedef enum { SNAP_UNKNOWN = 0, SNAP_SNA, SNAP_Z80 } SnapFormat;

/* Detecta formato pela extensao (.sna/.z80, sem diferenciar maiusculas). */
SnapFormat snapshot_format_from_name(const char *path);

/* Carregam e validam. 0 = ok; -1 = erro (mensagem em portugues em err).
 * Snapshots 128K/+3/Timex sao recusados com mensagem clara. */
int snapshot_load_file(const char *path, Snapshot *s, char *err, size_t errlen);
int snapshot_load_sna(const uint8_t *data, size_t len, Snapshot *s, char *err, size_t errlen);
int snapshot_load_z80(const uint8_t *data, size_t len, Snapshot *s, char *err, size_t errlen);

/* Gravam (Z80 v3 comprimido / SNA 48K). 0 = ok. */
int snapshot_save_file(const char *path, const Snapshot *s, char *err, size_t errlen);

/* --- Fita virtual --- (o chamador libera *out com free()) */

/* Snapshot -> TAP descrito acima. name: ate 10 caracteres (completa com espacos). */
int vtape_from_snapshot(const Snapshot *s, const char *name, uint8_t **out, size_t *outlen);

/* Indice do bloco de dados de 48K na fita gerada por vtape_from_snapshot. */
#define VTAPE_SNAPSHOT_DATA_BLOCK 3

/* Binario cru -> TAP com carregador BASIC:
 *   10 CLEAR <org-1>: LOAD ""CODE: RANDOMIZE USR <exec>
 * e um bloco Bytes start=org len=len. Se org+len > 65536 ou org < 16384, erro (-1).
 * Se org-1 < 24000 o "CLEAR <org-1>:" e omitido (a ROM daria "M RAMTOP no good";
 * ex.: codigo no buffer da impressora em 23296). len == 0 tambem e erro. */
int vtape_from_binary(const uint8_t *data, size_t len, uint16_t org, uint16_t exec,
                      const char *name, uint8_t **out, size_t *outlen);

/* Tela .SCR (6912 bytes) -> TAP:  10 LOAD ""SCREEN$: PAUSE 0  + bloco Bytes 16384,6912.
 * 6144 bytes (so bitmap) tambem e aceito: atributos completados com 0x38. Outros tamanhos: -1. */
int vtape_from_screen(const uint8_t *scr, size_t len, const char *name,
                      uint8_t **out, size_t *outlen);

#ifdef __cplusplus
}
#endif
#endif
