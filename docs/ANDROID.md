# TK90X no Android

O emulador do TK90X / TK95 também roda em celulares e tablets Android. O código C é o
mesmo do Linux e do Windows; no Android ele ganha controles de toque (joystick, botão de fogo e
o teclado do TK na tela), roda em tela cheia na horizontal e já traz as fitas da pasta `games/`.

- Android **5.0 ou mais novo** (API 21).
- Processadores: ARM 64 bits (`arm64-v8a`, praticamente todos os celulares atuais), ARM 32 bits
  (`armeabi-v7a`, aparelhos antigos) e `x86_64` (Chromebooks e o emulador do Android Studio).
- Nenhuma permissão especial: o app não pede acesso aos seus arquivos. Ele só lê o que você
  escolhe no seletor do sistema ou abre com ele ("Abrir com").

> **Situação:** o APK compila e passa nas verificações estáticas (`aapt2`, `apksigner`, símbolos
> JNI). A versão 1.0 foi instalada num celular: os jogos abriam, mas o joystick parecia não
> funcionar, porque a maioria dos jogos começa no teclado. A 1.1 corrige isso com os perfis dos
> jogos (veja "O joystick em cada jogo"), e a 1.2 revisa os perfis jogo a jogo. A 1.3 corrige a
> imagem congelada em celulares com recorte da câmera (veja "Versões"). Relate o que encontrar
> (veja "Depurar" no fim).

### Versões

| Versão | `versionCode` | O que muda |
|---|---|---|
| 1.3 | 4 | Celulares com recorte da câmera (notch ou furo na tela): a imagem não congela mais depois do primeiro quadro, a tela inteira é usada (inclusive o lado do recorte) e a imagem e os controles de toque ficam fora do recorte, nos dois sentidos da horizontal. |
| 1.2 | 3 | Perfis de controle revisados jogo a jogo; toque curto no FOGO e nas teclas dura no mínimo 200 ms. |
| 1.1 | 2 | Perfis de controle por jogo e correções de toque. |
| 1.0 | 1 | Primeira versão. |

Cada versão instala por cima das anteriores (mesma chave de assinatura), mantendo os dados.

## 1. Instalar no celular

Há dois APKs, gerados por `make android` em `build/android/`:

| Arquivo | Para quê |
|---|---|
| `tk90x-1.3-release.apk` (2,3 MB) | **O que você deve instalar.** Otimizado, assinado com a chave de desenvolvimento do projeto. |
| `tk90x-1.3-debug.apk` (2,9 MB) | Para desenvolvimento (depurável, um pouco maior e mais lento). |

Os dois têm o mesmo nome de pacote (`br.com.tk90x.emulador`), mas são assinados com chaves
diferentes. O Android só atualiza um app com um APK assinado pela **mesma chave**: para trocar
de release para debug (ou o contrário), desinstale o app antes. Desinstalar apaga os snapshots
gravados dentro do app.

### Sem cabo (o mais simples)

1. Copie o `tk90x-1.3-release.apk` para o celular: mande por e-mail ou mensagem para você
   mesmo, baixe de um link ou copie pelo cabo USB para a pasta `Download`.
2. Toque no arquivo, no gerenciador de arquivos ("Arquivos", "Meus arquivos") ou na lista de
   downloads do navegador.
3. O Android pede permissão para **instalar apps desconhecidos** a partir daquele app (o
   gerenciador de arquivos ou o navegador). Toque em **Configurações**, ligue **Permitir desta
   fonte**, volte e toque em **Instalar**.
   - No Android 8 ou mais novo a permissão é por app: Configurações → Apps → Acesso especial →
     Instalar apps desconhecidos.
   - No Android 7 ou mais antigo a chave é geral: Configurações → Segurança → **Fontes
     desconhecidas**.
4. O Play Protect pode avisar que o app é de um "desenvolvedor desconhecido": toque em **Mais
   detalhes → Instalar assim mesmo**. Se ele oferecer enviar o app para análise, tanto faz.
