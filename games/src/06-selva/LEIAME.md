# A Selva de Verdemor (e o Disco de Âmbar)

Aventura de ação num labirinto de selva, vista de cima e com troca de tela, para
**TK90X/TK95** (e qualquer ZX Spectrum 48K). É um jogo **original**: mapa, desenhos,
criaturas, nomes, textos e sons foram feitos para este projeto. A inspiração vem só do
gênero, o dos labirintos de selva de 1984. Nenhum mapa, sprite, nome ou música de outro
jogo foi usado.

É o segundo jogo do motor LABIRINTO, o mesmo de *A Fuga do Castelo Corvoalto*
(`games/src/04-castelo`). O motor foi copiado para cá e ganhou opções novas (veja abaixo);
o castelo não foi alterado.

- Fita: `games/06-sabre-wulf-alt-selva-de-verdemor.tap` (cerca de 11,4 KB, carregador BASIC + bloco CODE)
- Carga: `LOAD ""` (o emulador digita sozinho ao abrir o arquivo)
- Licença: MIT (veja o fim deste arquivo), © 2026 Projeto Emulador TK90X

## História

Nina Ventura, cartógrafa, caiu com seu balão na **Selva de Verdemor**: um labirinto vivo de
sebes que se fecha atrás de quem entra. A única saída é o portal do **Templo das Mil
Raízes**, que só abre com o **Disco de Âmbar**. O Guardião da selva partiu o disco em
**4 pedaços** e os escondeu nos quatro cantos da mata.

Nina sopra dardos com a zarabatana, come as frutas que encontra (a energia acaba depressa),
usa runas de pedra para desfazer os espinheiros coloridos e cheira as flores, que dão
efeitos passageiros, nem sempre bons.

## Controles

| Ação | Teclado | Joystick Sinclair (padrão do TK) | Cursor |
|---|---|---|---|
| Cima | Q | 9 | 7 |
| Baixo | A | 8 | 6 |
| Esquerda | O | 6 | 5 |
| Direita | P | 7 | 8 |
| Soprar dardo | ESPAÇO | 0 | 0 |

O joystick Sinclair e o Cursor usam as mesmas teclas (6, 7 e 8) para direções diferentes.
Por isso, na tela de título, a tecla **1** escolhe o Sinclair (padrão) e a **2** escolhe o
Cursor. Q/A/O/P + ESPAÇO funcionam sempre. ESPAÇO, 0 ou ENTER começam a partida.
A tela de título alterna duas páginas: a história e os controles, com as criaturas e as flores.

## Como se joga

- **Energia**: a barra cai sozinha (1 ponto a cada 2 s) e cai mais quando uma criatura
  encosta em Nina. A criatura que encosta some numa nuvem de folhas. Manga (+40), coco (+32)
  e banana (+24) repõem a energia.
- **Vidas**: são 3. Quando a energia zera, Nina perde uma vida e volta ao ponto por onde
  entrou na tela, com a energia cheia e sem efeito de flor.
- **Zarabatana**: um dardo por vez, na direção em que Nina olha, com alcance de 84 pixels.
- **Runas**: vermelha, amarela e branca. Cada uma desfaz, para sempre, os espinheiros da
  mesma cor; basta encostar no espinheiro com a runa. Sem ela, o espinheiro só faz "tum".
- **Disco de Âmbar**: os 4 pedaços (que piscam) abrem o portal do Templo das Mil Raízes.
  Quem atravessar o portal vence (+2000 pontos).
- **Tocas**: buracos entre raízes ligam partes distantes da selva. Pise na toca para entrar;
  saia dela e pise de novo para voltar.
- **Flores** (renascem quando Nina volta à tela; o ícone ao lado das runas mostra o efeito):

  | Flor | Efeito | Duração | Cor de Nina |
  |---|---|---|---|
  | Flor-do-vento (amarela, pétalas em hélice) | corre o dobro | 10 s | amarela |
  | Flor-de-cristal (branca, estrela) | invulnerável: a criatura que encosta é derrotada | 8 s | cintila branco/ciano |
  | Flor-tonta (roxa, espiral) | confusão: esquerda/direita e cima/baixo trocados | 6 s | magenta |

  Nos 2 últimos segundos a cor de Nina pisca, avisando que o efeito vai acabar.
