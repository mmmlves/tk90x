# Instalar o emulador TK90X / TK95 (sem compilar)

Esta pasta traz o emulador **pronto para usar** no Windows, no Android e no Linux, mais os jogos
de teste. Não é preciso compilar nada: baixe só o arquivo do seu sistema.

| Sistema | Arquivo | Download direto |
|---|---|---|
| Windows 10/11 (64 bits) | `windows/tk90x.exe` | [tk90x.exe](https://github.com/mmmlves/tk90x/raw/main/dist/windows/tk90x.exe) |
| Android 5.0 ou mais novo | `android/tk90x.apk` | [tk90x.apk](https://github.com/mmmlves/tk90x/raw/main/dist/android/tk90x.apk) |
| Linux x86_64 | `linux/tk90x` | [tk90x](https://github.com/mmmlves/tk90x/raw/main/dist/linux/tk90x) |
| Jogos (.tap/.tzx) | `games/` | [pasta games](games/) |

As ROMs do TK95, TK90X e TK90X v1 já vêm **dentro** do executável. A pasta `roms/` ao lado dele
é opcional: serve só para trocar uma ROM sem recompilar.

---

## Windows

1. Baixe o [tk90x.exe](https://github.com/mmmlves/tk90x/raw/main/dist/windows/tk90x.exe). É um
   executável único, sem instalação e sem DLL.
2. Coloque-o numa pasta qualquer (ex.: `Documentos\TK90X`) e dê dois cliques.
3. O Windows pode mostrar **"O Windows protegeu o computador"** (SmartScreen), porque o programa
   não tem assinatura digital paga. Clique em **Mais informações** e depois em
   **Executar assim mesmo**. Alguns antivírus também desconfiam de executáveis novos sem
   assinatura; se isso acontecer, libere o arquivo.
4. Aparece a tela de abertura do TK95. Para jogar, **arraste um `.tap` ou `.tzx` para a janela**
   ou aperte **F2** e escolha o arquivo.

Requer Windows 10 ou 11 de 64 bits (no Windows 7/8.1 é preciso a atualização KB2999226).

## Android

1. **No celular**, abra este link e baixe o APK:
   [tk90x.apk](https://github.com/mmmlves/tk90x/raw/main/dist/android/tk90x.apk).
2. Toque no arquivo baixado (na notificação do download ou no app **Arquivos**).
3. O Android pede permissão para **instalar apps desconhecidos** a partir do navegador ou do
   gerenciador de arquivos: toque em **Configurações**, ative **Permitir desta fonte** e volte.
4. Toque em **Instalar**. Se o **Play Protect** avisar que o app é desconhecido, toque em
   **Mais detalhes > Instalar mesmo assim** (o APK não vem da Play Store, por isso o aviso).
5. Abra o app **TK90X**. Os 10 jogos já vêm dentro dele: **MENU > Abrir jogo**.

Controles na tela: joystick à esquerda, **FOGO** à direita, **TECLADO** abre o teclado do TK,
**LOAD ""** carrega a fita, **MENU** tem reset, modelo, 50/60 Hz, joystick e **Ajuda**.
O botão Voltar fecha menus; apertado de novo, pergunta se quer sair.
Os jogos incluídos já abrem com o joystick certo e mostram `Controles: ...` e como começar
(por exemplo, `FOGO inicia` ou `TECLADO > 1, depois TECLADO`); em outros jogos, escolha em
**MENU > Joystick** o mesmo controle do menu do jogo. A versão 1.2 instala por cima das anteriores (1.0 e 1.1).
Para abrir outros jogos: **MENU > Abrir do aparelho**, ou toque num `.tap`/`.tzx` no gerenciador
de arquivos e escolha **TK90X**. Detalhes em [docs/ANDROID.md](../docs/ANDROID.md).

## Linux

Requer x86_64 com glibc 2.34 ou mais nova (Ubuntu 22.04+, Debian 12+, Fedora 35+, Mint 21+) e a
biblioteca SDL2:

```sh
sudo apt install libsdl2-2.0-0        # Debian / Ubuntu / Mint
sudo dnf install SDL2                 # Fedora
sudo pacman -S sdl2                   # Arch / Manjaro
```

Baixe e rode:

```sh
curl -LO https://github.com/mmmlves/tk90x/raw/main/dist/linux/tk90x
chmod +x tk90x                        # o navegador perde a permissao de execucao
./tk90x                               # abre na tela de abertura
./tk90x jogo.tap                      # ja abre carregando um jogo
```

Ou baixe o repositório inteiro (binário, jogos e documentação juntos):

```sh
git clone https://github.com/mmmlves/tk90x.git
cd tk90x/dist
./linux/tk90x games/04-atic-atac-alt-castelo-corvoalto.tap
```

Em outra distribuição ou arquitetura, compile a partir do código: veja o
[README principal](../README.md#compilar).

---

## Primeiros passos (todas as plataformas)

- **F1 abre o menu e a ajuda** (no Android: botão **MENU > Ajuda**). A ajuda explica como carregar
  uma fita, o teclado, o joystick, reset e modelos.
- **Carregar um jogo**: arraste o arquivo para a janela ou use **F2**. O emulador reinicia, digita
  `LOAD ""` sozinho e a fita toca com o som e as listras da época, **20x mais rápido**
  (F7 = velocidade máxima).
- **Jogar**: as setas do PC são o joystick do TK nos jogos (**Alt** = fogo); muitos jogos também
  usam **Q/A/O/P** e **espaço**. Controles USB funcionam direto.
- **Reiniciar**: **F5**. **Trocar modelo** (TK95, TK90X, TK90X v1): **F10**. **50/60 Hz**: **F8**.
  **Tela cheia**: **F11**. **Sair**: **Esc** duas vezes.

## Jogos incluídos

A pasta [games/](games/) tem 10 jogos e um extra. Origem e licença de cada um estão em
[games/LEIAME.md](games/LEIAME.md). Dois deles, *A Fuga do Castelo Corvoalto* e
*A Selva de Verdemor*, foram feitos para este projeto (código-fonte em
[../games/src/](../games/src/), licença MIT).

## Arquivos suportados

`.tap` e `.tzx` (fitas), `.z80` e `.sna` (estados salvos), `.scr` (tela) e `.bin`/`.cod`
(código de máquina; endereço padrão 32768, ou `--org`/`--exec` na linha de comando).
Opções de linha de comando: `tk90x --help`.
