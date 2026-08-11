#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

S=vendor/raylib-5.5/src

if [ ! -d "$S" ] || [ ! -x "$(echo vendor/zig-*/zig)" ]; then
  echo "toolchain missing - running tools/bootstrap.sh"
  tools/bootstrap.sh
fi

RAYLIB_CORE="$S/rcore.c $S/rshapes.c $S/rtextures.c $S/rtext.c $S/raudio.c $S/utils.c"

CFLAGS="-Os -I$S -I$S/external/glfw/include -Isrc \
  -DEXTERNAL_CONFIG_FLAGS -include cfg/config.h \
  -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 \
  -ffunction-sections -fdata-sections"

find_zig() {
  local z
  z=$(echo vendor/zig-*/zig)
  [ -x "$z" ] || { echo "zig missing - run tools/bootstrap.sh" >&2; exit 1; }
  echo "$z"
}

build_win() {
  local zig; zig=$(find_zig)
  rm -rf dist-win && mkdir -p dist-win
  "$zig" cc -target x86_64-windows-gnu $CFLAGS \
    -Wl,--gc-sections -s -Wl,--subsystem,windows \
    src/main.c $RAYLIB_CORE "$S/rglfw.c" -o dist-win/defrag.exe \
    -lgdi32 -luser32 -lshell32 -lwinmm -lopengl32 -lkernel32 -lole32 -loleaut32
  tools/check-size.sh dist-win
}

build_mac() {
  rm -rf dist-mac && mkdir -p dist-mac build
  cc $CFLAGS -w -x objective-c -c "$S/rglfw.c" -o build/rglfw.o || return 1
  cc $CFLAGS -w -Wl,-dead_strip \
    src/main.c $RAYLIB_CORE build/rglfw.o -o dist-mac/defrag \
    -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL \
    -framework CoreAudio -framework AudioToolbox || return 1
  strip -x dist-mac/defrag
  echo "dev build: $(wc -c < dist-mac/defrag | tr -d " ") bytes (not the submission target)"
}

build_host_dev() {
  case "$(uname -s)" in
    Darwin) build_mac ;;
    *)      echo "dev build skipped - host is not macOS (submission target is win)" ;;
  esac
}

case "${1:-both}" in
  win)   build_win ;;
  mac)   build_mac ;;
  both)
    build_host_dev || echo "WARN: dev build failed - submission build unaffected" >&2
    build_win
    ;;
  *) echo "usage: $0 [both|win|mac]   (default: both)" >&2; exit 2 ;;
esac
