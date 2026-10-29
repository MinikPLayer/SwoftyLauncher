#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# records_logic.hpp and choose_logic.hpp on the host: register.cpp registers into the core's registry (native/src/core/registry,
# the engine behind the SDK fake core), a second addon layers swofty.progressive/2017, and the overlaid
# years.json goes through Progressive's season planner and ratings parser (records_test.cpp).
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if command -v zig >/dev/null; then
    ZIG=(zig)
else
    ZIG=(nix run nixpkgs#zig --)
fi

REPO=$(readlink -f ../../../../..)
OUT=${PG_TEST_DIR:-${TMPDIR:-/tmp}/progressive-test}/records
mkdir -p "$OUT"
export ZIG_GLOBAL_CACHE_DIR=${ZIG_GLOBAL_CACHE_DIR:-$OUT/zig-cache} ZIG_LOCAL_CACHE_DIR=${ZIG_LOCAL_CACHE_DIR:-$OUT/zig-cache}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
CORE=$REPO/native/src/core/registry

"${PIN[@]}" "${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I"$REPO/packages/sdk/include" -I../../src \
    -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine -o "$OUT/records_test" records_test.cpp ../../src/register.cpp \
    "$CORE/store.cpp" "$CORE/table.cpp" "$CORE/validate.cpp" "$CORE/schema.cpp" "$CORE/engine.cpp"
"$OUT/records_test" "$REPO/addons/progressive"
