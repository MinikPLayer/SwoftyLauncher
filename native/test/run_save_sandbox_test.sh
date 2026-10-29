#!/usr/bin/env bash
# The test save sandbox: the host unit test of save_sandbox_logic.hpp (run token, device checks, the
# folder that stands in for Steam remote storage), then fake_save_sandbox.c under Wine with the built
# core: with a run token the fake game's Steam storage calls land in the sandbox and nothing reaches its
# "Steam"; every doubt (a wrong token, a missing binding, a local-save device, no Steam helper) ends the
# game before it starts its save device; without a token nothing changes.
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
OUT=${1:-$PWD/../build/save-sandbox-test}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
rm -rf "$OUT/game" "$OUT/runs" "$OUT/steam"
mkdir -p "$OUT/game" "$OUT/runs" "$OUT/steam"
status=0
"${PIN[@]}" zig c++ -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I../../packages/sdk/include \
    -o "$OUT/save_sandbox_test" save_sandbox_test.cpp
"$OUT/save_sandbox_test" "$OUT/host-folder" || status=1
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -I../../packages/sdk/include -o "$OUT/save_sandbox_test.exe" save_sandbox_test.cpp
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -o "$OUT/game/F1_25.exe" fake_save_sandbox.c -lversion -luser32
cp -r ../dist/. "$OUT/game/"
python3 - "$OUT/game/swofty/addons/core/bindings.json" <<'EOB'
import json, sys
tag = lambda t: '48 B8 53 57 46 54 ' + ' '.join(f'{ord(c):02X}' for c in t)
func = lambda i, t: {'id': i, 'kind': 'func', 'status': 'experimental', 'section': '.xtls', 'pattern': tag(t),
                     'anchor': 0, 'resolve': {'kind': 'addr'}}
mine = [func('Ne_SaveSystem2Win_Start', 'SVST'), func('Ne_SaveSystem2Win_Mount', 'SVMT'),
        func('Ne_SaveSystem2Win_WriteInfoXml', 'SVUP')]
