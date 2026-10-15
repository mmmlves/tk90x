#!/usr/bin/env python3
# mapa_lib.py - descrição de mapas para o motor LABIRINTO (TK90X/TK95, 48K).
# Licença MIT - (c) 2026 Projeto Emulador TK90X
#
# Cada jogo escreve um mapa.py que importa este módulo, descreve as salas e chama
# Mapa.gerar(). A biblioteca:
#   - gera o arquivo .inc com os dados compactos que o motor lê;
#   - valida cada sala (portas, escadas e objetos alcançáveis pelo jogador 16x16);
#   - simula a progressão (chaves -> portas, pedaços -> saída) e garante que dá para vencer.
#
# Tela da sala: 32x20 células de 8x8. Paredes externas com 2 células; chão nas
# colunas 2..29 e linhas 2..17. Vãos de porta: N/S nas colunas 14..17, O/L nas linhas 8..11.
import sys
from collections import deque

T_FLOOR, T_WALL, T_MASTER, T_UP, T_DOWN, T_DECO1, T_DECO2 = 0, 1, 6, 7, 8, 9, 10
WALKABLE = (T_FLOOR, T_UP, T_DOWN)
TRANCAS = {None: 0, "vermelha": 2, "verde": 3, "azul": 4, "amarela": 5, "mestra": 6}
LADOS = "NSOL"            # Norte, Sul, Oeste, Leste (ordem no registro da sala)
OPOSTO = {"N": "S", "S": "N", "O": "L", "L": "O"}
GAP = {"N": (14, 0, 4, 2), "S": (14, 18, 4, 2), "O": (0, 8, 2, 4), "L": (30, 8, 2, 4)}
SAIDA = 0xFE
NENHUM = 0xFF
ROOM_SIZE = 21


class Sala:
    def __init__(self, idx, nome, andar, parede, chao, deco, estilo, modelo, inimigo):
        self.idx, self.nome, self.andar = idx, nome, andar
        self.parede, self.chao, self.deco, self.estilo = parede, chao, deco, estilo
        self.modelo = modelo
        self.inimigo = inimigo          # (tipo, quantidade); tipo|0x80 = misturado
        self.saidas = {}                # lado -> (destino, link)
        self.escada = None              # (destino, cx, cy, tile)


