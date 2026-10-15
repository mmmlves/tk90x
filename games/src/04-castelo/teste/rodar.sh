#!/bin/sh
# rodar.sh - testes automáticos do jogo no núcleo do emulador (sem janela).
# Licença MIT - (c) 2026 Projeto Emulador TK90X
# Compila teste/harness.c com os fontes do núcleo (src/) e roda os roteiros
# teste/*.txt: carga pela fita original (LOAD "" digitado), título, movimento,
# troca de sala, tiro, chaves/portas, escadas, energia, fim de jogo e vitória.
# Uso: teste/rodar.sh [pasta_das_telas]
set -e
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")/.."
JOGO=$(pwd)
RAIZ=$(cd ../../.. && pwd)
SAIDA=${1:-$JOGO/gerado/telas}
mkdir -p "$SAIDA" gerado
[ -f gerado/castelo.sym ] || ./build.sh --sem-copiar
cc -O2 -w -I"$RAIZ/src" -o gerado/harness teste/harness.c \
   "$RAIZ/src/z80.c" "$RAIZ/src/tape.c" "$RAIZ/src/snapshot.c" "$RAIZ/src/machine.c" \
   "$RAIZ/src/roms_embedded.c" "$RAIZ/src/png.c" -lm
falhas=0
for t in teste/[0-9]*.txt; do
    sed -e "s|@JOGO@|$JOGO|g" -e "s|@SAIDA@|$SAIDA|g" "$t" > gerado/roteiro.txt
    if ./gerado/harness gerado/roteiro.txt > "gerado/$(basename "$t" .txt).log"; then
        echo "ok     $t"
    else
        echo "FALHOU $t (veja gerado/$(basename "$t" .txt).log)"; falhas=$((falhas+1))
    fi
done
echo "telas em $SAIDA"
exit $falhas