- **Criaturas** (todas inventadas para este jogo):

  | Criatura | Como age | Dardos | Dano | Pontos |
  |---|---|---|---|---|
  | Névoa (espírito do brejo, ciano) | persegue devagar e atravessa as sebes | 1 | 12 | 50 |
  | Zunzum (libélula gigante, magenta) | voa rápido em diagonal e quica nas sebes | 1 | 8 | 30 |
  | Casco (besouro de carapaça, amarelo) | persegue contornando as sebes | 2 | 16 | 100 |
  | Bocão (planta carnívora, vermelha) | fica parada, abrindo e fechando a boca | 3 | 20 | 150 |

  As criaturas surgem numa nuvem de folhas em pontos livres da tela, longe de Nina, e
  voltam alguns segundos depois de derrotadas.
- Pontos também por runa (100), pedaço do disco (500), fruta (50), flor (10) e espinheiro desfeito (20).

## Mapa

São 30 telas numa grade de 6 x 5, em 5 regiões (Mata Alta, Brejo, Mata Fechada,
Espinhais e Ruínas), cada tela com nome. O nome aparece no painel junto com a região.
`-` e `|` são passagens livres; `V`, `A` e `B` são espinheiros vermelho, amarelo e branco;
`P` é o portal do templo. A partida começa na Clareira do Balão (`*`).

```
                                               P
[Ninho Zunz]  [Bosque Cip]--[Escadaria ]--[  TEMPLO  ]  [Pátio Ídol]--[Torre Tomb]
     |             |                           |             |
[ Cl. Ecos ]BB[Tr.Sinuosa]--[Raízes Gêm]  [Portal Ped]AA[Gal. Musgo]--[Col.Caídas]
                                 |             |                           A
[Lagoa Esc.]--[Brejo Név.]--[Ponte Tron]--[Encruzilh.]  [Campo Esp.]--[Esp. Rubro]
     |                                         |             |
[Charco Ver]--[Ilha Junc.]  [Mata Suss.]--[*Cl.Balão ]VV[Vale Flor.]--[Toca Casco]
     V             |             |             |             |             B
[Poço Lama ]  [Árvore Oca]--[Samambaial]--[Tr.Riacho ]AA[Jard.Carn.]--[Fim Trilha]
```

- Tocas: Árvore Oca ↔ Escadaria Verde e Lagoa Escura ↔ Trilha do Riacho.
- Pedaços do disco: Ninho dos Zunzuns, Torre Tombada, Poço de Lama e Fim da Trilha.
- Runas: vermelha na Árvore Oca, amarela no Espinheiro Rubro, branca nas Colunas Caídas.
- O `mapa.py` confere ao montar que todas as telas, passagens, tocas e objetos podem ser
  alcançados, que nenhuma tela tem bolsão fechado e que o jogo pode ser vencido, simulando
  a ordem de coleta das runas.

## Como montar

Requisitos: `python3` e o `sjasmplus` de `tools/zxdev/bin` (já incluso no projeto).

```sh
cd games/src/06-selva
./build.sh               # gera gerado/selva.tap e copia para games/06-sabre-wulf-alt-selva-de-verdemor.tap
./build.sh --sem-copiar  # só monta
teste/rodar.sh           # testes automáticos no núcleo do emulador (telas em gerado/telas)
NUCLEO=/outra/pasta teste/rodar.sh   # usa uma cópia dos fontes do núcleo em vez de src/
```

O `build.sh` roda três etapas:

1. `motor/gfx2asm.py graficos.txt gerado/graficos.inc` converte os desenhos em texto em dados.
2. `mapa.py gerado/mapa.inc` gera o mapa e o valida.
3. O `sjasmplus` monta `selva.asm` e grava o TAP com o carregador BASIC
   (`10 CLEAR 32767: LOAD ""CODE: RANDOMIZE USR 32768`).

Os testes em `teste/` compilam `teste/harness.c` com o núcleo do emulador (`src/`) e rodam
roteiros que carregam a fita pelo caminho original (`LOAD ""` digitado, fita tocando). Eles
seguram teclas, leem e gravam a memória pelos símbolos do `.sym` e gravam PNGs:

| Roteiro | O que confere |
|---|---|
| `01-movimento-telas-comida` | título, início, QAOP nas 4 direções, troca de tela para N, S, O e L, manga, ritmo de 25 passos/s |
| `02-zarabatana-criaturas` | dardo nas 4 direções; Névoa com 1 dardo, Casco com 2, Bocão com 3; pontos; Zunzum encostando tira 8 de energia |
| `03-flores` | velocidade (4 px por passo), fim do efeito, confusão (P vai à esquerda, Q desce), flor que renasce, invulnerável (Casco encosta e morre, energia não cai) |
| `04-espinheiros-tocas` | espinheiro vermelho fechado sem runa e aberto com ela; toca de ida e volta |
| `05-energia-fim-vitoria` | energia caindo, vida perdida, fim de jogo, volta ao título, nova partida, portal aberto com 4 pedaços e vitória |
| `06-controles` | página 2 do título, joystick Sinclair, troca para Cursor pela tecla 2, Cursor, QAOP sempre |
| `07-caminho-runa-vermelha` | a pé, sem teletransporte: Clareira do Balão → Mata dos Sussurros → Samambaial → Árvore Oca (runa vermelha) → toca → Escadaria Verde |

