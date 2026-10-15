/*
 * z80.h - Nucleo Z80 (Zilog Z80A) com instrucoes e flags nao documentadas,
 * registrador MEMPTR (WZ) e contagem de T-states por ciclo de maquina.
 *
 * Contrato de temporizacao (estilo Fuse):
 *   - O nucleo e quem soma os T-states em cpu->tstates.
 *   - Antes de cada acesso a memoria, se bus.mem_contention != NULL, o nucleo
 *     soma o atraso devolvido por mem_contention(ctx, addr, tstates_atual).
 *     Isso vale tambem para os ciclos internos "sem MREQ" que o Z80 real
 *     coloca no barramento de endereco (ex.: os 5 T-states extras de JR,
 *     cada um contado como contend(addr,1)).
 *   - Busca de opcode (M1): contend(PC) + 4 T-states, incrementa R (7 bits).
 *   - Leitura/escrita de memoria: contend(addr) + 3 T-states.
 *   - Ciclo de E/S: o nucleo soma io_contention(ctx, port, t) (se != NULL)
 *     mais 4 T-states; o callback in/out e chamado depois de somados
 *     contencao + 3 T-states (ponto T3) e o ultimo T-state e somado depois.
 *   - Os callbacks read/write/in/out NAO mexem em tstates.
 *
 * HALT: igual ao Fuse. PC permanece apontando para a instrucao HALT;
 * cada z80_step() enquanto halted executa um "NOP" de 4 T-states (com M1 e
 * incremento de R). Ao aceitar interrupcao, PC avanca 1 antes de empilhar.
 *
 * SCF/CCF: flags 3 e 5 = ((Q ^ F) | A) & (3|5), onde Q e o F deixado pela
 * instrucao anterior se ela mexeu em flags, senao 0 (Z80 NMOS; comportamento
 * Fuse >= 1.6, necessario para passar na suite de testes do Fuse).
 *
 * Durante um callback (read/write/in/out/contencao) so cpu->tstates esta
 * atualizado; os demais registradores so sao gravados no fim de z80_step().
 */
#ifndef TK_Z80_H
#define TK_Z80_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bits do registrador F */
#define Z80_FLAG_C  0x01
#define Z80_FLAG_N  0x02
#define Z80_FLAG_PV 0x04
#define Z80_FLAG_3  0x08
#define Z80_FLAG_H  0x10
#define Z80_FLAG_5  0x20
#define Z80_FLAG_Z  0x40
#define Z80_FLAG_S  0x80

typedef struct Z80Bus {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);                 /* leitura de memoria (inclui busca de opcode) */
    void    (*write)(void *ctx, uint16_t addr, uint8_t value);  /* escrita de memoria */
    uint8_t (*in)(void *ctx, uint16_t port);                    /* IN: port = endereco completo de 16 bits */
    void    (*out)(void *ctx, uint16_t port, uint8_t value);    /* OUT */
    /* Opcionais (NULL = sem contencao). Devolvem T-states de espera. */
    unsigned (*mem_contention)(void *ctx, uint16_t addr, uint32_t tstates);
    unsigned (*io_contention)(void *ctx, uint16_t port, uint32_t tstates);
} Z80Bus;

typedef struct Z80 {
    /* Pares de registradores (byte alto = primeiro registrador: A em AF, B em BC...) */
    uint16_t af, bc, de, hl;
    uint16_t af_, bc_, de_, hl_;   /* conjunto alternativo */
    uint16_t ix, iy, sp, pc;
    uint16_t memptr;               /* WZ */
    uint8_t  i;
    uint8_t  r;                    /* R completo; o bit 7 so muda por LD R,A */
    uint8_t  iff1, iff2;
    uint8_t  im;                   /* 0, 1 ou 2 */
    uint8_t  halted;
    uint8_t  ei_delay;             /* 1 logo apos EI (ou prefixo DD/FD isolado): interrupcao nao e aceita antes da proxima instrucao */
    uint8_t  q;                    /* registrador interno Q (F da ultima instrucao que mexeu em flags); usado por SCF/CCF */
    uint8_t  iff2_read;            /* 1 logo apos LD A,I / LD A,R: interrupcao aceita agora zera P/V (NMOS) */
    uint8_t  pending_prefix;       /* 0, ou DD/FD em PC cujo M1 ja foi feito (cadeia de prefixos); zere ao carregar estado */
    uint32_t tstates;              /* contador de T-states (a maquina o rebaseia a cada quadro) */
    Z80Bus   bus;
} Z80;

/* Acessores de 8 bits */
#define Z80_A(c)  ((uint8_t)((c)->af >> 8))
#define Z80_F(c)  ((uint8_t)((c)->af & 0xFF))
#define Z80_B(c)  ((uint8_t)((c)->bc >> 8))
#define Z80_C(c)  ((uint8_t)((c)->bc & 0xFF))
#define Z80_D(c)  ((uint8_t)((c)->de >> 8))
#define Z80_E(c)  ((uint8_t)((c)->de & 0xFF))
#define Z80_H(c)  ((uint8_t)((c)->hl >> 8))
#define Z80_L(c)  ((uint8_t)((c)->hl & 0xFF))

/* Inicializa (zera tudo, copia o barramento) e faz reset. */
void z80_init(Z80 *cpu, const Z80Bus *bus);

/* Reset: PC=0, I=R=0, IFF1=IFF2=0, IM 0, AF=SP=0xFFFF, halted=0, tstates inalterado. */
void z80_reset(Z80 *cpu);

/* Executa uma instrucao completa (com todos os prefixos CB/DD/ED/FD/DDCB/FDCB).
 * Uma sequencia de prefixos DD/FD conta como instrucoes separadas (como no Z80 real).
 * Devolve os T-states consumidos (inclusive contencao). */
int z80_step(Z80 *cpu);

/* Pedido de interrupcao mascaravel. Se IFF1==0 ou ei_delay, nao aceita e devolve 0.
 * Caso aceite: sai de HALT, IFF1=IFF2=0, R++, e executa conforme IM:
 *   IM0/IM1: RST 38h, 13 T-states (7 + 2 escritas de 3)
 *   IM2: le vetor em (I<<8)|bus_value, 19 T-states
 * memptr = endereco de destino. Devolve os T-states consumidos. */
int z80_interrupt(Z80 *cpu, uint8_t bus_value);

/* Interrupcao nao mascaravel (11 T-states, salta para 0x66, IFF1=0). */
int z80_nmi(Z80 *cpu);

#ifdef __cplusplus
}
#endif
#endif
