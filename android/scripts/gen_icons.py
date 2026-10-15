#!/usr/bin/env python3
"""Gera o icone do app Android do TK90X (desenho original, sem logotipos de terceiros).

Desenho: fundo preto, as letras "TK" em blocos (como a fonte 8x8 da ROM) e quatro
listras diagonais coloridas (vermelho, amarelo, verde, ciano), lembrando o arco-iris
dos micros da familia Spectrum.

Saidas (em android/app/src/main/res):
  drawable/ic_launcher_foreground.xml   camada da frente do icone adaptavel (vetor, 108x108)
  values/ic_launcher_background.xml     cor de fundo (preto)
  mipmap-anydpi-v26/ic_launcher.xml     icone adaptavel (Android 8+), com camada monocromatica
  mipmap-anydpi-v26/ic_launcher_round.xml
  mipmap-*dpi/ic_launcher.png           icone quadrado arredondado (Android 5-7)
  mipmap-*dpi/ic_launcher_round.png     icone redondo (Android 7.1)

Uso (so a biblioteca padrao do Python):  python3 -I android/scripts/gen_icons.py
"""
import os
import struct
import sys
import zlib

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'app', 'src', 'main', 'res')

# Tela do icone adaptavel: 108x108 unidades; a parte sempre visivel e o circulo de
# diametro 66 no centro (21..87). Os icones PNG antigos mostram o recorte 18..90.
CANVAS = 108.0
LEGACY_MIN, LEGACY_MAX = 18.0, 90.0

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
STRIPES = [(232, 35, 31), (245, 211, 0), (35, 177, 77), (31, 169, 225)]  # verm., amar., verde, ciano

# Letras em blocos 5x7 (# = bloco)
GLYPHS = {
    'T': ["#####",
          "..#..",
          "..#..",
          "..#..",
          "..#..",
          "..#..",
          "..#.."],
    'K': ["#...#",
          "#..#.",
          "#.#..",
          "##...",
          "#.#..",
          "#..#.",
          "#...#"],
}
TEXT = "TK"
CELL = 4.0                    # lado de um bloco, em unidades
TEXT_X, TEXT_Y = 32.0, 27.0   # canto superior esquerdo do texto (largura 11 blocos = 44)

# Listras "/": faixa entre as retas x + y = c e x + y = c + STRIPE_W, recortada em y
STRIPE_C0 = 113.0
STRIPE_W = 6.0
STRIPE_Y0, STRIPE_Y1 = 62.0, 80.0


def letter_rects():
    """Retangulos (x, y, w, h) dos blocos do texto, juntando blocos vizinhos na linha."""
    rects = []
    x = TEXT_X
    for ch in TEXT:
        rows = GLYPHS[ch]
        for r, row in enumerate(rows):
            c = 0
            while c < len(row):
                if row[c] == '#':
                    start = c
                    while c < len(row) and row[c] == '#':
                        c += 1
                    rects.append((x + start * CELL, TEXT_Y + r * CELL, (c - start) * CELL, CELL))
                else:
                    c += 1
        x += (len(rows[0]) + 1) * CELL
    return rects


def stripe_polys():
    """Paralelogramos das listras: vertices em sentido horario."""
    polys = []
    for i in range(len(STRIPES)):
        c = STRIPE_C0 + i * STRIPE_W
        y0, y1 = STRIPE_Y0, STRIPE_Y1
        polys.append([(c - y0, y0), (c + STRIPE_W - y0, y0),
                      (c + STRIPE_W - y1, y1), (c - y1, y1)])
    return polys


def fmt(v):
    s = ('%.2f' % v).rstrip('0').rstrip('.')
    return s if s else '0'


