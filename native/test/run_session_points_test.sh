#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/session-points-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
SH=../third_party/safetyhook
mkdir -p "$OUT"
[[ $OUT/Zydis.o -nt $SH/Zydis.c ]] || "${PIN[@]}" zig cc -target x86_64-linux-musl -O2 -I$SH -c $SH/Zydis.c -o "$OUT/Zydis.o"
[[ $OUT/safetyhook.o -nt $SH/safetyhook.cpp ]] ||
    "${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -I$SH -c $SH/safetyhook.cpp -o "$OUT/safetyhook.o"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness \
    -I../../packages/sdk/include -I$SH -o "$OUT/session_points_test" hooks/session_points_test.cpp "$OUT/safetyhook.o" "$OUT/Zydis.o"
"$OUT/session_points_test"
