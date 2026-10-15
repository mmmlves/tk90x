/*
 * tape.h - Gravador de fita virtual: le TAP e TZX e gera o sinal EAR
 * (sequencia de pulsos) exatamente como uma fita real, em T-states de
 * um Z80 a 3,5 MHz. A ROM carrega o programa pelo seu proprio LD-BYTES,
 * com listras na borda e som; a "velocidade" vem de o emulador rodar em
 * turbo enquanto a fita toca.
 *
 * Relogio: todas as marcas de tempo sao em T-states relativos ao quadro
 * corrente (o mesmo relogio de cpu->tstates). No fim de cada quadro a
 * maquina chama tape_frame_end(t, frame_len) para rebasear.
 *
 * Nivel do sinal: o nivel inicial ao dar play e 0 (baixo). Cada pulso
 * alterna o nivel no seu inicio (regras TZX: blocos 0x2B "set signal level"
 * e pausas forcam nivel). Uma pausa > 0 ms termina o pulso anterior apos
 * 1 ms e mantem nivel baixo pelo restante, conforme especificacao TZX.
 * Modelo completo: docs/research/formats.md secao 3. Pausa 0 num bloco de
 * dados = proximo bloco imediato (nao para); so 0x20 com 0 ms e 0x2A param
 * (se o ultimo pulso ainda nao terminou, antes 1 ms que o encerra, como no
 * fim da fita).
 * Fim da fita: pausa implicita de 1 ms (garante a borda que encerra o
 * ultimo pulso) e entao a fita para com tape_at_end() = 1.
 */
#ifndef TK_TAPE_H
#define TK_TAPE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAPE_NO_EDGE 0xFFFFFFFFu

typedef struct Tape Tape;

Tape *tape_new(void);
void  tape_free(Tape *t);

/* Clock da CPU emulada em Hz (padrao 3500000). As duracoes dos arquivos
 * TAP/TZX sao em T-states de 3,5 MHz (tempo real da fita); internamente
 * cada duracao e convertida: t_maquina = t_arquivo * cpu_hz / 3500000
 * (com acumulador de fracao, sem deriva). O TK90X roda a 3575611 Hz. */
void  tape_set_cpu_hz(Tape *t, uint32_t cpu_hz);

/* Ejeta a fita atual (se houver). */
void  tape_eject(Tape *t);

/* Carrega TAP ou TZX (detecta pela assinatura "ZXTape!\x1A"; senao trata como TAP).
 * Devolve 0 em sucesso; em erro devolve -1 e escreve mensagem em err (portugues). */
int   tape_load_file(Tape *t, const char *path, char *err, size_t errlen);
int   tape_load_tap_mem(Tape *t, const uint8_t *data, size_t len, char *err, size_t errlen);
int   tape_load_tzx_mem(Tape *t, const uint8_t *data, size_t len, char *err, size_t errlen);
/* Formato de fita conhecido mas nao suportado (PZX, CSW avulso)? Devolve 1 e
 * escreve a mensagem em err; 0 se nao for. */
int   tape_sniff_unsupported(const uint8_t *data, size_t len, char *err, size_t errlen);

int   tape_is_loaded(const Tape *t);
void  tape_play(Tape *t, uint32_t now);   /* comeca a tocar a partir do bloco atual no instante 'now' */
void  tape_stop(Tape *t);
void  tape_rewind(Tape *t);               /* volta ao bloco 0 (parada) */
int   tape_is_playing(const Tape *t);
int   tape_at_end(const Tape *t);         /* passou do ultimo bloco */

/* Instante (T-states, relogio do quadro) da proxima mudanca de nivel,
 * ou TAPE_NO_EDGE se parada/sem fita. Chamada barata (so devolve campo).
 * Pode ser ocasionalmente uma fronteira sem troca de nivel (ex.: inicio
 * de bloco ou parada da fita); basta chamar tape_advance nesse instante.
 * Se ja passou (atrasado), devolve 0. */
uint32_t tape_next_edge(const Tape *t);

/* Processa todas as transicoes com instante <= now e devolve o nivel EAR
 * atual (0/1). Pode parar a fita sozinha (pausa 0, bloco 0x20 com 0 ms,
 * bloco 0x2A "stop if 48K", fim da fita). */
int   tape_advance(Tape *t, uint32_t now);

/* Nivel atual sem avancar. */
int   tape_level(const Tape *t);

/* Rebaseia relogios internos subtraindo frame_len. */
void  tape_frame_end(Tape *t, uint32_t frame_len);

/* Informacao para a interface */
int   tape_block_count(const Tape *t);
int   tape_current_block(const Tape *t);
/* Descricao curta do bloco i (ex.: "Program: ROBOCOP", "Bytes: tela", "Dados 6914", "Turbo 49154").
 * Devolve string interna valida ate o proximo load. */
const char *tape_block_desc(const Tape *t, int i);
/* Progresso aproximado 0..1000 (pelo total de pulsos/tempo estimado da fita). */
int   tape_progress_permille(const Tape *t);
/* Tempo total estimado da fita em segundos reais (a 3,5 MHz). */
double tape_total_seconds(const Tape *t);

/* --- Suporte ao carregamento instantaneo ("flash load", armadilha na ROM) ---
 * Se o bloco atual (ou o proximo nao vazio) e um bloco de dados padrao
 * (TAP ou TZX 0x10, ou 0x11 com temporizacao ROM), devolve ponteiro para
 * os bytes (flag+dados+checksum) e o tamanho, e avanca a fita para depois
 * dele. Caso contrario devolve 0 e nao mexe na fita. */
int   tape_take_rom_block(Tape *t, const uint8_t **data, size_t *len);

/* Marcador de bloco usado pela fita virtual de snapshots: devolve 1 se o
 * ultimo bloco de dados completamente tocado foi o de indice idx.
 * Definicao exata: "bloco de dados" = 0x10/0x11/0x12/0x13/0x14/0x15/0x18/0x19
 * ou bloco TAP. Ele conta como tocado no instante em que termina o seu
 * ultimo pulso, isto e, quando comeca a sua pausa (ou o bloco seguinte,
 * se a pausa for 0) - o mesmo instante da borda que a ROM usa para ler o
 * ultimo bit. tape_take_rom_block tambem marca o bloco que entregou.
 * Volta a 0 quando outro bloco de dados termina, e em rewind/eject/load. */
int   tape_block_finished(const Tape *t, int idx);

/* Avisos nao fatais do ultimo load (bloco truncado, bloco desconhecido
 * ignorado, CSW corrompido...) e da reproducao, separados por "; ".
 * Devolve "" se nao houver. String interna. */
const char *tape_warnings(const Tape *t);

#ifdef __cplusplus
}
#endif
#endif
