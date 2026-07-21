#!/usr/bin/env bash
#
# build_mac.sh — Compile BeatMate (version macOS) A LANCER SUR UN MAC.
# Ce dossier est autonome : le source Windows n'est ni ici ni touche.
#
#   Prerequis :  xcode-select --install ; brew install cmake
#   Usage :
#     ./build_mac.sh                 # arch native
#     ARCH=arm64  ./build_mac.sh     # Apple Silicon
#     ARCH=x86_64 ./build_mac.sh     # Intel / VM x86
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build-mac"

echo "== BeatMate macOS — build =="
CMAKE_ARGS=(-S "$HERE" -B "$BUILD" -G Xcode -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15)
if [[ -n "${ARCH:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="$ARCH")
  echo "  arch : $ARCH"
else
  echo "  arch : native"
fi

if [[ -n "${EXTRA_CMAKE_ARGS:-}" ]]; then
  CMAKE_ARGS+=(${EXTRA_CMAKE_ARGS})
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" --config Release --target BeatMateV11

APP="$BUILD/BeatMateV11_artefacts/Release/BeatMate V11.app"
if [[ -d "$APP" ]]; then
  echo ""
  echo "OK -> $APP"
  echo "Tester :     open \"$APP\""
  echo "Empaqueter : ./package_dmg.sh \"$APP\""
else
  echo ""
  echo "L'app n'a pas ete produite. Copie-moi les erreurs ci-dessus, je corrige."
  exit 1
fi
