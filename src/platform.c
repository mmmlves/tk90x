/* platform.c - Diretorios, caminhos e hora (POSIX e Windows). */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static void copy_str(char *out, size_t outlen, const char *s)
{
    if (!outlen) return;
    size_t n = strlen(s);
    if (n >= outlen) n = outlen - 1;
    memmove(out, s, n);
    out[n] = 0;
}

static int is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

int plat_strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) return ca - cb;
    }
}

/* Lista dinamica de entradas */
typedef struct { PlatEntry *v; int n, cap; } EntryList;

static int list_add(EntryList *l, const char *name, int is_dir)
{
    if (l->n == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 64;
        PlatEntry *nv = (PlatEntry *)realloc(l->v, (size_t)ncap * sizeof(PlatEntry));
        if (!nv) return -1;
        l->v = nv; l->cap = ncap;
    }
    char *d = dup_str(name);
    if (!d) return -1;
    l->v[l->n].name = d;
    l->v[l->n].is_dir = is_dir;
    l->n++;
    return 0;
}

void plat_free_entries(PlatEntry *e, int count)
{
    if (!e) return;
    for (int i = 0; i < count; i++) free(e[i].name);
    free(e);
}

#ifdef _WIN32
/* ---------------- Windows ---------------- */

static int to_wide(const char *s, wchar_t *w, int wlen)
{
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wlen) ? 0 : -1;
}

static int to_utf8(const wchar_t *w, char *s, int slen)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, s, slen, NULL, NULL) ? 0 : -1;
}

