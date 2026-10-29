#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/career-ops-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
mkdir -p "$OUT"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness -o "$OUT/lineup_plan_test" \
    career_ops/lineup_plan_test.cpp
"$OUT/lineup_plan_test"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness -o "$OUT/teams_plan_test" \
    career_ops/teams_plan_test.cpp
"$OUT/teams_plan_test"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I../../packages/sdk/include -o "$OUT/sdk_ops_test" career_ops/sdk_ops_test.cpp
"$OUT/sdk_ops_test"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I../../packages/sdk/include -o "$OUT/sdk_ops_test.exe" career_ops/sdk_ops_test.cpp
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness -o "$OUT/ops_logic_test" \
    career_ops/ops_logic_test.cpp
"$OUT/ops_logic_test"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -include career_ops/progressive_fuel_alias.hpp -I../src/core/career_ops -o "$OUT/core_fuel_logic_test" \
    ../../addons/progressive/native/test/fuel_logic_test.cpp
"$OUT/core_fuel_logic_test"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I../../packages/sdk/include -o "$OUT/sdk_ops_v4_test" career_ops/sdk_ops_v4_test.cpp
"$OUT/sdk_ops_v4_test"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I../../packages/sdk/include -o "$OUT/sdk_ops_v4_test.exe" career_ops/sdk_ops_v4_test.cpp
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I../../packages/sdk/include -o "$OUT/sdk_ops_v6_test" career_ops/sdk_ops_v6_test.cpp
"$OUT/sdk_ops_v6_test"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I../../packages/sdk/include -o "$OUT/sdk_ops_v6_test.exe" career_ops/sdk_ops_v6_test.cpp
