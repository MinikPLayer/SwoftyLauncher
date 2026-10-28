#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
DIST=${DIST:-dist}
OUT=${1:-$PWD/../build/wine-netplay}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
rm -rf "$OUT/game"
mkdir -p "$OUT/game"
zig dlltool -d fake_net_ws2.def -l "$OUT/libfake_ws2.a" -m i386:x86-64
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -o "$OUT/game/F1_25.exe" fake_netplay.c -lversion -luser32 -lws2_32 \
    -liphlpapi "$OUT/libfake_ws2.a"
# Same second ws2_32 descriptor as the socket trace test: linked under a placeholder name, renamed here.
python3 - "$OUT/game/F1_25.exe" <<'EOF'
import sys
b = open(sys.argv[1], 'rb').read()
at = b.find(b'ws2_32.dlx\0')
assert at > 0 and b.find(b'ws2_32.dlx\0', at + 1) < 0, 'placeholder import name'
open(sys.argv[1], 'wb').write(b[:at] + b'ws2_32.dll' + b[at + 10:])
EOF
cp -r "../$DIST/." "$OUT/game/"
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
BASE=$((40000 + RANDOM % 20000))
LOG=$OUT/game/swofty/loader.log
TRACE=$OUT/game/swofty/net-trace.log
STATUS=$OUT/game/swofty/netplay.json
status=0
check() {
    if grep -qE "$2" "${3:-$LOG}"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
none() {
    if grep -qE "$2" "${3:-$LOG}"; then echo "FAIL: $1"; status=1; else echo "ok  : $1"; fi
}
run() {
    rm -f "$LOG" "$TRACE" "$STATUS"
    (cd "$OUT/game" && timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe") || status=1
}
for mode in off armed invalid host traced; do
    unset SWOFTY_NETPLAY SWOFTY_NET_TRACE
    export FAKE_NETPLAY_SELF=1 FAKE_NETPLAY_BASE=$BASE FAKE_NETPLAY_HOOKS=1 FAKE_NETPLAY_ROOM=0
    case $mode in
    off) export FAKE_NETPLAY_HOOKS=0 ;;
    armed) export SWOFTY_NETPLAY=armed ;;
    invalid) export SWOFTY_NETPLAY="self=1;base=$BASE;port=21414" ;;
    host) export SWOFTY_NETPLAY="self=1;base=$BASE" FAKE_NETPLAY_ROOM=1 ;;
    traced) export SWOFTY_NETPLAY="base=$BASE;self=7" FAKE_NETPLAY_SELF=7 FAKE_NETPLAY_ROOM=1 SWOFTY_NET_TRACE=1 ;;
    esac
    echo "==== $mode${SWOFTY_NETPLAY:+ SWOFTY_NETPLAY=$SWOFTY_NETPLAY}${SWOFTY_NET_TRACE:+ SWOFTY_NET_TRACE=1}"
    run
    [[ ! -e $STATUS ]] && echo "ok  : no netplay.json after the process exits" || { echo "FAIL: netplay.json left behind"; status=1; }
    slots='sendto exe\+0x[0-9a-f]+,exe\+0x[0-9a-f]+; recvfrom exe\+0x[0-9a-f]+; bind exe\+0x[0-9a-f]+,exe\+0x[0-9a-f]+; GetAdaptersAddresses exe\+0x[0-9a-f]+'
    case $mode in
    off)
        none "no bridge line without the variable" '\[netplay\] (bridge|SWOFTY_NETPLAY|room|own address|first datagram)'
        ;;
    armed | invalid)
        if [[ $mode == invalid ]]; then
            check "the invalid value is logged" "\[netplay\] SWOFTY_NETPLAY=\"self=1;base=$BASE;port=21414\" is not self=<1-254>;base=<port>; armed without a room"
        else
            none "no invalid-value line for a plain value" '\[netplay\] SWOFTY_NETPLAY='
        fi
        check "armed on every slot without a room" "\[netplay\] bridge armed: $slots; no room yet[[:cntrl:]]?$"
        check "the game socket's bind writes the status without a room" "\[netplay\] bridge on, self=0 base=0 game_port=21414[[:cntrl:]]?$"
        none "nothing is rewritten" '\[netplay\] (own address|first datagram)'
        ;;
    host | traced)
        n=$FAKE_NETPLAY_SELF
        check "armed on both sendto slots, recvfrom, both bind slots and GetAdaptersAddresses, in the given room" \
            "\[netplay\] bridge armed: $slots; room self=$n base=$BASE from SWOFTY_NETPLAY[[:cntrl:]]?$"
        none "nothing is missing" '\[netplay\] bridge incomplete'
        check "the game socket's bind turns the bridge on" "\[netplay\] bridge on, self=$n base=$BASE game_port=21414[[:cntrl:]]?$"
        check "the first datagram to member 2 is logged" "\[netplay\] first datagram to member 2: 10\.77\.0\.2:21414 sent to 127\.0\.0\.1:$((BASE + 2))[[:cntrl:]]?$"
        check "the first datagram from member 2 is logged" "\[netplay\] first datagram from member 2: 127\.0\.0\.1:$((BASE + 2)) received as 10\.77\.0\.2:21414[[:cntrl:]]?$"
        check "the own address rewrite is logged" "\[netplay\] own address: adapter 13 192\.168\.50\.13 reported as 10\.77\.0\.$n[[:cntrl:]]?$"
        ;;
    esac
    if [[ $mode == traced ]]; then
        check "the trace sits below the bridge: it logs the loopback destination" \
            " sendto #[0-9]+ .* to=127\.0\.0\.1:$((BASE + 2)) len=16 flags=0x0 data=$(printf hello-member-two | xxd -p) ret=16 " "$TRACE"
        check "and the loopback source of the reply" \
            " recvfrom #[0-9]+ .* local=127\.0\.0\.1:21414 .* from=127\.0\.0\.1:$((BASE + 2)) data=$(printf reply-from-two!! | xxd -p) ret=16 " "$TRACE"
    fi
done
[[ $status == 0 ]] && echo "netplay test: ALL PASSED" || { echo "netplay test: FAILED"; grep '\[netplay\]' "$LOG" 2>/dev/null | tail -20 || true; }
exit $status
