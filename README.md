# Emulador TK90X / TK95

Emulador dos microcomputadores brasileiros **TK90X** (1985) e **TK95** (1986) da Microdigital,
clones do ZX Spectrum 48K. Escrito em C99 com SDL2, o mesmo código roda no **Linux**, no
**Windows** e no **Android** (veja a seção [Android](#android)).

> **Instalar sem compilar:** os binários prontos (Windows `.exe`, Android `.apk`, Linux) e as
> instruções de instalação estão em [dist/](dist/README.md). Download direto:
> [Windows](https://github.com/mmmlves/tk90x/raw/main/dist/windows/tk90x.exe) ·
> [Android](https://github.com/mmmlves/tk90x/raw/main/dist/android/tk90x.apk) ·
> [Linux](https://github.com/mmmlves/tk90x/raw/main/dist/linux/tk90x).

A ideia é reproduzir a experiência do aparelho: ele liga com a tela de abertura original da ROM
("TK Color Computer" e a barra colorida), e os programas carregam **como de fita**. O emulador
digita `LOAD ""`, dá play na fita e a própria ROM carrega o programa, com o `Program: NOME`, as
listras na borda e a tela sendo desenhada linha a linha. A diferença é que, durante a carga, o
emulador acelera: por padrão, **20 vezes** mais rápido, com o som e as listras originais, e um jogo
de 3 a 5 minutos de fita carrega em 9 a 15 segundos.

## Recursos

- **Boot original** pela ROM do modelo escolhido (TK95, TK90X ou TK90X v1 de 1985).
- **Carga "como de fita"**: `.tap` e `.tzx` tocam como fita de verdade (inclusive blocos
  turbo, pulsos puros e carregadores próprios dos jogos).
  - **Carga nostálgica: som e listras originais, 20x mais rápido** (`--load-speed N`; 0 = máximo;
    F7 alterna). Cada quadro exibido é um quadro emulado inteiro, então a borda mostra as listras
    vermelho/ciano do tom piloto e azul/amarelo dos dados; o som é o de um desses quadros, na
    altura original, então o chiado da fita continua o mesmo (piloto de ~807 Hz, dados de 1 e
    2 kHz) e só a cadência fica 20x mais rápida. Uma linha fina na borda de baixo mostra
    `FITA 45% 20x Program: NOME` por 3 segundos a cada bloco novo e some, para não cobrir as
    listras; o progresso da fita também aparece no menu (F1).
  - Snapshots (`.sna`, `.z80`), telas (`.scr`) e binários crus (`.bin`, `.cod`) viram uma **fita
    virtual** que a ROM também carrega. No fim, o estado do snapshot é restaurado exatamente
    (depois de um reset, `LOAD ""` carrega a mesma fita virtual de novo). Um `.bin`/`.cod` com
    cabeçalho +3DOS carrega no endereço do cabeçalho (o `--org` prevalece).
  - Arquivo com outra extensão (ou sem extensão) é reconhecido pelo conteúdo: TZX, SNA (49179
    bytes), TAP, tela de 6912 bytes ou cabeçalho +3DOS; PZX, CSW avulso e SZX são recusados com
    mensagem, e o que não for reconhecido também (nunca é executado como código).
  - `--instant` aplica o snapshot direto, sem fita. `--flash` carrega instantaneamente os blocos
    padrão (armadilha na rotina LD-BYTES da ROM).
- **Temporização medida no TK90X real**: 3,575611 MHz, 228 T-states por linha, 50 Hz
  (312 linhas) ou 60 Hz PAL-M (262 linhas), com troca em tempo real (F8).
- Joystick embutido do TK (Sinclair 1, teclas 6-0), além de Kempston, Sinclair 2, Cursor,
  **Teclas QAOP** (Q, A, O, P e espaço) e **Teclas do jogo**. Controles USB (SDL
  GameController) funcionam direto.
- **Controles prontos para os jogos de `games/`**: ao abrir um jogo conhecido (reconhecido pelo
  CRC32 do arquivo), o emulador escolhe o joystick que funciona com os controles *padrão* dele,
  sem mexer no menu do jogo, e avisa `Controles: ...` quando a carga termina, com a dica de como
  começar (veja [Joystick e perfis dos jogos](#joystick-e-perfis-dos-jogos)).
- **Setas automáticas**: num jogo, as setas do PC são o joystick escolhido (Alt = fogo); no
  BASIC, com o cursor piscando, movem o cursor de edição. F4 troca para "sempre joystick" ou
  "sempre cursores".
- **Menu e ajuda (F1)**: menu com todas as funções (teclado, setas ou mouse) e uma ajuda em 6
  páginas para quem nunca usou um TK: primeiros passos, como carregar uma fita, formatos de
  arquivo, teclado, jogos e joystick, reset e modelos.
- Teclado do PC em qualquer layout (inclusive **ABNT2**): letras, números e pontuação vão para as
  teclas certas do Spectrum (por exemplo, `"` vira SYMBOL+P).
- Navegador de arquivos interno (F2), arrastar e soltar arquivos na janela, menu e ajuda na tela
  (F1), snapshot `.z80` (F6), captura de tela PNG (F12), tela cheia (F11).
- Modo **headless** (sem janela) para testes automáticos.

## Compilar

### Linux

```sh
sudo apt install build-essential libsdl2-dev
make
./build/tk90x
```

O `Makefile` usa o `sdl2-config` se ele existir. Sem ele, usa os cabeçalhos em
`tools/sdl2-linux/`. Também dá para compilar com CMake:

```sh
sudo apt install cmake libsdl2-dev
cmake -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
```

### Windows: compilação cruzada a partir do Linux

```sh
make windows        # gera build/win/tk90x.exe (SDL2 estático, um único .exe sem DLL)
```

Esse alvo usa o compilador `tools/llvm-mingw` e o pacote `tools/sdl2-mingw`. O passo a passo para
baixá-los (sem sudo) está em `docs/research/windows-build.md`.

### Windows nativo (MSYS2 ou Visual Studio)

Com o [MSYS2](https://www.msys2.org/), no terminal **UCRT64**:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cmake -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DTK_STATIC_SDL=ON
cmake --build build-cmake
```

Com o Visual Studio, baixe o pacote "SDL2-devel-2.x-VC" e informe onde ele está:

```bat
cmake -B build-cmake -DSDL2_DIR=C:\SDL2-2.32.10\cmake
cmake --build build-cmake --config Release
```

O CMake copia `SDL2.dll` (quando o SDL é dinâmico) e a pasta `roms/` para junto do executável.

## Uso

```
tk90x [opções] [arquivo]
```

Sem arquivo, o emulador liga na tela de abertura, pronto para o BASIC. Com um arquivo, ele faz o
boot, digita `LOAD ""` e carrega o programa.

| Opção | Descrição |
|---|---|
| `--model tk95\|tk90x\|tk90x-v1` | modelo (padrão `tk95`) |
| `--rom ARQUIVO` | ROM alternativa de 16 KB |
| `--50hz`, `--60hz` | frequência do vídeo (padrão 50 Hz) |
| `--scale N` | escala da janela (padrão 3, ou seja, 960x720) |
| `--fullscreen` | começa em tela cheia |
| `--instant` | snapshots aplicados direto, sem fita virtual |
| `--flash` | carga instantânea dos blocos padrão (armadilha na ROM) |
| `--load-speed N` | velocidade durante a carga: N vezes (padrão 20, a carga nostálgica), 1 = tempo real, 0 = máxima |
| `--org END`, `--exec END` | endereço de carga e de execução de `.bin`/`.cod` (padrão 32768, ou o do cabeçalho +3DOS; exec = org). Aceita `0x8000`, `$8000`, `8000h` |
| `--joystick kempston\|sinclair1\|sinclair2\|cursor\|qaop\|keys` | modo do joystick (padrão `sinclair1`, o conector embutido do TK). `keys` (teclas do jogo) só existe com o perfil de um jogo conhecido; sem perfil vale o `sinclair1`. Um jogo conhecido troca o modo ao abrir |
| `--no-contention` | desliga a contenção de memória da ULA (padrão: ligada, como no aparelho) |
| `--no-sound` | sem som |
| `--headless` | sem janela nem áudio (testes) |
| `--frames N` | (headless) roda N quadros e sai (padrão 500) |
| `--until-loaded` | (headless) roda até a carga terminar e mais 150 quadros (ou `--frames N`) |
| `--screenshot ARQ.png` | grava a tela (sem os menus) ao sair |
| `--wav-out ARQ.wav` | (depuração) grava em WAV exatamente o áudio entregue à placa de som. No modo headless simula a cadência da janela (um quadro de áudio por quadro exibido) |
| `--touch` | controles de toque na tela, os mesmos do Android, usados com o mouse (veja "Controles de toque") |
| `--type "TEXTO"` | digita o texto no teclado emulado depois do boot (e da carga, se houver arquivo). `\n` = ENTER; maiúsculas usam CAPS SHIFT. Fora de aspas e de `REM`, as palavras-chave do BASIC viram tokens, como no teclado do TK: `PRINT` no início da instrução é a tecla P, `INT` é modo E + R, `TO` é SYMBOL+F (aceita `GOTO`/`GO TO`, `SOUND`/`BEEP`, `RAND`/`RANDOMIZE`). Ex.: `--type 'PRINT 2+2\n'` |

Exemplos:

```sh
./build/tk90x jogo.tzx
./build/tk90x --model tk90x --60hz jogo.z80
./build/tk90x --org 0x6000 rotina.bin
./build/tk90x --headless --until-loaded jogo.tap --screenshot tela.png
./build/tk90x --headless --frames 400 --type 'P2+2\n' --screenshot conta.png
```

## Jogos de teste (pasta `games/`)

A pasta [`games/`](games/) traz imagens de fita para experimentar o emulador: jogos clássicos de
distribuição permitida e alternativas livres no lugar dos que não podem ser distribuídos. A
origem, a licença e as instruções de cada um estão em [`games/LEIAME.md`](games/LEIAME.md).

Tempo de carga na carga nostálgica (20x), medido no emulador: "fita" é a duração real da fita
(`tape_total_seconds`) e "na tela" é o tempo que o usuário espera do boot até o fim da carga
(`shown_seconds` do modo headless, que inclui uns 2,5 s de boot e de `LOAD ""`).

| Arquivo | Fita | Fita / 20 | Na tela |
|---|---|---|---|
| `01-knight-lore-alt-deep-blue.tap` | 2min51 (171 s) | 8,6 s | 11,0 s |
| `02-manic-miner-alt-frank-n-stein-rebooted.tzx` | 4min45 (285 s) | 14,3 s | 16,7 s |
| `03-jet-set-willy-alt-lala-prologue.tzx` | 3min26 (206 s) | 10,3 s | 12,7 s |
| `04-atic-atac-alt-castelo-corvoalto.tap` | 1min10 (70 s) | 3,5 s | 5,9 s |
| `05-bruce-lee.tzx` | 3min01 (181 s) | 9,1 s | 11,5 s |
| `06-sabre-wulf-alt-selva-de-verdemor.tap` | 1min20 (80 s) | 4,0 s | 6,4 s |
| `07-commando-alt-sgt-helmet-training-day.tap` | 4min17 (257 s) | 12,8 s | 15,2 s |
| `08-dizzy.tzx` | 4min45 (285 s) | 14,2 s | 16,7 s |
| `09-green-beret.tzx` | 3min48 (228 s) | 11,4 s | 13,8 s |
| `10-boulder-dash.tzx` | 3min22 (202 s) | 10,1 s | 12,5 s |
| `extras/06-extra-doomskulle.tzx` | 5min00 (300 s) | 15,0 s | 17,4 s |

Com `--load-speed 0` (ou F7 durante a carga) qualquer um carrega em 2 a 4 segundos, sem o som.

## Teclas

| Tecla do PC | Função |
|---|---|
| Letras, números, Enter, Espaço | teclas equivalentes do Spectrum |
| Shift esquerdo | CAPS SHIFT |
| Ctrl (qualquer um) ou Shift direito | SYMBOL SHIFT |
| Backspace | DELETE (CAPS+0) |
| Setas | **automático** (padrão): joystick do tipo escolhido no F9 enquanto um programa roda; cursores do BASIC (CAPS+5/6/7/8) quando a ROM espera tecla no editor (cursor K/L, `INPUT`). F4 muda |
| Alt (esquerdo ou direito) | fogo, quando as setas são joystick |
| Tab | modo estendido (CAPS+SYMBOL) |
| CapsLock | CAPS LOCK (CAPS+2) |
| Home / Insert / End | EDIT (CAPS+1) / GRAPHICS (CAPS+9) / BREAK (CAPS+SPACE) |
| PgUp / PgDn | TRUE VIDEO (CAPS+3) / INV. VIDEO (CAPS+4) |
| Pontuação (`" , . ; : + - * / = < > ? ( ) $ ' _ ! @ # % & ^ ~ \| \ { } [ ]`) | digite normalmente, em qualquer layout. O emulador aperta SYMBOL+tecla (ou modo E + SYMBOL+tecla para `~ \| \ { } [ ]`) |
| F1 | **menu** (abrir jogo, fita, reset, turbo, estado, captura, modelo, 50/60 Hz, joystick, setas, tela cheia, diagnóstico, ajuda, sair) |
| F2 | abrir arquivo (navegador interno) |
| F3 | play/pausa da fita (o `LOAD ""` já dá play sozinho) |
| F4 | setas: automático → joystick → cursores (o aviso mostra o modo e o tipo de joystick) |
| F5 | reset |
| F6 | grava snapshot `tk90x-AAAAMMDD-HHMMSS.z80` no diretório atual (`-2`, `-3`... se já existir). Durante a carga da fita virtual de um snapshot, grava o snapshot sendo carregado |
| F7 | turbo máximo liga/desliga. Durante a carga, alterna entre o máximo e a velocidade de `--load-speed` (20x, a carga nostálgica; com `--load-speed 0`, o F7 passa para 20x) |
| F8 | alterna 50/60 Hz |
| F9 | próximo modo de joystick (Sinclair 1, Kempston, Sinclair 2, Cursor, Teclas QAOP e, com um jogo conhecido, Teclas do jogo) |
| F10 | próximo modelo (TK95, TK90X, TK90X v1), com reset; tira a fita (abra o jogo de novo) |
| F11 | tela cheia |
| F12 | captura de tela `tk90x-AAAAMMDD-HHMMSS.png` (`-2`, `-3`... se já existir) |
| Esc | fecha menus (na ajuda aberta pelo menu, volta ao menu); sem menu, pergunta se quer sair (Esc ou S confirma; F1 abre o menu) |
| Arrastar um arquivo para a janela | abre o arquivo (carga "original") |

### Setas: automático, joystick ou cursores (F4)

Quase todos os jogos do TK leem o joystick embutido, que são as teclas 6 (esquerda), 7 (direita),
8 (baixo), 9 (cima) e 0 (fogo). Se as setas fossem só os cursores do Spectrum (CAPS+5/6/7/8), num
jogo a seta para baixo viraria o 6 (esquerda), a da direita o 8 (baixo) e assim por diante. Por
isso o modo padrão é o **automático**:

- com um programa rodando (jogo em código de máquina, programa BASIC usando `INKEY$`, carga da
  fita), as setas são o joystick do tipo escolhido no F9 (Sinclair 1 por padrão; também
  Kempston, Sinclair 2 e Cursor), sem CAPS SHIFT junto, e o Alt é o fogo;
- quando a ROM está esperando tecla no editor (cursor K/L piscando, `INPUT`, "scroll?"), as
  setas movem o cursor de edição (CAPS+5..8). O emulador sabe disso porque a rotina de leitura
  do teclado da ROM (KEY-INPUT, 0x10B4) rodou nos últimos 10 quadros.

O F4 alterna entre automático, sempre joystick e sempre cursores (o modo atual aparece no menu,
F1). Se um jogo não obedece às setas, troque o tipo de joystick no F9 ou use as teclas que o
jogo pede (muitos usam Q, A, O, P e espaço: é o modo **Teclas QAOP**). Com um jogo conhecido,
as setas já são o joystick do perfil dele (veja abaixo).

### Joystick e perfis dos jogos

O joystick (setas + Alt no PC, joystick e FOGO na tela de toque, controle USB) aperta as teclas
do modo escolhido, juntas ao teclado, como o conector do TK fazia (só o Kempston usa a porta 31):

| Modo (F9 / MENU > Joystick) | Teclas: cima, baixo, esquerda, direita, fogo |
|---|---|
| Sinclair 1 (padrão, o embutido do TK) | 9, 8, 6, 7, 0 |
| Kempston | porta 31 (interface externa) |
| Sinclair 2 | 4, 3, 1, 2, 5 |
| Cursor (Protek, AGF) | 7, 6, 5, 8, 0 |
| Teclas QAOP | Q, A, O, P, espaço |
| Teclas do jogo | as do perfil do jogo aberto (até 2 teclas juntas por direção) |

O modo tem de ser o que o jogo lê. A maioria dos jogos começa no **teclado** e só lê o joystick
depois de escolhê-lo no menu dele; no celular, sem teclado, o joystick parecia morto. Por isso
os jogos de `games/` têm **perfis** (`src/profiles.c`): ao abrir um deles, o emulador troca o
joystick, só nesta sessão, para o modo que funciona com os controles padrão do jogo e, quando a
carga termina, mostra por 5 segundos `Controles: <resumo>` e a dica para começar (por exemplo,
`FOGO inicia` ou `TECLADO > 1, depois TECLADO`). O resumo também aparece no menu (F1 / MENU) e
na página "Jogos e joystick" da ajuda. Abrir um arquivo sem perfil (ou trocar de modelo) devolve
o modo de antes. O F9 continua trocando o modo à vontade; "Teclas do jogo" só aparece com um
perfil que as tenha.
Os controles de cada jogo estão em [`games/LEIAME.md`](games/LEIAME.md#controles).

### Menu e ajuda (F1)

O F1 abre o menu principal, que pausa a emulação. Ele navega com as setas (ou a roda do mouse),
Enter ou clique executam, Esc fecha, e a letra inicial é atalho (com mais de um item com a mesma
letra, cada toque passa para o próximo). As teclas de função também funcionam com o menu aberto.
Turbo, modelo, 50/60 Hz, joystick, setas e tela cheia mostram o valor novo e deixam o menu
aberto. O item "Ajuda" abre 6 páginas (Esquerda/Direita, PgUp/PgDn, 1 a 6, clique na metade
esquerda ou direita): primeiros passos, como carregar uma fita à mão (`J`, `Ctrl+P` duas vezes,
Enter; a fita toca sozinha), formatos de arquivo (`.tap .tzx .z80 .sna .scr .bin .cod`), teclado, jogos e
joystick, reset e modelos. No diálogo de saída (Esc), `Sim`/`Não` também aceitam clique.

Sobre o Shift: como `Shift+1` no PC produz `!`, o emulador entrega `!` (SYMBOL+1) e não CAPS+1.
As funções CAPS+número que se perdem com isso têm teclas próprias na tabela acima (Home, CapsLock,
PgUp, PgDn, Insert e as setas).

No navegador de arquivos (F2): setas, PgUp/PgDn, Home/End, Enter abre, Backspace sobe um nível,
uma letra pula para o próximo nome que começa com ela; com o mouse, clique escolhe, clique duplo
abre e a roda rola. No Windows, o nível mais alto lista as
unidades (C:, D:...). O navegador lembra o último diretório usado.

## Controles de toque (Android e `--touch`)

No Android o emulador abre em tela cheia, deitado (paisagem), com os controles de toque sempre
ligados. No Linux e no Windows, a opção `--touch` mostra a mesma interface, usada com o mouse; sem
ela, nada muda na janela.

- **Imagem**: 4:3, a maior que cabe, em escala inteira quando ela fica perto do tamanho máximo.
  Os controles ficam nas margens dos lados; numa tela 4:3 (tablet), ficam por cima da imagem,
  translúcidos.
- **Joystick** (esquerda): arraste o polegar a partir do centro. São 8 direções, e o setor
  aceso mostra a direção atual. Ele age como o joystick do modo escolhido: Sinclair 1 (teclas 6 a
  0, o conector do TK) por padrão, ou Kempston, Sinclair 2, Cursor, Teclas QAOP e Teclas do jogo
  no MENU; os jogos de `games/` já abrem no modo certo e avisam `Controles: ...` (veja
  [Joystick e perfis dos jogos](#joystick-e-perfis-dos-jogos)). O modo atual aparece acima da
  base. A área de toque fica afastada das bordas, longe das faixas de gesto do Android, e um dedo
  que continua na tela depois do menu, do teclado ou de voltar ao app volta a valer ao se mover.
- **FOGO** (direita): o botão de tiro. O joystick e o FOGO funcionam juntos (multitoque). Um
  toque rápido no FOGO ou numa tecla do teclado da tela fica pressionado por pelo menos 200 ms
  (10 quadros): há jogos que só leem o teclado de vez em quando (entre as notas da música do
  título, por exemplo) e perdiam um toque curto. Dois toques rápidos seguidos continuam sendo dois.
- **TECLADO** (alto, à direita): mostra ou esconde as 40 teclas do TK90X/TK95 nas 4 fileiras do
  aparelho (1-0, Q-P, A-ENTER, CAPS SHIFT-SPACE), com as palavras do modo K sob as letras (nomes do
  TK: `GOTO`, `GOSUB`, `CONT`, `RAND`) e os símbolos do SYMBOL SHIFT em vermelho. Com o teclado
  aberto, a imagem diminui para que as 24 linhas da tela fiquem acima dele; só a borda de baixo
  passa sob as teclas translúcidas, e o joystick some.
  - Multitoque: segure CAPS SHIFT ou SYMBOL SHIFT e toque outra tecla (por exemplo, SYMBOL + P
    dá `"`).
  - Trava: um toque rápido em CAPS SHIFT ou SYMBOL SHIFT (a tecla fica amarela) vale para a
    próxima tecla. CAPS travado seguido de SYMBOL entra no modo E, e outro toque na tecla
    travada a destrava. Segurar um deles e tocar o outro também entra no modo E, sem travar
    nenhum dos dois.
- **LOAD ""**: digita `LOAD ""` + ENTER, como no boot, e dá play na fita (rebobina se ela já
  chegou ao fim). Com um jogo rodando, reinicia antes, como ao abrir a fita; enquanto o `LOAD ""`
  automático de um jogo recém-aberto ainda está sendo digitado, só avisa "Carregando...". Sem fita
  no gravador, abre o navegador de jogos.
- **MENU**: abrir jogo, abrir do aparelho (seletor de arquivos do Android), fita play/pausa,
  rebobinar fita, reiniciar, turbo, salvar estado (`.z80`), captura de tela, 50/60 Hz, modelo,
  joystick, diagnóstico, ajuda e sair. Com um jogo conhecido, o título do menu mostra os controles
  dele. **Diagnóstico** liga no canto de cima da imagem `JOY:XX modo`, os bits do joystick que
  chegam ao TK (000FUDLR: 01 direita, 02 esquerda, 04 baixo, 08 cima, 10 fogo); se o número muda
  ao tocar, o toque chegou (no desktop, o item fica no menu do F1). A ajuda tem as mesmas 6 páginas do desktop, com o texto falando dos
  toques (MENU, TECLADO, FOGO); os botões `<` e `>` (ou um toque na metade esquerda/direita)
  trocam de página e FECHAR fecha. A emulação fica pausada enquanto o menu está aberto. Turbo, 50/60 Hz e joystick mostram o
  valor novo e deixam o menu aberto.
- **Navegador de jogos**: um toque abre o arquivo ou entra no diretório, e arrastar rola a lista
  (no desktop, a roda do mouse também rola). No Android ele começa nos jogos que vêm no app. A
  primeira linha, "Abrir arquivo do aparelho...", abre o seletor de documentos do sistema.
- **Voltar** (botão do Android, ou Esc): fecha o menu, o navegador ou o teclado. Sem nada aberto,
  pergunta se quer sair, e um segundo Voltar confirma.
- No Android, a emulação e o som param quando o app sai da tela ou perde o foco. Snapshots e
  capturas vão para a pasta do app no armazenamento externo
  (`Android/data/<pacote>/files`, acessível pelo cabo USB).

Para testar a interface sem aparelho há duas opções escondidas. `--touch-script ARQ` (ou
`--key-script ARQ`, o mesmo script, que também serve sem `--touch`) injeta toques sintéticos
(`SDL_FINGER*`), cliques de mouse e teclas (`key F1`, `keydown left`, `keyup left`,
`keydown left_alt`) em quadros dados, acerta os controles pelo nome, mostra o estado (`print`:
teclas, joystick, modo das setas, `editor=1` com a ROM no editor) e lê a memória
(`peek 0x94A2 2`). `--window-shot ARQ.png`
grava a janela inteira ao sair, `--window-size LxA` fixa o tamanho da janela e
`--safe-insets E,A,D,B` simula o recorte da câmera de um celular (pixels cobertos à esquerda, no
alto, à direita e embaixo; por exemplo `136,0,0,0`). Com
`SDL_VIDEODRIVER=offscreen`, tudo roda sem tela. A sintaxe do script está comentada em `src/main.c`
(procure `--touch-script`). Um exemplo que abre o teclado e digita `LOAD ""` com SYMBOL segurado:

```
# QUADRO COMANDO ARGUMENTOS  (+N = N quadros depois da linha anterior)
100 tap @btn:KBD
+20 tap @key:J
+15 down 1 @key:SYM
+6 tap @key:P
+12 tap @key:P
+12 up 1
+10 tap @key:ENTER
+30 shot load.png
+5 quit
```

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ./build/tk90x --touch --window-size 2400x1080 --touch-script load.txt
```

## Android

O emulador também vira um app Android (5.0 ou mais novo; ARM 64 e 32 bits e x86_64), com os
controles de toque acima, tela cheia na horizontal e as fitas de `games/` já incluídas. Os APKs
saem de:

```sh
make android
# build/android/tk90x-1.3-release.apk   <- o que se instala no celular
# build/android/tk90x-1.3-debug.apk     <- depuração
```

O kit de compilação (JDK, SDK, NDK, Gradle e SDL2) fica todo em `tools/android/`, sem instalar
nada no sistema. Como instalar no celular, usar, compilar e depurar está em
[`docs/ANDROID.md`](docs/ANDROID.md). A versão 1.0 rodou num celular (os jogos abriam, mas o
joystick parecia morto nos jogos que começam no teclado); a 1.1 traz os perfis dos jogos, a 1.2
os revisa jogo a jogo e a 1.3 corrige a imagem que congelava em celulares com recorte da câmera
(notch). Cada versão instala por cima das anteriores.

## Modelos e 50/60 Hz

| Modelo | ROM | Observação |
|---|---|---|
| **TK95** (padrão) | `roms/tk95.rom`, CRC32 `17368e07` | ROM de 2ª geração, a mais compatível |
| TK90X | `roms/tk90x.rom`, CRC32 `3e785f6f` | mesmo código do TK95, só muda a faixa da abertura |
| TK90X v1 | `roms/tk90x-v1.rom`, CRC32 `d1b2f352` | ROM original de 1985. Trata IM 2 de um jeito que quebra cerca de 20% das demos |

Todos são 48K, com a mesma placa e a mesma ULA da Microdigital. O relógio é de 3,575611 MHz, com
228 T-states por linha:

- **50 Hz** (padrão): 312 linhas, 71.136 T por quadro, 50,26 quadros/s. Os jogos europeus foram
  feitos para 50 Hz e rodam na velocidade e no ritmo de música certos. No aparelho real era a
  modificação "chave 50 Hz" (pino 1 da ULA aterrado), comum entre os donos de TK.
- **60 Hz** (`--60hz` ou F8): 262 linhas, 59.736 T por quadro, 59,86 quadros/s. É o padrão de
  fábrica brasileiro (PAL-M). Os jogos ficam cerca de 20% mais rápidos, e alguns efeitos de borda e
  multicolor quebram (por exemplo, Comando Tracer trava).

Diferenças de compatibilidade das ROMs TK em relação à ROM Sinclair: os tokens UDG/TRACE nos
códigos 0 e 1, o `RST 18h` desviado para 0x3CFB e as mensagens em português. Detalhes em
`docs/research/tk95.md` e `docs/research/tk90x-hardware.md`.

As ROMs ficam embutidas no executável. Se houver uma pasta `roms/` ao lado do executável (ou no
diretório atual) com os mesmos nomes, ela tem preferência.

## Créditos das ROMs

As ROMs são da Microdigital Eletrônica. As imagens usadas são as distribuídas pelos emuladores
MAME e ZEsarUX:

- `tk95.rom` (CRC32 `17368e07`, SHA1 `94edc401d43b0e9a9cdc1d35de4b6462dc414ab3`): idêntica à do
  MAME (`ROM_START(tk95)`) e à do EightyOne.
- `tk90x.rom` (CRC32 `3e785f6f`, SHA1 `9a943a008be13194fb006bddffa7d22d2277813f`): idêntica à do
  MAME (`ROM_START(tk90x)`).
- `tk90x-v1.rom` (CRC32 `d1b2f352`): o arquivo `tk90xs.rom` do ZEsarUX, que é a ROM original de 1985
  (máscara 9128B).

A pesquisa de temporização e de hardware se apoia nas medições do blog
[Cantinho do TK90X](https://cantinhotk90x.blogspot.com/) (Flávio Matsumoto, com Einar Saukas e
Fábio Belavenuto), no clone da ULA em VHDL de Fábio Belavenuto e Victor Trucco, e nos emuladores
ESPectrum, ZEsarUX e MAME. As fontes completas estão em `docs/research/`.

## Testes

```sh
make test         # Z80 (suite do Fuse), fita, snapshot e máquina
make test-zex     # ZEXDOC e ZEXALL (demora alguns minutos)

# front-end sem janela
./build/tk90x --headless --frames 300 --screenshot boot.png
./build/tk90x --headless --until-loaded tests/data/dogmole.tap --screenshot dogmole.png
```

O modo headless imprime um resumo numa linha só, fácil de conferir em scripts:

```
frames=10519 emu_seconds=209.27 wall_seconds=2.391 speed=87.5x model=TK95 hz=50 tape_block=5/6 tape_pos=100% tape_playing=0 loaded=1 pc=0xC05F shown_seconds=15.6
```

`frames` conta quadros emulados; `shown_seconds` é o tempo que a mesma sessão levaria na janela
(quadros exibidos: durante a carga, cada um vale 20 emulados). No exemplo, 15,6 s = boot e
`LOAD ""` (2,4 s) + carga (10,2 s) + os 150 quadros finais do `--until-loaded` (3 s).

Para conferir o som da carga: `./build/tk90x --headless --until-loaded --wav-out carga.wav jogo.tap`.

Com CMake: `cmake -B b -DTK_BUILD_TESTS=ON && cmake --build b && (cd b && ctest -LE slow)`.

Para diagnosticar a temporização, `TK90X_STATS=1 ./build/tk90x` imprime no terminal, a cada
segundo, os quadros emulados e exibidos por segundo e o tamanho da fila de áudio.

## Limitações conhecidas

- Só máquinas 48K: snapshots de 128K, +3 ou Timex são recusados, e não há som AY.
- A contenção de memória segue o padrão do 48K (6,5,4,3,2,1,0,0) com a temporização do TK, como
  mostra o VHDL do clone da ULA Microdigital; a contenção de E/S usa as regras do 48K, não as
  tabelas específicas do TK que o ESPectrum aplica. `--no-contention` desliga a contenção.
- Não há gravação de fita (`SAVE`). Para guardar o estado, use o snapshot (F6).
- Na carga acelerada (20x ou turbo), o som é o de um quadro em cada N emulados: a altura é a
  original, mas não é o áudio contínuo da fita.
- O modo 60 Hz é fiel ao aparelho, mas parte dos jogos europeus roda rápido demais ou com efeitos
  quebrados. Use 50 Hz (o padrão) para jogar.
- A fonte da tela (menus e ajuda) é a da própria ROM, só ASCII. Nomes de arquivo com acento aparecem
  sem acento, também no nome da fita virtual (`Program: PASSARO AC`).
