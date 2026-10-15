#!/usr/bin/env bash
# Gera os APKs do TK90X para Android (debug e release) - chamado por "make android".
#
#   android/build.sh [DIR_SAIDA]        (padrao: <repo>/build/android)
#
# Saidas: DIR_SAIDA/tk90x-<versao>-debug.apk e DIR_SAIDA/tk90x-<versao>-release.apk
# (release assinado com a chave de desenvolvimento de tools/android/keystore).
# Os intermediarios do Gradle/CMake ficam em DIR_SAIDA/gradle e DIR_SAIDA/cxx.
# TK_GRADLE_DAEMON=1 mantem o daemon do Gradle vivo (builds seguintes bem mais rapidos).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
OUT="${1:-$REPO/build/android}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

if [ ! -f "$REPO/tools/android/env.sh" ]; then
    echo "tools/android/env.sh nao encontrado: instale o kit Android (docs/research/android-build.md)" >&2
    exit 1
fi
# shellcheck source=/dev/null
. "$REPO/tools/android/env.sh"

VERSION="$(sed -n "s/^def tkVersionName = '\(.*\)'.*/\1/p" "$HERE/app/build.gradle")"
[ -n "$VERSION" ] || { echo "versionName nao encontrado em app/build.gradle" >&2; exit 1; }

DAEMON=--no-daemon
[ "${TK_GRADLE_DAEMON:-0}" = 1 ] && DAEMON=--daemon

cd "$HERE"
./gradlew $DAEMON --console=plain -PtkBuildDir="$OUT" assembleDebug assembleRelease

APKS="$OUT/gradle/app/outputs/apk"
cp "$APKS/debug/app-debug.apk" "$OUT/tk90x-$VERSION-debug.apk"
if [ -f "$APKS/release/app-release.apk" ]; then
    cp "$APKS/release/app-release.apk" "$OUT/tk90x-$VERSION-release.apk"
else
    echo "AVISO: sem tools/android/keystore/keystore.properties o release sai sem assinatura" >&2
    cp "$APKS/release/app-release-unsigned.apk" "$OUT/tk90x-$VERSION-release-unsigned.apk"
fi
ls -l "$OUT"/tk90x-"$VERSION"-*.apk
