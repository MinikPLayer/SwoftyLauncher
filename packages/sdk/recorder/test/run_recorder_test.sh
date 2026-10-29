#!/usr/bin/env bash
# Builds the recorder and five test addons, runs the recorder on each under Wine (directly on Windows),
# and compares t_register's recording with expected/t_register.json. Skips when Wine isn't available.
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*) WINE=() ;;
*)
    if ! command -v wine >/dev/null || ! command -v zig >/dev/null; then
        if command -v nix >/dev/null && [[ -z ${SWOFTY_RECORDER_TEST_NIX:-} ]]; then
            SWOFTY_RECORDER_TEST_NIX=1 exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable -c "$SELF" "$@"
        fi
        if ! command -v wine >/dev/null; then
            echo "SKIP: recorder test: wine isn't installed (and nix isn't there to fetch it)"
            exit 0
        fi
    fi
    WINE=(wine)
    ;;
esac
OUT=${1:-$PWD/../../../../native/build/recorder-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
command -v taskset >/dev/null || PIN=()
mkdir -p "$OUT"
export ZIG_GLOBAL_CACHE_DIR=$PWD/../../../../native/build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../../../../native/build/zig-cache
bash ../build.sh "$OUT/bin" >/dev/null
pids=()
for t in t_register t_crash t_unexpected t_plain t_hang_edit; do
    "${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Werror -Wno-nullability-completeness \
        -I../../include -shared -s -o "$OUT/$t.dll" "$t.cpp" &
    pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
rm -f "$OUT"/*.lib
if [[ ${#WINE[@]} -gt 0 ]]; then
    export WINEPREFIX="$OUT/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d"
    [[ -d $WINEPREFIX ]] || wineboot -i >/dev/null 2>&1 || true
fi
winpath() { if [[ ${#WINE[@]} -gt 0 ]]; then echo "Z:${1//\//\\}"; else echo "$1"; fi; }
record() {
    local t=$1 code=0
    shift
    timeout 120 "${WINE[@]}" "$OUT/bin/swofty_record.exe" "$(winpath "$OUT/$t.dll")" "$(winpath "$OUT/$t.json")" \
        --manifest "$(winpath "$PWD/manifest.json")" "$@" >"$OUT/$t.out" 2>&1 || code=$?
    echo "$code"
}
status=0
expect() {
    local t=$1 want=$2 got=$3
    if [[ $got != "$want" ]]; then
        echo "FAIL recorder: $t exited $got, expected $want"
        cat "$OUT/$t.out"
        status=1
    fi
}
expect t_register 0 "$(record t_register)"
expect t_crash 3 "$(record t_crash)"
expect t_unexpected 6 "$(record t_unexpected)"
expect t_plain 0 "$(record t_plain)"
expect t_hang_edit 5 "$(record t_hang_edit --timeout 3)"
python3 - "$OUT" expected <<'PY' || status=1
import json, sys
out, exp = sys.argv[1], sys.argv[2]
def load(p):
    return json.load(open(p))
bad = 0
want = load(f'{exp}/t_register.json')
got = load(f'{out}/t_register.json')
got['registries'] = {k: v.get('x-registry') for k, v in got['registries'].items()}
if got != want:
    print('FAIL recorder: t_register recording differs from expected/t_register.json')
    print(json.dumps(got, indent=2))
    bad = 1
crash = load(f'{out}/t_crash.json')
ids = [r['id'] for r in crash['records']]
if crash['status'] != 'crashed' or crash['error'].get('code') != '0xc0000005' or crash['error'].get('module') != 't_crash.dll' \
        or ids != ['t.recorder/before_crash'] or crash['error'].get('access') != 'write':
    print('FAIL recorder: t_crash', json.dumps(crash, indent=2))
    bad = 1
un = load(f'{out}/t_unexpected.json')
if un['status'] != 'unexpected' or un['unexpected'] != ['api.scan'] or [r['id'] for r in un['records']] != ['t.recorder/one']:
    print('FAIL recorder: t_unexpected', json.dumps(un, indent=2))
    bad = 1
hang = load(f'{out}/t_hang_edit.json')
if hang['status'] != 'timeout' or 'variant edit of career.mode' not in hang['error'].get('in', ''):
    print('FAIL recorder: t_hang_edit', json.dumps(hang, indent=2))
    bad = 1
plain = load(f'{out}/t_plain.json')
if plain['status'] != 'no_register' or plain['records']:
    print('FAIL recorder: t_plain', json.dumps(plain, indent=2))
    bad = 1
sys.exit(bad)
PY
[[ $status == 0 ]] && echo "recorder test: 5 addons recorded as expected"
exit $status
