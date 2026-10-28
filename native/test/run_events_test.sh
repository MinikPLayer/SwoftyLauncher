#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/events-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
mkdir -p "$OUT"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/events_dispatch_test" events_dispatch_test.cpp
"$OUT/events_dispatch_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/career_state_test" career_state_test.cpp
"$OUT/career_state_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/career_start_test" career_start_test.cpp
"$OUT/career_start_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/career_content_test" career_content_test.cpp
"$OUT/career_content_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/era_logic_test" era_logic_test.cpp
"$OUT/era_logic_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/car_category_test" car_category_test.cpp
"$OUT/car_category_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/save_splice_test" save_splice_test.cpp
"$OUT/save_splice_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/netplay_drive_test" netplay_drive_test.cpp
"$OUT/netplay_drive_test"
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/storage_logic_test" storage_logic_test.cpp
"$OUT/storage_logic_test"
