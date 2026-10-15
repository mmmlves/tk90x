# Arquitetura do emulador TK90X / TK95

Emulador dos micros brasileiros da Microdigital TK90X e TK95 (clones do ZX Spectrum 48K),
em **C99 + SDL2**, um mesmo codigo para **Linux** e **Windows**.

## Objetivos de produto

1. Boot real pela ROM: a tela de abertura original (mensagem de copyright da Microdigital) aparece
   como no aparelho, com o cursor `K`.
2. Rodar os formatos de programas: `.tap`, `.tzx`, `.sna`, `.z80`, `.scr` e binarios crus `.bin`/`.cod`.
3. Carregar "como de fita": depois do boot o emulador digita `LOAD ""` + ENTER no teclado
   emulado, da play na fita e a ROM carrega pelo seu proprio LD-BYTES: aparece `Program: NOME`,
   listras na borda, a tela sendo desenhada linha a linha. So que muito mais rapido, porque
   o emulador acelera enquanto a fita toca: por padrao 20x (carga nostalgica: som e listras
   originais; cada quadro exibido e um quadro emulado inteiro e o audio e o desse quadro, na
   altura original). `--load-speed 0` ou F7: sem limite de velocidade.
   - Snapshots e binarios viram uma **fita virtual** (ver `src/snapshot.h`), de modo que tambem
     carregam pela ROM; ao fim do bloco de 48K o estado da CPU e restaurado.
   - Opcao `--instant`: snapshot direto, sem fita. Opcao `--flash`: armadilha na ROM (blocos padrao instantaneos).
4. Modelos (todos 48K): `--model tk95` (padrao), `--model tk90x`, `--model tk90x-v1` (ROM original de 1985).
   Temporizacao medida no aparelho real: 3,575611 MHz, 228 T/linha, 312 linhas em 50 Hz (71136 T/quadro)
   ou 262 linhas em 60 Hz (59736 T/quadro). Padrao **50 Hz** (jogos europeus rodam na velocidade
   certa); `--60hz` ou F8 para o modo PAL-M brasileiro. Contencao de memoria ligada (padrao 6,5,4,3,2,1,0,0
   do 48K; o VHDL do clone da ULA mostra a mesma logica, ver `docs/research/tk90x-hardware.md`
   secao 2.1); `--no-contention` a desliga. Detalhes: `docs/research/tk95.md`.

## Modulos (cada um com um dono; interfaces nos headers sao contrato)

| Arquivo | Conteudo |
|---|---|
| `src/z80.h/.c` | Nucleo Z80 completo (documentado + nao documentado, MEMPTR, temporizacao por ciclo). Validado pela suite do Fuse e ZEXDOC/ZEXALL. |
| `src/tape.h/.c` | TAP e TZX 1.20 -> sequencia de pulsos (EAR). Suporte a flash load. |
| `src/snapshot.h/.c` | SNA/Z80 (ler/gravar) + fita virtual (snapshot, .bin, .scr -> TAP). |
| `src/machine.h/.c` | ULA, memoria, portas, teclado, joystick, video, audio, armadilhas, autotype, `machine_open_file`. |
| `src/png.h/.c` | Gravador PNG minimo (zlib "stored" + CRC + Adler) para capturas de tela. |
| `src/platform.h/.c` | Listagem de diretorio portavel (dirent / FindFirstFile), caminho do executavel, separadores. |
| `src/osd.h/.c` | Texto sobreposto usando a fonte da propria ROM (0x3D00): barra de status, menu principal (F1, itens montados por main.c), ajuda em 6 paginas (versoes desktop e toque), navegador de arquivos, mouse no layout classico (`osd_mouse`). |
| `src/main.c` | Front-end SDL2: janela, escala, tela cheia, audio, mapeamento de teclado PC->Spectrum, controles, linha de comando, modo headless de testes. |
| `roms/tk90x.rom`, `roms/tk95.rom` | ROMs (16 KB). |
| `games/` | Fitas de jogos para teste (origem e licencas em `games/LEIAME.md`); mantida pelos testadores de jogos. |
| `tests/` | `z80_fusetest.c`, `zextest.c` (shim CP/M), `tape_test.c`, `snapshot_test.c`, dados em `tests/data/`. |
| `Makefile`, `CMakeLists.txt` | Build Linux (gcc + SDL2) e Windows (mingw cruzado ou nativo). |