class Mapa:
    def __init__(self):
        self.salas, self.andares, self.links, self.objetos = [], [], [], []
        self.alturas = []
        self.modelos, self.nomes_modelos = [], {}
        self.modelo("vazio", [])

    # ---------------- descrição ----------------
    def andar(self, nome, altura):
        """altura: 0 = térreo, 1 = acima, -1 = abaixo (decide se a escada sobe ou desce)."""
        self.andares.append(nome)
        self.alturas.append(altura)
        return len(self.andares) - 1

    def modelo(self, nome, retangulos):
        """retangulos: lista de (x, y, larg, alt, tile) em células."""
        self.nomes_modelos[nome] = len(self.modelos)
        self.modelos.append((nome, retangulos))

    def sala(self, nome, andar, parede, chao, deco, estilo=0, modelo="vazio", inimigo=(0, 0)):
        s = Sala(len(self.salas), nome, andar, parede, chao, deco, estilo,
                 self.nomes_modelos[modelo], inimigo)
        self.salas.append(s)
        return s.idx

    def liga(self, a, lado, b, tranca=None):
        """Porta entre a sala a (no lado dado) e a sala b (no lado oposto)."""
        link = len(self.links)
        self.links.append(TRANCAS[tranca])
        self._saida(a, lado, b, link)
        self._saida(b, OPOSTO[lado], a, link)

    def saida(self, a, lado, tranca="mestra"):
        """Porta para fora do castelo: atravessá-la vence o jogo."""
        link = len(self.links)
        self.links.append(TRANCAS[tranca])
        self._saida(a, lado, SAIDA, link)

    def _saida(self, a, lado, b, link):
        s = self.salas[a]
        if lado in s.saidas:
            sys.exit("sala %s: lado %s repetido" % (s.nome, lado))
        s.saidas[lado] = (b, link)

    def escada(self, a, pos_a, b, pos_b):
        """Escada de a (sobe ou desce conforme o andar) até b, nos dois sentidos."""
        sa, sb = self.salas[a], self.salas[b]
        sobe = self.alturas[sb.andar] > self.alturas[sa.andar]
        sa.escada = (b, pos_a[0], pos_a[1], T_UP if sobe else T_DOWN)
        sb.escada = (a, pos_b[0], pos_b[1], T_DOWN if sobe else T_UP)

    def objeto(self, sala, cx, cy, tipo):
        self.objetos.append((sala, cx, cy, tipo))

    # ---------------- montagem da sala (igual ao motor) ----------------
    def cmap(self, s, abertas=True, links_abertos=()):
        m = [[T_WALL] * 32 for _ in range(20)]
        for y in range(2, 18):
            for x in range(2, 30):
                m[y][x] = T_FLOOR
        for lado, (dest, link) in s.saidas.items():
            tr = self.links[link]
            t = T_FLOOR if (tr == 0 or abertas or link in links_abertos) else tr
            gx, gy, gw, gh = GAP[lado]
            for y in range(gy, gy + gh):
                for x in range(gx, gx + gw):
                    m[y][x] = t
        for (x, y, w, h, t) in self.modelos[s.modelo][1]:
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    m[yy][xx] = t
        if s.escada:
            _, cx, cy, t = s.escada
            for yy in (cy, cy + 1):
                for xx in (cx, cx + 1):
                    m[yy][xx] = t
        return m

    @staticmethod
    def livre(m, x, y):
        if x < 0 or x > 240 or y < 0 or y > 144:
            return False
        for r in range(y >> 3, ((y + 15) >> 3) + 1):
            for c in range(x >> 3, ((x + 15) >> 3) + 1):
                if m[r][c] not in WALKABLE:
                    return False
        return True

    def alcance(self, m, inicio):
        vis = set()
        q = deque()
        for p in inicio:
            if self.livre(m, *p) and p not in vis:
                vis.add(p)
                q.append(p)
        while q:
            x, y = q.popleft()
            for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)):
                n = (x + dx, y + dy)
                if n not in vis and self.livre(m, *n):
                    vis.add(n)
                    q.append(n)
        return vis

    def pontos(self, s, inicio_xy=None):
        """Pontos de interesse da sala: (descrição, lista de posições do jogador ou teste)."""
        pts = []
        for lado, _ in s.saidas.items():
            if lado == "N":
                pts.append(("entrada N", [(x, 16) for x in range(112, 129, 2)]))
            elif lado == "S":
                pts.append(("entrada S", [(x, 128) for x in range(112, 129, 2)]))
            elif lado == "O":
                pts.append(("entrada O", [(16, y) for y in range(64, 81, 2)]))
            else:
                pts.append(("entrada L", [(224, y) for y in range(64, 81, 2)]))
        if s.escada:
            _, cx, cy, _ = s.escada
            pts.append(("escada", [(cx * 8, cy * 8)]))
        if inicio_xy:
            pts.append(("início", [inicio_xy]))
        return pts

    # ---------------- validação ----------------
    def validar(self, inicio, inicio_xy, tipos_obj, pecas):
        erros = []
        for s in self.salas:
            m = self.cmap(s)
            for (x, y, w, h, t) in self.modelos[s.modelo][1]:
                if x % 2 or y % 2 or w % 2 or h % 2:
                    erros.append("modelo %s: retângulo fora da grade par" % self.modelos[s.modelo][0])
            if s.escada:
                d, cx, cy, _ = s.escada
                if cx % 2 or cy % 2 or not (2 <= cx <= 28 and 2 <= cy <= 16):
                    erros.append("%s: escada em posição inválida" % s.nome)
                if self.salas[d].escada is None or self.salas[d].escada[0] != s.idx:
                    erros.append("%s: escada sem volta" % s.nome)
            pts = self.pontos(s, inicio_xy if s.idx == inicio else None)
            for nome, ps in pts:
                for p in ps:
                    if not self.livre(m, *p):
                        erros.append("%s: %s bloqueada em %s" % (s.nome, nome, p))
            todos = [p for _, ps in pts for p in ps]
            comp = self.alcance(m, todos[:1])
            for nome, ps in pts:
                if not any(p in comp for p in ps):
                    erros.append("%s: %s inalcançável" % (s.nome, nome))
            # gatilhos de saída
            for lado in s.saidas:
                ok = any((lado == "N" and y < 4) or (lado == "S" and y >= 140) or
                         (lado == "O" and x < 4) or (lado == "L" and x >= 236) for (x, y) in comp)
                if not ok:
                    erros.append("%s: não chega ao vão %s" % (s.nome, lado))
            for (sala, cx, cy, tipo) in self.objetos:
                if sala != s.idx:
                    continue
                ix, iy = cx * 8, cy * 8
                for r in (cy, cy + 1):
                    for c in (cx, cx + 1):
                        if m[r][c] != T_FLOOR:
                            erros.append("%s: objeto em cima de parede (%d,%d)" % (s.nome, cx, cy))
                if not any(abs(x - ix) < 12 and abs(y - iy) < 12 for (x, y) in comp):
                    erros.append("%s: objeto (%d,%d) inalcançável" % (s.nome, cx, cy))
        # progressão: chaves abrem portas, pedaços abrem a saída
        chaves, n_pecas, pegos = set(), 0, set()
        venceu = False
        while True:
            vis, q = {inicio}, deque([inicio])
            while q:
                a = q.popleft()
                s = self.salas[a]
                viz = []
                for lado, (d, link) in s.saidas.items():
                    tr = self.links[link]
                    pode = tr == 0 or (2 <= tr <= 5 and (tr - 2) in chaves) or (tr == 6 and n_pecas >= pecas)
                    if not pode:
                        continue
                    if d == SAIDA:
                        venceu = True
                        continue
                    viz.append(d)
                if s.escada:
                    viz.append(s.escada[0])
                for d in viz:
                    if d not in vis:
                        vis.add(d)
                        q.append(d)
            novo = False
            for k, (sala, cx, cy, tipo) in enumerate(self.objetos):
                if sala in vis and k not in pegos:
                    pegos.add(k)
                    efeito, valor = tipos_obj[tipo]
                    if efeito == "chave":
                        chaves.add(valor)
                    elif efeito == "peca":
                        n_pecas += 1
                    novo = True
            if not novo:
                break
        fora = [self.salas[s].nome for s in range(len(self.salas)) if s not in vis]
        if fora:
            erros.append("salas nunca alcançadas: %s" % ", ".join(fora))
        if not venceu:
            erros.append("a saída nunca abre (pedaços: %d de %d)" % (n_pecas, pecas))
        return erros

    # ---------------- saída asm ----------------
    def gerar(self, saida_inc, inicio, inicio_xy, tipos_obj, pecas):
        erros = self.validar(inicio, inicio_xy, tipos_obj, pecas)
        if erros:
            for e in erros:
                print("ERRO:", e, file=sys.stderr)
            sys.exit(1)
        o = ["; Gerado por mapa.py (motor/mapa_lib.py) - nao edite.", ""]
        o.append("NUM_ROOMS   EQU %d" % len(self.salas))
        o.append("NUM_LINKS   EQU %d" % len(self.links))
        o.append("NUM_ITEMS   EQU %d" % len(self.objetos))
        o.append("START_ROOM  EQU %d" % inicio)
        o.append("START_X     EQU %d" % inicio_xy[0])
        o.append("START_Y     EQU %d" % inicio_xy[1])
        o.append("")
        o.append("; registro da sala (%d bytes): nome, andar, attr parede, attr chão, attr decoração," % ROOM_SIZE)
        o.append("; estilo de parede, modelo, N(dest,link) S O L, escada(dest,cx,cy,tile), inimigo(tipo,qtd)")
        o.append("rooms:")
        for s in self.salas:
            b = [s.idx, s.andar, s.parede, s.chao, s.deco, s.estilo, s.modelo]
            for lado in LADOS:
                d, link = s.saidas.get(lado, (NENHUM, 0))
                b += [d, link]
            if s.escada:
                b += list(s.escada)
            else:
                b += [NENHUM, 0, 0, 0]
            b += [s.inimigo[0], s.inimigo[1]]
            assert len(b) == ROOM_SIZE
            o.append("        DB " + ",".join("%d" % v for v in b) + "   ; %d %s" % (s.idx, s.nome))
        o.append("")
        o.append("room_names:")
        o.append("        DW " + ",".join("rname_%d" % s.idx for s in self.salas))
        for s in self.salas:
            o.append('rname_%d: DB "%s",0' % (s.idx, s.nome))
        o.append("floor_names:")
        o.append("        DW " + ",".join("fname_%d" % k for k in range(len(self.andares))))
        for k, n in enumerate(self.andares):
            o.append('fname_%d: DB "%s",0' % (k, n))
        o.append("")
        o.append("templates:")
        o.append("        DW " + ",".join("tmpl_%d" % k for k in range(len(self.modelos))))
        for k, (nome, rs) in enumerate(self.modelos):
            o.append("tmpl_%d: DB %d   ; %s" % (k, len(rs), nome))
            for r in rs:
                o.append("        DB %d,%d,%d,%d,%d" % r)
        o.append("")
        o.append("links_lock:")
        o.append("        DB " + ",".join(str(v) for v in self.links))
        o.append("")
        o.append("; objetos: sala, cx, cy, tipo")
        o.append("items_def:")
        for (sala, cx, cy, tipo) in self.objetos:
            o.append("        DB %d,%d,%d,%d   ; %s" % (sala, cx, cy, tipo, self.salas[sala].nome))
        open(saida_inc, "w", encoding="utf-8").write("\n".join(o) + "\n")
        print("mapa: %d salas, %d portas, %d objetos, validação OK" %
              (len(self.salas), len(self.links), len(self.objetos)))
