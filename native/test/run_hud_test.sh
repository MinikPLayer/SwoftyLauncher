#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
CXX=${CXX:-c++}
if ! command -v "$CXX" >/dev/null; then
    exec nix shell nixpkgs#gcc -c "$SELF" "$@"
fi
OUT=${1:-$PWD/../build/hud-test}
mkdir -p "$OUT"
"$CXX" -std=c++23 -O2 -Wall -Wextra -I../src/core -I../../packages/sdk/include -o "$OUT/hud_test" hud_test.cpp
"$OUT/hud_test" "$OUT/capture-test.png" "$OUT"
if command -v python3 >/dev/null; then
    python3 check_png.py "$OUT/capture-test.png"
fi