Como o `rodar.sh` compila com o `src/` do projeto, uma mudança na API do núcleo
(`machine.h`) pode quebrar o harness; nesse caso use `NUCLEO=` com uma cópia estável.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `build.sh` | monta o jogo (veja "Como montar") |
| `selva.asm` | arquivo principal do jogo: opções do motor, carregador, regras, tabelas de criaturas, objetos e flores, textos, telas, som do dardo e melodia da vitória |
| `graficos.txt` | todos os desenhos (sprites 16x16, dardos 8x8, objetos, sebes, espinheiro, portal, toca, árvore, pedra, ícones) |
| `mapa.py` | as 30 telas, os 14 modelos de sebe, passagens, tocas, objetos e criaturas |
| `motor/engine.asm` | o motor LABIRINTO, versão 2 (reutilizável, veja abaixo) |
| `motor/gfx2asm.py`, `motor/mapa_lib.py` | ferramentas do motor |
| `teste/` | harness de testes e roteiros |
| `gerado/` | saída do build (`.inc`, `.tap`, `.sym`, `.lst`) |
| `LEIAME.md`, `LICENSE` | este texto e a licença MIT |

## Motor LABIRINTO, versão 2

`motor/engine.asm` continua sem nada da selva. As novidades são opções que o jogo liga com
`DEFINE` antes do `include`; sem elas o motor se comporta como no castelo:

- `HEDGE_EDGES`: cada célula de parede que encosta em algo que não é parede recebe uma
  franja recortada (folhas) daquele lado. Assim retângulos viram moitas de bordas
  orgânicas, sem desenhos extras.
- `SHADOW_ATTRS`: as cores dos objetos são montadas numa sombra em 0xE000 e copiadas para a
  tela depois, uma célula por vez e já com o valor final. Nenhum objeto passa pela cor do
  chão no meio do quadro. Sem isso, criaturas na metade de baixo da tela piscavam em cinza
  num quadro sim, noutro não.
- `PROJ_DIRECTIONAL`: o projétil usa um desenho por direção (o dardo aponta para onde vai).
- `BEH_STATIC`: comportamento 3, criatura parada que só anima (o Bocão).
- `RANDOM_SPAWN`: as criaturas surgem em pontos livres sorteados na tela, em vez de 8 pontos fixos.
- `FLOWERS`: objetos de efeito 3 com velocidade, invulnerabilidade e confusão temporárias,
  ícone no painel, flores que renascem e som próprio.
- `GAME_TUNE`, `GAME_SHOOT_SND`: o jogo fornece a melodia da vitória e o som do tiro.

O `mapa_lib.py` ganhou `modelo_arte()`, que monta modelos de tela a partir de desenhos em
texto (blocos de 16x16), e a verificação de bolsões fechados.

Técnica herdada do motor: tela de 32x20 células com painel nas linhas 20 a 23; sprites
16x16 em XOR, pré-deslocados em 4 posições (passos de 2 pixels); 25 passos por segundo;
até 4 criaturas por tela; acentos do português montados sobre a fonte da ROM; som no beeper.
Memória: código e dados de 0x8000 a cerca de 0xAC00; os buffers de 0xC000 a 0xE27F são
preenchidos na hora e não vão para a fita.

## Limitações conhecidas

- Os sprites são monocromáticos por célula, como no Spectrum. Quando dois objetos dividem a
  mesma célula de 8x8, um deles fica com a cor do outro; a cor de Nina prevalece.
- A Névoa atravessa as sebes desenhada em XOR por cima delas: é proposital, mas os pixels se
  misturam enquanto ela passa.
- A troca de tela leva cerca de um quarto de segundo (13 quadros), porque as franjas das
  sebes são calculadas célula a célula. A tela nova aparece de uma vez.
- O progresso não é gravado (não há senha nem save). A partida recomeça na Clareira do Balão.
- Há só um dardo na tela por vez e só 4 direções de tiro.
- Os efeitos sonoros param o jogo por uma fração de segundo (beeper sem interrupção), como
  nos jogos da época.
- Não há música durante o jogo; só uma melodia curta na vitória.

## Licença (MIT)

Copyright (c) 2026 Projeto Emulador TK90X

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
