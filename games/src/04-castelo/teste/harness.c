/* harness.c (Licença MIT - Projeto Emulador TK90X) - roteiro de teste para jogos no núcleo do emulador TK90X.
 * Uso: harness script.txt
 * Comandos (um por linha, '#' comenta):
 *   sym ARQ.sym               carrega símbolos do sjasmplus (NOME: EQU 0x....)
 *   load ARQ.tap              reset + LOAD "" digitado + fita (caminho original)
 *   untilpc LO HI MAXF        roda até PC ficar em [LO,HI] por 50 quadros seguidos
 *   frames N                  roda N quadros
 *   press TECLAS / release TECLAS / releaseall   (q a o p space enter 0-9 caps sym ...)
 *   hold TECLAS N             aperta, roda N quadros, solta
 *   shot ARQ.png              grava a tela
 *   poke END VAL / pokew END VAL / peek END / peekw END   (END = número ou símbolo[+n])
 *   expect END VAL            falha se mem[END] != VAL
 *   expectne END VAL          falha se mem[END] == VAL
 *   untilmem END VAL MAXF     roda até mem[END]==VAL
 *   echo TEXTO
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "machine.h"
#include "png.h"

static Machine *M;
static struct { char name[64]; unsigned v; } syms[4096];
static int nsyms;
static int fails;
static long total_frames;

static unsigned addr_of(const char *s)
{
    char buf[128];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    int off = 0;
    char *plus = strchr(buf, '+');
    if (plus) { *plus = 0; off = (int)strtol(plus + 1, NULL, 0); }
    if (isdigit((unsigned char)buf[0])) return (unsigned)strtol(buf, NULL, 0) + off;
    for (int i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, buf)) return syms[i].v + off;
    fprintf(stderr, "símbolo desconhecido: %s\n", buf);
    exit(2);
}

static int keycode(const char *k)
{
    static const char *names[ZXK_COUNT] = {
        "caps","z","x","c","v","a","s","d","f","g","q","w","e","r","t",
        "1","2","3","4","5","0","9","8","7","6","p","o","i","u","y",
        "enter","l","k","j","h","space","sym","m","n","b" };
    for (int i = 0; i < ZXK_COUNT; i++) if (!strcmp(names[i], k)) return i;
    fprintf(stderr, "tecla desconhecida: %s\n", k);
    exit(2);
}

static void keys(char *list, int pressed)
{
    char *save = NULL;
    for (char *t = strtok_r(list, ",", &save); t; t = strtok_r(NULL, ",", &save))
        machine_key(M, (ZXKey)keycode(t), pressed);
}

static void run(long n) { for (long i = 0; i < n; i++) { machine_run_frame(M); total_frames++; } }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: harness script\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 2; }
    static uint8_t rom[16384];
    machine_model_rom(MODEL_TK95, NULL, rom);
    M = calloc(1, sizeof *M);
    MachineConfig cfg = {0};
    cfg.model = MODEL_TK95;
    cfg.joymode = JOYMODE_SINCLAIR1;
    machine_init(M, rom, &cfg);
    char line[512];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!*p || *p == '#') continue;
        p[strcspn(p, "\r\n")] = 0;
        char cmd[64] = {0}, a1[256] = {0}, a2[256] = {0}, a3[256] = {0};
        sscanf(p, "%63s %255s %255s %255s", cmd, a1, a2, a3);
        if (!strcmp(cmd, "sym")) {
            FILE *s = fopen(a1, "r");
            if (!s) { perror(a1); return 2; }
            char l2[256];
            while (fgets(l2, sizeof l2, s) && nsyms < 4096) {
                char nm[128]; unsigned v;
                if (sscanf(l2, "%127[^:]: EQU 0x%x", nm, &v) == 2) {
                    strncpy(syms[nsyms].name, nm, 63);
                    syms[nsyms].v = v;
                    nsyms++;
                }
            }
            fclose(s);
        } else if (!strcmp(cmd, "load")) {
            char err[256] = {0};
            if (machine_open_file(M, a1, 32768, 0, 0, err, sizeof err) != 0) {
                fprintf(stderr, "erro ao abrir %s: %s\n", a1, err);
                return 2;
            }
        } else if (!strcmp(cmd, "untilpc")) {
            unsigned lo = addr_of(a1), hi = addr_of(a2);
            long max = strtol(a3, NULL, 0), in = 0, i;
            for (i = 0; i < max && in < 50; i++) {
                run(1);
                if (M->cpu.pc >= lo && M->cpu.pc <= hi) in++; else in = 0;
            }
            printf("[%d] untilpc: %ld quadros, pc=0x%04X %s\n", ln, i, M->cpu.pc, in >= 50 ? "ok" : "FALHOU");
            if (in < 50) fails++;
        } else if (!strcmp(cmd, "frames")) {
            run(strtol(a1, NULL, 0));
        } else if (!strcmp(cmd, "press")) {
            keys(a1, 1);
        } else if (!strcmp(cmd, "release")) {
            keys(a1, 0);
        } else if (!strcmp(cmd, "releaseall")) {
            machine_release_all_keys(M);
        } else if (!strcmp(cmd, "hold")) {
            char k[256]; strcpy(k, a1);
            keys(k, 1);
            run(strtol(a2, NULL, 0));
            strcpy(k, a1);
            keys(k, 0);
        } else if (!strcmp(cmd, "shot")) {
            if (png_write_argb(a1, M->fb, FB_W, FB_H) != 0) fprintf(stderr, "erro png %s\n", a1);
            else printf("[%d] tela -> %s\n", ln, a1);
        } else if (!strcmp(cmd, "poke")) {
            M->mem[addr_of(a1) & 0xFFFF] = (uint8_t)strtol(a2, NULL, 0);
        } else if (!strcmp(cmd, "pokew")) {
            unsigned a = addr_of(a1), v = (unsigned)strtol(a2, NULL, 0);
            M->mem[a & 0xFFFF] = v & 255; M->mem[(a + 1) & 0xFFFF] = v >> 8;
        } else if (!strcmp(cmd, "peek")) {
            unsigned a = addr_of(a1);
            printf("[%d] %s = %u (0x%02X)\n", ln, a1, M->mem[a & 0xFFFF], M->mem[a & 0xFFFF]);
        } else if (!strcmp(cmd, "peekw")) {
            unsigned a = addr_of(a1);
            unsigned v = M->mem[a & 0xFFFF] | (M->mem[(a + 1) & 0xFFFF] << 8);
            printf("[%d] %s = %u (0x%04X)\n", ln, a1, v, v);
        } else if (!strcmp(cmd, "expect") || !strcmp(cmd, "expectne")) {
            unsigned a = addr_of(a1), v = (unsigned)strtol(a2, NULL, 0);
            int eq = M->mem[a & 0xFFFF] == v;
            int ok = !strcmp(cmd, "expect") ? eq : !eq;
            printf("[%d] %s %s %s %u (valor %u): %s\n", ln, cmd, a1, !strcmp(cmd, "expect") ? "==" : "!=",
                   v, M->mem[a & 0xFFFF], ok ? "ok" : "FALHOU");
            if (!ok) fails++;
        } else if (!strcmp(cmd, "untilmem")) {
            unsigned a = addr_of(a1), v = (unsigned)strtol(a2, NULL, 0);
            long max = strtol(a3, NULL, 0), i;
            for (i = 0; i < max && M->mem[a & 0xFFFF] != v; i++) run(1);
            int ok = M->mem[a & 0xFFFF] == v;
            printf("[%d] untilmem %s==%u: %ld quadros %s\n", ln, a1, v, i, ok ? "ok" : "FALHOU");
            if (!ok) fails++;
        } else if (!strcmp(cmd, "echo")) {
            printf("[%d] %s\n", ln, p + 5);
        } else {
            fprintf(stderr, "linha %d: comando desconhecido '%s'\n", ln, cmd);
            return 2;
        }
        fflush(stdout);
    }
    printf("quadros=%ld falhas=%d\n", total_frames, fails);
    return fails ? 1 : 0;
}
