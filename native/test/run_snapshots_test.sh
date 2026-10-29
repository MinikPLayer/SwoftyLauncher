#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/snapshots-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
ZS=../third_party/zstd
FIXTURES=../../packages/sdk/std/fixtures
mkdir -p "$OUT/zstd"
ZOBJ=()
for c in $ZS/common/*.c $ZS/compress/*.c $ZS/decompress/*.c; do
    o=$OUT/zstd/$(basename "${c%.c}").o
    ZOBJ+=("$o")
    [[ $o -nt $c ]] || "${PIN[@]}" zig cc -target x86_64-linux-musl -O2 -DXXH_NAMESPACE=ZSTD_ -DZSTD_LEGACY_SUPPORT=0 \
        -DZSTD_DISABLE_ASM -DDEBUGLEVEL=0 -I$ZS -c "$c" -o "$o" &
done
wait
SRC=(../src/core/snapshots/snapshots.cpp ../src/core/registry/store.cpp
    ../src/core/registry/validate.cpp ../src/core/registry/schema.cpp)
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -I../../packages/sdk/include -I$ZS -o "$OUT/snapshots_test" snapshots/snapshots_test.cpp "${SRC[@]}" "${ZOBJ[@]}"
"$OUT/snapshots_test" --fixtures "$FIXTURES"
