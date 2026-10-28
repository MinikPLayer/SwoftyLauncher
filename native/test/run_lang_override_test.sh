#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
DIST=${DIST:-dist}
OUT=${1:-$PWD/../build/wine-lang-override}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
rm -rf "$OUT/game"
mkdir -p "$OUT/game"
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -o "$OUT/game/F1_25.exe" fake_lang.c -lversion -luser32
cp -r "../$DIST/." "$OUT/game/"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -I../../packages/sdk/include -shared -s \
    -o "$OUT/game/swofty/addons/t_lang.dll" lang/t_lang.cpp
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
status=0
timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe" || status=1
LOG=$OUT/game/swofty/loader.log
check() {
    if grep -qE "$2" "$LOG"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
check "core found the lookup and hooked the module init" '\[lang\] override: ready, hooked on first use \(find found, language table found, module hook on\)'
check "the first override hooked Loc_LangSet_Find" '\[lang\] override: Loc_LangSet_Find hooked at'
check "the module init was seen" '\[lang\] locale module .* initialised \(0\); current language eng'
check "refresh reached one listener" '\[lang\] override: refresh sent language 0 to 1 listener'
[[ $status == 0 ]] && echo "lang override test: ALL PASSED" || { echo "lang override test: FAILED"; tail -40 "$LOG"; }
exit $status
