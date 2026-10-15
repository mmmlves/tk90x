# Jogos para o emulador TK90X/TK95

Esta pasta guarda imagens de fita para testar o emulador: **4 dos 10 jogos clássicos**
escolhidos (05, 08, 09 e 10) e, no lugar dos outros 6, **alternativas livres** — jogos
independentes, de autoria própria, cujo autor permite a redistribuição gratuita (veja
[Alternativas livres](#alternativas-livres)).

Só baixamos um título quando o arquivo oficial **ZXDB / Spectrum Computing / World of Spectrum**
o marca como de distribuição permitida e oferece o download do próprio arquivo (com permissão do
detentor dos direitos), ou quando o próprio autor publica o jogo sob licença livre. Os títulos
com **"Distribution denied"** não foram baixados de lugar nenhum, nem de espelhos de terceiros.

Consulta feita em 14/10/2026 (API `api.zxinfo.dk/v3`, índice ZXDB de 07/10/2026, conferida nas
páginas do Spectrum Computing e do World of Spectrum).

## Situação dos 10 títulos

| # | Título | Editora / ano | Arquivo | Formato | Situação no ZXDB | Origem |
|---|--------|---------------|---------|---------|------------------|--------|
| 1 | Knight Lore | Ultimate Play The Game, 1984 | — | — | **Distribution denied** | [entrada 9366](https://spectrumcomputing.co.uk/entry/9366/) |
| 2 | Manic Miner | Bug-Byte Software, 1983 | — | — | Available (ver observação) | [entrada 3012](https://spectrumcomputing.co.uk/entry/3012/) |
| 3 | Jet Set Willy | Software Projects, 1984 | — | — | Available (ver observação) | [entrada 2589](https://spectrumcomputing.co.uk/entry/2589/) |
| 4 | Atic Atac | Ultimate Play The Game, 1983 | — | — | **Distribution denied** | [entrada 9305](https://spectrumcomputing.co.uk/entry/9305/) |
| 5 | Bruce Lee | U.S. Gold, 1985 | `05-bruce-lee.tzx` | TZX (fita original, Speedlock 1) | Available | https://spectrumcomputing.co.uk/pub/sinclair/games/b/BruceLee.tzx.zip |
| 6 | Sabre Wulf | Ultimate Play The Game, 1984 | — | — | **Distribution denied** | [entrada 9408](https://spectrumcomputing.co.uk/entry/9408/) |
| 7 | Commando | Elite Systems, 1985 | — | — | **Distribution denied** | [entrada 9325](https://spectrumcomputing.co.uk/entry/9325/) |
| 8 | Dizzy: The Ultimate Cartoon Adventure | Code Masters, 1987 | `08-dizzy.tzx` | TZX (versão 48K, carregador ROM) | Available | https://spectrumcomputing.co.uk/zxdb/sinclair/entries/0009332/Dizzy1-TheUltimateCartoonAdventure(48K).tzx.zip |
| 9 | Green Beret | Imagine Software, 1986 | `09-green-beret.tzx` | TZX (fita original, Speedlock 2) | Available | https://spectrumcomputing.co.uk/pub/sinclair/games/g/GreenBeret.tzx.zip |
| 10 | Boulder Dash | Front Runner, 1984 | `10-boulder-dash.tzx` | TZX (fita original, PowerLoad) | Available | https://spectrumcomputing.co.uk/pub/sinclair/games/b/BoulderDash.tzx.zip |

### Arquivos baixados (verificação)

Todos são TZX com a assinatura `ZXTape!\x1A`, e a leitura percorreu os blocos até o fim do
arquivo sem erro. Os blocos padrão da ROM (0x10) têm checksum correto. Os blocos dos
carregadores turbo (Speedlock, PowerLoad) usam esquema próprio, e por isso o XOR simples não
fecha neles, o que é o esperado.

| Arquivo | Tamanho (bytes) | MD5 | Blocos |
|---------|-----------------|-----|--------|
| `05-bruce-lee.tzx` | 45418 | `51cb1a6e1fb58304a15a588b82d8e001` | 136 |
| `08-dizzy.tzx` | 48885 | `8e405b0255997bcc3714cb26e7359160` | 6 |
| `09-green-beret.tzx` | 50824 | `3e6d792c32c0791fd26341e118532274` | 146 |
| `10-boulder-dash.tzx` | 33375 | `e09a1bf595c52ca07eabfea4c0dd8a11` | 10 |

### Títulos não baixados e motivo

Esses 6 jogos (Knight Lore, Manic Miner, Jet Set Willy, Atic Atac, Sabre Wulf e Commando)
**não são de domínio público**. Jogos de 1983–1986 continuam protegidos por direito autoral (no
Brasil, 70 anos), e os detentores dos direitos existem e não liberaram a distribuição. Encontrar
cópias espalhadas pela internet não muda isso.

- **Knight Lore, Atic Atac, Sabre Wulf** (Ultimate Play The Game): o ZXDB marca
  *Distribution denied*. O Spectrum Computing informa que "these files are distribution denied
  and you can't download them". O ZXDB não detalha o motivo. Os direitos da Ultimate passaram
  para a Rare, hoje da Microsoft, e o detentor não autorizou a distribuição.
- **Commando** (Elite Systems, licença do arcade da Capcom): *Distribution denied*, com o mesmo
  aviso no Spectrum Computing. O detentor dos direitos (Elite Systems) não autorizou a
  distribuição.
- **Manic Miner e Jet Set Willy** (direitos de Matthew Smith, autor vivo): a situação é
  **ambígua**, então ficaram de fora por cautela.
  O campo de disponibilidade do ZXDB diz *Available*, e a página do World of Spectrum também
  diz *Available* e mostra links de download. Já a página do Spectrum Computing, na seção
  "Main File Downloads", bloqueia os arquivos com o aviso "Unfortunately these files are
  distribution denied and you can't download them", sem explicar o motivo. Antes de incluí-los,
  confirme a situação. Se a permissão for confirmada, os arquivos recomendados são
  `ManicMiner.tzx.zip` (Bug-Byte original) e `JetSetWilly_2.tzx.zip` ou `JetSetWilly(original).tap.zip`.

**Use a sua própria cópia:** para qualquer título não baixado, quem tiver uma cópia legal (por
exemplo, a fita original digitalizada por você mesmo) pode colocar o arquivo `.tzx`, `.tap`,
`.z80` ou `.sna` nesta pasta, seguindo o padrão de nome (`01-knight-lore.tzx`,
`02-manic-miner.tzx`, `03-jet-set-willy.tzx`, `04-atic-atac.tzx`, `06-sabre-wulf.tzx`,
`07-commando.tzx`). Use sempre a versão 48K.

## Alternativas livres

Para cada clássico que não pôde ser incluído, escolhemos um jogo **original** (código, gráficos,
mapas e música próprios) com jeito parecido e redistribuição gratuita permitida pelo autor.
Ficaram de fora, de propósito, os *hacks* e "remakes" feitos sobre o motor ou os dados dos
originais (por exemplo, as centenas de variações de Manic Miner e Jet Set Willy que reaproveitam o
motor de Matthew Smith), as releituras com personagens de terceiros (Willy, Sabreman, Horace,
Mario…) e os jogos sem licença explícita. Nenhum arquivo dos jogos originais foi baixado ou usado.

Nenhuma das alternativas foi desenvolvida por nós: todas são jogos de terceiros (não há
`games/src/`). Consulta feita em 14/10/2026.

| # | Clássico | Arquivo | Jogo (autor, ano) | Licença |
|---|----------|---------|-------------------|---------|
| 1 | Knight Lore | `01-knight-lore-alt-deep-blue.tap` | Deep Blue (Errazking, 2017) | Freeware, ZXDB *Available* |
| 2 | Manic Miner | `02-manic-miner-alt-frank-n-stein-rebooted.tzx` | Frank N Stein Re-booted v2.17 (Colin Stewart, 2011) | Freeware, ZXDB *Available* |
| 3 | Jet Set Willy | `03-jet-set-willy-alt-lala-prologue.tzx` | Lala Prologue (The Mojon Twins, 2010) | CC BY-NC-SA 3.0 |
| 4 | Atic Atac | `04-atic-atac-alt-castelo-corvoalto.tap` | A Fuga do Castelo Corvoalto (Projeto Emulador TK90X, 2026), jogo original nosso | MIT (fonte em `src/04-castelo/`) |
| 6 | Sabre Wulf | `06-sabre-wulf-alt-selva-de-verdemor.tap` | A Selva de Verdemor (Projeto Emulador TK90X, 2026), jogo original nosso; extra: `extras/06-extra-doomskulle.tzx` | MIT (fonte em `src/06-selva/`) |
| 7 | Commando | `07-commando-alt-sgt-helmet-training-day.tap` | Sgt. Helmet – Training Day (The Mojon Twins, 2013) | CC BY-NC-SA 3.0 |

> **Sobre o 04:** não achamos uma alternativa livre de terceiros que encaixasse bem em Atic Atac.
> Por isso o 04 é um jogo original nosso, feito para este projeto (código, mapa, desenhos, textos
> e sons próprios).

### 01 — Knight Lore → *Deep Blue*

- **Arquivo:** `01-knight-lore-alt-deep-blue.tap` (TAP, carregador da ROM, 48K)
- **Autor / ano:** Errazking (Igor Errazkin), 2017.
- **Licença:** *Freeware (free but copyrighted)*; o ZXDB marca a disponibilidade como
  *Available* e o Spectrum Computing oferece o arquivo para download. Feito com o *3D Game Maker*
  da CRL (1987), também *Available* no ZXDB. Evidência: https://spectrumcomputing.co.uk/entry/30418
- **Origem:** https://spectrumcomputing.co.uk/zxdb/sinclair/entries/0030418/DeepBlue.tap.zip
- **Semelhança:** aventura isométrica em 3D com troca de tela, no estilo *Filmation* de Knight
  Lore: explorar salas e recolher objetos. O tema é submarino e original, e o motor é o do 3D Game
  Maker, não o da Ultimate. Não há a transformação dia/noite.

### 02 — Manic Miner → *Frank N Stein Re-booted*

- **Arquivo:** `02-manic-miner-alt-frank-n-stein-rebooted.tzx` (TZX, carregador da ROM, 48K)
- **Autor / ano:** Colin Stewart, autor do *Frank N Stein* original (PSS, 1984); tela de abertura
  de Einar Saukas. Versão 2.17, 2011.
- **Licença:** *Freeware (free but copyrighted)*, disponibilidade *Available*, com o TZX entre os
  "Main File Downloads". Evidência: https://spectrumcomputing.co.uk/entry/26834 . O jogo é do
  próprio autor e não compartilha código, gráficos nem fases com Manic Miner. (Edições físicas
  foram vendidas depois — ZX Vega, Midnight Brew —, mas a versão digital gratuita de 2011 segue
  *Available*.)
- **Origem:** https://spectrumcomputing.co.uk/pub/sinclair/games/f/FrankNSteinRe-bootedV2.17.tzx.zip
- **Semelhança:** plataforma de tela única com muitas fases, como Manic Miner. Em cada tela é
  preciso juntar as peças do monstro, na ordem, desviando de inimigos com rota fixa. Um cronômetro
  faz o papel do ar de Manic Miner, e há gelo, molas, postes e teletransportes. A análise da RGCD
  (2011) o põe na linhagem Miner 2049er / Manic Miner.
- **Controles:** o menu oferece teclado, Kempston, Protek/Cursor e **Sinclair ZX2** (o joystick
  embutido do TK90X).

### 03 — Jet Set Willy → *Lala Prologue*

- **Arquivo:** `03-jet-set-willy-alt-lala-prologue.tzx` (TZX, carregador da ROM, 48K)
- **Autor / ano:** The Mojon Twins (na_th_an, José Luis Castaño González), 2010, lançado pelo selo
  Ubhres Productions. Motor La Churrera/MK1 e splib2 (Alvin Albrecht).
- **Licença:** **Creative Commons BY-NC-SA 3.0**. A página de licença dos autores diz: "All the
  content on this site, and all of our creative work, including source code, are licensed under
  Creative Commons 3.0 Attribution-NonCommercial-ShareAlike 3.0 Unported (CC BY-NC-SA 3.0.)", e
  em espanhol que se pode "Copiar, distribuir y comunicar públicamente la obra". Evidência:
  https://www.mojontwins.com/licencia-mt/ (o ZXDB, entrada 24369, também o marca como *Freeware*,
  *Available*).
- **Origem:** página oficial https://www.mojontwins.com/juegos_mojonos/lala-prologue/ , arquivo
  https://www.mojontwins.com/juegos/Mojon-Twins--Lala-Prologue.tzx . (A cópia do Spectrum
  Computing, `LalaPrologue.tzx.zip`, tem o mesmo tamanho e estrutura, mas difere em alguns bytes;
  provavelmente é uma revisão menor.)
- **Semelhança:** plataforma de exploração com troca de tela. Lala percorre a academia da sua
  mestra bruxa, o cemitério e as igrejas em ruínas recolhendo 25 poções espalhadas pelas salas,
  com esteiras, guardiões (morcegos e bichos), espinhos mortais e chaves que abrem portões. O jogo
  termina quando todos os itens são recolhidos — o mesmo ciclo de Jet Set Willy de explorar a casa
  sala por sala atrás de cada objeto. Não tem cordas nem rampas.
- **Condições da licença:** manter o crédito aos Mojon Twins ("Powered by La Churrera") e não usar
  comercialmente.

### 04 — Atic Atac → *A Fuga do Castelo Corvoalto* (original deste projeto)

- **Arquivo:** `04-atic-atac-alt-castelo-corvoalto.tap` (TAP, carregador BASIC
  `CLEAR 32767: LOAD ""CODE: RANDOMIZE USR 32768` + um bloco CODE, 48K, 9553 bytes)
- **Autor / ano:** Projeto Emulador TK90X, 2026. Fonte (assembly sjasmplus), instruções de
  montagem e testes em `src/04-castelo/` (veja o `LEIAME.md` de lá).
- **Licença:** **MIT**. Tudo é original: mapa, desenhos, personagens, nomes, textos e sons. Nada
  foi tirado de Atic Atac nem de outro jogo da Ultimate; a semelhança é só de gênero.
- **Semelhança:** aventura de ação vista de cima, com troca de tela por salas, num castelo, como
  Atic Atac. Lia, aprendiz de alquimista, foge do castelo do Barão Sombrio: 24 salas em 3 andares
  (térreo, andar superior e porão) ligadas por portas e escadas, chaves de 4 cores que abrem as
  portas da mesma cor, faíscas mágicas contra fantasmas, morcegos e cavaleiros esqueletos que
  reaparecem, energia que cai com o tempo e volta com comida, e 3 pedaços da Chave Mestra que
  abrem o Portão Principal.
- **Controles:** Q/A/O/P + ESPAÇO sempre; na tela de título, **1** escolhe o joystick
  **Sinclair** (6 a 0, padrão do TK) e **2** o **Cursor** (5 a 8 e 0).

### 06 — Sabre Wulf → *A Selva de Verdemor* (original deste projeto)

- **Arquivo:** `06-sabre-wulf-alt-selva-de-verdemor.tap` (TAP, carregador BASIC
  `CLEAR 32767: LOAD ""CODE: RANDOMIZE USR 32768` + um bloco CODE, 48K, 11351 bytes)
- **Autor / ano:** Projeto Emulador TK90X, 2026. Fonte, instruções de montagem e testes em
  `src/06-selva/` (veja o `LEIAME.md` de lá).
- **Licença:** **MIT**. Tudo é original: mapa, desenhos, criaturas, nomes, textos e sons. Nada
  foi tirado de Sabre Wulf nem de outro jogo da Ultimate; a semelhança é só de gênero.
- **Semelhança:** labirinto de selva visto de cima, com troca de tela, como Sabre Wulf. Nina
  Ventura, cartógrafa, precisa juntar os 4 pedaços do Disco de Âmbar para abrir o portal do
  Templo das Mil Raízes. São 30 telas em 5 regiões, espinheiros que só se desfazem com runas
  coloridas, 2 tocas que ligam pontos distantes, zarabatana, frutas que repõem a energia, 4
  criaturas inventadas e flores com efeitos passageiros (velocidade, invulnerabilidade e confusão).
- **Controles:** Q/A/O/P + ESPAÇO sempre; na tela de título, **1** escolhe o joystick
  **Sinclair** (6 a 0, padrão do TK) e **2** o **Cursor** (5 a 8 e 0).

**Extra — *Doomskulle*** (`extras/06-extra-doomskulle.tzx`, TZX, carregador da ROM, 48K): era a
alternativa anterior do slot 06 e continua aqui como bônus.

- **Autor / ano:** Richard Hutchison (a SPOT/SPEX também cita Richard Cowie, sem confirmação),
  1990, publicado na fita da revista *Crash* nº 77 ("Presents 13", Newsfield).
- **Licença:** **não há licença explícita do autor.** O ZXDB/Spectrum Computing marca
  *Availability: Available* e oferece o TZX e o Z80 em "Main File Downloads"; é com base nisso
  que ele foi incluído. Evidência: https://spectrumcomputing.co.uk/entry/1449 . É a base de
  permissão mais fraca desta pasta: um jogo de leitor distribuído em fita de revista, não um
  homebrew moderno com CC/GPL.
- **Origem:** https://spectrumcomputing.co.uk/pub/sinclair/games/d/Doomskulle.tzx.zip
- **Semelhança:** o ZXDB o registra como "Inspired by: Sabre Wulf". É um labirinto de ação visto
  de cima, com troca de tela, numa selva de folhagens, com criaturas para evitar ou enfrentar e
  objetos e chaves para recolher; mais adiante há catacumbas e ruínas. O ZXDB **não** o marca
  como modificação de Sabre Wulf, e o mapa, os gráficos, o placar e o personagem são próprios.
- **Controles:** o menu oferece redefinir teclas, teclado, cursores, **Sinclair** e Kempston.

### 07 — Commando → *Sgt. Helmet – Training Day*

- **Arquivo:** `07-commando-alt-sgt-helmet-training-day.tap` (TAP, carregador da ROM, 48K)
- **Autor / ano:** The Mojon Twins (na_th_an, Anjuel, Kendroock, Maribip, Davidian), 2013, versão
  em inglês, feito no motor MT Engine MK1 "La Churrera".
- **Licença:** **Creative Commons BY-NC-SA 3.0**, a mesma de toda a obra dos Mojon Twins, código
  incluído: "This means you can freely: – Copy, distribute and transmit our work." Evidência:
  https://www.mojontwins.com/licencia-mt/ (a página do jogo aponta para ela: "Léete la LICENCIA,
  porfaplis!"). No ZXDB, entrada https://spectrumcomputing.co.uk/entry/30034 : *Freeware*,
  *Available*. O motor MK1 no GitHub é GPL-2.0.
- **Origem:** https://www.mojontwins.com/juegos/mojon-twins--sgt-helmet-training-day--eng.zip
  (baixado de https://www.mojontwins.com/juegos_mojonos/sgt-helmet-training-day/ ); o TAP é
  idêntico byte a byte ao do Spectrum Computing
  (`zxdb/sinclair/entries/0030034/Sgt.Helmet-TrainingDay(Eng).tap.zip`).
- **Semelhança:** é a resposta dos próprios autores a Commando. A página do jogo diz: "You don't
  have the guts to make an Ikary Warriors sort of contraption with your engine? You got it!", e o
  nome interno do programa é `COMANDO`. Você é um soldado visto de cima, atira em soldados que não
  param de chegar, explode arame farpado, recolhe munição, empurra pedras para bloquear lasers e
  planta 5 bombas no computador inimigo antes de voltar à base. As diferenças: a tela troca em vez
  de rolar na vertical, e não há granadas.
- **Condições da licença:** manter o crédito aos Mojon Twins e não usar comercialmente.

## Todos os arquivos desta pasta

Conferidos em 14/10/2026 (`ls` e `md5sum`). Os TAP e os TZX das alternativas usam só blocos
padrão da ROM, com todos os checksums corretos e leitura até o fim do arquivo.

| Arquivo | Tipo | Tamanho (bytes) | MD5 |
|---------|------|-----------------|-----|
| `01-knight-lore-alt-deep-blue.tap` | alternativa livre (Knight Lore) | 26094 | `24e3c4a6c9614f1d02096355aea735af` |
| `02-manic-miner-alt-frank-n-stein-rebooted.tzx` | alternativa livre (Manic Miner) | 45314 | `a8091543ffc1c3e02a5ff811c3c4dcce` |
| `03-jet-set-willy-alt-lala-prologue.tzx` | alternativa livre (Jet Set Willy) | 32889 | `f7e3931379d6b74ff0e029bd02e2f264` |
| `04-atic-atac-alt-castelo-corvoalto.tap` | jogo original (alternativa a Atic Atac), MIT | 9553 | `208f48902133147d0f317f2d51e13282` |
| `05-bruce-lee.tzx` | clássico original | 45418 | `51cb1a6e1fb58304a15a588b82d8e001` |
| `06-sabre-wulf-alt-selva-de-verdemor.tap` | jogo original (alternativa a Sabre Wulf), MIT | 11351 | `9c39661b9ccd458a59a5d750625426dd` |
| `extras/06-extra-doomskulle.tzx` | extra do slot 06 (inspirado em Sabre Wulf) | 48156 | `1383847496f38701609a7249a5c0bcda` |
| `07-commando-alt-sgt-helmet-training-day.tap` | alternativa livre (Commando) | 43229 | `a6ed794cdf4c47f7a55a511f50817b22` |
| `08-dizzy.tzx` | clássico original | 48885 | `8e405b0255997bcc3714cb26e7359160` |
| `09-green-beret.tzx` | clássico original | 50824 | `3e6d792c32c0791fd26341e118532274` |
| `10-boulder-dash.tzx` | clássico original | 33375 | `e09a1bf595c52ca07eabfea4c0dd8a11` |
| `LEIAME.md` | este arquivo | — | — |

## Como jogar

Na raiz do projeto, depois de compilar:

```
build/tk90x games/05-bruce-lee.tzx
```

Também dá para abrir o emulador e arrastar o arquivo para a janela (drag-and-drop) ou usar
**F2** para escolher o arquivo. Os jogos em TZX carregam pelo carregador original da fita
(Speedlock e PowerLoad incluídos), e o carregamento leva o mesmo tempo da fita real. Os TAP
(01, 04 e 07) carregam do mesmo jeito, pelo carregador da ROM.

**Controles:** cada jogo tem os seus, normalmente pelo teclado (veja a tela de opções ou as
instruções do jogo). O TK90X/TK95 tem **joystick Sinclair embutido**, que equivale às teclas
**6, 7, 8, 9 e 0** (esquerda, direita, baixo, cima e tiro, como no Interface 2 da Sinclair).
Bruce Lee, Green Beret e Boulder Dash aceitam esse modo ("Interface 2" ou "Sinclair" no menu do
jogo). Dizzy usa o teclado ou o joystick Kempston. Entre as alternativas, Frank N Stein
Re-booted ("SINCLAIR ZX2") e Doomskulle ("SINCLAIR") têm o modo Sinclair no menu; nos demais,
veja a tela de opções do jogo.
