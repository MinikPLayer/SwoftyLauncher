#!/usr/bin/env bash
# Runs every templates/std addon's test/*.cpp on the host with the core's registry engine linked, so the
# fake core answers effective values and origins (swofty dev test runs the same tests without it).
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
REPO=$(readlink -f ../..)
OUT=${1:-${TMPDIR:-/tmp}/std-template-tests}
mkdir -p "$OUT"
export ZIG_GLOBAL_CACHE_DIR=${ZIG_GLOBAL_CACHE_DIR:-$OUT/zig-cache} ZIG_LOCAL_CACHE_DIR=${ZIG_LOCAL_CACHE_DIR:-$OUT/zig-cache}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
command -v taskset >/dev/null || PIN=()
ENGINE=(store.cpp table.cpp validate.cpp schema.cpp engine.cpp older.cpp)
ENGINE=("${ENGINE[@]/#/$REPO/native/src/core/registry/}")
failed=0
for addon in */; do
    addon=${addon%/}
    for t in "$addon"/test/*.cpp; do
        [ -f "$t" ] || continue
        name=$addon-$(basename "$t" .cpp)
        if ! "${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness \
            -Wno-missing-designated-field-initializers -Wno-ignored-attributes -Wno-cast-function-type-mismatch -fdeclspec -I"$REPO/packages/sdk/include" -I"$addon/native/src" \
            -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine -o "$OUT/$name" "$t" "${ENGINE[@]}" 2>"$OUT/$name.log"; then
            cat "$OUT/$name.log"
            echo "FAIL  $name: it doesn't build"
            failed=$((failed + 1))
            continue
        fi
        if (cd "$addon" && "$OUT/$name"); then
            echo "ok    $name"
        else
            echo "FAIL  $name"
            failed=$((failed + 1))
        fi
    done
done
echo "templates/std tests: $failed failed"
[ "$failed" -eq 0 ]
