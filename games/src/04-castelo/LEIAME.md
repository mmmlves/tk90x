# A Fuga do Castelo Corvoalto

Aventura de ação vista de cima, com troca de tela por salas, para **TK90X/TK95**
(e qualquer ZX Spectrum 48K). É um jogo **original**: mapa, desenhos, personagens, textos e
sons foram feitos para este projeto. A inspiração vem só do gênero, o dos labirintos de
castelo de 1983–84. Nenhum mapa, sprite, nome ou música de outro jogo foi usado.

- Fita: `games/04-atic-atac-alt-castelo-corvoalto.tap` (9,5 KB, carregador BASIC + bloco CODE)
- Carga: `LOAD ""` (o emulador digita sozinho ao abrir o arquivo)
- Licença: MIT (veja o fim deste arquivo), © 2026 Projeto Emulador TK90X

## História

Lia, aprendiz de alquimista, foi trancada no Castelo Corvoalto pelo terrível Barão Sombrio.
O portão principal só abre com a **Chave Mestra**. O Barão partiu a chave em **3 pedaços**
e os escondeu entre o porão e o andar superior.

Fantasmas, morcegos e cavaleiros esqueletos rondam os salões. Lia lança faíscas mágicas,
procura as chaves coloridas que abrem as portas trancadas e come o que encontra pelo
caminho, porque a energia acaba depressa. Com os três pedaços, ela foge pelo Portão Principal.

## Controles

| Ação | Teclado | Joystick Sinclair (padrão do TK) | Cursor |
|---|---|---|---|
| Cima | Q | 9 | 7 |
| Baixo | A | 8 | 6 |
| Esquerda | O | 6 | 5 |
| Direita | P | 7 | 8 |
| Lançar faísca | ESPAÇO | 0 | 0 |

O joystick Sinclair e o Cursor usam as mesmas teclas (6, 7 e 8) para direções diferentes.
Por isso, na tela de título, a tecla **1** escolhe o Sinclair (padrão) e a **2** escolhe o
Cursor. Q/A/O/P + ESPAÇO funcionam sempre. ESPAÇO, 0 ou ENTER começam a partida.

## Como se joga

- **Energia**: a barra cai sozinha (1 ponto a cada 1,6 s) e cai mais quando um monstro toca
  em Lia. O monstro que toca some numa nuvem. Frango (+40), pão (+32) e maçã (+24) repõem a energia.
- **Vidas**: são 3. Quando a energia zera, Lia perde uma vida e volta à porta por onde
  entrou na sala, com a energia cheia.
- **Chaves**: vermelha, verde, azul e amarela. Cada uma abre, para sempre, as portas da
  mesma cor; basta encostar na porta com a chave. Sem a chave, a porta só faz "tum".
- **Chave Mestra**: os 3 pedaços (losangos que piscam) abrem a porta que pisca no Portão
  Principal. Quem atravessar essa porta vence (+1000 pontos).
- **Escadas**: pise nos degraus para subir ou descer de andar.
- **Monstros**: o fantasma persegue devagar e atravessa móveis (50 pontos); o morcego voa
  rápido em diagonal e quica nas paredes (30 pontos); o cavaleiro esqueleto contorna os
  obstáculos e aguenta 2 faíscas (100 pontos). Os monstros voltam alguns segundos depois de derrotados.
- Pontos também por chave (100), pedaço (500), comida (50) e porta aberta (20).

## Mapa

São 24 salas em 3 andares, todas com nome, que aparece no painel junto com o andar:

```
ANDAR SUPERIOR                 TÉRREO                                         PORÃO
[Torre Norte][Biblioteca][Laboratório]  [Corr. Oeste][Capela ][S. do Trono][S. de Armas]   [Cripta][Catacumbas][S. Correntes]
[S. Música ][Escritório][Quarto Barão]  [Jardim Inv.][Galeria][S. Entrada ][Corr. Leste]   [Adega ][Masmorra  ][Poço Antigo ]
                                          [Despensa  ][Cozinha][Portão Pr. ][Refeitório ]
```

- Escadas: Galeria ↔ Escritório do Barão, Despensa ↔ Adega, Sala de Armas ↔ Sala das Correntes.
- Portas trancadas: vermelha (Salão → Galeria), amarela (Trono → Capela e Escritório →
  Quarto), verde (Despensa → Jardim), azul (Masmorra → Poço) e a da Chave Mestra (Portão → saída).
- O `mapa.py` confere ao montar que todas as salas, portas, escadas e objetos podem ser
  alcançados e que o jogo pode ser vencido, simulando a ordem de coleta das chaves.

## Como montar

Requisitos: `python3` e o `sjasmplus` de `tools/zxdev/bin` (já incluso no projeto).

```sh
cd games/src/04-castelo
./build.sh               # gera gerado/castelo.tap e copia para games/04-atic-atac-alt-castelo-corvoalto.tap
./build.sh --sem-copiar  # só monta
teste/rodar.sh           # testes automáticos no núcleo do emulador (telas em gerado/telas)
```

O `build.sh` roda três etapas:

