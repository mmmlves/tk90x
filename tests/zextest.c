/*
 * zextest.c - Executa ZEXDOC / ZEXALL (programas CP/M) no nucleo Z80.
 *
 * O .com e carregado em 0x0100. Shim do BDOS em 0x0005 (C=2: caractere em E;
 * C=9: cadeia em DE terminada por '$'); salto para 0x0000 encerra.
 * Ao final mostra o tempo, os T-states e os MHz emulados, e o resultado de
 * um benchmark curto com o gancho de contencao ligado (devolvendo 0).
 *
 * Uso: zextest arquivo.com [--contention]
 *   --contention  roda o teste inteiro com mem_contention/io_contention ligados
 * Codigo de saida: 0 se nenhum teste imprimiu "ERROR".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "z80.h"
#include "compat.h"

static uint8_t mem[65536];

static uint8_t z_read(void *ctx, uint16_t a) { return mem[a]; }
static void z_write(void *ctx, uint16_t a, uint8_t v) { mem[a] = v; }
static uint8_t z_in(void *ctx, uint16_t p) { return 0xFF; }
static void z_out(void *ctx, uint16_t p, uint8_t v) { }
static unsigned z_cont(void *ctx, uint16_t a, uint32_t t) { return 0; }
static unsigned z_iocont(void *ctx, uint16_t p, uint32_t t) { return 0; }

/* deteccao de "ERROR" na saida */
static const char *err_word = "ERROR";
static int err_pos, err_count;

static void out_char(int ch)
{
    putchar(ch);
    if (ch == '\n') fflush(stdout);
    if (ch == err_word[err_pos]) {
        if (!err_word[++err_pos]) { err_count++; err_pos = 0; }
    } else {
        err_pos = (ch == err_word[0]) ? 1 : 0;
    }
}

static double now_sec(void)
{
#if defined(CLOCK_MONOTONIC) && !defined(_WIN32)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

static int load_com(const char *path)
{
    size_t len;
    uint8_t *buf = tk_read_file(path, &len, 65536 - 0x100);
    if (!buf) return 0;
    memset(mem, 0, sizeof(mem));
    memcpy(mem + 0x100, buf, len);
    free(buf);
    mem[0x0000] = 0x76;     /* HALT (nunca executado: saida detectada antes) */
    mem[0x0005] = 0xC9;     /* RET: o BDOS e tratado antes de executar */
    mem[0x0006] = 0x00;     /* topo da memoria para o SP (lido pelo zex em 0x0006) */
    mem[0x0007] = 0xF0;
    return 1;
}

/* Roda o programa CP/M; devolve T-states totais */
static uint64_t run_cpm(Z80 *cpu, int echo, uint64_t limit)
{
    uint64_t total = 0;
    for (;;) {
        uint16_t pc = cpu->pc;
        if (pc == 0x0005) {
            uint8_t fn = Z80_C(cpu);
            if (echo) {
                if (fn == 2) {
                    out_char(Z80_E(cpu));
                } else if (fn == 9) {
                    uint16_t a = cpu->de;
                    int n = 0;
                    while (mem[a] != '$' && n < 65536) { out_char(mem[a]); a++; n++; }
                }
            }
        } else if (pc == 0x0000) {
            break;
        }
        /* o relogio de 32 bits e rebaseado como faria a maquina */
        if (cpu->tstates > 0x40000000u) { total += cpu->tstates; cpu->tstates = 0; }
        z80_step(cpu);
        if (limit && total + cpu->tstates >= limit) break;
    }
    return total + cpu->tstates;
}

int main(int argc, char **argv)
{
    Z80 cpu;
    Z80Bus bus;
    const char *path = NULL;
    int with_cont = 0, k;
    double t0, el;
    uint64_t ts;

    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--contention")) with_cont = 1;
        else path = argv[k];
    }
    if (!path) {
        fprintf(stderr, "uso: %s zexdoc.com|zexall.com [--contention]\n", argv[0]);
        return 2;
    }
    if (!load_com(path)) {
        fprintf(stderr, "erro: nao consegui ler %s\n", path);
        return 2;
    }

    memset(&bus, 0, sizeof(bus));
    bus.read = z_read;
    bus.write = z_write;
    bus.in = z_in;
    bus.out = z_out;
    if (with_cont) {
        bus.mem_contention = z_cont;
        bus.io_contention = z_iocont;
    }
    z80_init(&cpu, &bus);
    cpu.pc = 0x0100;
    cpu.sp = 0xF000;
    cpu.tstates = 0;

    printf("== %s%s ==\n", path, with_cont ? " (com gancho de contencao)" : "");
    t0 = now_sec();
    ts = run_cpm(&cpu, 1, 0);
    el = now_sec() - t0;
    if (el <= 0) el = 1e-9;
    printf("\n-- %s: %llu T-states em %.2f s = %.1f MHz emulados (%.0fx um Z80 de 3,5 MHz)\n",
           path, (unsigned long long)ts, el, ts / el / 1e6, ts / el / 3.5e6);

    /* benchmark curto com o gancho de contencao ligado (custo do gancho) */
    if (!with_cont) {
        uint64_t lim = 2000000000ull;
        uint64_t ts2;
        double el2;
        load_com(path);
        bus.mem_contention = z_cont;
        bus.io_contention = z_iocont;
        z80_init(&cpu, &bus);
        cpu.pc = 0x0100; cpu.sp = 0xF000; cpu.tstates = 0;
        t0 = now_sec();
        ts2 = run_cpm(&cpu, 0, lim);
        el2 = now_sec() - t0;
        if (el2 <= 0) el2 = 1e-9;
        printf("-- benchmark com gancho de contencao (retornando 0): %llu T-states em %.2f s = %.1f MHz\n",
               (unsigned long long)ts2, el2, ts2 / el2 / 1e6);
    }

    if (err_count) {
        printf("FALHOU: %d teste(s) com ERROR\n", err_count);
        return 1;
    }
    printf("OK: nenhum ERROR\n");
    return 0;
}
