#!/usr/bin/env python3
# mapa.py - mapa original de "A Selva de Verdemor" (30 telas de labirinto na selva).
# Licença MIT - (c) 2026 Projeto Emulador TK90X
#
# Uso: python3 mapa.py saida.inc
# Gera os dados do mapa para o motor LABIRINTO e valida telas e progressão.
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "motor"))
from mapa_lib import Mapa, T_WALL, T_DECO1, T_DECO2  # noqa: E402

PRETO, AZUL, VERMELHO, MAGENTA, VERDE, CIANO, AMARELO, BRANCO = range(8)


def attr(tinta, papel=PRETO, brilho=False, pisca=False):
    return (0x80 if pisca else 0) | (0x40 if brilho else 0) | (papel << 3) | tinta


# estilos de sebe (ordem dos ladrilhos sebe0..3 em graficos.txt)
FOLHAGEM, SAMAMBAIA, ESPINHEIRO, BAMBU = range(4)
# monstros (tabela enemy_types em selva.asm); | MISTO alterna os tipos
NEVOA, ZUNZUM, CASCO, BOCAO = range(4)
MISTO = 0x80
# objetos (tabela item_kinds em selva.asm)
(RUNA_VERMELHA, RUNA_AMARELA, RUNA_BRANCA, PECA, MANGA, BANANA, COCO,
 FLOR_VENTO, FLOR_CRISTAL, FLOR_TONTA) = range(10)
TIPOS_OBJ = {
    RUNA_VERMELHA: ("chave", 0), RUNA_AMARELA: ("chave", 1), RUNA_BRANCA: ("chave", 2),
    PECA: ("peca", 0), MANGA: ("comida", 40), BANANA: ("comida", 24), COCO: ("comida", 32),
    FLOR_VENTO: ("flor", 1), FLOR_CRISTAL: ("flor", 2), FLOR_TONTA: ("flor", 3),
}
# espinheiros trancados: número da runa que os desfaz
VERMELHA, AMARELA, BRANCA = 0, 1, 2

m = Mapa()
MATA_ALTA = m.andar("MATA ALTA", 0)
BREJO = m.andar("BREJO", 0)
MATA_FECHADA = m.andar("MATA FECHADA", 0)
ESPINHAIS = m.andar("ESPINHAIS", 0)
RUINAS = m.andar("RUÍNAS", 0)

# ---- modelos das telas, em blocos de 16x16 sobre o chão (14x8 blocos) ----
# Linhas de 7 caracteres valem 2x2 blocos (corredores de 32 pixels).
# '#' sebe, 'T' árvore, 'R' pedra das ruínas, '.' chão.
LEG = {"#": T_WALL, "T": T_DECO1, "R": T_DECO2}
ARTE = {
    "clareira": ["#.....#",
                 ".......",
                 ".......",
                 "#.....#"],
    "moitas": ["..............",
               ".##..##..##...",
               ".##..##..##...",
               "..............",
               "..............",
               "...##..##..##.",
               "...##..##..##.",
               ".............."],
    "anel": [".......",
             ".##.##.",
             ".##.##.",
             "......."],
    "zigue": [".#...#.",
              ".#.#.#.",
              "...#...",
              "##...##"],
    "corredores": ["##...##",
                   "...#...",
                   "...#...",
                   "##...##"],
    "serpente": [".#.....",
                 ".#.###.",
                 "...#...",
                 "##...#."],
    "bolsoes": ["..#.#..",
                ".......",
                ".#.#.#.",
                ".#...#."],
    "trilha": ["###.###",
               ".......",
               ".......",
               "###.###"],
    "ilhas": [".......",
              "..#.#..",
              ".#...#.",
              "......."],
    "arvoredo": [".T...T.",
                 "...T...",
                 ".T...T.",
                 "T.....T"],
    "ruinas": ["R.....R",
               "..R.R..",
               "..R.R..",
               "R.....R"],
    "templo": ["T.R.R.T",
               ".......",
               ".......",
               "R.T.T.R"],
    "brejo": ["..#.......#...",
              "..............",
              ".....#....#...",
              "..............",
              "...#.....#....",
              "..............",
              ".#......#...#.",
              ".............."],
    "galhos": ["#####....#####",
               "#####....#####",
               "..............",
               "..##..##..##..",
               "..##..##..##..",
               "..............",
               "####......####",
               "####......####"],
}
for nome, linhas in ARTE.items():
    m.modelo_arte(nome, linhas, LEG)

CHAO = attr(BRANCO)                       # chão preto
VERDE_V = attr(VERDE, brilho=True)
VERDE_E = attr(VERDE)
AMAR_V = attr(AMARELO, brilho=True)
AMAR_E = attr(AMARELO)
VERM_V = attr(VERMELHO, brilho=True)
VERM_E = attr(VERMELHO)
ARVORE = attr(VERDE, brilho=True)
TRONCO = attr(AMARELO)
PEDRA = attr(AMARELO, brilho=True)
PEDRA_B = attr(BRANCO)


