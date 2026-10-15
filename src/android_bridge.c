/*
 * android_bridge.c - Ponte C <-> Java (JNI) da versao Android. Ver android_bridge.h.
 *
 * Todo o arquivo so existe no Android (__ANDROID__); no Linux/Windows o header ja
 * fornece stubs inline e este arquivo compila vazio.
 */
#ifdef __ANDROID__

#include "android_bridge.h"

#include <SDL.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "platform.h"

/* Indice das fitas gerado pelo Gradle (android/app/build.gradle, tarefa tk90xGamesAssets):
 *   1a linha: "tk90x-games <carimbo>" (versionCode + hash do conteudo)
 *   demais:   caminhos relativos a assets/games ("01-jogo.tap", "extras/x.tzx", "LEIAME.md")
 * Uma copia dele fica em <interno>/games/.indice e serve de carimbo da ultima extracao
 * (arquivos com '.' no inicio nao aparecem no navegador). */
#define ASSET_INDEX   "games.idx"
#define ASSET_GAMES   "games"
#define STAMP_FILE    ".indice"
#define ASSET_MAX     (16u * 1024u * 1024u)   /* limite de tamanho de um asset */

/* ------------------------------------------------------------------------- */
/* Arquivo escolhido: thread Java -> thread do SDL                             */
/* ------------------------------------------------------------------------- */

static pthread_mutex_t g_pick_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_pick_path[PLAT_PATH_MAX];
static int  g_pick_ready;

/* Chamado pelo Java (TK90XActivity.nativeFilePicked) numa thread qualquer, depois de
 * copiar o documento para o cache. O caminho chega como byte[] em UTF-8 comum (nao
 * jstring: GetStringUTFChars daria o UTF-8 "modificado" do Java, que difere dos bytes
 * do nome no disco fora do BMP). NULL = seletor cancelado (nada a fazer). */
JNIEXPORT void JNICALL
Java_br_com_tk90x_emulador_TK90XActivity_nativeFilePicked(JNIEnv *env, jclass cls,
                                                          jbyteArray jpath);

JNIEXPORT void JNICALL
Java_br_com_tk90x_emulador_TK90XActivity_nativeFilePicked(JNIEnv *env, jclass cls,
                                                          jbyteArray jpath)
{
    char tmp[PLAT_PATH_MAX];
    if (!jpath) return;
    jsize n = (*env)->GetArrayLength(env, jpath);
    if (n <= 0 || (size_t)n >= sizeof tmp) {
        SDL_Log("TK90X: caminho escolhido invalido ou longo demais (%d bytes)", (int)n);
        return;
    }
    (*env)->GetByteArrayRegion(env, jpath, 0, n, (jbyte *)tmp);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }
    tmp[n] = 0;
    if (strlen(tmp) != (size_t)n) {             /* byte 0 no meio: nao e um caminho */
        SDL_Log("TK90X: caminho escolhido com byte nulo");
        return;
    }
    pthread_mutex_lock(&g_pick_lock);
    memcpy(g_pick_path, tmp, (size_t)n + 1);
    g_pick_ready = 1;                           /* um novo arquivo substitui o anterior */
    pthread_mutex_unlock(&g_pick_lock);
}

int android_poll_picked(char *path, size_t n)
{
    int got = 0;
    if (!path || !n) return 0;
    pthread_mutex_lock(&g_pick_lock);
    if (g_pick_ready) {
        size_t len = strlen(g_pick_path);
        if (len < n) {
            memcpy(path, g_pick_path, len + 1);
            got = 1;
        }
        g_pick_ready = 0;
    }
    pthread_mutex_unlock(&g_pick_lock);
    return got;
}

/* Chama um metodo de instancia "void metodo()" da Activity (thread do SDL).
 * Nao usa FindClass: numa thread nativa ele usaria o class loader errado. */