int plat_list_dir(const char *dir, PlatEntry **out, int *count)
{
    EntryList l = { NULL, 0, 0 };
    *out = NULL; *count = 0;
    if (!dir[0]) {
        /* Nivel "Computador": unidades */
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (mask & (1u << i)) {
                char name[3] = { (char)('A' + i), ':', 0 };
                if (list_add(&l, name, 1)) { plat_free_entries(l.v, l.n); return -1; }
            }
        }
        *out = l.v; *count = l.n;
        return 0;
    }
    char pat[PLAT_PATH_MAX];
    plat_path_join(dir, "*", pat, sizeof pat);
    wchar_t wpat[PLAT_PATH_MAX];
    if (to_wide(pat, wpat, PLAT_PATH_MAX)) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        /* diretorio vazio (raiz sem arquivos) ou erro */
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    }
    do {
        char name[PLAT_PATH_MAX];
        if (to_utf8(fd.cFileName, name, sizeof name)) continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        if (list_add(&l, name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) {
            FindClose(h);
            plat_free_entries(l.v, l.n);
            return -1;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    *out = l.v; *count = l.n;
    return 0;
}

int plat_getcwd(char *out, size_t outlen)
{
    wchar_t w[PLAT_PATH_MAX];
    DWORD n = GetCurrentDirectoryW(PLAT_PATH_MAX, w);
    if (!n || n >= PLAT_PATH_MAX) return -1;
    return to_utf8(w, out, (int)outlen);
}

void plat_abspath(const char *path, char *out, size_t outlen)
{
    wchar_t w[PLAT_PATH_MAX], full[PLAT_PATH_MAX];
    char tmp[PLAT_PATH_MAX];
    if (!path[0] || to_wide(path, w, PLAT_PATH_MAX)) { copy_str(out, outlen, path); return; }
    DWORD n = GetFullPathNameW(w, PLAT_PATH_MAX, full, NULL);
    if (!n || n >= PLAT_PATH_MAX || to_utf8(full, tmp, sizeof tmp)) {
        copy_str(out, outlen, path);
        return;
    }
    copy_str(out, outlen, tmp);
}

int plat_is_dir(const char *path)
{
    wchar_t w[PLAT_PATH_MAX];
    if (!path[0]) return 1;     /* lista de unidades */
    if (to_wide(path, w, PLAT_PATH_MAX)) return 0;
    DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Comprimento do prefixo de raiz: "C:\" = 3, "C:" = 2, "\\srv\share\" ..., "\" = 1 */
static size_t root_len(const char *p)
{
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        return is_sep(p[2]) ? 3 : 2;
    if (is_sep(p[0]) && is_sep(p[1])) {
        /* UNC: \\servidor\compartilhamento\ */
        size_t i = 2;
        int parts = 0;
        while (p[i] && parts < 2) {
            if (is_sep(p[i])) parts++;
            i++;
        }
        return i;
    }
    return is_sep(p[0]) ? 1 : 0;
}

#else
/* ---------------- POSIX ---------------- */

int plat_list_dir(const char *dir, PlatEntry **out, int *count)
{
    EntryList l = { NULL, 0, 0 };
    *out = NULL; *count = 0;
    DIR *d = opendir(dir[0] ? dir : "/");
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.') continue;   /* ".", ".." e ocultos */
        char full[PLAT_PATH_MAX];
        plat_path_join(dir, name, full, sizeof full);
        struct stat st;
        if (stat(full, &st) != 0) continue;     /* link quebrado etc. */
        int isdir = S_ISDIR(st.st_mode) ? 1 : 0;
        if (!isdir && !S_ISREG(st.st_mode)) continue;
        if (list_add(&l, name, isdir)) {
            closedir(d);
            plat_free_entries(l.v, l.n);
            return -1;
        }
    }
    closedir(d);
    *out = l.v; *count = l.n;
    return 0;
}

int plat_getcwd(char *out, size_t outlen)
{
    return getcwd(out, outlen) ? 0 : -1;
}

void plat_abspath(const char *path, char *out, size_t outlen)
{
    char tmp[PATH_MAX];
    if (path[0] && realpath(path, tmp)) copy_str(out, outlen, tmp);
    else copy_str(out, outlen, path);
}

int plat_is_dir(const char *path)
{
    struct stat st;
    return stat(path[0] ? path : "/", &st) == 0 && S_ISDIR(st.st_mode);
}

static size_t root_len(const char *p)
{
    return p[0] == '/' ? 1 : 0;
}

#endif

int plat_parent_dir(const char *path, char *out, size_t outlen)
{
    char p[PLAT_PATH_MAX];
    copy_str(p, sizeof p, path);
    if (!p[0]) return -1;
    size_t rl = root_len(p);
    size_t n = strlen(p);
    /* remove separadores finais (exceto a raiz) */
    while (n > rl && is_sep(p[n - 1])) p[--n] = 0;
    if (n <= rl) {
#ifdef _WIN32
        /* raiz de unidade -> lista de unidades */
        if (rl >= 2 && p[1] == ':') { copy_str(out, outlen, ""); return 0; }
#endif
        return -1;
    }
    /* procura o ultimo separador */
    size_t i = n;
    while (i > rl && !is_sep(p[i - 1])) i--;
    if (i == 0) {
        /* caminho relativo sem separador: pai = "." */
        copy_str(out, outlen, ".");
        return strcmp(p, ".") ? 0 : -1;
    }
    /* i aponta logo apos o separador; remove-o (exceto se for a raiz) */
    size_t cut = i;
    while (cut > rl && is_sep(p[cut - 1])) cut--;
    if (cut < rl) cut = rl;
    p[cut] = 0;
#ifdef _WIN32
    /* "C:" -> "C:\" */
    if (cut == 2 && p[1] == ':') { p[2] = '\\'; p[3] = 0; }
#endif
    copy_str(out, outlen, p);
    return 0;
}

void plat_path_join(const char *dir, const char *name, char *out, size_t outlen)
{
    char tmp[PLAT_PATH_MAX];
    size_t dl = strlen(dir);
    if (!dl) {
#ifdef _WIN32
        /* "C:" no nivel das unidades -> "C:\" */
        snprintf(tmp, sizeof tmp, "%s%s", name,
                 (name[0] && name[1] == ':' && !name[2]) ? "\\" : "");
#else
        snprintf(tmp, sizeof tmp, "/%s", name);
#endif
    } else if (is_sep(dir[dl - 1])) {
        snprintf(tmp, sizeof tmp, "%s%s", dir, name);
    } else {
        snprintf(tmp, sizeof tmp, "%s%c%s", dir, PLAT_SEP, name);
    }
    copy_str(out, outlen, tmp);
}

const char *plat_basename(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (is_sep(*p)) b = p + 1;
    return b;
}

void plat_dirname(const char *path, char *out, size_t outlen)
{
    const char *b = plat_basename(path);
    if (b == path) { copy_str(out, outlen, "."); return; }
    char tmp[PLAT_PATH_MAX];
    size_t n = (size_t)(b - path);
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, path, n);
    tmp[n] = 0;
    size_t rl = root_len(tmp);
    while (n > rl && is_sep(tmp[n - 1])) tmp[--n] = 0;
    copy_str(out, outlen, tmp);
}

double plat_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

void plat_timestamp(char *out, size_t outlen)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) { copy_str(out, outlen, "00000000-000000"); return; }
    snprintf(out, outlen, "%04d%02d%02d-%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1,
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}