## Relogio

`cpu.tstates` conta T-states desde o inicio do quadro. `machine_run_frame` roda ate
`frame_tstates`, depois subtrai `frame_tstates` do relogio da CPU e chama `tape_frame_end`.
A fita gera transicoes em T-states nesse mesmo relogio; a maquina, antes de cada instrucao,
compara `cpu.tstates >= tape_next_edge(tape)` e so entao chama `tape_advance` (barato).

## Linha de comando (main.c)

```
tk90x [opcoes] [arquivo]
  --model tk95|tk90x|tk90x-v1   (padrao tk95)
  --rom ARQUIVO           ROM alternativa de 16 KB
  --50hz | --60hz         (padrao 50 Hz)
  --scale N               escala da janela (padrao 3)
  --fullscreen
  --instant               snapshots sem fita virtual
  --flash                 carga instantanea por armadilha na ROM
  --load-speed N          velocidade durante a carga: N vezes (padrao 20), 1 = tempo real, 0 = maxima
  --org ENDERECO --exec ENDERECO   para .bin/.cod (padrao org 32768, exec = org)
  --joystick kempston|sinclair1|sinclair2|cursor
  --no-contention         desliga a contencao de memoria da ULA (padrao: ligada)
  --no-sound
  --headless              sem janela/audio (SDL dummy) - para testes
  --frames N              (headless) roda N quadros e sai
  --until-loaded          (headless) roda ate a carga terminar (+ N quadros)
  --screenshot ARQ.png    grava a tela ao sair
  --type "TEXTO"          digita texto no teclado emulado apos o boot (palavras-chave do BASIC viram tokens)
  --wav-out ARQ.wav       (depuracao) grava em WAV o audio entregue ao SDL (tambem no headless)
```

`--type`: o texto so e digitado quando a ROM esta esperando tecla. A armadilha em
`ROM_KEY_IN_RET` (0x10B4, KEY-INPUT sem tecla nova) grava `key_wait_frame = frame_count + 1`
durante o quadro; como `frame_count` e incrementado no fim do quadro, main.c compara
`key_wait_frame == frame_count` logo depois (ROM ociosa no ultimo quadro). Medido:
`--type 'PRINT 2+2\n'` mostra o 4 no quadro 135 (o boot fica pronto por volta do 90; 5 toques
de 9 quadros), sem as esperas de 50 quadros.

## Carga nostalgica: som e listras originais, 20x mais rapido (--load-speed N; 0 = maximo; F7 alterna)

Enquanto a fita toca (`tape_is_playing`, ou snapshot da fita virtual pendente, ou carga
instantanea entre blocos - `is_loading` em main.c), cada **quadro exibido** roda N quadros
emulados (`emu_step`): N = `--load-speed` (padrao 20; 1 = tempo real; 0 = turbo maximo, que
roda quantos quadros couberem em 15 ms). Fora da carga, 1 quadro por quadro exibido.

- **Video**: o quadro exibido e o ultimo dos N, um quadro emulado inteiro, com a borda desenhada
  por T-state: as listras vermelho/ciano do piloto e azul/amarelo dos dados aparecem como no
  aparelho.
- **Audio**: para cada quadro exibido vai para a fila do SDL o audio de **um** quadro emulado (o
  ultimo), na altura original, com `m->tape_sound = 1` (EAR da fita + beeper). Nunca se comprime
  o tempo (20x a altura seria ultrassom): o resultado e o chiado continuo da fita, na altura
  certa, com a cadencia (piloto, dados, pausa, proximo bloco) 20x mais rapida. Durante a carga,
  um passa-baixa de 1 polo (~7 kHz) suaviza as bordas da onda quadrada. Volume da fita: 8/27 da
  escala contra 16/27 do alto-falante (amplitudes em `machine_audio`). Ver `queue_audio`.
