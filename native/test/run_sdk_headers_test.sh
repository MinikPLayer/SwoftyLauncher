#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/sdk-headers-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
INC=(-I../../packages/sdk/include -Isdk_stub)
if [ -n "${SWOFTY_JSON_INCLUDE:-}" ]; then
    INC=(-I../../packages/sdk/include -I"$SWOFTY_JSON_INCLUDE")
fi
mkdir -p "$OUT"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness -Wno-missing-designated-field-initializers "${INC[@]}" \
    -o "$OUT/sdk_headers_test.exe" sdk_headers_test.cpp
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness -Wno-missing-designated-field-initializers "${INC[@]}" \
    -o "$OUT/sdk_headers_test" sdk_headers_test.cpp
"$OUT/sdk_headers_test"
"${PIN[@]}" zig cc -target x86_64-windows-gnu -std=c11 -Wall -Wextra -Werror -pedantic -I../../packages/sdk/include \
    -o "$OUT/sdk_api_c_test.exe" sdk_api_c_test.c
"${PIN[@]}" zig cc -target x86_64-linux-musl -std=c11 -Wall -Wextra -Werror -pedantic -I../../packages/sdk/include \
    -o "$OUT/sdk_api_c_test" sdk_api_c_test.c
"$OUT/sdk_api_c_test"
