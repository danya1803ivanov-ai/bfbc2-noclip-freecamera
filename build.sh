#!/usr/bin/env bash
# Build Noclip for BFBC2 (32-bit) with zig cc (pip install ziglang) or CC="i686-w64-mingw32-gcc".
#   ./build.sh           -> build/Noclip.dll, build/NoclipLauncher.exe (copied to dist/)
#   ./build.sh install   -> also copies into "<game>/Noclip/" (GAME_DIR overrides the path)
set -euo pipefail
cd "$(dirname "$0")"
CC=${CC:-"python -m ziglang cc -target x86-windows-gnu"}
mkdir -p build
$CC -shared -O2 -Wall -o build/Noclip.dll src/dllmain.c src/noclip.c src/freecam.c src/extras.c src/hud.c src/input.c
$CC -O2 -Wall -o build/NoclipLauncher.exe src/inject.c
cp build/Noclip.dll build/NoclipLauncher.exe dist/
echo "built build/Noclip.dll build/NoclipLauncher.exe (copied to dist/)"
if [ "${1:-}" = install ]; then
  GAME_DIR=${GAME_DIR:-"/d/Battlefield - Bad Company 2"}
  mkdir -p "$GAME_DIR/Noclip"
  cp dist/Noclip.dll dist/NoclipLauncher.exe "$GAME_DIR/Noclip/"
  [ -f "$GAME_DIR/Noclip/Noclip.ini" ] || cp dist/Noclip.ini "$GAME_DIR/Noclip/"
  echo "installed into $GAME_DIR/Noclip"
fi
