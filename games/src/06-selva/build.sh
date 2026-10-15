#!/bin/sh
# build.sh - monta "A Selva de Verdemor" (TK90X/TK95, 48K)
# Licença MIT - (c) 2026 Projeto Emulador TK90X
# Requisitos: python3 e o sjasmplus de tools/zxdev (ou outro sjasmplus >= 1.20 no PATH).
set -e
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")"
RAIZ=$(cd ../../.. && pwd)
ZXDEV="$RAIZ/tools/zxdev/bin"
if [ -x "$ZXDEV/sjasmplus" ]; then SJASM="$ZXDEV/sjasmplus"; else SJASM=sjasmplus; fi
SAIDA="$RAIZ/games/06-sabre-wulf-alt-selva-de-verdemor.tap"

mkdir -p gerado
python3 motor/gfx2asm.py graficos.txt gerado/graficos.inc
python3 mapa.py gerado/mapa.inc
"$SJASM" --nologo --msg=war --sym=gerado/selva.sym --lst=gerado/selva.lst selva.asm
if [ -f "$ZXDEV/tapcheck.py" ]; then python3 -I "$ZXDEV/tapcheck.py" gerado/selva.tap; fi
if [ "$1" != "--sem-copiar" ]; then
    cp gerado/selva.tap "$SAIDA"
    echo "copiado para $SAIDA"
fi