static int call_activity_void(const char *method)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (!env) return -1;
    jobject act = (jobject)SDL_AndroidGetActivity();     /* referencia local */
    if (!act) return -1;
    int rc = -1;
    jclass cls = (*env)->GetObjectClass(env, act);
    if (cls) {
        jmethodID mid = (*env)->GetMethodID(env, cls, method, "()V");
        if (mid) {
            (*env)->CallVoidMethod(env, act, mid);
            rc = 0;
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        rc = -1;
    }
    (*env)->DeleteLocalRef(env, act);
    if (rc) SDL_Log("TK90X: falha ao chamar %s() na Activity", method);
    return rc;
}

int android_in_multiwindow(void)
{
    /* Activity.isInMultiWindowMode() existe desde a API 24; antes, nunca ha
     * varias janelas (GetMethodID falha e a excecao e limpa) */
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (!env) return 0;
    jobject act = (jobject)SDL_AndroidGetActivity();     /* referencia local */
    if (!act) return 0;
    int on = 0;
    jclass cls = (*env)->GetObjectClass(env, act);
    if (cls) {
        jmethodID mid = (*env)->GetMethodID(env, cls, "isInMultiWindowMode", "()Z");
        if (mid && !(*env)->ExceptionCheck(env))
            on = (*env)->CallBooleanMethod(env, act, mid) ? 1 : 0;
        (*env)->DeleteLocalRef(env, cls);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        on = 0;
    }
    (*env)->DeleteLocalRef(env, act);
    return on;
}

void android_open_picker(void)
{
    call_activity_void("openFilePicker");
}

void android_keep_screen_on(int on)
{
    /* No Android o SDL traduz o protetor de tela para FLAG_KEEP_SCREEN_ON da janela. */
    if (on) SDL_DisableScreenSaver();
    else SDL_EnableScreenSaver();
}

/* ------------------------------------------------------------------------- */
/* Fitas que vem no APK -> armazenamento interno                               */
/* ------------------------------------------------------------------------- */

/* AAssetManager da Activity (getAssets() por JNI). *ref recebe uma referencia
 * global ao objeto Java, que mantem o AAssetManager valido: liberar com
 * release_asset_manager. NULL se falhar. */
static AAssetManager *get_asset_manager(jobject *ref)
{
    *ref = NULL;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (!env) return NULL;
    jobject act = (jobject)SDL_AndroidGetActivity();     /* referencia local */
    if (!act) return NULL;
    AAssetManager *mgr = NULL;
    jclass cls = (*env)->GetObjectClass(env, act);
    if (cls) {
        jmethodID mid = (*env)->GetMethodID(env, cls, "getAssets",
                                            "()Landroid/content/res/AssetManager;");
        if (mid) {
            jobject am = (*env)->CallObjectMethod(env, act, mid);
            if (am && !(*env)->ExceptionCheck(env)) {
                *ref = (*env)->NewGlobalRef(env, am);
                if (*ref) mgr = AAssetManager_fromJava(env, *ref);
            }
            if (am) (*env)->DeleteLocalRef(env, am);
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        mgr = NULL;
    }
    (*env)->DeleteLocalRef(env, act);
    if (!mgr && *ref) { (*env)->DeleteGlobalRef(env, *ref); *ref = NULL; }
    if (!mgr) SDL_Log("TK90X: AAssetManager indisponivel");
    return mgr;
}

static void release_asset_manager(jobject ref)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (env && ref) (*env)->DeleteGlobalRef(env, ref);
}

/* Le um asset inteiro de dentro do APK. Usa o AAssetManager direto, e nao o
 * SDL_RWFromFile: com caminho relativo o SDL tenta antes o armazenamento
 * interno, onde ficam justamente as copias extraidas (<interno>/games/...), e
 * numa atualizacao copiaria o arquivo antigo sobre ele mesmo. */
static uint8_t *read_asset(AAssetManager *mgr, const char *name, size_t *len)
{
    AAsset *as = AAssetManager_open(mgr, name, AASSET_MODE_STREAMING);
    if (!as) return NULL;
    off64_t total = AAsset_getLength64(as);
    if (total < 0 || (uint64_t)total > ASSET_MAX) { AAsset_close(as); return NULL; }
    size_t cap = (size_t)total;
    uint8_t *buf = (uint8_t *)malloc(cap + 1);
    if (!buf) { AAsset_close(as); return NULL; }
    size_t n = 0;
    while (n < cap) {
        int r = AAsset_read(as, buf + n, cap - n);
        if (r <= 0) break;
        n += (size_t)r;
    }
    AAsset_close(as);
    if (n != cap) { free(buf); return NULL; }   /* leitura incompleta */
    buf[n] = 0;                         /* sobra 1 byte: o indice vira texto C */
    *len = n;
    return buf;
}

