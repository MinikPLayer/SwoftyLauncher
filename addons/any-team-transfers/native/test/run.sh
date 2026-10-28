#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if command -v zig >/dev/null; then
    ZIG=(zig)
else
    ZIG=(nix run nixpkgs#zig --)
fi

OUT=${AT_TEST_DIR:-${TMPDIR:-/tmp}/any-team-test}
mkdir -p "$OUT"
export ZIG_GLOBAL_CACHE_DIR=$OUT/zig-cache ZIG_LOCAL_CACHE_DIR=$OUT/zig-cache

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/offers_logic_test" offers_logic_test.cpp
"$OUT/offers_logic_test" "$@"
