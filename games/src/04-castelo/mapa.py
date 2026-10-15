#!/usr/bin/env python3
# mapa.py - mapa original de "A Fuga do Castelo Corvoalto" (24 salas em 3 andares).
# Licença MIT - (c) 2026 Projeto Emulador TK90X
#
# Uso: python3 mapa.py saida.inc
# Gera os dados do mapa para o motor LABIRINTO e valida salas e progressão.
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "motor"))
from mapa_lib import Mapa, T_WALL, T_DECO1, T_DECO2  # noqa: E402

PRETO, AZUL, VERMELHO, MAGENTA, VERDE, CIANO, AMARELO, BRANCO = range(8)


def attr(tinta, papel=PRETO, brilho=False, pisca=False):
    return (0x80 if pisca else 0) | (0x40 if brilho else 0) | (papel << 3) | tinta


# estilos de parede (ordem dos ladrilhos parede0..3 em graficos.txt)
TIJOLO, PEDRA, MADEIRA, ROCHA = range(4)
# tipos de inimigo (tabela enemy_types em castelo.asm); | MISTO alterna os tipos
FANTASMA, MORCEGO, CAVALEIRO = range(3)
MISTO = 0x80
# tipos de objeto (tabela item_kinds em castelo.asm)
CH_VERMELHA, CH_VERDE, CH_AZUL, CH_AMARELA, PECA, FRANGO, MACA, PAO = range(8)
TIPOS_OBJ = {
    CH_VERMELHA: ("chave", 0), CH_VERDE: ("chave", 1), CH_AZUL: ("chave", 2), CH_AMARELA: ("chave", 3),
    PECA: ("peca", 0), FRANGO: ("comida", 40), MACA: ("comida", 24), PAO: ("comida", 32),
}

m = Mapa()
TERREO = m.andar("TÉRREO", 0)
SUPERIOR = m.andar("ANDAR SUPERIOR", 1)
PORAO = m.andar("PORÃO", -1)

# ---- modelos de mobília (x, y, largura, altura, ladrilho), em células de 8x8 ----
m.modelo("pilares4", [(6, 4, 2, 2, T_DECO1), (24, 4, 2, 2, T_DECO1),
                      (6, 14, 2, 2, T_DECO1), (24, 14, 2, 2, T_DECO1)])
m.modelo("colunata", [(x, y, 2, 2, T_DECO1) for y in (6, 12) for x in (6, 10, 20, 24)])
m.modelo("mesa", [(10, 8, 12, 4, T_DECO2)])
m.modelo("caixotes", [(4, 4, 4, 2, T_DECO2), (24, 4, 2, 4, T_DECO2), (4, 12, 2, 4, T_DECO2),
                      (20, 14, 6, 2, T_DECO2), (10, 12, 2, 2, T_DECO2)])
m.modelo("cruz", [(2, 2, 8, 4, T_WALL), (22, 2, 8, 4, T_WALL),
                  (2, 14, 8, 4, T_WALL), (22, 14, 8, 4, T_WALL)])
m.modelo("bloco", [(12, 6, 8, 8, T_WALL)])
m.modelo("tumulos", [(6, 6, 4, 2, T_DECO2), (22, 6, 4, 2, T_DECO2), (6, 12, 4, 2, T_DECO2),
                     (22, 12, 4, 2, T_DECO2), (14, 8, 4, 4, T_DECO1)])
m.modelo("jardim", [(x, y, 2, 2, T_DECO1) for y in (4, 14) for x in (6, 12, 18, 24)])
m.modelo("trono", [(8, 4, 2, 2, T_DECO1), (22, 4, 2, 2, T_DECO1), (8, 12, 2, 2, T_DECO1),
                   (22, 12, 2, 2, T_DECO1), (14, 4, 4, 2, T_DECO2)])
m.modelo("capela", [(6, 8, 6, 2, T_DECO2), (20, 8, 6, 2, T_DECO2),
                    (6, 12, 6, 2, T_DECO2), (20, 12, 6, 2, T_DECO2)])
