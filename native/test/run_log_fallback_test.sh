#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
OUT=${1:-$PWD/../build/wine-log-fallback}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
GAME=$OUT/game
[[ -d $GAME/swofty ]] && chmod u+w "$GAME/swofty"
rm -rf "$GAME"
mkdir -p "$GAME/swofty"
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -o "$GAME/F1_25.exe" fake_log.c -lversion
cp ../dist/version.dll "$GAME/"
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
LOCAL=$(winepath -u "$(wine cmd /c 'echo %LOCALAPPDATA%' | tr -d '\r')")
FALLBACK=$LOCAL/Swofty/state/loader-fallback.log
rm -f "$FALLBACK" "${FALLBACK%.log}.prev.log"
status=0
check() {
    if grep -qE "$3" "$2"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
absent() {
    if [[ -e $2 ]]; then echo "FAIL: $1"; status=1; else echo "ok  : $1"; fi
}
run() { timeout 60 "${PIN[@]}" env "$@" xvfb-run -a wine "$GAME/F1_25.exe" || { echo "FAIL: the fake game did not exit cleanly"; status=1; }; }

echo "==== the game folder takes the log"
run
check "the loader logs in the game folder" "$GAME/swofty/loader.log" '\(version\.dll proxy\) attached'
absent "no fallback log while the game folder takes the log" "$FALLBACK"

echo "==== the game folder refuses the log"
rm "$GAME/swofty/loader.log"
chmod a-w "$GAME/swofty"
if [[ -w $GAME/swofty ]]; then
    echo "skip: this user writes in read-only folders (root?), so the refusal cannot be staged"
    chmod u+w "$GAME/swofty"
    exit $status
fi
run __COMPAT_LAYER="DetectorsAdminProtection Installer"
chmod u+w "$GAME/swofty"
absent "nothing reached the game folder" "$GAME/swofty/loader.log"
check "the loader attached in the fallback log" "$FALLBACK" '\(version\.dll proxy\) attached'
check "the fallback log names the refused log and the error" "$FALLBACK" \
    'log: .*\\swofty\\loader\.log could not be opened \(error 5\); this is the fallback log .*\\Swofty\\state\\loader-fallback\.log'
check "the fallback log has the game's rights" "$FALLBACK" \
    'process: user .* \(S-1-[0-9-]+\), [a-z]+ integrity, (elevated|not elevated[^,]*), UAC virtualization (on|off), __COMPAT_LAYER=DetectorsAdminProtection Installer[[:space:]]*$'
check "the loader goes on after the fallback" "$FALLBACK" 'forwards: [0-9]+ of 17 resolved'

[[ $status == 0 ]] && echo "log fallback test: ALL PASSED" || { echo "log fallback test: FAILED"; tail -20 "$FALLBACK" 2>/dev/null; }
exit $status
