#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

S=vendor/raylib-5.5/src
[ -d "$S" ] || { echo "raylib missing - run tools/bootstrap.sh" >&2; exit 1; }

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
  cc $CFLAGS -w -x objective-c -c "$S/rglfw.c" -o build/rglfw.o
  cc $CFLAGS -w -Wl,-dead_strip \
    src/main.c $RAYLIB_CORE build/rglfw.o -o dist-mac/defrag \
    -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL \
    -framework CoreAudio -framework AudioToolbox
  strip -x dist-mac/defrag
  echo "dev build: $(wc -c < dist-mac/defrag | tr -d " ") bytes (not the submission target)"
}

build_linux() {
  rm -rf dist-dev && mkdir -p dist-dev
  cc $CFLAGS -w -D_GLFW_X11 -Wl,--gc-sections -s \
    src/main.c $RAYLIB_CORE "$S/rglfw.c" -o dist-dev/defrag \
    -lGL -lm -lpthread -ldl -lrt -lX11
  echo "dev build: $(wc -c < dist-dev/defrag | tr -d " ") bytes (not the submission target)"
}

build_host_dev() {
  case "$(uname -s)" in
    Darwin) build_mac ;;
    *)      build_linux ;;
  esac
}

case "${1:-win}" in
  win)   build_win ;;
  mac)   build_mac ;;
  linux) build_linux ;;
  both)  build_host_dev; build_win ;;
  *) echo "usage: $0 win|mac|linux|both" >&2; exit 2 ;;
esac
