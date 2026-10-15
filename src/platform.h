/*
 * platform.h - Funcoes portaveis Linux/Windows do front-end: listagem de
 * diretorio, caminhos (pai, juncao, nome base), unidades de disco (Windows)
 * e data/hora para nomes de arquivo.
 *
 * Caminhos sempre em UTF-8. No Windows sao convertidos para UTF-16
 * (FindFirstFileW etc.), de modo que nomes com acento funcionam.
 * No Windows o caminho vazio "" representa o nivel "Computador"
 * (lista das unidades C:, D: ...).
 */
#ifndef TK_PLATFORM_H
#define TK_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define PLAT_SEP '\\'
#define PLAT_SEP_STR "\\"
#else
#define PLAT_SEP '/'
#define PLAT_SEP_STR "/"
#endif

#define PLAT_PATH_MAX 1024

typedef struct PlatEntry {
    char *name;     /* UTF-8, alocado (sem "." e "..") */
    int   is_dir;
} PlatEntry;

/* Lista o diretorio (sem ordenar, sem "." e "..", sem ocultos).
 * No Windows, dir "" lista as unidades ("C:", "D:" ...) como diretorios.
 * Devolve 0 ok (lista em *out, liberar com plat_free_entries), -1 erro. */
int  plat_list_dir(const char *dir, PlatEntry **out, int *count);
void plat_free_entries(PlatEntry *e, int count);

/* Diretorio pai. Devolve 0 e escreve em out; -1 se ja e a raiz
 * (no Windows "C:\" tem como pai "" = lista de unidades). */
int  plat_parent_dir(const char *path, char *out, size_t outlen);

/* out = dir + separador + name (trata dir vazio / com separador final). */
void plat_path_join(const char *dir, const char *name, char *out, size_t outlen);

/* Diretorio que contem o arquivo (sem separador final, exceto na raiz). */
void plat_dirname(const char *path, char *out, size_t outlen);

/* Ponteiro para o nome do arquivo dentro de path (apos o ultimo / ou \). */
const char *plat_basename(const char *path);

/* Diretorio de trabalho atual. 0 ok, -1 erro. */
int  plat_getcwd(char *out, size_t outlen);

/* Caminho absoluto (se possivel; senao copia path). */
void plat_abspath(const char *path, char *out, size_t outlen);

/* 1 se path e um diretorio existente. */
int  plat_is_dir(const char *path);

/* Data/hora local "AAAAMMDD-HHMMSS" para nomes de arquivo. */
void plat_timestamp(char *out, size_t outlen);

/* Relogio monotono em segundos (para medir tempo real sem SDL). */
double plat_seconds(void);

/* Comparacao sem diferenciar maiusculas (ASCII). */
int  plat_strcasecmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
#endif
