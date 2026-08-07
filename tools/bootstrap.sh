#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p vendor && cd vendor

if [ ! -d zig-x86_64-linux-0.16.0 ]; then
  curl -L -o zig.tar.xz https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz
  tar xf zig.tar.xz && rm zig.tar.xz
fi
if [ ! -d raylib-5.5 ]; then
  curl -L -o raylib.tar.gz https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
  tar xf raylib.tar.gz && rm raylib.tar.gz
fi
echo "toolchain ready"