def sala(nome, regiao, parede, deco, estilo, modelo, inimigo):
    return m.sala(nome, regiao, parede, CHAO, deco, estilo, modelo, inimigo)


# ---- as 30 telas, numa grade de 6 colunas x 5 linhas (coluna, linha) ----
G = {}
# linha 0 (norte)
G[0, 0] = sala("NINHO DOS ZUNZUNS", MATA_ALTA, VERDE_V, ARVORE, FOLHAGEM, "bolsoes", (ZUNZUM, 3))
G[1, 0] = sala("BOSQUE DOS CIPÓS", MATA_ALTA, VERDE_E, ARVORE, SAMAMBAIA, "arvoredo", (NEVOA, 2))
G[2, 0] = sala("ESCADARIA VERDE", MATA_ALTA, VERDE_V, PEDRA, FOLHAGEM, "corredores", (CASCO, 2))
G[3, 0] = sala("TEMPLO DAS MIL RAÍZES", RUINAS, AMAR_V, PEDRA, BAMBU, "templo", (NEVOA | MISTO, 3))
G[4, 0] = sala("PÁTIO DOS ÍDOLOS", RUINAS, AMAR_E, PEDRA, BAMBU, "ruinas", (CASCO, 2))
G[5, 0] = sala("TORRE TOMBADA", RUINAS, AMAR_V, PEDRA_B, FOLHAGEM, "ruinas", (CASCO | MISTO, 3))
# linha 1
G[0, 1] = sala("CLAREIRA DOS ECOS", MATA_ALTA, VERDE_V, ARVORE, FOLHAGEM, "clareira", (NEVOA, 3))
G[1, 1] = sala("TRILHA SINUOSA", MATA_ALTA, VERDE_E, ARVORE, SAMAMBAIA, "serpente", (ZUNZUM, 2))
G[2, 1] = sala("RAÍZES GÊMEAS", MATA_ALTA, VERDE_V, ARVORE, FOLHAGEM, "anel", (BOCAO, 2))
G[3, 1] = sala("PORTAL DE PEDRA", RUINAS, AMAR_V, PEDRA, BAMBU, "trilha", (CASCO, 2))
G[4, 1] = sala("GALERIA DE MUSGO", RUINAS, VERDE_E, PEDRA, SAMAMBAIA, "ruinas", (NEVOA, 2))
G[5, 1] = sala("COLUNAS CAÍDAS", RUINAS, AMAR_E, PEDRA_B, BAMBU, "templo", (ZUNZUM | MISTO, 3))
# linha 2
G[0, 2] = sala("LAGOA ESCURA", BREJO, VERDE_E, TRONCO, SAMAMBAIA, "brejo", (NEVOA, 2))
G[1, 2] = sala("BREJO DAS NÉVOAS", BREJO, VERDE_E, TRONCO, SAMAMBAIA, "ilhas", (NEVOA, 3))
G[2, 2] = sala("PONTE DE TRONCOS", MATA_FECHADA, AMAR_E, TRONCO, BAMBU, "trilha", (ZUNZUM, 2))
G[3, 2] = sala("ENCRUZILHADA", MATA_FECHADA, VERDE_V, ARVORE, FOLHAGEM, "corredores", (CASCO, 1))
G[4, 2] = sala("CAMPO DE ESPINHOS", ESPINHAIS, VERM_V, ARVORE, ESPINHEIRO, "zigue", (BOCAO, 2))
G[5, 2] = sala("ESPINHEIRO RUBRO", ESPINHAIS, VERM_E, ARVORE, ESPINHEIRO, "galhos", (CASCO | MISTO, 3))
# linha 3
G[0, 3] = sala("CHARCO VERDE", BREJO, VERDE_E, TRONCO, SAMAMBAIA, "moitas", (ZUNZUM, 2))
G[1, 3] = sala("ILHA DE JUNCOS", BREJO, AMAR_E, TRONCO, BAMBU, "ilhas", (BOCAO | MISTO, 3))
G[2, 3] = sala("MATA DOS SUSSURROS", MATA_FECHADA, VERDE_V, ARVORE, FOLHAGEM, "serpente", (ZUNZUM, 2))
G[3, 3] = sala("CLAREIRA DO BALÃO", MATA_FECHADA, VERDE_V, ARVORE, FOLHAGEM, "clareira", (NEVOA, 1))
G[4, 3] = sala("VALE DAS FLORES", ESPINHAIS, VERM_V, ARVORE, FOLHAGEM, "ilhas", (ZUNZUM, 2))
G[5, 3] = sala("TOCA DO CASCO", ESPINHAIS, VERM_E, TRONCO, ESPINHEIRO, "bolsoes", (CASCO, 2))
# linha 4 (sul)
G[0, 4] = sala("POÇO DE LAMA", BREJO, VERDE_E, TRONCO, SAMAMBAIA, "galhos", (NEVOA | MISTO, 3))
G[1, 4] = sala("ÁRVORE OCA", BREJO, VERDE_V, ARVORE, FOLHAGEM, "arvoredo", (BOCAO, 2))
G[2, 4] = sala("SAMAMBAIAL", MATA_FECHADA, VERDE_E, ARVORE, SAMAMBAIA, "moitas", (ZUNZUM, 2))
G[3, 4] = sala("TRILHA DO RIACHO", MATA_FECHADA, VERDE_V, ARVORE, FOLHAGEM, "anel", (NEVOA, 2))
G[4, 4] = sala("JARDIM CARNÍVORO", ESPINHAIS, VERM_V, ARVORE, ESPINHEIRO, "moitas", (BOCAO, 3))
G[5, 4] = sala("FIM DA TRILHA", ESPINHAIS, VERM_E, TRONCO, ESPINHEIRO, "zigue", (CASCO | MISTO, 3))