1. `motor/gfx2asm.py graficos.txt gerado/graficos.inc` converte os desenhos em texto em dados.
2. `mapa.py gerado/mapa.inc` gera o mapa e o valida.
3. O `sjasmplus` monta `castelo.asm` e grava o TAP com o carregador BASIC
   (`10 CLEAR 32767: LOAD ""CODE: RANDOMIZE USR 32768`).

Os testes em `teste/` compilam `teste/harness.c` com o núcleo do emulador (`src/`) e rodam
roteiros que carregam a fita pelo caminho original (`LOAD ""` digitado, fita tocando). Eles
seguram teclas, leem e gravam a memória pelos símbolos do `.sym` e gravam PNGs. Cobrem:
título, início, movimento nas 4 direções (QAOP, Sinclair e Cursor), troca de sala, comida,
tiro (fantasma com 1 faísca, cavaleiro com 2), porta sem e com chave, escadas, perda de
energia, morte, fim de jogo, volta ao título e vitória pela porta da Chave Mestra. Um dos
roteiros vai a pé do Salão até a chave vermelha na Cripta, passando por 6 salas e uma escada.
Outro abre portas trancadas encostando de viés (a caixa de Lia tocando a parede e a porta).

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `castelo.asm` | arquivo principal do jogo: carregador, regras, tabelas de monstros e objetos, textos, telas |
| `graficos.txt` | todos os desenhos (sprites 16x16, faísca 8x8, objetos, ladrilhos, ícones) |
| `mapa.py` | as 24 salas, portas, escadas, objetos e monstros |
| `motor/engine.asm` | o motor (reutilizável, veja abaixo) |
| `motor/gfx2asm.py`, `motor/mapa_lib.py` | ferramentas do motor |
| `teste/` | harness de testes e roteiros |
| `gerado/` | saída do build (`.inc`, `.tap`, `.sym`, `.lst`) |

## Motor LABIRINTO (reutilizável)

`motor/engine.asm` não contém nada do castelo. Um segundo jogo do mesmo gênero (uma selva,
uma nave, uma mina...) precisa só de um novo `jogo.asm`, de um `graficos.txt` e de um `mapa.py`.
O cabeçalho do `engine.asm` lista todos os rótulos e constantes que o jogo fornece.

Técnica:

- **Tela**: a sala ocupa 32x20 células (linhas 0–19). O painel fica nas linhas 20–23. A
  sala é montada num mapa de células (`CMAP`) a partir de um registro compacto de 21 bytes:
  nome, andar, cores da parede, do chão e da mobília, estilo da parede (4 ladrilhos de
  16x16), modelo de mobília (lista de retângulos), 4 saídas (sala + porta), escada e
  monstros (tipo e quantidade). As 24 salas ocupam 504 bytes, mais nomes e modelos.
- **Troca de tela**: ao cruzar o vão de uma porta (N/S/O/L) ou pisar numa escada, a sala nova
  é desenhada com os atributos apagados e aparece de uma vez.
- **Sprites**: 16x16, com 2 quadros por direção, desenhados com XOR e **pré-deslocados em
  4 posições** na inicialização (movimento de 2 em 2 pixels na horizontal, sem máscara). A
  cada passo, o motor apaga e redesenha cada objeto logo em seguida, e só se ele mudou. Depois
  pinta os atributos por baixo, só nas células de chão, mantendo papel e brilho do fundo.
- **Ritmo**: 25 passos por segundo (a cada 2 quadros, pelo contador `FRAMES` da ROM). O
  desenho começa logo depois do `HALT`, antes do feixe chegar à área da sala. Nos testes
  quadro a quadro não apareceu sprite sumindo nem cortado.
- **Monstros**: até 4 por sala, com 3 comportamentos prontos: perseguir atravessando
  móveis, voar quicando e perseguir contornando obstáculos. Cada tipo define sprite, cor,
  velocidade, vida, dano e pontos.
- **Texto**: fonte da ROM, com acentos do português montados na hora (UTF-8 `Ã`, `ç`,
  `é`... direto no fonte), mais altura dupla para títulos.
- **Som**: beeper (tiro, acerto, golpe, coleta, porta, escada, morte e vitória).
- **Memória**: código e dados de 0x8000 a cerca de 0xA500. Os buffers de 0xC000 a 0xDFFF
  são preenchidos na hora e não vão para a fita.

## Limitações conhecidas

- Os sprites são monocromáticos por célula, como no Spectrum. Quando dois objetos dividem a
  mesma célula de 8x8, um deles fica com a cor do outro, e a cor do jogador prevalece.
- Os fantasmas atravessam a mobília desenhados em XOR por cima dela: é proposital, mas os
  pixels se misturam enquanto passam.
- As cores são pintadas depois dos desenhos. Na metade de cima da sala, quando um sprite entra
  numa célula nova, a cor dela pode atrasar um quadro (1/50 s) e o pedaço novo aparece com a cor
  do chão nesse quadro.
- O progresso não é gravado (não há senha nem save). A partida recomeça do Salão de Entrada.
- Há só uma faísca na tela por vez e só 4 direções de tiro.
- Os efeitos sonoros param o jogo por uma fração de segundo (beeper sem interrupção), como
  nos jogos da época.
- Não há música.

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
