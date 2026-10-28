#!/usr/bin/env bash
# Binding resolver tests: a synthetic packed exe (deferral, decrypted-stage commit, retries, fail closed, the [bind]
# line format) and, when the Steam exe is on disk, every Steam binding resolved through the core over that exe mapped
# read-only as an image. SWOFTY_BASE_DIST names a dist of another core to compare RVAs against, binding by binding.
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable -c "$SELF" "$@"
fi
. ./game_lock.sh
OUT=${1:-$PWD/../build/bind-test}
DIST=${DIST:-$PWD/../dist}
EXE=${SWOFTY_TEST_EXE:-$PWD/../../work/exe/F1_25.exe}
BASE=${SWOFTY_BASE_DIST:-}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
export WINEPREFIX="$OUT/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d"
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
mkdir -p "$OUT"
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -I../../packages/sdk/include -I../include \
    -o "$OUT/bind_harness.exe" bind_harness.cpp
status=0
win() { echo "Z:${1//\//\\}"; }
root() {
    rm -rf "$1"
    mkdir -p "$1/swofty/addons/core"
    cp "$2/swofty/addons/core/swofty_core.dll" "$1/swofty/addons/core/"
}
check_format() {
    python3 - "$1" <<'EOF' || status=1
import re, sys
line = re.compile(r'^\[bind\] id=\S+ build=(steam|ea|unknown) pass=(init|window|stage:\d+) '
                  r'result=(resolved|deferred|unresolved) method=(hint|scan|anchor|anchor-fallback|-) matches=(\d+|-) '
                  r'rva=(0x[0-9a-f]+|-) match=(0x[0-9a-f]+|-) verified=(yes|no|-) reason="((?:[^"\\]|\\.)*)"'
                  r'( site=(0x[0-9a-f]+|-))? site_bytes="((?:[0-9A-F]{2})(?: [0-9A-F]{2}){0,31})?"$')
summary = re.compile(r'^\[bind\] summary build=(steam|ea|unknown) pass=(init|window|stage:\d+) resolved=\d+ deferred=\d+ '
                     r'unresolved=\d+ total=\d+ pe_timestamp=-?\d+ size=-?\d+ sha256=([0-9a-f]{64}|-)$')
bad, n, s = [], 0, 0
for raw in open(sys.argv[1], encoding='utf-8', errors='replace'):
    raw = raw.rstrip('\r\n')
    if not raw.startswith('[bind]'):
        continue
    if raw.startswith('[bind] summary'):
        s += 1
        if not summary.match(raw):
            bad.append(raw)
        continue
    n += 1
    m = line.match(raw)
    if not m or (m.group(3) == 'unresolved') != (m.group(10) is not None) or \
            (m.group(3) == 'resolved') != (m.group(6) != '-') or \
            (m.group(4) == '-') != (m.group(3) != 'resolved') or \
            (m.group(4).startswith('anchor') and m.group(7) != '-'):
        bad.append(raw)
for b in bad[:5]:
    print('      bad line:', b)
print(f"{'ok  ' if not bad and n and s else 'FAIL'}: format: {n} [bind] line(s) and {s} summary line(s) in "
      f"{sys.argv[1].rsplit('/', 1)[-1]} follow the log contract")
sys.exit(1 if bad or not n or not s else 0)
EOF
}
for mode in packed measured plain; do
    echo "==== $mode"
    R=$OUT/$mode
    root "$R" "$DIST"
    "${PIN[@]}" wine "$OUT/bind_harness.exe" "$mode" "$(win "$R")" || status=1
    check_format "$R/bind.log"
done
if [[ -f $EXE ]]; then
    size=$(stat -c %s "$OUT/bind_harness.exe")
    for side in new ${BASE:+base}; do
        dist=$DIST
        [[ $side == base ]] && dist=$BASE
        for mode in hint scan cache; do
            echo "==== steam $side $mode"
            R=$OUT/steam-$side-$mode
            root "$R" "$dist"
            python3 - "$dist/swofty/addons/core/bindings.json" "$R/swofty/addons/core/bindings.json" "$mode" "$size" <<'EOF'
import json, sys
doc = json.load(open(sys.argv[1]))
if sys.argv[3] == 'hint':
    doc['build']['size'] = int(sys.argv[4])
json.dump(doc, open(sys.argv[2], 'w'), indent=1)
EOF
            runs=1
            [[ $mode == cache ]] && runs=2
            for ((i = 1; i <= runs; i++)); do
                SWOFTY_RESOLVE=$([[ $mode == scan ]] && echo scan) "${PIN[@]}" wine "$OUT/bind_harness.exe" steam "$(win "$R")" \
                    "$(win "$EXE")" "$(win "$R/rva.tsv")" || status=1
            done
            [[ $side == new ]] && check_format "$R/bind.log"
            python3 - "$dist/swofty/addons/core/bindings.json" "$R/rva.tsv" "$R/bind.log" "steam $side $mode" <<'EOF' || status=1
import json, sys
kb = {b['id']: int(str(b['rva']), 0) for b in json.load(open(sys.argv[1]))['bindings']}
got = dict(l.rstrip('\n').split('\t') for l in open(sys.argv[2]))
same = sum(got.get(k) == hex(v) for k, v in kb.items())
log = open(sys.argv[3], errors='replace').read()
print(f"{'ok  ' if same == len(kb) else 'FAIL'}: {sys.argv[4]}: {same} of {len(kb)} binding(s) resolve to their KB rva")
if sys.argv[4].endswith('cache'):
    hits = log.count('the address cached for this exe')
    print(f"      the second run took {hits} address(es) from the scan cache")
sys.exit(same != len(kb))
EOF
        done
    done
    if [[ -n $BASE ]]; then
        for mode in hint scan cache; do
            if cmp -s "$OUT/steam-new-$mode/rva.tsv" "$OUT/steam-base-$mode/rva.tsv"; then
                echo "ok  : steam $mode: this core and the base core resolve every binding to the same rva"
            else
                echo "FAIL: steam $mode: the cores disagree"; diff "$OUT/steam-base-$mode/rva.tsv" "$OUT/steam-new-$mode/rva.tsv" | head; status=1
            fi
        done
    fi
else
    echo "skip: no $EXE; the Steam regression is not run"
fi
exit $status
