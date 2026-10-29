#!/usr/bin/env bash
# Builds src/core/hooks/guard.cpp exactly as native/build.sh does and runs each guard_test.cpp case in its
# own Wine process. Install: guard_test.cpp -> native/test/hooks/, this script -> native/test/.
set -uo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/guard-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
SDK=../../packages/sdk/include
mkdir -p "$OUT"
set -e
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -fms-extensions \
    -I$SDK -c ../src/core/hooks/guard.cpp -o "$OUT/hooks_guard.o" 2>/dev/null
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -fms-extensions \
    -Wno-missing-designated-field-initializers -I$SDK -o "$OUT/guard_test.exe" hooks/guard_test.cpp "$OUT/hooks_guard.o"
set +e
export WINEPREFIX=${WINEPREFIX:-$OUT/prefix} WINEDEBUG=-all
timeout 300 wineboot -i >/dev/null 2>&1
status=0
for c in fine av_plain av_locals throw_plain throw_locals msvc_code sdk_av sdk_throw sdk_service_throw sdk_blamed blamed; do
    out=$(timeout 60 "${PIN[@]}" wine "$OUT/guard_test.exe" "$c" 2>/dev/null)
    r=$?
    echo "$out"
    if [[ $r -ne 0 ]]; then
        grep -q "^ok  \|^FAIL" <<<"$out" || echo "FAIL: $c: the process died (exit $r) before reporting"
        status=1
    fi
done
echo "guard test: $([[ $status -eq 0 ]] && echo passed || echo FAILED)"
exit $status