5. O ícone **TK90X** (letras TK e listras coloridas sobre fundo preto) aparece na lista de apps.

### Com cabo, pelo `adb`

1. No celular: Configurações → Sobre o telefone → toque 7 vezes em **Número da versão**. Em
   Opções do desenvolvedor, ligue **Depuração USB**.
2. Ligue o cabo e aceite a chave RSA que aparece no celular. No computador:

   ```sh
   . tools/android/env.sh
   adb devices                                         # o aparelho deve aparecer como "device"
   adb install -r build/android/tk90x-1.3-release.apk  # -r = atualiza mantendo os dados
   ```

   No Android 11 ou mais novo também dá sem cabo: Opções do desenvolvedor → Depuração por Wi-Fi,
   `adb pair IP:PORTA` e `adb connect IP:PORTA`.

## 2. Usar

O app abre direto na tela de abertura do TK95, em tela cheia e na horizontal (gire o aparelho
para qualquer um dos dois lados). As barras do sistema ficam escondidas; arrastar a partir da
borda as mostra por alguns segundos. A tela não apaga enquanto o emulador está aberto.

### Controles de toque

*(Os controles são desenhados por `src/touch.c`; no PC dá para vê-los com `./build/tk90x --touch`,
usando o mouse no lugar do dedo. Ainda falta conferir tamanhos e posições num aparelho.)*

