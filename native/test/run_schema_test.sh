#!/usr/bin/env bash
# The core's schema check (src/core/registry/schema.cpp): its unit test, then the agreement test, which
# validates every example and must-fail case under each registries folder with the core and with
# tools/std/validate.py and fails on any difference in verdicts or error pointers (schema/agree.py), and
# the pattern matcher against Python's re (schema/agree_patterns.py) and swofty::json::merge_patch against
# stdschema.merge_patch (schema/agree_merge.py).
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=${ZIG_GLOBAL_CACHE_DIR:-$PWD/../build/zig-cache} ZIG_LOCAL_CACHE_DIR=${ZIG_LOCAL_CACHE_DIR:-$PWD/../build/zig-cache}
OUT=${1:-$PWD/../build/schema-test}
REPO=$(readlink -f ../..)
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
FLAGS=(-std=c++23 -O2 -Wall -Wextra -Werror -Wno-nullability-completeness -I../../packages/sdk/include)
SRC=../src/core/registry/schema.cpp
mkdir -p "$OUT"
# The core builds for x86_64-windows-gnu; the check must compile there as it is.
"${PIN[@]}" zig c++ -target x86_64-windows-gnu "${FLAGS[@]}" -c -o "$OUT/schema.obj" "$SRC"
"${PIN[@]}" zig c++ -target x86_64-linux-musl "${FLAGS[@]}" -o "$OUT/schema_test" schema/schema_test.cpp "$SRC"
"${PIN[@]}" zig c++ -target x86_64-linux-musl "${FLAGS[@]}" -o "$OUT/schema_agree" schema/schema_agree.cpp "$SRC"
"${PIN[@]}" "$OUT/schema_test" "${SCHEMA_FUZZ_ROUNDS:-20000}" | tee "$OUT/schema_test.log"
grep '^speed:' "$OUT/schema_test.log" | sed "s/^/$(date -u +%FT%TZ) /" >>"$OUT/perf.log"
# A sanitizer build runs the same cases and a longer fuzz loop where the host compiler supports it.
CXX=${CXX:-c++}
if command -v "$CXX" >/dev/null && echo 'int main(){}' | "$CXX" -x c++ -fsanitize=address,undefined -o "$OUT/probe" - 2>/dev/null; then
    "${PIN[@]}" "$CXX" -std=c++23 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
        -I../../packages/sdk/include -o "$OUT/schema_test_san" schema/schema_test.cpp "$SRC"
    "${PIN[@]}" "$OUT/schema_test_san" "${SCHEMA_SAN_FUZZ_ROUNDS:-100000}"
else
    echo "schema: no host compiler with -fsanitize=address,undefined; skipped the sanitizer build"
fi

# Each registries folder that exists: the SDK's real schemas and the schema tooling's fixtures, plus any
# extra folders (relative to the repository) in SCHEMA_AGREE_ROOTS.
ROOTS=(packages/sdk/std/registries tools/std/tests/fixtures/registries ${SCHEMA_AGREE_ROOTS:-})
checked=0
for root in "${ROOTS[@]}"; do
    [[ -d $REPO/$root ]] || continue
    name=$(echo "$root" | tr / _)
    (cd "$REPO" && python3 tools/std/validate.py --registries "$root" --json >"$OUT/python_$name.json") || true
    status=0
    (cd "$REPO" && "$OUT/schema_agree" "$root" "$root" >"$OUT/core_$name.json") || status=$?
    if [[ $status -gt 1 ]]; then
        echo "schema: the core couldn't compile a schema under $root (exit $status)" >&2
        exit 1
    fi
    echo "== $root"
    python3 schema/agree.py "$OUT/python_$name.json" "$OUT/core_$name.json"
    checked=$((checked + 1))
done
# The pattern matcher against Python's re on random patterns and texts.
python3 schema/agree_patterns.py "$OUT/schema_agree" "${SCHEMA_PATTERN_ROUNDS:-20000}"
# The keyed merge rule against stdschema.merge_patch, per rule and on random bases and patches.
python3 schema/agree_merge.py "$OUT/schema_agree" "${SCHEMA_MERGE_ROUNDS:-5000}"
if [[ $checked -eq 0 ]]; then
    echo "schema: no registries folder to check" >&2
    exit 1
fi
