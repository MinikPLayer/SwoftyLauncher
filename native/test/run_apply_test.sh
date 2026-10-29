#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/apply-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
mkdir -p "$OUT/reports"
SRC=(../src/core/apply/scheduler.cpp ../src/core/apply/ids.cpp ../src/core/registry/store.cpp ../src/core/registry/table.cpp
     ../src/core/registry/validate.cpp ../src/core/registry/schema.cpp ../src/core/registry/engine.cpp)
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness \
    -I../../packages/sdk/include -o "$OUT/apply_test" apply/apply_test.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -c ../src/core/apply/scheduler.cpp -o "$OUT/scheduler.windows.o"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -c ../src/core/apply/ids.cpp -o "$OUT/ids.windows.o"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness \
    -I../../packages/sdk/include -o "$OUT/files_test" apply/files_test.cpp
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -c ../src/core/apply/files.cpp -o "$OUT/files.windows.o"
"$OUT/apply_test" "$OUT/reports"
"$OUT/files_test"