/* Le um arquivo comum inteiro (o carimbo da ultima extracao). */
static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n + 1 >= cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            if (ncap > ASSET_MAX) { free(buf); fclose(f); return NULL; }
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb; cap = ncap;
        }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        if (r == 0) break;
        n += r;
    }
    fclose(f);
    if (!buf) return NULL;
    buf[n] = 0;
    return buf;
}

/* Grava em arquivo temporario e renomeia (nunca deixa um arquivo pela metade). */
static int write_file(const char *path, const void *data, size_t len)
{
    char tmp[PLAT_PATH_MAX];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* Cria os diretorios intermediarios de path (nao o ultimo componente). */
static int make_parent_dirs(const char *path)
{
    char p[PLAT_PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof p) return -1;
    memcpy(p, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (p[i] != '/') continue;
        p[i] = 0;
        if (mkdir(p, 0700) != 0 && !plat_is_dir(p)) return -1;
        p[i] = '/';
    }
    return 0;
}

/* Caminho relativo seguro: nao vazio, sem '/' inicial, sem componente "..". */
static int safe_rel(const char *rel)
{
    if (!rel[0] || rel[0] == '/') return 0;
    for (const char *s = rel; *s; ) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == 0 || (len == 2 && s[0] == '.' && s[1] == '.')) return 0;
        if (!e) break;
        s = e + 1;
    }
    return 1;
}

/* Proxima linha de um texto (modificado no lugar: '\n' e '\r' viram 0).
 * Devolve NULL no fim. */
static char *next_line(char **cur)
{
    char *s = *cur;
    if (!s || !*s) return NULL;
    char *e = strchr(s, '\n');
    if (e) { *e = 0; *cur = e + 1; }
    else *cur = s + strlen(s);
    size_t n = strlen(s);
    if (n && s[n - 1] == '\r') s[n - 1] = 0;
    return s;
}

/* 1 se a linha rel aparece entre as entradas do indice (copia nao modificada). */
static int index_has(const char *index, const char *rel)
{
    size_t rl = strlen(rel);
    const char *s = strchr(index, '\n');          /* pula o carimbo */
    while (s) {
        s++;
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len && s[len - 1] == '\r') len--;
        if (len == rl && !memcmp(s, rel, rl)) return 1;
        s = e;
    }
    return 0;
}

/* Primeira linha (carimbo) de dois indices iguais? */
static int same_stamp(const char *a, const char *b)
{
    size_t la = strcspn(a, "\r\n"), lb = strcspn(b, "\r\n");
    return la == lb && la > 0 && !memcmp(a, b, la);
}

/* Remove os arquivos da extracao anterior que nao estao no indice novo. */
static void remove_stale(const char *dir, char *old_index, const char *new_index)
{
    char *cur = old_index;
    next_line(&cur);                              /* carimbo */
    for (char *rel; (rel = next_line(&cur)) != NULL; ) {
        if (!safe_rel(rel) || index_has(new_index, rel)) continue;
        char path[PLAT_PATH_MAX];
        if (snprintf(path, sizeof path, "%s/%s", dir, rel) >= (int)sizeof path) continue;
        if (unlink(path) == 0) SDL_Log("TK90X: removido %s (nao vem mais no APK)", rel);
    }
}