m.modelo("estantes", [(4, 4, 8, 2, T_DECO2), (20, 4, 8, 2, T_DECO2), (4, 14, 8, 2, T_DECO2),
                      (20, 14, 8, 2, T_DECO2), (12, 8, 8, 4, T_DECO2)])
m.modelo("corredor_h", [(2, 2, 28, 4, T_WALL), (2, 14, 28, 4, T_WALL)])

CHAO = attr(BRANCO)                     # chão preto
CHAO_PORAO = attr(BRANCO, AZUL, brilho=True)   # chão azul no porão
PILAR = attr(BRANCO, brilho=True)
CAIXA = attr(AMARELO)

# ---- térreo ----
portao = m.sala("PORTÃO PRINCIPAL", TERREO, attr(BRANCO), CHAO, PILAR, PEDRA, "pilares4", (FANTASMA, 1))
salao = m.sala("SALÃO DE ENTRADA", TERREO, attr(VERMELHO, brilho=True), CHAO, PILAR, TIJOLO, "colunata", (MORCEGO, 1))
trono = m.sala("SALA DO TRONO", TERREO, attr(AMARELO, brilho=True), CHAO, attr(MAGENTA, brilho=True), PEDRA, "trono", (CAVALEIRO, 2))
cozinha = m.sala("COZINHA", TERREO, attr(VERMELHO), CHAO, CAIXA, TIJOLO, "mesa", (MORCEGO, 2))
despensa = m.sala("DESPENSA", TERREO, attr(AMARELO), CHAO, CAIXA, MADEIRA, "caixotes", (MORCEGO, 2))
refeitorio = m.sala("REFEITÓRIO", TERREO, attr(MAGENTA, brilho=True), CHAO, CAIXA, TIJOLO, "mesa", (FANTASMA, 2))
capela = m.sala("CAPELA", TERREO, attr(BRANCO, brilho=True), CHAO, attr(AMARELO), PEDRA, "capela", (FANTASMA, 3))
galeria = m.sala("GALERIA DE RETRATOS", TERREO, attr(MAGENTA), CHAO, PILAR, MADEIRA, "pilares4", (FANTASMA | MISTO, 2))
armas = m.sala("SALA DE ARMAS", TERREO, attr(CIANO), CHAO, attr(CIANO, brilho=True), PEDRA, "colunata", (CAVALEIRO, 2))
corr_leste = m.sala("CORREDOR LESTE", TERREO, attr(VERMELHO, brilho=True), CHAO, PILAR, TIJOLO, "bloco", (MORCEGO, 2))
corr_oeste = m.sala("CORREDOR OESTE", TERREO, attr(VERDE), CHAO, PILAR, TIJOLO, "cruz", (CAVALEIRO, 2))
jardim = m.sala("JARDIM DE INVERNO", TERREO, attr(VERDE, brilho=True), CHAO, attr(VERDE), PEDRA, "jardim", (MORCEGO | MISTO, 3))

# ---- andar superior ----
biblioteca = m.sala("BIBLIOTECA", SUPERIOR, attr(AMARELO), CHAO, attr(VERMELHO, brilho=True), MADEIRA, "estantes", (FANTASMA, 2))
escritorio = m.sala("ESCRITÓRIO DO BARÃO", SUPERIOR, attr(MAGENTA, brilho=True), CHAO, CAIXA, MADEIRA, "caixotes", (MORCEGO, 2))
quarto = m.sala("QUARTO DO BARÃO", SUPERIOR, attr(VERMELHO, brilho=True), CHAO, PILAR, TIJOLO, "pilares4", (CAVALEIRO | MISTO, 3))
musica = m.sala("SALA DE MÚSICA", SUPERIOR, attr(CIANO, brilho=True), CHAO, attr(AMARELO, brilho=True), MADEIRA, "colunata", (FANTASMA, 2))
alquimia = m.sala("LABORATÓRIO", SUPERIOR, attr(VERDE, brilho=True), CHAO, attr(CIANO, brilho=True), PEDRA, "tumulos", (MORCEGO, 3))
torre = m.sala("TORRE NORTE", SUPERIOR, attr(BRANCO), CHAO, PILAR, PEDRA, "cruz", (CAVALEIRO, 2))

