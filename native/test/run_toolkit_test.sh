#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/toolkit-test}
mkdir -p "$OUT"
zig cc -target x86_64-windows-gnu -O2 -shared -o "$OUT/swofty_core.dll" toolkit_stub_core.c
zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/toolkit_test.exe" toolkit_test.cpp
rm -f "$OUT"/*.lib
cd "$OUT"
WINEDEBUG=-all wine toolkit_test.exe
