#!/usr/bin/env bash
# The test agent in a fake game, on the test run contract (docs/design/std-2/test-suite.md "The test
# run"): with SWOFTY_TEST_RUN naming a run folder and SWOFTY_TEST_TOKEN equal to its run.json's token it
# arms, answers the commands in <run>/agent/commands.jsonl through the flow tick, reports unbound
# commands as unavailable and refuses career load, advance and save now outside the save sandbox; with
# another token the game quits before the title; a retired run.json stops it; without the variables it
# does nothing.
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
DIST=${DIST:-dist}
OUT=${1:-$PWD/../build/wine-testagent}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
rm -rf "$OUT/game"
mkdir -p "$OUT/game"
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -o "$OUT/game/F1_25.exe" fake_testagent.c -lversion -luser32
cp -r "../$DIST/." "$OUT/game/"
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
LOG=$OUT/game/swofty/loader.log
RUN=wt-1
RUNDIR=$OUT/runs/$RUN
BOX=$RUNDIR/agent
RES=$BOX/results.jsonl
TOKEN=0123456789abcdef0123456789abcdef
status=0
check() {
    if grep -qE "$2" "${3:-$LOG}"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
none() {
    if grep -qE "$2" "${3:-$LOG}" 2>/dev/null; then echo "FAIL: $1"; status=1; else echo "ok  : $1"; fi
}
runjson() {
    printf '{"schema":1,"run":"%s",%s"agent":{"v":1,"dlls":[]}}' "$RUN" "${1:+\"token\":\"$1\",}" >"$RUNDIR/run.json"
}
mailbox() {
    rm -rf "$OUT/runs"
    mkdir -p "$BOX"
    runjson "$TOKEN"
    cat >"$BOX/commands.jsonl" <<'EOC'
{"seq":1,"cmd":"hello"}
{"seq":2,"cmd":"route","args":{"route":"career"}}
{"seq":3,"cmd":"read","args":{"path":"agent.ticks"}}
{"seq":4,"cmd":"read","args":{"path":"car[0].chassis.mass"}}
{"seq":5,"cmd":"set","args":{"path":"car[player].engine.max_torque","value":500}}
{"seq":6,"cmd":"wait","args":{"event":"race.start"},"timeout_ms":300}
{"seq":7,"cmd":"skip","args":{"to":"session"}}
{"seq":8,"cmd":"advance","args":{"days":7}}
{"seq":9,"cmd":"save","args":{"what":"now"}}
{"seq":10,"cmd":"pad","args":{"press":"a","ms":40}}
{"seq":11,"cmd":"bogus"}
{"seq":12,"cmd":"career","args":{"op":"load","slot":"newest","at":"team_hq"}}
{"seq":13,"cmd":"title"}
EOC
}
run() {
    rm -f "$LOG"
    local code=0
    (cd "$OUT/game" && T_SECONDS=${T_SECONDS:-8} timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe") || code=$?
    echo "$code"
}
for mode in armed refused retired off; do
    unset SWOFTY_TEST_RUN SWOFTY_TEST_TOKEN
    echo "==== $mode"
    mailbox
    case $mode in
    armed | retired) export SWOFTY_TEST_RUN=$RUNDIR SWOFTY_TEST_TOKEN=$TOKEN ;;
    refused) export SWOFTY_TEST_RUN=$RUNDIR SWOFTY_TEST_TOKEN=fedcba9876543210fedcba9876543210 ;;
    esac
    if [[ $mode == retired ]]; then
        (for _ in $(seq 150); do grep -qs 'agent armed' "$LOG" && break; sleep 0.2; done; sleep 1; runjson "") &
    fi
    code=$(if [[ $mode == retired ]]; then T_SECONDS=14 run; else run; fi | tail -1)
    wait
    case $mode in
    armed)
        [[ $code == 0 ]] && echo "ok  : the game ran to its end" || { echo "FAIL: exit $code"; status=1; }
        check "an agent-only run has no save sandbox" "\[test\] test run $RUN: the test agent's only; no save sandbox"
        check "the agent arms on the matching token" "\[test\] agent armed for run $RUN: 0 test DLL\(s\).*save sandbox off"
        check "the hello line names the run and the sandbox state" '"event":"hello".*"run":"wt-1".*"sandbox":"off"' "$RES"
        check "hello answers" '"seq":1,"status":"ok","value":\{"v":1,' "$RES"
        check "route answers unavailable with the reason" '"seq":2,"status":"unavailable","error":"Vm_Fire_Route, Ne_Make_StrFromCStr or FeHome_Handle_Command did not resolve' "$RES"
        check "read agent.ticks answers from the flow tick" '"seq":3,"status":"ok","value":\{"value":[1-9][0-9]*,"type":"u64"\}' "$RES"
        check "a car read off the KB build is unavailable" '"seq":4,"status":"unavailable","error":"the car field offsets' "$RES"
        check "a car set off the KB build is unavailable" '"seq":5,"status":"unavailable","error":"the car field offsets' "$RES"
        check "a wait times out" '"seq":6,"status":"timeout"' "$RES"
        check "skip off the KB build is unavailable" '"seq":7,"status":"unavailable","error":"MessageSetAdvanceDestination' "$RES"
        check "advance outside the sandbox is refused" '"seq":8,"status":"error","error":"advance runs only in the save sandbox' "$RES"
        check "save now outside the sandbox is refused" '"seq":9,"status":"error","error":"save now runs only in the save sandbox' "$RES"
        check "the pad answers" '"seq":10,"status":"(ok|unavailable)"' "$RES"
        check "an unknown command is an error" '"seq":11,"status":"error","error":"unknown command' "$RES"
        check "career load outside the sandbox is refused" '"seq":12,"status":"error","error":"career load runs only in the save sandbox' "$RES"
        check "title off the KB build is unavailable, so the CLI falls back to the pad" '"seq":13,"status":"unavailable","error":"StateIntro.s state layout' "$RES"
        n=$(grep -c '"seq":' "$RES" || true)
        [[ $n == 13 ]] && echo "ok  : every command answered once" || { echo "FAIL: $n answers, want 13"; status=1; }
        ;;
    refused)
        [[ $code == 91 ]] && echo "ok  : another token ends the game (91)" || { echo "FAIL: exit $code, want 91"; status=1; }
        check "another token fails closed" "TEST SANDBOX FAILED: the token in SWOFTY_TEST_TOKEN is not this run's"
        none "nothing arms" '\[test\] agent armed'
        none "nothing is answered" '"seq":' "$RES"
        ;;
    retired)
        check "the agent arms" "\[test\] agent armed for run $RUN"
        check "a retired run.json stops the agent" "\[test\] the run's token is gone from run.json: the agent stops"
        ;;
    off)
        none "no test agent line without SWOFTY_TEST_RUN" '\[test\]'
        none "no results without SWOFTY_TEST_RUN" '.' "$RES"
        ;;
    esac
done
exit $status