/* Extrai todos os arquivos do indice. Devolve o numero de falhas. */
static int extract_all(AAssetManager *mgr, const char *dir, const char *index)
{
    char *copy = SDL_strdup(index), *cur = copy;
    if (!copy) return 1;
    int fails = 0, count = 0;
    next_line(&cur);                              /* carimbo */
    for (char *rel; (rel = next_line(&cur)) != NULL; ) {
        if (!rel[0]) continue;
        if (!safe_rel(rel)) { SDL_Log("TK90X: entrada invalida no indice: %s", rel); fails++; continue; }
        char asset[PLAT_PATH_MAX], path[PLAT_PATH_MAX];
        if (snprintf(asset, sizeof asset, ASSET_GAMES "/%s", rel) >= (int)sizeof asset ||
            snprintf(path, sizeof path, "%s/%s", dir, rel) >= (int)sizeof path) {
            fails++;
            continue;
        }
        size_t len = 0;
        uint8_t *data = read_asset(mgr, asset, &len);
        if (!data) { SDL_Log("TK90X: asset ausente: %s", asset); fails++; continue; }
        if (make_parent_dirs(path) != 0 || write_file(path, data, len) != 0) {
            SDL_Log("TK90X: falha ao gravar %s", path);
            fails++;
        } else {
            count++;
        }
        free(data);
    }
    SDL_free(copy);
    SDL_Log("TK90X: %d arquivo(s) de fita extraido(s) para %s (%d falha(s))", count, dir, fails);
    return fails;
}

const char *android_games_dir(void)
{
    static char dir[PLAT_PATH_MAX];
    static int ready;                             /* 1 = ja extraido nesta execucao */
    if (ready) return dir;

    const char *base = SDL_AndroidGetInternalStoragePath();
    if (!base) { SDL_Log("TK90X: sem armazenamento interno: %s", SDL_GetError()); return NULL; }
    if (snprintf(dir, sizeof dir, "%s/games", base) >= (int)sizeof dir) return NULL;
    if (mkdir(dir, 0700) != 0 && !plat_is_dir(dir)) {
        SDL_Log("TK90X: nao foi possivel criar %s", dir);
        return NULL;
    }

    jobject mgr_ref = NULL;
    AAssetManager *mgr = get_asset_manager(&mgr_ref);
    if (!mgr) return NULL;
    size_t len = 0;
    char *index = (char *)read_asset(mgr, ASSET_INDEX, &len);
    if (!index) {
        SDL_Log("TK90X: o APK nao traz %s; sem fitas embutidas", ASSET_INDEX);
        release_asset_manager(mgr_ref);
        return NULL;
    }

    char stamp_path[PLAT_PATH_MAX];
    if (snprintf(stamp_path, sizeof stamp_path, "%s/" STAMP_FILE, dir) >= (int)sizeof stamp_path) {
        free(index);
        release_asset_manager(mgr_ref);
        return NULL;
    }
    char *old = read_text_file(stamp_path);
    if (old && same_stamp(old, index)) {
        /* mesma versao ja extraida */
        free(old);
        free(index);
        release_asset_manager(mgr_ref);
        ready = 1;
        return dir;
    }
    if (old) {
        remove_stale(dir, old, index);
        free(old);
    }

    /* Carimbo: o indice novo. Se algo falhou, a lista vai com um carimbo invalido, para a
     * proxima execucao tentar de novo (e ainda saber o que limpar). */
    int fails = extract_all(mgr, dir, index);
    release_asset_manager(mgr_ref);
    const char *body = index;
    size_t body_len = len;
    char *partial = NULL;
    if (fails) {
        const char *rest = strchr(index, '\n');
        rest = rest ? rest + 1 : index + len;
        size_t rest_len = len - (size_t)(rest - index);
        static const char bad[] = "tk90x-games incompleto\n";
        partial = (char *)malloc(sizeof bad - 1 + rest_len);
        if (partial) {
            memcpy(partial, bad, sizeof bad - 1);
            memcpy(partial + sizeof bad - 1, rest, rest_len);
            body = partial;
            body_len = sizeof bad - 1 + rest_len;
        }
    }
    if ((!fails || partial) && write_file(stamp_path, body, body_len) != 0)
        SDL_Log("TK90X: falha ao gravar o carimbo %s", stamp_path);
    free(partial);
    free(index);
    ready = 1;
    return dir;
}

#else

/* ISO C nao aceita uma unidade de traducao vazia. */
typedef int tk_android_bridge_unused;

#endif /* __ANDROID__ */
