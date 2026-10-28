#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
. ./game_lock.sh
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/scan-test}
EXE=${SWOFTY_TEST_EXE:-$PWD/../../work/exe/F1_25.exe}
BINDINGS=${SWOFTY_TEST_BINDINGS:-$PWD/../dist/swofty/addons/core/bindings.json}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
mkdir -p "$OUT"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -o "$OUT/scan_test" scan_test.cpp
if [[ -f $EXE && -f $BINDINGS ]]; then
    "${PIN[@]}" "$OUT/scan_test" "$EXE" "$BINDINGS"
else
    echo "scan_test: no $EXE or $BINDINGS; the checks against the game exe are skipped"
    "$OUT/scan_test"
fi
