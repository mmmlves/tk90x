#!/usr/bin/env python3
# gfx2asm.py - converte desenhos em texto (ASCII) em dados para o motor LABIRINTO.
# Licença MIT - (c) 2026 Projeto Emulador TK90X
#
# Formato do arquivo de entrada (uma definição por bloco):
#
#   sprite NOME      16 linhas x 16 colunas  -> sprite pré-deslocado pelo motor (jogador, inimigos)
#   sprite8 NOME      8 linhas x  8 colunas  -> sprite pequeno pré-deslocado (projétil)
#   item NOME        16 linhas x 16 colunas  -> desenho fixo 16x16 (objetos no chão)
#   tile NOME        16 linhas x 16 colunas  -> ladrilho 16x16 (paredes, portas, escadas...)
#   char NOME         8 linhas x  8 colunas  -> caractere 8x8 (ícones do painel)
#   mirror NOME = ORIGEM                      -> sprite espelhado na horizontal
#
#   '#' (ou 'X', '@') = pixel aceso; '.' (ou espaço) = apagado. Linhas com '#'
#   no começo da linha de definição são comentários apenas fora dos blocos.
#
# Saída (asm sjasmplus): rótulos spr16_src, spr8_src, ITEM_<nome>, TILE_<nome>,
# CHR_<nome> e constantes SPR_<NOME>, SPR8_<NOME>, NUM_SPR16, NUM_SPR8.
import sys

SIZES = {"sprite": 16, "sprite8": 8, "item": 16, "tile": 16, "char": 8}


def parse(path):
    defs = []          # (kind, name, rows)
    lines = open(path, encoding="utf-8").read().split("\n")
    i = 0
    while i < len(lines):
        ln = lines[i].rstrip()
        i += 1
        if not ln or ln.startswith(";") or ln.startswith("//"):
            continue
        parts = ln.split()
        kind = parts[0]
        if kind == "mirror":
            # mirror NOME = ORIGEM
            name, src = parts[1], parts[3]
            orig = [d for d in defs if d[1] == src]
            if not orig:
                sys.exit("mirror: origem %s nao encontrada" % src)
            k, _, rows = orig[0]
            defs.append((k, name, [r[::-1] for r in rows]))
            continue
        if kind not in SIZES:
            sys.exit("linha %d: tipo desconhecido '%s'" % (i, kind))
        name = parts[1]
        n = SIZES[kind]
        rows = []
        while len(rows) < n:
            if i >= len(lines):
                sys.exit("%s %s: faltam linhas" % (kind, name))
            r = lines[i].rstrip("\n")
            i += 1
            r = r.strip()
            if len(r) != n:
                sys.exit("%s %s: linha %d tem %d colunas (esperado %d): '%s'"
                         % (kind, name, len(rows), len(r), n, r))
            rows.append("".join("1" if c in "#X@" else "0" for c in r))
        defs.append((kind, name, rows))
    return defs


def bytes_rowmajor(rows):
    out = []
    for r in rows:
        for k in range(0, len(r), 8):
            out.append(int(r[k:k + 8], 2))
    return out


def bytes_quadrants(rows):
    out = []
    for qy in (0, 8):
        for qx in (0, 8):
            for y in range(8):
                out.append(int(rows[qy + y][qx:qx + 8], 2))
    return out


def db(vals):
    lines = []
    for k in range(0, len(vals), 16):
        lines.append("        DB " + ",".join("0x%02X" % v for v in vals[k:k + 16]))
    return "\n".join(lines)


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: gfx2asm.py graficos.txt saida.inc")
    defs = parse(sys.argv[1])
    out = ["; Gerado por motor/gfx2asm.py a partir de %s - nao edite." % sys.argv[1].split("/")[-1], ""]
    groups = [("sprite", "spr16_src", "SPR_"), ("sprite8", "spr8_src", "SPR8_")]
    for kind, label, pre in groups:
        items = [d for d in defs if d[0] == kind]
        out.append("%s:" % label)
        for idx, (_, name, rows) in enumerate(items):
            out.append("%s%s EQU %d" % (pre, name.upper(), idx))
            out.append(db(bytes_rowmajor(rows)))
        out.append("NUM_%s EQU %d" % ("SPR16" if kind == "sprite" else "SPR8", len(items)))
        out.append("")
    for kind, pre, conv in (("item", "ITEM_", bytes_rowmajor), ("tile", "TILE_", bytes_quadrants),
                            ("char", "CHR_", bytes_rowmajor)):
        for (_, name, rows) in [d for d in defs if d[0] == kind]:
            out.append("%s%s:" % (pre, name))
            out.append(db(conv(rows)))
        out.append("")
    open(sys.argv[2], "w", encoding="utf-8").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
