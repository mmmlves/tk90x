/*
 * android_bridge.h - Ponte C <-> Java (JNI) da versao Android.
 *
 *   - seletor de arquivos do aparelho (Storage Access Framework, ACTION_OPEN_DOCUMENT);
 *   - arquivos abertos por outro app ("Abrir com" no gerenciador de arquivos ou nos
 *     downloads do navegador, ACTION_VIEW) chegam pelo mesmo caminho do seletor;
 *   - fitas que vem dentro do APK (assets/games) extraidas para o armazenamento interno;
 *   - manter a tela ligada.
 *
 * O lado Java (android/app/src/main/java/br/com/tk90x/emulador/TK90XActivity.java)
 * copia o documento escolhido (content://) para getCacheDir()/picked/<nome original>
 * e entrega o caminho comum ao C por nativeFilePicked(). A entrega chega numa thread
 * Java; android_poll_picked() e chamado pela thread do SDL (laco principal), e a troca
 * passa por um buffer protegido por mutex.
 *
 * Fora do Android todas as funcoes sao stubs inline que nao fazem nada, de modo que
 * main.c pode chama-las sem #ifdef (android_games_dir devolve NULL).
 */
#ifndef TK_ANDROID_BRIDGE_H
#define TK_ANDROID_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __ANDROID__

/* Abre o seletor de documentos do sistema (ACTION_OPEN_DOCUMENT). Retorna na hora;
 * o arquivo escolhido aparece depois em android_poll_picked(). Chamar da thread do SDL. */
void android_open_picker(void);

/* 1 se um arquivo foi escolhido (seletor ou "Abrir com") desde a ultima chamada:
 * o caminho (ja copiado para o cache, com a extensao original) vai para path.
 * 0 se nao ha nada novo. Pode ser chamado a cada quadro (barato). */
int  android_poll_picked(char *path, size_t n);

/* Extrai as fitas que vem no APK (assets/games: jogos, LEIAME.md e extras/) para
 * <armazenamento interno>/games na primeira execucao ou quando o conteudo do APK
 * muda (carimbo de versao), e devolve esse diretorio. NULL em falha. */
const char *android_games_dir(void);

/* 1 = mantem a tela ligada (padrao enquanto o app roda); 0 = deixa apagar. */
void android_keep_screen_on(int on);

/* 1 se o app esta em tela dividida / janela flutuante (Android 7+): ai perder o
 * foco nao pausa a emulacao (o outro app tocado continua com o TK90X visivel). */
int  android_in_multiwindow(void);

#else  /* desktop: nada a fazer */

static inline void android_open_picker(void) { }
static inline int  android_poll_picked(char *path, size_t n) { (void)path; (void)n; return 0; }
static inline const char *android_games_dir(void) { return NULL; }
static inline void android_keep_screen_on(int on) { (void)on; }
static inline int  android_in_multiwindow(void) { return 0; }

#endif

#ifdef __cplusplus
}
#endif
#endif