def liga(a, lado, b, tranca=None):
    m.liga(G[a], lado, G[b], tranca)


# ---- passagens (N/S/O/L); trancadas = muralha de espinhos ----
liga((3, 3), "N", (3, 2))
liga((3, 3), "S", (3, 4))
liga((3, 3), "L", (4, 3), VERMELHA)
liga((3, 3), "O", (2, 3))
liga((2, 3), "S", (2, 4))
liga((2, 4), "O", (1, 4))
liga((3, 4), "O", (2, 4))
liga((3, 4), "L", (4, 4), AMARELA)
liga((3, 2), "O", (2, 2))
liga((3, 2), "N", (3, 1))
liga((2, 2), "N", (2, 1))
liga((2, 2), "O", (1, 2))
liga((2, 1), "O", (1, 1))
liga((1, 1), "N", (1, 0))
liga((1, 0), "L", (2, 0))
liga((2, 0), "L", (3, 0))
liga((1, 1), "O", (0, 1), BRANCA)
liga((0, 1), "N", (0, 0))
liga((3, 1), "N", (3, 0))
liga((3, 1), "L", (4, 1), AMARELA)
liga((4, 1), "N", (4, 0))
liga((4, 0), "L", (5, 0))
liga((4, 1), "L", (5, 1))
liga((5, 2), "N", (5, 1), AMARELA)
liga((1, 2), "O", (0, 2))
liga((0, 2), "S", (0, 3))
liga((0, 3), "L", (1, 3))
liga((1, 3), "S", (1, 4))
liga((0, 3), "S", (0, 4), VERMELHA)
liga((4, 3), "N", (4, 2))
liga((4, 2), "L", (5, 2))
liga((4, 3), "L", (5, 3))
liga((4, 3), "S", (4, 4))
liga((4, 4), "L", (5, 4))
liga((5, 3), "S", (5, 4), BRANCA)
m.saida(G[3, 0], "N", "mestra")          # portal do templo: vitória

# ---- tocas (túneis entre partes distantes da selva) ----
m.escada(G[1, 4], (14, 8), G[2, 0], (14, 8))
m.escada(G[0, 2], (6, 4), G[3, 4], (14, 8))


def pos(sx, sy):
    """Centro do supercélula (sx, sy) da grade 7x4, em células."""
    return 3 + 4 * sx, 3 + 4 * sy


def obj(sala_xy, sx, sy, tipo):
    cx, cy = pos(sx, sy)
    m.objeto(G[sala_xy], cx, cy, tipo)


# ---- pedaços do Disco de Âmbar, runas ----
obj((0, 0), 0, 0, PECA)
obj((5, 0), 3, 1, PECA)
m.objeto(G[0, 4], 15, 13, PECA)
obj((5, 4), 6, 2, PECA)
obj((1, 4), 1, 3, RUNA_VERMELHA)
obj((5, 2), 6, 1, RUNA_AMARELA)
obj((5, 1), 3, 2, RUNA_BRANCA)

# ---- frutas ----
obj((3, 3), 1, 2, MANGA)
obj((2, 1), 3, 0, BANANA)
obj((1, 2), 0, 0, COCO)
obj((0, 3), 6, 0, MANGA)
obj((4, 2), 6, 0, BANANA)
obj((4, 0), 1, 3, COCO)
obj((1, 0), 4, 2, MANGA)
obj((5, 3), 0, 3, COCO)
obj((3, 4), 0, 0, BANANA)
obj((0, 1), 3, 2, MANGA)
m.objeto(G[4, 4], 3, 2, COCO)

# ---- flores (renascem quando Nina volta à tela) ----
obj((3, 3), 5, 1, FLOR_VENTO)
obj((2, 3), 6, 0, FLOR_CRISTAL)
obj((3, 2), 4, 3, FLOR_TONTA)
obj((4, 3), 3, 1, FLOR_CRISTAL)
obj((1, 1), 4, 0, FLOR_VENTO)
obj((0, 2), 5, 2, FLOR_TONTA)
obj((3, 1), 3, 2, FLOR_VENTO)
m.objeto(G[4, 4], 27, 16, FLOR_TONTA)
obj((5, 0), 5, 3, FLOR_CRISTAL)

if __name__ == "__main__":
    saida = sys.argv[1] if len(sys.argv) > 1 else "mapa.inc"
    m.gerar(saida, G[3, 3], (120, 72), TIPOS_OBJ, pecas=4)