def write_vectors():
    paths = []
    rects = letter_rects()
    d = ' '.join('M%s,%sh%sv%sh-%sz' % (fmt(x), fmt(y), fmt(w), fmt(h), fmt(w))
                 for x, y, w, h in rects)
    paths.append(('#FFFFFF', d))
    for color, poly in zip(STRIPES, stripe_polys()):
        d = 'M' + ' L'.join('%s,%s' % (fmt(x), fmt(y)) for x, y in poly) + 'z'
        paths.append(('#%02X%02X%02X' % color, d))
    body = '\n'.join('    <path android:fillColor="%s"\n        android:pathData="%s" />' % p
                     for p in paths)
    fg = ('<?xml version="1.0" encoding="utf-8"?>\n'
          '<!-- Gerado por android/scripts/gen_icons.py: nao editar a mao. -->\n'
          '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
          '    android:width="108dp" android:height="108dp"\n'
          '    android:viewportWidth="108" android:viewportHeight="108">\n'
          + body + '\n</vector>\n')
    save(os.path.join(RES, 'drawable', 'ic_launcher_foreground.xml'), fg.encode())
    save(os.path.join(RES, 'values', 'ic_launcher_background.xml'),
         b'<?xml version="1.0" encoding="utf-8"?>\n'
         b'<!-- Gerado por android/scripts/gen_icons.py -->\n'
         b'<resources>\n    <color name="ic_launcher_background">#000000</color>\n</resources>\n')
    adaptive = (b'<?xml version="1.0" encoding="utf-8"?>\n'
                b'<!-- Gerado por android/scripts/gen_icons.py -->\n'
                b'<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                b'    <background android:drawable="@color/ic_launcher_background" />\n'
                b'    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
                b'    <monochrome android:drawable="@drawable/ic_launcher_foreground" />\n'
                b'</adaptive-icon>\n')
    for name in ('ic_launcher.xml', 'ic_launcher_round.xml'):
        save(os.path.join(RES, 'mipmap-anydpi-v26', name), adaptive)


def inside_poly(px, py, poly):
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def render(size, round_icon):
    """Rasteriza o icone antigo (recorte 18..90) com 4x4 amostras por pixel."""
    rects = letter_rects()
    polys = list(zip(STRIPES, stripe_polys()))
    span = LEGACY_MAX - LEGACY_MIN
    mid = (LEGACY_MIN + LEGACY_MAX) / 2
    radius_sq = (span / 2) ** 2
    corner = span * 0.16                       # raio dos cantos do quadrado arredondado
    ss = 4
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    x = LEGACY_MIN + (px + (sx + 0.5) / ss) * span / size
                    y = LEGACY_MIN + (py + (sy + 0.5) / ss) * span / size
                    if round_icon:
                        if (x - mid) ** 2 + (y - mid) ** 2 > radius_sq:
                            continue
                    else:
                        dx = max(LEGACY_MIN + corner - x, 0, x - (LEGACY_MAX - corner))
                        dy = max(LEGACY_MIN + corner - y, 0, y - (LEGACY_MAX - corner))
                        if dx * dx + dy * dy > corner * corner:
                            continue
                    col = BLACK
                    for rx, ry, rw, rh in rects:
                        if rx <= x < rx + rw and ry <= y < ry + rh:
                            col = WHITE
                            break
                    else:
                        for c, poly in polys:
                            if inside_poly(x, y, poly):
                                col = c
                                break
                    acc[0] += col[0]
                    acc[1] += col[1]
                    acc[2] += col[2]
                    acc[3] += 255
            n = ss * ss
            a = acc[3] // n
            if a:
                # cor nao pre-multiplicada: media so das amostras cobertas
                cov = acc[3] // 255
                row += bytes((acc[0] // cov, acc[1] // cov, acc[2] // cov, a))
            else:
                row += b'\0\0\0\0'
        rows.append(bytes(row))
    return rows


def png_bytes(size, rows):
    raw = b''.join(b'\0' + r for r in rows)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def save(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    print('gerado', os.path.relpath(path, os.path.join(RES, '..', '..', '..', '..')))


def main():
    write_vectors()
    densities = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}
    for dens, size in densities.items():
        for round_icon, name in ((False, 'ic_launcher.png'), (True, 'ic_launcher_round.png')):
            save(os.path.join(RES, 'mipmap-' + dens, name),
                 png_bytes(size, render(size, round_icon)))
    if len(sys.argv) > 1:                       # opcional: amostra grande para conferir
        save(sys.argv[1], png_bytes(512, render(512, False)))


if __name__ == '__main__':
    main()
