#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
DIST=${DIST:-dist}
OUT=${1:-$PWD/../build/wine-net-trace}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
rm -rf "$OUT/game"
mkdir -p "$OUT/game"
zig dlltool -d fake_net_ws2.def -l "$OUT/libfake_ws2.a" -m i386:x86-64
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -o "$OUT/game/F1_25.exe" fake_net.c -lversion -luser32 -lws2_32 \
    "$OUT/libfake_ws2.a"
# The game imports ws2_32 through two descriptors, WS2_32.dll and ws2_32.dll. lld merges names that differ only
# in case, so the second one links under a placeholder and is renamed here.
python3 - "$OUT/game/F1_25.exe" <<'EOF'
import sys
b = open(sys.argv[1], 'rb').read()
at = b.find(b'ws2_32.dlx\0')
assert at > 0 and b.find(b'ws2_32.dlx\0', at + 1) < 0, 'placeholder import name'
open(sys.argv[1], 'wb').write(b[:at] + b'ws2_32.dll' + b[at + 10:])
EOF
cp -r "../$DIST/." "$OUT/game/"
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
status=0
LOG=$OUT/game/swofty/net-trace.log
check() {
    if grep -qE "$2" "$LOG"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
count() {
    local n
    n=$(grep -cE "$3" "$LOG" || true)
    if [[ $n == "$2" ]]; then echo "ok  : $1"; else echo "FAIL: $1 (logged $n times, want $2)"; status=1; fi
}
exact() {
    local n
    n=$(grep -cE " $1 #[0-9]+ " "$LOG" || true)
    if [[ $n == "$2" ]]; then echo "ok  : $1 logged $2 time(s)"; else echo "FAIL: $1 logged $n time(s), want $2"; status=1; fi
}
run() {
    rm -f "$LOG" "$OUT/game/ports.txt"
    (cd "$OUT/game" && timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe") || status=1
}
for mode in off 1 inline cap; do
    echo "==== SWOFTY_NET_TRACE=$mode"
    unset SWOFTY_NET_TRACE SWOFTY_NET_TRACE_CAP_KB
    if [[ $mode == off ]]; then
        run
        [[ ! -e $LOG ]] && echo "ok  : no net-trace.log without the variable" || { echo "FAIL: net-trace.log written while off"; status=1; }
        continue
    fi
    if [[ $mode == cap ]]; then
        export SWOFTY_NET_TRACE=1 SWOFTY_NET_TRACE_CAP_KB=8
        run
        size=$(stat -c %s "$LOG" 2>/dev/null || echo 0)
        [[ $(tail -1 "$LOG" 2>/dev/null) == "# log cap of 8192 bytes reached; trace stopped" ]] &&
            echo "ok  : the capped log ends with the cap line" || { echo "FAIL: no cap line at the end"; status=1; }
        ((size > 4096 && size <= 8192 + 64)) && echo "ok  : the capped log is $size bytes" ||
            { echo "FAIL: the capped log is $size bytes"; status=1; }
        continue
    fi
    export SWOFTY_NET_TRACE=$mode
    run
    if [[ ! -s $LOG ]]; then
        echo "FAIL: no net-trace.log"
        status=1
        continue
    fi
    read -r RPORT SPORT <"$OUT/game/ports.txt" || true
    WAITS=1
    if [[ $mode == 1 ]]; then
        check "header names iat mode and 16 data bytes" '^# swofty net trace: .* mode iat, first 16 data bytes'
        check "the fake's WSARecvFrom import slot is hooked by name" '^# iat table [12] slot exe\+0x[0-9a-f]+ \(name\) -> WSARecvFrom$'
        check "sendto is hooked by ordinal in one table" '^# iat table [12] slot exe\+0x[0-9a-f]+ \(ordinal 20\) -> sendto$'
        check "bind is hooked by ordinal in one table" '^# iat table [12] slot exe\+0x[0-9a-f]+ \(ordinal 2\) -> bind$'
        check "sendto is hooked by name in the other table" '^# iat table [12] slot exe\+0x[0-9a-f]+ \(name\) -> sendto$'
        check "WSASendTo is not imported, so it is hooked inline" '^# inline hook on ws2_32!WSASendTo'
        check "WSAEventSelect is not imported, so it is hooked inline" '^# inline hook on ws2_32!WSAEventSelect'
        check "every call is hooked" '^# ready: .*not hooked: none$'
    else
        check "header names inline mode" '^# swofty net trace: .* mode inline'
        check "WSARecvFrom is hooked inline" '^# inline hook on ws2_32!WSARecvFrom'
        count "no IAT slot is touched in inline mode" 0 '^# iat table'
        if grep -q '^# ws2_32!WSAWaitForMultipleEvents forwards to .*; not hooked inline$' "$LOG"; then
            echo "ok  : the forwarded WSAWaitForMultipleEvents is not hooked inline"
            check "the ready line names it as not hooked" '^# ready: .*not hooked: WSAWaitForMultipleEvents$'
            WAITS=0
        else
            check "every call is hooked" '^# ready: .*not hooked: none$'
        fi
    fi
    exact socket 6
    exact WSASocketW 1
    exact bind 4
    exact connect 1
    exact setsockopt 1
    exact ioctlsocket 1
    exact getsockname 2
    exact sendto 204
    exact send 1
    exact WSASendTo 1
    exact WSARecvFrom 1
    exact WSARecv 0
    exact WSAIoctl 0
    exact WSAGetOverlappedResult 1
    exact WSAWaitForMultipleEvents $WAITS
    exact WSAEventSelect 1
    exact WSACreateEvent 2
    exact closesocket 8
    count "recvfrom logged each of its two datagrams once" 2 ' recvfrom #[0-9]+ .* ret=16 '
    count "recv logged its datagram once" 1 ' recv #[0-9]+ .* ret=16 '
    check "WSASocketW logged with family, type and overlapped flag" ' WSASocketW #1 sock=0x[0-9a-f]+ af=2 type=2 proto=17 .*af=2 type=2 proto=17 flags=0x1 ret=0 err=0 from=exe\+0x'
    check "socket logged" ' socket #1 sock=0x[0-9a-f]+ af=2 type=2 proto=17 '
    check "setsockopt logged with the value bytes" ' setsockopt #1 .* level=0xffff name=0x1002 len=4 val=00000100 ret=0'
    check "ioctlsocket FIONBIO logged" ' ioctlsocket #1 .* cmd=0x8004667e arg=1 ret=0'
    check "bind logged with the address" " bind #1 .* addr=127\.0\.0\.1:0 len=16 ret=0 "
    check "the failed bind keeps its error" " bind #1 .* addr=127\.0\.0\.1:$RPORT len=16 ret=-1 err=10048 "
    check "getsockname logged with the bound port" " getsockname #1 sock=.* addr=127\.0\.0\.1:$RPORT ret=0"
    check "WSACreateEvent logged" ' WSACreateEvent #1 .* event=0x[0-9a-f]+ ret=0 '
    check "the overlapped WSARecvFrom logged pending with its buffers" " WSARecvFrom #1 sock=.* local=127\.0\.0\.1:$RPORT .* cap=1264 from_buf=0x[0-9a-f]+ fromlen=16 .* ov=0x[0-9a-f]+ ev=0x[0-9a-f]+ .* ret=-1 err=997 "
    check "sendto logged with the destination and data" " sendto #1 sock=.* to=127\.0\.0\.1:$RPORT len=16 flags=0x0 data=$(printf swofty-net-trace | xxd -p) ret=16 err=0 from=exe\+0x"
    check "the ordinal-table sendto is logged" " sendto #1 sock=.* to=127\.0\.0\.1:$RPORT len=16 flags=0x0 data=$(printf table-two-sendto | xxd -p) ret=16 err=0 from=exe\+0x"
    if [[ $WAITS == 1 ]]; then
        check "WSAWaitForMultipleEvents logged the signalled event" ' WSAWaitForMultipleEvents #1 .* n=1 events=0x[0-9a-f]+ all=0 timeout=5000 signalled=0x[0-9a-f]+ ret=0 '
    fi
    check "WSAGetOverlappedResult logged bytes, data and the from address" " WSAGetOverlappedResult #1 .* bytes=16 flags=0x0 buf=0x[0-9a-f]+ data=$(printf swofty-net-trace | xxd -p) from_buf=0x[0-9a-f]+ from=127\.0\.0\.1:$SPORT ret=1 err=0 "
    check "WSASendTo logged" " WSASendTo #1 .* to=127\.0\.0\.1:$RPORT nbufs=1 len0=16 sent=16 data=$(printf via-WSASendTo!!! | xxd -p) .* ret=0 "
    check "recvfrom logged with the from address" " recvfrom #[0-9]+ .* cap=64 flags=0x0 from=127\.0\.0\.1:$SPORT data=$(printf via-WSASendTo!!! | xxd -p) ret=16 "
    check "connect logged" " connect #1 .* addr=127\.0\.0\.1:$RPORT len=16 ret=0 "
    check "send logged with the connected peer" " send #1 sock=.* peer=127\.0\.0\.1:$RPORT len=16 flags=0x0 data=$(printf connected-send!! | xxd -p) ret=16 "
    check "recv logged" " recv #[0-9]+ .* data=$(printf connected-send!! | xxd -p) ret=16 "
    check "WSAEventSelect logged" ' WSAEventSelect #1 .* event=0x[0-9a-f]+ mask=0x1 ret=0 '
    check "closesocket logged the socket it closed" " closesocket #1 sock=0x[0-9a-f]+ af=2 type=2 proto=17 local=127\.0\.0\.1:$RPORT peer=unset closed ret=0 "
    check "the failed closesocket keeps WSAENOTSOCK" ' closesocket #[0-9]+ .* ret=-1 err=10038 '
    count "450 sendto calls on one socket log the first 200, then #300 and #400" 202 ' sendto #[0-9]+ sock=.* data=72617465 '
    check "the 300th call carries its count" ' sendto #300 .* data=72617465 '
done
[[ $status == 0 ]] && echo "net trace test: ALL PASSED" || { echo "net trace test: FAILED"; tail -40 "$LOG" 2>/dev/null || true; }
exit $status