# ---- porão ----
adega = m.sala("ADEGA", PORAO, attr(AMARELO), CHAO_PORAO, attr(AMARELO, AZUL, True), ROCHA, "caixotes", (MORCEGO, 2))
masmorra = m.sala("MASMORRA", PORAO, attr(BRANCO), CHAO_PORAO, attr(BRANCO, AZUL, True), PEDRA, "pilares4", (CAVALEIRO, 2))
cripta = m.sala("CRIPTA", PORAO, attr(MAGENTA), CHAO_PORAO, attr(BRANCO, AZUL, True), ROCHA, "tumulos", (FANTASMA, 3))
poco = m.sala("POÇO ANTIGO", PORAO, attr(CIANO), CHAO_PORAO, attr(BRANCO, AZUL, True), ROCHA, "bloco", (FANTASMA | MISTO, 3))
catacumbas = m.sala("CATACUMBAS", PORAO, attr(VERDE), CHAO_PORAO, attr(BRANCO, AZUL, True), ROCHA, "cruz", (CAVALEIRO | MISTO, 3))
correntes = m.sala("SALA DAS CORRENTES", PORAO, attr(VERMELHO), CHAO_PORAO, attr(AMARELO, AZUL, True), PEDRA, "pilares4", (MORCEGO, 3))

# ---- portas (N/S/O/L) ----
m.liga(salao, "N", trono)
m.liga(salao, "S", portao)
m.liga(salao, "O", galeria, "vermelha")
m.liga(salao, "L", corr_leste)
m.liga(portao, "O", cozinha)
m.liga(portao, "L", refeitorio)
m.saida(portao, "S", "mestra")
m.liga(cozinha, "O", despensa)
m.liga(despensa, "N", jardim, "verde")
m.liga(refeitorio, "N", corr_leste)
m.liga(corr_leste, "N", armas)
m.liga(armas, "O", trono)
m.liga(trono, "O", capela, "amarela")
m.liga(capela, "O", corr_oeste)
m.liga(corr_oeste, "S", jardim)

m.liga(escritorio, "N", biblioteca)
m.liga(escritorio, "O", musica)
m.liga(escritorio, "L", quarto, "amarela")
m.liga(biblioteca, "O", torre)
m.liga(biblioteca, "L", alquimia)
m.liga(musica, "N", torre)

m.liga(adega, "L", masmorra)
m.liga(adega, "N", cripta)
m.liga(masmorra, "L", poco, "azul")
m.liga(masmorra, "N", catacumbas)
m.liga(cripta, "L", catacumbas)
m.liga(catacumbas, "L", correntes)

# ---- escadas ----
m.escada(galeria, (14, 8), escritorio, (14, 8))
m.escada(despensa, (24, 10), adega, (6, 8))
m.escada(armas, (14, 8), correntes, (14, 8))

# ---- objetos ----
m.objeto(cripta, 26, 4, CH_VERMELHA)
m.objeto(biblioteca, 14, 4, CH_VERDE)
m.objeto(capela, 14, 4, CH_AZUL)
m.objeto(torre, 14, 8, CH_AMARELA)
m.objeto(poco, 4, 4, PECA)
m.objeto(quarto, 26, 14, PECA)
m.objeto(jardim, 14, 8, PECA)

m.objeto(cozinha, 6, 8, FRANGO)
m.objeto(refeitorio, 14, 4, PAO)
m.objeto(despensa, 6, 8, MACA)
m.objeto(adega, 22, 10, PAO)
m.objeto(masmorra, 24, 8, MACA)
m.objeto(catacumbas, 14, 8, FRANGO)
m.objeto(musica, 14, 4, MACA)
m.objeto(alquimia, 26, 14, FRANGO)
m.objeto(armas, 4, 4, PAO)
m.objeto(corr_oeste, 14, 8, MACA)
m.objeto(trono, 4, 14, FRANGO)
m.objeto(corr_leste, 4, 4, MACA)

if __name__ == "__main__":
    saida = sys.argv[1] if len(sys.argv) > 1 else "mapa.inc"
    m.gerar(saida, salao, (120, 72), TIPOS_OBJ, pecas=3)
