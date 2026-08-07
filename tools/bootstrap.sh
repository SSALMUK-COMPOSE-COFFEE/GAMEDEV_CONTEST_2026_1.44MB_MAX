#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p vendor && cd vendor

ZIG_VER=0.16.0
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)  ZIG_DIR=zig-aarch64-macos-$ZIG_VER ;;
  Darwin-x86_64) ZIG_DIR=zig-x86_64-macos-$ZIG_VER ;;
  Linux-x86_64)  ZIG_DIR=zig-x86_64-linux-$ZIG_VER ;;
  Linux-aarch64) ZIG_DIR=zig-aarch64-linux-$ZIG_VER ;;
  *) echo "unsupported host: $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

if [ ! -d "$ZIG_DIR" ]; then
  curl -L -o zig.tar.xz "https://ziglang.org/download/$ZIG_VER/$ZIG_DIR.tar.xz"
  tar xf zig.tar.xz && rm zig.tar.xz
fi
if [ ! -d raylib-5.5 ]; then
  curl -L -o raylib.tar.gz https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
  tar xf raylib.tar.gz && rm raylib.tar.gz
fi
echo "toolchain ready ($ZIG_DIR)"
