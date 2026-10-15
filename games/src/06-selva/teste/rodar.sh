#!/bin/sh
# rodar.sh - testes automáticos do jogo no núcleo do emulador (sem janela).
# Licença MIT - (c) 2026 Projeto Emulador TK90X
# Compila teste/harness.c com os fontes do núcleo (src/ do projeto, ou a pasta
# dada em NUCLEO=...) e roda os roteiros teste/*.txt: carga pela fita original
# (LOAD "" digitado), título, movimento, troca de tela, dardos, flores, runas,
# tocas, energia, fim de jogo e vitória.
# Uso: teste/rodar.sh [pasta_das_telas]
set -e
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")/.."
JOGO=$(pwd)
RAIZ=$(cd ../../.. && pwd)
NUCLEO=${NUCLEO:-$RAIZ/src}
SAIDA=${1:-$JOGO/gerado/telas}
mkdir -p "$SAIDA" gerado
[ -f gerado/selva.sym ] || ./build.sh --sem-copiar
cc -O2 -w -I"$NUCLEO" -o gerado/harness teste/harness.c \
   "$NUCLEO/z80.c" "$NUCLEO/tape.c" "$NUCLEO/snapshot.c" "$NUCLEO/machine.c" \
   "$NUCLEO/roms_embedded.c" "$NUCLEO/png.c" -lm
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
