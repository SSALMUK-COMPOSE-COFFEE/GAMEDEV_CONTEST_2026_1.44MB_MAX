#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

ZIG=vendor/zig-x86_64-linux-0.16.0/zig
S=vendor/raylib-5.5/src
[ -x "$ZIG" ] && [ -d "$S" ] || { echo "toolchain missing - run tools/bootstrap.sh" >&2; exit 1; }
RAYLIB_SRC="$S/rcore.c $S/rshapes.c $S/rtextures.c $S/rtext.c $S/raudio.c $S/utils.c $S/rglfw.c"
COMMON="-Os -I$S -I$S/external/glfw/include -Isrc \
  -DEXTERNAL_CONFIG_FLAGS -include cfg/config.h \
  -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -s"

build_win() {
  rm -rf dist-win && mkdir -p dist-win
  $ZIG cc -target x86_64-windows-gnu $COMMON -Wl,--subsystem,windows \
    src/main.c $RAYLIB_SRC -o dist-win/defrag.exe \
    -lgdi32 -luser32 -lshell32 -lwinmm -lopengl32 -lkernel32 -lole32 -loleaut32
  tools/check-size.sh dist-win
}

build_linux() {
  rm -rf dist-dev && mkdir -p dist-dev
  cc $COMMON -D_GLFW_X11 -w src/main.c $RAYLIB_SRC -o dist-dev/defrag \
    -lGL -lm -lpthread -ldl -lrt -lX11
  echo "dev build: $(stat -c%s dist-dev/defrag) bytes (not the submission target)"
}

case "${1:-win}" in
  win)   build_win ;;
  linux) build_linux ;;
  both)  build_linux; build_win ;;
  *) echo "usage: $0 win|linux|both" >&2; exit 2 ;;
esac
