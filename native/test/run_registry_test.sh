#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/registry-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
CASES=../../packages/sdk/std/tests/cases
mkdir -p "$OUT"
SRC=(../src/core/registry/store.cpp ../src/core/registry/table.cpp ../src/core/registry/validate.cpp ../src/core/registry/schema.cpp ../src/core/registry/engine.cpp)
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine -o "$OUT/corpus_test" registry/corpus_test.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -I../../packages/sdk/include \
    -c ../src/core/registry/store.cpp -o "$OUT/store.windows.o"
"${PIN[@]}" zig cc -target x86_64-linux-musl -O2 -Wall -Wextra -I../src/loader -o "$OUT/pe_exports_test" \
    registry/pe_exports_test.c
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -shared -o "$OUT/with_register.dll" registry/fixture_register.c
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -shared -o "$OUT/without_register.dll" registry/fixture_plain.c
"$OUT/pe_exports_test" "$OUT/with_register.dll" "$OUT/without_register.dll"
python3 "$CASES/../check_cases.py"
"$OUT/corpus_test" "$CASES"
"$OUT/corpus_test" --fake "$CASES"
python3 registry/ref.py "$CASES"
python3 registry/random_stacks.py --runner "$OUT/corpus_test" --count "${REGISTRY_STACKS:-300}" --seed "${REGISTRY_SEED:-1606}"
"$OUT/corpus_test" --bench "$OUT/perf.log"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-cast-function-type-mismatch -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine \
    -o "$OUT/tolerant_edit_test" registry/tolerant_edit_test.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-cast-function-type-mismatch -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine \
    -o "$OUT/iterate_test" registry/iterate_test.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-cast-function-type-mismatch -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine \
    -o "$OUT/index_load_test" registry/index_load_test.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-cast-function-type-mismatch -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine \
    -o "$OUT/declare_all_test" registry/declare_all_test.cpp ../src/core/registry/older.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-cast-function-type-mismatch -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine \
    -o "$OUT/career_wait_test" registry/career_wait_test.cpp "${SRC[@]}"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -Wno-cast-function-type-mismatch -I../../packages/sdk/include -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine \
    -o "$OUT/older_test" registry/older_test.cpp ../src/core/registry/older.cpp "${SRC[@]}"
"$OUT/older_test"
"$OUT/index_load_test"
"$OUT/declare_all_test"
"$OUT/career_wait_test"
"$OUT/tolerant_edit_test"
"$OUT/iterate_test"
