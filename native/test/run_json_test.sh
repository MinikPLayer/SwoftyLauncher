#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/json-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
FLAGS=(-std=c++23 -O2 -Wall -Wextra -Werror -Wno-nullability-completeness -I../../packages/sdk/include)
mkdir -p "$OUT"
# The SDK builds addons with zig for x86_64-windows-gnu; the header must compile there as it is.
"${PIN[@]}" zig c++ -target x86_64-windows-gnu "${FLAGS[@]}" -c -o "$OUT/json_test.obj" json/json_test.cpp
"${PIN[@]}" zig c++ -target x86_64-linux-musl "${FLAGS[@]}" -o "$OUT/json_test" json/json_test.cpp
"${PIN[@]}" "$OUT/json_test" "${JSON_FUZZ_ROUNDS:-20000}"
# A sanitizer build runs the same cases and a longer fuzz loop where the host compiler supports it.
CXX=${CXX:-c++}
if command -v "$CXX" >/dev/null && echo 'int main(){}' | "$CXX" -x c++ -fsanitize=address,undefined -o "$OUT/probe" - 2>/dev/null; then
    "${PIN[@]}" "$CXX" -std=c++23 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
        -I../../packages/sdk/include -o "$OUT/json_test_san" json/json_test.cpp
    "${PIN[@]}" "$OUT/json_test_san" "${JSON_SAN_FUZZ_ROUNDS:-100000}"
else
    echo "json: no host compiler with -fsanitize=address,undefined; skipped the sanitizer build"
fi