doc = json.load(open(sys.argv[1]))
ids = {b['id'] for b in mine}
doc['bindings'] = [b for b in doc['bindings'] if b['id'] not in ids] + mine
json.dump(doc, open(sys.argv[1], 'w'), indent=1)
EOB
cp "$OUT/game/swofty/addons/core/bindings.json" "$OUT/bindings.full.json"
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
timeout 300 wineboot -i >/dev/null 2>&1 || true
echo "==== the folder logic built for Windows, under Wine"
timeout 120 "${PIN[@]}" wine "$OUT/save_sandbox_test.exe" 'Z:'"${OUT//\//\\}"'\win-folder' 2>/dev/null | tail -1 | tee "$OUT/win-host.txt"
grep -q "^ALL PASSED" "$OUT/win-host.txt" || status=1
LOG=$OUT/game/swofty/loader.log
TOKEN=0123456789abcdef0123456789abcdef
check() {
    if grep -qE "$2" "$LOG"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
none() {
    if grep -qE "$2" "$LOG"; then echo "FAIL: $1"; status=1; else echo "ok  : $1"; fi
}
run_dir() {
    local d=$OUT/runs/$1
    rm -rf "$d"
    mkdir -p "$d/saves/remote"
    printf '{"schema":1,"run":"%s","token":"%s","saves":"saves","exe_sha256":"fake","agent":{"v":1,"dlls":[]}}\n' "$1" "$TOKEN" >"$d/run.json"
    echo "$d"
}
# play <name> <expected exit> [VAR=value ...]: start the fake game with the given environment.
play() {
    local name=$1 want=$2
    shift 2
    echo "==== $name"
    rm -rf "$OUT/steam/$name"
    mkdir -p "$OUT/steam/$name"
    cp "$OUT/bindings.full.json" "$OUT/game/swofty/addons/core/bindings.json"
    local code=0
    env "$@" T_STEAM_DIR='Z:'"${OUT//\//\\}"'\steam\'"$name" SWOFTY_EARLY_STAGE=1 \
        timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe" >"$OUT/$name.out" 2>&1 || code=$?
    grep -E '^(ok  |FAIL|ALL|SOME)' "$OUT/$name.out" | sed 's/^/      game: /'
    if [[ $code == "$want" ]]; then echo "ok  : $name: the game exited with $want"; else echo "FAIL: $name: exit $code, want $want"; status=1; fi
    cp "$LOG" "$OUT/$name.loader.log" 2>/dev/null || true
}
steam_untouched() {
    if [[ -e $OUT/steam/$1/steam.log ]]; then
        echo "FAIL: $1: the fake Steam was called:"
        cat "$OUT/steam/$1/steam.log"
        status=1
    else
        echo "ok  : $1: nothing reached the fake Steam"
    fi
}

R=$(run_dir armed)
play armed 0 SWOFTY_TEST_RUN="$R" SWOFTY_TEST_TOKEN=$TOKEN T_LINGER_MS=2500
check "armed: the run is recognised" '\[saves\] test run armed: the save sandbox is required'
check "armed: the three wraps install" '\[saves\] test sandbox hooked: Ne_SaveSystem2Win_Start, Ne_SaveSystem2Win_Mount and Ne_SaveSystem2Win_WriteInfoXml are wrapped'
check "armed: the sandbox arms at the device's start" '\[saves\] test sandbox armed \(run armed\): .* saves are read and written in [A-Z]:\\.*\\runs\\armed\\saves\\remote; nothing reaches Steam'
check "armed: the writes are logged" '\[saves\] sandbox: FileWrite savegame@profile#QKRHMYXE \(416 bytes\) -> written'
check "armed: an unserved slot is logged" '\[saves\] sandbox: FileShare is not served by the sandbox'
check "armed: the corrupt-save upload is off" "\[saves\] sandbox: the game's corrupt-save upload \(#1\) is off in a test run"
none "armed: no failure" 'TEST SANDBOX FAILED'
check "armed: the same run arms the test agent, its mailbox in the run folder" "\[test\] test run armed: arming the test agent \(mailbox [A-Z]:.*\\\\runs\\\\armed\\\\agent\)"
check "armed: the agent knows the sandbox is required" '\[test\] agent armed for run armed: .*save sandbox required'
if grep -qE '"event":"hello".*"run":"armed".*"sandbox":"(armed|required)"' "$R/agent/results.jsonl" 2>/dev/null; then
    echo "ok  : armed: the agent's hello in <run>/agent/results.jsonl names the run and the sandbox"
else
    echo "FAIL: armed: no hello with the sandbox in $R/agent/results.jsonl"; status=1
fi
steam_untouched armed
python3 - "$R" <<'EOF' || status=1
import hashlib, json, os, sys
r = sys.argv[1]
names = sorted(os.listdir(os.path.join(r, 'saves/remote')))
want = ['savegame@profile#NXDSMWW.BWW', 'savegame@profile#QKRHMYXE']
data = bytes((i * 2654435761 >> 13) & 0xff for i in range(65536))
index = bytes((i * 40503 >> 3) & 0xff for i in range(416))
got = [open(os.path.join(r, 'saves/remote', n), 'rb').read() for n in want]
status = json.load(open(os.path.join(r, 'sandbox.json')))
ok = names == want and got == [data, index] and status['state'] == 'armed'
print(('ok  ' if ok else 'FAIL') + f': armed: the sandbox holds exactly the saved files, byte for byte ({names}), status {status["state"]}')
sys.exit(0 if ok else 1)
EOF

R=$(run_dir wrongtoken)
play wrongtoken 91 SWOFTY_TEST_RUN="$R" SWOFTY_TEST_TOKEN=ffffffffffffffffffffffffffffffff
check "wrong token: the game quits before the title screen" "\[saves\] TEST SANDBOX FAILED: the token in SWOFTY_TEST_TOKEN is not this run's"
grep -q "the game's own start ran" "$OUT/wrongtoken.out" && { echo "FAIL: wrong token: the device started"; status=1; } || echo "ok  : wrong token: the device never started"
steam_untouched wrongtoken

play tokenonly 91 SWOFTY_TEST_TOKEN=$TOKEN
check "a token without a run folder fails closed" "TEST SANDBOX FAILED: SWOFTY_TEST_TOKEN is set without SWOFTY_TEST_RUN"

R=$(run_dir nobinding)
echo "==== nobinding"
python3 - "$OUT/bindings.full.json" "$OUT/bindings.nomount.json" <<'EOF'
import json, sys
doc = json.load(open(sys.argv[1]))
doc['bindings'] = [b for b in doc['bindings'] if b['id'] != 'Ne_SaveSystem2Win_Mount']
json.dump(doc, open(sys.argv[2], 'w'))
EOF
code=0
cp "$OUT/bindings.nomount.json" "$OUT/game/swofty/addons/core/bindings.json"
SWOFTY_TEST_RUN="$R" SWOFTY_TEST_TOKEN=$TOKEN SWOFTY_EARLY_STAGE=1 T_STEAM_DIR='Z:'"${OUT//\//\\}"'\steam\nobinding' \
    timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe" >"$OUT/nobinding.out" 2>&1 || code=$?
[[ $code == 91 ]] && echo "ok  : nobinding: the game exited with 91" || { echo "FAIL: nobinding: exit $code"; status=1; }
check "a missing binding fails closed" 'TEST SANDBOX FAILED: Ne_SaveSystem2Win_Mount does not resolve on this build'
python3 -c "import json,sys; s=json.load(open(sys.argv[1])); sys.exit(s['state']!='failed')" "$R/sandbox.json" &&
    echo "ok  : nobinding: sandbox.json says failed" || { echo "FAIL: nobinding: sandbox.json"; status=1; }

R=$(run_dir localsave)
play localsave 91 SWOFTY_TEST_RUN="$R" SWOFTY_TEST_TOKEN=$TOKEN T_LOCAL=1
check "a device with m_storeInCloud off fails closed at its start" "TEST SANDBOX FAILED: the save device's m_storeInCloud \(\+0x3e8\) is off"
steam_untouched localsave

R=$(run_dir nohelper)
play nohelper 91 SWOFTY_TEST_RUN="$R" SWOFTY_TEST_TOKEN=$TOKEN T_NO_HELPER=1
check "a device without a Steam helper fails closed at its start" 'TEST SANDBOX FAILED: the save device has no Steam helper'

play normal 0 T_EXPECT_NORMAL=1
none "no test run: nothing of the sandbox runs" '\[saves\] (test |TEST |sandbox)'

[[ $status == 0 ]] && echo "save sandbox test: ALL PASSED" || echo "save sandbox test: FAILED"
exit $status