- **Joystick** (à esquerda): arraste o polegar para as 8 direções. Ele segue o modo de joystick
  escolhido: por padrão o **Sinclair 1** embutido no TK (teclas 6, 7, 8, 9 e 0); o nome do modo
  aparece acima do joystick. Kempston, Sinclair 2, Cursor, Teclas QAOP e Teclas do jogo ficam no
  MENU > Joystick, e os jogos que vêm no app já abrem no modo certo (veja "O joystick em cada
  jogo" abaixo). A área de toque fica longe das bordas (faixas de gesto do Android), e um polegar
  que continua na tela depois de abrir o menu ou o teclado, ou de voltar ao app, volta a valer
  assim que se mexe.
- **FOGO** (à direita). Um toque rápido no FOGO ou numa tecla do teclado da tela fica
  pressionado por pelo menos 200 ms, porque há jogos que só leem o teclado de vez em quando e
  perdiam um toque curto; dois toques rápidos seguidos continuam sendo dois.
- **Botões no alto, à direita:**
  - **MENU**: abre o menu de toque (a emulação pausa enquanto ele está aberto);
  - **TECLADO**: mostra ou esconde o teclado do TK;
  - **LOAD ""**: digita `LOAD ""` + ENTER e dá play na fita, como no aparelho. Com um jogo
    rodando, reinicia antes; logo depois de abrir um jogo, enquanto o `LOAD ""` automático ainda
    está sendo digitado, só avisa "Carregando...".
- **Teclado do TK**: as 40 teclas em 4 fileiras, como no aparelho, translúcido na parte de
  baixo da tela (a imagem do emulador sobe para ficar acima dele). Vale tocar duas teclas ao
  mesmo tempo (CAPS SHIFT ou SYMBOL SHIFT + tecla). Um toque rápido em CAPS ou SYMBOL o trava
  para a próxima tecla, o que facilita digitar com um dedo só. Segurar CAPS e tocar SYMBOL (ou o
  contrário) entra no modo E sem travar nenhum dos dois.
- **Menu de toque** (botão MENU): **Abrir jogo**, **Abrir do aparelho**, **Fita** (play/pausa),
  **Rebobinar fita**, **Reiniciar**, **Turbo**, **Salvar estado** (snapshot `.z80`), **Captura
  de tela**, **50/60 Hz**, **Modelo** (TK95, TK90X, TK90X v1), **Joystick** (modo),
  **Diagnóstico**, **Ajuda** e **Sair**. Com um jogo conhecido aberto, o título do menu mostra
  `Controles: ...`. O **X** no canto fecha o menu. A **Ajuda** tem 6 páginas (primeiros passos, carregar
  uma fita, formatos, teclado, joystick, reset e modelos), trocadas com `<` e `>` ou um toque na
  metade esquerda/direita do texto; FECHAR (ou Voltar) fecha.
- **Voltar** (botão ou gesto do Android): fecha o menu ou a janela aberta, como o Esc no PC.
  Sem nada aberto, pergunta se você quer sair.

Também funcionam um **teclado físico** (USB ou Bluetooth), com o mesmo mapeamento do PC
(veja o README), e **controles de jogo** USB/Bluetooth, que viram o joystick do modo escolhido.

### O joystick em cada jogo

O joystick da tela não é mágico: ele aperta as teclas do modo escolhido, como o conector do TK.
O jogo só obedece se estiver lendo essas teclas. A maioria dos jogos começa no **teclado**
(Q, A, O, P, ou Z, X, M...) e só lê o joystick depois que você o escolhe no menu do jogo; sem
teclado no celular, o joystick parecia morto.

- **Jogos que vêm no app**: cada um tem um perfil. Ao abrir, o emulador troca o joystick para o
  modo que funciona com os controles padrão do jogo e, quando a carga termina, mostra por 5 s
  `Controles: <resumo>` e como começar, por exemplo `FOGO inicia` ou `TECLADO > 1, depois
  TECLADO` (toque TECLADO, a tecla, e TECLADO de novo para voltar ao joystick). A tabela completa está em
  `games/LEIAME.md`, seção "Controles". Abrir outro arquivo devolve o modo que você usava.
- **Outros jogos**: o modo tem de ser o mesmo que o jogo lê. Escolha em **MENU > Joystick**:
  - opção "Interface 2", "Sinclair" ou "joystick" no menu do jogo: **Sinclair 1**;
  - "Kempston": **Kempston**;
  - "Cursor", "Protek" ou "AGF": **Cursor** (5 a 8 e 0);
  - jogo no teclado com Q, A, O, P e espaço: **Teclas QAOP**;
  - outras teclas: escolha no menu do jogo um dos joysticks acima.
- **Diagnóstico** (MENU > Diagnóstico): mostra no canto de cima `JOY:XX modo`, os bits que o
  joystick entrega ao TK (01 direita, 02 esquerda, 04 baixo, 08 cima, 10 fogo). Se o número muda
  quando você toca, o toque chegou ao emulador e o problema é o modo (veja acima).

### Abrir jogos

Há três jeitos:

1. **Jogos que vêm no app.** MENU → **Abrir jogo** mostra o navegador de arquivos já na pasta
   dos jogos incluídos: os mesmos de `games/` (Knight Lore alternativo, Manic Miner alternativo,
   Bruce Lee, Dizzy, Green Beret, Boulder Dash e os outros; a origem e a licença de cada um estão
   em `LEIAME.md`, que também vem junto). Toque no jogo: o emulador reinicia, digita `LOAD ""` e
   carrega como de fita, na carga nostálgica de 20x (de uns 6 a 17 segundos; os tempos de cada
   jogo estão na tabela do `README.md`).
2. **Arquivo do aparelho.** MENU → **Abrir do aparelho**, ou a primeira linha do navegador,
   **Abrir arquivo do aparelho...**: abre o seletor do Android, de onde dá para pegar um `.tap`, `.tzx`, `.z80`, `.sna`, `.scr`,
   `.bin` ou `.cod` da pasta Download, do cartão SD, do Google Drive etc.
3. **"Abrir com" a partir de outro app.** Num gerenciador de arquivos ou na lista de downloads do
   navegador, toque no arquivo `.tap` / `.tzx` / `.z80` / `.sna` e escolha **TK90X**. Se o app
   estiver aberto, o jogo novo substitui o atual. Como alguns apps informam esses arquivos só como
   "binário" (`application/octet-stream`), o TK90X pode aparecer também como opção para outros
   arquivos binários; um formato que ele não conhece é recusado com um aviso. Só valem arquivos
   entregues como `content://` (todos os apps atuais fazem assim); os links `file://` de apps
   muito antigos não são aceitos, porque o TK90X não pede permissão de acesso aos arquivos.

Nos casos 2 e 3, o arquivo é copiado para o cache do app (só o último fica guardado) e aberto
de lá; o original não é alterado. Um nome muito comprido é encurtado pelo começo (a extensão fica)
para caber no limite de 255 bytes do sistema de arquivos. Arquivo sem extensão é reconhecido pelo conteúdo, como no PC.

### Snapshots e capturas de tela

O **Salvar estado** (`.z80`) e a **captura de tela** (`.png`) vão para a pasta do app no
armazenamento compartilhado: `Android/data/br.com.tk90x.emulador/files/` (no computador, pelo
cabo USB; alguns gerenciadores de arquivos também a mostram). Se essa pasta não estiver
disponível, eles ficam no armazenamento interno do app. Desinstalar o app apaga as duas.

### Segundo plano

Ao trocar de app, apagar a tela ou receber uma ligação, a emulação e o som pausam; ao voltar,
continuam de onde pararam. Em tela dividida (ou janela flutuante), tocar no outro app não pausa
o TK90X, que continua visível; só ir para o segundo plano pausa. O Android pode encerrar o app em segundo plano, e nesse caso ele
recomeça do zero: grave o estado antes se não quiser perder o jogo.

## 3. Compilar

### Requisitos

O kit Android fica todo em `tools/android/` (JDK 17, SDK, NDK r27d, CMake 3.22.1, Gradle 8.9 e o
código-fonte do SDL 2.32.10); nada é instalado no sistema e não é preciso sudo. Como montar o kit
do zero está em [`docs/research/android-build.md`](research/android-build.md).

### Gerar os APKs

```sh
make android
```

O alvo roda `android/build.sh`, que carrega `tools/android/env.sh`, chama
`./gradlew assembleDebug assembleRelease` e copia o resultado para:

```
build/android/tk90x-1.3-debug.apk
build/android/tk90x-1.3-release.apk
```

O primeiro build leva uns 2 minutos (o SDL é compilado para as 3 arquiteturas, em debug e
release). Os intermediários do Gradle e do CMake ficam em `build/android/gradle/` e
`build/android/cxx/`, de modo que `make clean` limpa tudo. Com `make BUILD=outro-dir android`,
tudo vai para `outro-dir/android/`.

Direto pelo Gradle (por exemplo, para instalar no aparelho conectado):

```sh
. tools/android/env.sh
cd android
./gradlew assembleDebug           # build/android/gradle/app/outputs/apk/debug/app-debug.apk
./gradlew installDebug            # compila e instala pelo adb
```

Opções (`-P` no Gradle):

| Opção | Padrão | Para quê |
|---|---|---|
| `-PtkBuildDir=DIR` | `<repo>/build/android` | onde ficam os intermediários |
| `-Psdl2Dir=DIR` | `$SDL2_ANDROID_SRC`, senão `tools/android/SDL2-2.32.10` | código-fonte do SDL2 |
| `-PtkSrcDir=DIR` | `<repo>/src` | fontes C do emulador (útil para testar uma cópia) |
| `-PtkKeystoreDir=DIR` | `tools/android/keystore` | chave de assinatura do release |

`TK_GRADLE_DAEMON=1 make android` mantém o daemon do Gradle vivo entre builds: um build
incremental cai para uns 15 segundos.

### Versão

`versionCode` e `versionName` ficam no começo de `android/app/build.gradle`
(`tkVersionCode` / `tkVersionName`). Para publicar uma versão nova, aumente os dois: o
`versionCode` tem de crescer para o Android aceitar a atualização, e o `versionName` entra no
nome dos APKs. Versão atual: `versionCode` 4, `versionName` 1.3 (instala por cima da 1.0, da 1.1
e da 1.2, que têm a mesma chave de assinatura).

### Assinatura

O release é assinado com a **chave de desenvolvimento** `tools/android/keystore/tk90x-dev.jks`
(senha publicada de propósito em `keystore.properties`; não use para a Play Store). O debug usa a
chave de debug do kit (`tools/android/android-user-home/debug.keystore`). Guarde as duas: um
celular só aceita atualizar o app com APKs assinados pela mesma chave. Sem
`keystore.properties`, o release sai sem assinatura (`tk90x-1.3-release-unsigned.apk`).

### Estrutura

```
android/
  build.sh                     usado por "make android"
  build.gradle, settings.gradle, gradle.properties, gradlew, gradle/wrapper/
  app/build.gradle             versao, SDK 35, ABIs, assinatura, tarefa que copia games/
  app/jni/CMakeLists.txt       SDL2 (libSDL2.so) + emulador (libmain.so)
  app/src/main/AndroidManifest.xml
  app/src/main/java/br/com/tk90x/emulador/TK90XActivity.java
  app/src/main/res/            icone (gerado), nome do app, tema
  scripts/gen_icons.py         gera o icone (python3 -I android/scripts/gen_icons.py)
src/android_bridge.h, .c       ponte C <-> Java (so compila no Android)
```

- **SDL2** não é copiado para o repositório: o CMake compila o código de
  `tools/android/SDL2-2.32.10` e o Gradle usa as classes Java do SDL (`SDLActivity` etc.) de
  `SDL2-2.32.10/android-project`, então C e Java do SDL têm sempre a mesma versão.
- **Bibliotecas nativas**: `libSDL2.so` e `libmain.so` por arquitetura. As de 64 bits saem
  alinhadas em páginas de 16 KB (`-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`), exigência dos
  aparelhos com páginas de 16 KB do Android 15 em diante.
- **Fontes C**: `z80.c tape.c snapshot.c machine.c profiles.c roms_embedded.c png.c platform.c
  osd.c touch.c main.c android_bridge.c`, de `src/`, com os mesmos avisos do build Linux
  (`-std=c99 -Wall -Wextra -Wno-unused-parameter`). O código só do Android fica em
  `#ifdef __ANDROID__` ou em `android_bridge.c`.
- **Jogos**: a tarefa `tk90xGamesAssets*` do Gradle copia `games/` (sem `games/src/` e sem
  arquivos ocultos) para os assets do APK, em `assets/games/`, e gera `assets/games.idx` (a lista
  de arquivos e um carimbo com o `versionCode` e um hash do conteúdo). Os arquivos não ficam
  em `app/src/main/assets`: vão para um diretório gerado no build, então mexer em `games/`
  basta para o próximo APK.
- **Ícone**: desenho próprio (letras TK em blocos, como a fonte da ROM, e quatro listras
  diagonais). `gen_icons.py` gera o ícone adaptável em vetor (Android 8+, com camada
  monocromática para os ícones temáticos do Android 13) e os PNGs para as versões antigas, só
  com a biblioteca padrão do Python.

### Como funciona a ponte C <-> Java

`TK90XActivity` estende o `SDLActivity` do SDL, que carrega `libSDL2.so` e `libmain.so` e
roda o `main()` de `src/main.c` numa thread própria (a "thread do SDL").

| Função (`src/android_bridge.h`) | O que faz |
|---|---|
| `android_open_picker()` | Chama `TK90XActivity.openFilePicker()` por JNI, que abre o `ACTION_OPEN_DOCUMENT` (tipo `*/*`, porque `.tap`/`.tzx` não têm tipo MIME registrado). |
| `android_poll_picked(path, n)` | Devolve 1 e o caminho quando chegou um arquivo. O Java confere a extensão (`.tap .tzx .sna .z80 .scr .bin .cod`, ou nenhuma), copia o `content://` para `getCacheDir()/picked/<nome original>` numa thread de trabalho e chama `nativeFilePicked(byte[])` com o caminho em UTF-8 comum (não `String`: o `GetStringUTFChars` daria o UTF-8 "modificado" do Java, que difere dos bytes do nome no disco para emoji e outros caracteres fora do BMP); as entregas são serializadas, e o C guarda o caminho num buffer protegido por mutex até a thread do SDL buscá-lo. |
| `android_games_dir()` | Na primeira execução, ou quando o carimbo de `games.idx` muda (APK novo com outras fitas), extrai `assets/games/*` para `<armazenamento interno>/games`, lendo do APK pelo `AAssetManager` do NDK (`getAssets()` por JNI; não pelo `SDL_RWFromFile`, que com caminho relativo tentaria antes o armazenamento interno e, numa atualização, copiaria a fita extraída antiga sobre ela mesma), e grava a lista em `games/.indice`. Arquivos que saíram do APK são apagados. Se algo falhar, a próxima execução tenta de novo. Devolve o diretório (ou NULL). |
| `android_in_multiwindow()` | `Activity.isInMultiWindowMode()` por JNI (Android 7+; antes, 0). Em tela dividida ou janela flutuante, perder o foco não pausa a emulação; só ir para o segundo plano pausa. |
| `android_keep_screen_on(on)` | `SDL_DisableScreenSaver()` / `SDL_EnableScreenSaver()`, que o SDL traduz para `FLAG_KEEP_SCREEN_ON`. O `main.c` desliga ao ir para o segundo plano e liga ao voltar. |
| `android_safe_insets(ins)` | Faixas da janela cobertas pelo recorte da câmera (esquerda, alto, direita, baixo, em pixels da superfície do SDL). O Java lê o `DisplayCutout` a cada novo layout da janela (Android 9+; o recorte troca de lado ao girar 180 graus) e chama `nativeSafeInsets()`, que guarda os valores sob mutex. O `render_touch` os passa a cada quadro para `touch_set_safe_insets()`, e o `touch_layout` monta a imagem e os controles na parte livre da tela. No PC dá para simular com `--safe-insets 136,0,0,0`. |

O "Abrir com" (`ACTION_VIEW`) chega ao `onCreate` (app fechado) ou ao `onNewIntent` (app
aberto; a Activity é `singleTask`) e segue o mesmo caminho do seletor. O `SDLActivity` original
mandaria o `getPath()` da URI como `SDL_DROPFILE`, o que não serve para `content://`: por isso
a `TK90XActivity` tira a URI da intent antes de chamar o `onCreate` do SDL.

Outros ajustes do lado Java e do manifesto:

- `SDL_ENV.SDL_IOS_ORIENTATIONS = "LandscapeLeft LandscapeRight"` (meta-data): sem essa dica, uma
  janela `SDL_WINDOW_RESIZABLE` faria o SDL liberar todas as orientações, retrato inclusive.
  O `main.c` também define a dica antes de criar a janela.
- `SDL_ENV.SDL_ACCELEROMETER_AS_JOYSTICK = "0"` (meta-data, e a mesma dica no `main.c` antes de
  iniciar os joysticks): por padrão o SDL apresenta o acelerômetro como joystick, e o emulador o
  abriria como controle: segurar o celular inclinado apertaria uma direção e um controle
  conectado depois seria ignorado. O `main.c` também pula um joystick chamado "Android
  Accelerometer", e um controle de jogo conectado depois substitui um joystick genérico.
- `registerReceiver` com `RECEIVER_NOT_EXPORTED` (API 33+): o `HIDDeviceManager` do SDL 2.32
  registra um receptor sem sinalizador, e o Android 14 recusaria com `SecurityException`
  (controles USB via HIDAPI). Não precisa ser exportado: o `PendingIntent` de `USB_PERMISSION` é
  explícito e do próprio app, e USB/Bluetooth conectado ou desconectado são broadcasts do sistema.
- Tela cheia e recorte da câmera definidos antes do primeiro layout (veja "Imagem congelada com
  recorte da câmera"); o modo imersivo é conferido no `onResume` e ao recuperar o foco, e só é
  reaplicado se algo mudou. No Android, F11 (teclado físico) não sai da tela cheia. `configChanges` cobre giro,
  tamanho de tela, teclado, densidade, texto em negrito, gênero gramatical, troca de chip
  (`mcc`/`mnc`) etc., para a Activity não ser recriada (o SDL recomeçaria do zero).
- Se mesmo assim a Activity for recriada (ou o processo encerrado e retomado pelos Recentes), o
  arquivo do "Abrir com" original não é aberto de novo: só num lançamento de verdade
  (`savedInstanceState == null` e sem `FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY`).
- Erro fatal: a caixa de mensagem aparece, o `main()` desmonta o SDL (`SDL_Quit`) e retorna. A
  biblioteca continua carregada no processo; sem isso, abrir o app de novo falharia com
  "Android only supports one window".
- Contexto gráfico perdido ao voltar ao app (`SDL_RENDER_DEVICE_RESET`): o renderizador e as
  texturas (imagem, menus, fonte dos controles) são recriados sem perder o estado. No PC dá para
  simular com `event reset` no `--touch-script`.

Fora do Android, `android_bridge.h` traz versões vazias das funções (`android_games_dir()`
devolve NULL), e `main.c` pode chamá-las sem `#ifdef`.

### Imagem congelada com recorte da câmera

Até a 1.2, num aparelho com recorte da câmera (notch ou furo) o app ia para a horizontal,
desenhava o primeiro quadro e a imagem congelava. O `logcat` mostrava, sem parar:

```
SurfaceFlinger: [SurfaceView - br.com.tk90x.emulador/...TK90XActivity#0] rejecting buffer:
  bufWidth=2400, bufHeight=1080, front.active_legacy.{w=2264, h=1080}
```

Reproduzido no emulador (Android 11, 1080x2400, sobreposição `com.android.internal.emulation.pixel_5`,
recorte de 136 px). A causa tem duas partes:

1. **A janela mudava de tamanho logo depois de criada.** O primeiro layout era feito sem o modo de
   recorte `shortEdges` e com as barras do sistema visíveis: o `SDLActivity.onCreate` enfileira
   `setWindowStyle(false)` (barras visíveis, `FLAG_FORCE_NOT_FULLSCREEN`), e o
   `layoutInDisplayCutoutMode` só era aplicado no `onResume`/foco e pelo `setWindowStyle(true)` que
   o SDL chama ao criar a janela `SDL_WINDOW_FULLSCREEN_DESKTOP`. A superfície nascia com 2264x1080
   (a tela menos o recorte) e, segundos depois, passava a 2400x1080 (`surfaceChanged` duas vezes).
2. **O Android 11 só aplica a mudança de tamanho/posição da `SurfaceView` junto com um quadro novo da
   janela-mãe.** O SDL desenha tudo na `SurfaceView` e a janela-mãe quase nunca redesenha; se esse
   quadro não sai, o SurfaceFlinger fica com o tamanho antigo (2264, deslocado 136 px) e recusa
   todos os quadros de 2400 que o EGL passa a entregar. No emulador ATD isso acontece sempre, porque
   a imagem vem com `debug.hwui.drawing_enabled=0` (a interface Java nunca desenha quadros): com
   `adb shell setprop debug.hwui.drawing_enabled 1` a mesma 1.2 recusa um único quadro e segue.
   Num celular comum a janela-mãe redesenha e o problema vira, no máximo, um quadro perdido ao
   abrir, o que explica um celular sem recorte (ou rápido) não congelar.

Não era o `SDL_WINDOW_FULLSCREEN` (o app já usa `SDL_WINDOW_FULLSCREEN_DESKTOP`, sem trocar o modo
de vídeo) nem tamanho fixo de superfície (o SDL 2.32 não chama `setFixedSize`; o tamanho dos
buffers segue o da `SurfaceView`).

A correção da 1.3 faz a janela nascer com o tamanho final, para a superfície do SDL nunca mudar de
tamanho logo depois de criada:

- `res/values-v28/styles.xml`: `android:windowLayoutInDisplayCutoutMode = shortEdges` no tema;
- `TK90XActivity.onCreate`, **antes** do `super.onCreate`: `FLAG_FULLSCREEN` (sem
  `FLAG_FORCE_NOT_FULLSCREEN`) e `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES`;
- logo depois do `super.onCreate`, uma mensagem na fila do Looper (que roda depois do
  `setWindowStyle(false)` do SDL e antes do primeiro layout) restaura o modo imersivo; o
  `setWindowStyle(true)` que vem do C pede os mesmos sinalizadores e nada muda de tamanho;
- `hideSystemUi()` (foco, `onResume`) só chama `setAttributes`/`setSystemUiVisibility` quando algo
  está diferente, e no Android o F11 não sai da tela cheia.

O C já segue o tamanho real da superfície: o `render_touch` lê `SDL_GetRendererOutputSize` a cada
quadro e refaz o layout (no Android, `SDL_WINDOWEVENT_SIZE_CHANGED` só é registrado no log como
`tk90x: janela LxA`); o app não usa texturas de destino, então `SDL_RENDER_TARGETS_RESET` não
exige nada, e `SDL_RENDER_DEVICE_RESET` recria renderizador e texturas. Com a tela inteira em uso,
o `DisplayCutout` vai para o C (`android_safe_insets`) e a imagem e os controles ficam fora do
recorte, do lado esquerdo ou direito conforme o giro.

Limite que continua: no emulador ATD (`debug.hwui.drawing_enabled=0`), qualquer mudança de tamanho
da superfície com o app aberto (tela dividida, por exemplo) ainda congela a imagem, pela parte 2
acima; em aparelhos de verdade a janela-mãe redesenha e isso não acontece.

### Depurar

Num aparelho com depuração USB:

```sh
. tools/android/env.sh
adb install -r build/android/tk90x-1.3-debug.apk
adb logcat -s SDL SDL/APP TK90X AndroidRuntime
```

As mensagens do emulador (`SDL_Log`) aparecem com a tag `SDL/APP`; as do lado Java, com `TK90X`.

Sem aparelho, há um emulador do Android no kit (sem KVM, só emulação por software, uns 4 quadros
por segundo): `tools/android/start-emulator.sh` liga o AVD `tk90x_atd30` (Android 11 ATD x86_64,
1080x2400) em uns 7 minutos, e `. tools/android/emulator-env.sh` prepara o `adb`. Para testar o
recorte da câmera: `adb shell cmd overlay enable-exclusive --category
com.android.internal.emulation.pixel_5` (desligar: `cmd overlay disable` com o mesmo nome); para
girar: `adb shell settings put system accelerometer_rotation 0` e `user_rotation 1` ou `3`. Duas
diferenças da imagem ATD para um celular: a interface Java não desenha
(`debug.hwui.drawing_enabled=0`) e, como o emulador é ligado com `-no-audio`, a saída de som falha
(`pcm_write failed` no `logcat`): a fila do OpenSL ES esvazia sem parar e o log enche de
`AudioTrack: stop`/`PlayerBase::stop()` e `pthread_mutex_lock_timeout_np returned 110`. Isso é do
emulador sem som, não do app: o SDL abre o dispositivo uma vez e não o reabre.
Para ver os arquivos extraídos no aparelho (só com o APK de debug):
`adb shell run-as br.com.tk90x.emulador ls -la files/games`.