- **Status**: uma linha fina, so da largura do texto, no canto da borda inferior
  (`FITA 45% 20x Program: NOME`, ou `MAX` no turbo), visivel por 3 s a cada bloco ou mudanca de
  velocidade; o menu (F1) mostra sempre o progresso, o bloco e a velocidade.
- **F7**: na carga, alterna entre o maximo e a velocidade configurada (com `--load-speed 0`, o
  F7 vai para 20x); fora da carga, liga/desliga o turbo maximo.
- **Headless**: o laco usa a mesma cadencia (`emu_step` com limite de quadros); `--frames` conta
  quadros emulados e a linha de resumo traz `shown_seconds` (tempo na janela). `--wav-out` grava
  exatamente o que iria para o SDL.

Verificacao objetiva (dogmole.tap, `--wav-out`, cortes alinhados a 877,35 amostras/quadro):
piloto 807,2 Hz (esperado 3575611 / (2 x 2168 x 3575611/3500000) = 807,2 Hz, ja que a fita
converte T-states de 3,5 MHz para o relogio do TK); dados: meias-ondas de 2044 Hz (bit 0, 855 T,
esperado 2047) e 1023 Hz (bit 1, 1710 T). RMS da fita ~1250 contra ~2100 da musica do jogo.
Tempos de carga dos jogos de `games/` no README.

## Teclas do front-end

| Tecla PC | Funcao |
|---|---|
| Letras, digitos, Enter, Espaco | teclas equivalentes |
| Shift esquerdo | CAPS SHIFT |
| Ctrl (qualquer) / Shift direito | SYMBOL SHIFT |
| Backspace | CAPS+0 (DELETE) |
| Setas | automatico (padrao): joystick do modo do F9 (so os bits JOY_*, sem CAPS) com um programa rodando; CAPS+5/6/7/8 com a ROM no editor (`machine_editor_active`: KEY-INPUT 0x10B4 sem tecla nova nos ultimos 10 quadros). F4: automatico -> joystick -> cursores. Decidido ao apertar a tecla |
| Pontuacao (`" , . ; : + - * / = < > ? ( ) $ ' _ !` ...) | SYMBOL + tecla correspondente |
| Alt | fogo, quando as setas sao joystick (mesma regra) |
| Tab | modo E (CAPS+SYMBOL) |
| CapsLock | CAPS LOCK (CAPS+2) |
| Home / Insert / End | EDIT (CAPS+1) / GRAPHICS (CAPS+9) / BREAK (CAPS+SPACE) |
| PgUp / PgDn | TRUE VIDEO (CAPS+3) / INV. VIDEO (CAPS+4) |
| F1 | menu principal (pausa; progresso da fita no topo; itens com a tecla de funcao e o valor atual; setas/Enter/mouse/letra inicial; "Ajuda" abre as paginas). No modo toque, F1 abre direto a ajuda |
| F2 | abrir arquivo (navegador interno) |
| F3 | play/pausa da fita |
| F4 | setas: automatico -> joystick -> cursores |
| F5 | reset |
| F6 | grava snapshot (.z80) |
| F7 | turbo maximo liga/desliga (na carga: alterna entre o maximo e `--load-speed`, padrao 20x) |
| F8 | 50/60 Hz |
| F9 | proximo modo de joystick |
| F10 | proximo modelo (TK95, TK90X, TK90X v1), com reset; tira a fita |
| F11 | tela cheia |
| F12 | captura de tela PNG |
| Esc | fecha menus (ajuda aberta pelo menu volta a ele) / sai (confirmar; o dialogo lembra "F1 = menu e ajuda") |
| Arrastar arquivo para a janela | abre o arquivo (carga "original") |

Controles de jogo (SDL GameController) sao o joystick do modo escolhido.
