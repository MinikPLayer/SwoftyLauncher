#!/usr/bin/env bash
# row_for_row.sh <year> [work dir]: build Classic Seasons' shared addon and its seasons from 2016 to <year> with the
# launcher CLI into scratch data dirs (never the game folder), database operations only, three ways:
#   a   0.6.x: each season's build operations in one file, with launcher 1.5 (CLI15);
#   a15 the seasons as they are (split operation files, registry.json, registry/), with launcher 1.5;
#   b16 the seasons as they are with launcher 1.6 (CLI16) and its loader (LOADER16), which leaves db/records.json to
#       the core, then the core's boot apply of their registry records over that build (rows_tool, the code the core
#       runs in the game).
# a15 must put exactly the files a puts in the game folder, and b16 the same database as a, rows matched by label in
# the tables the core fills in id order. The prefix profile gives every season's rows the stat ids they have in a
# full 0.6.4 install. With BASE_REF (a git commit), a15ref builds that commit's seasons with launcher 1.5 too, and its
# build hash must be a15's: launcher 1.5 builds the seasons exactly as before. Every build runs at the same data dir
# path with the addon files' times fixed, as the compatibility gate does.
set -euo pipefail
YEAR=$1 W=${2:-/mnt/gaming/swofty-std16/w11/rows-$1}
REPO=$(cd "$(dirname "$(readlink -f "$0")")/../../.." && pwd)
CS=$REPO/addons/classicseasons
CLI15=${CLI15:-$HOME/SwoftyLauncher/bin/swofty}
CLI16=${CLI16:-${CARGO_TARGET_DIR:-$REPO/target}/release/swofty}
LOADER16=${LOADER16:-$REPO/native/dist}
TOOL=${TOOL:-$REPO/native/build/content-test/rows_tool}
GAME=${GAME:-/mnt/gaming/SteamLibrary/steamapps/common/F1 25}
BASE=${BASE:-$HOME/.local/share/swofty}
SEED=${SEED:-$(dirname "$W")/seed}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
YEARS=($(seq 2016 "$YEAR"))
for f in "$CLI15" "$CLI16" "$TOOL"; do [[ -x $f ]] || { echo "row_for_row.sh: $f is missing" >&2; exit 2; }; done
[[ -f $LOADER16/swofty/addons/core/features.json ]] || { echo "row_for_row.sh: no loader in $LOADER16 (native/build.sh)" >&2; exit 2; }
grep -q '"registry_rows": "stable"' "$LOADER16/swofty/addons/core/features.json" || { echo "row_for_row.sh: the loader in $LOADER16 has no registry_rows" >&2; exit 2; }
mkdir -p "$W" "$SEED"
# Each data dir's baseline is hard links into one seed copy on the work dir's disk (tools/compat's), never a copy.
[[ $(stat -c %d "$W") == $(stat -c %d "$SEED") ]] || { echo "row_for_row.sh: $SEED is not on $W's disk" >&2; exit 2; }
python3 - "$REPO/tools/compat" "$BASE" "$SEED" <<'PY' || { echo "row_for_row.sh: the baseline seed failed" >&2; exit 2; }
import sys
sys.path.insert(0, sys.argv[1])
from compat import runner
if runner.seed_baseline(sys.argv[2], sys.argv[3]) is None:
    raise SystemExit(f"{sys.argv[2]} has no baseline")
PY
STOCK=$(find "$BASE/baseline/objects" -name "$(python3 -c "
import json,glob
for p in glob.glob('$BASE/baseline/*/baseline.json'):
    f=json.load(open(p))['files'].get('game_package/gamedatabases/gamedatabases.2025.bin')
    if f: print(f['sha256']); break")" | head -1)

# addon <dir> <source folder> <one|split>: a database-only copy of an addon folder
addon() {
    local dir=$1 src=$2 how=$3
    rm -rf "$dir"; mkdir -p "$dir/db"
    [[ $how == split && -d $src/registry ]] && cp -a "$src/registry" "$dir/"
    [[ $how == split && -f $src/registry.json ]] && cp "$src/registry.json" "$dir/"
    python3 - "$src" "$dir" "$how" <<'PY'
import json, shutil, sys
from pathlib import Path
src, dst, how = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
m = json.loads((src / "swofty.json").read_text())
files = m["operations"]["database"]
if how == "one" and len(files) > 1:
    parts = [json.loads((src / f).read_text()) for f in files]
    fields = {}
    for p in parts:
        fields.update(p.get("fields", {}))
    (dst / "db" / "season.json").write_text(json.dumps({"schema_version": 1, "fields": fields,
                                                         "ops": [op for p in parts for op in p["ops"]]}))
    files = ["db/season.json"]
else:
    for f in files:
        shutil.copy(src / f, dst / f)
m["operations"] = {"database": files}
m.pop("icon", None)
m["kind"] = ["database"]
(dst / "swofty.json").write_text(json.dumps(m, indent=1))
PY
}

# build <name> <cli> <loader or ""> <how> [seasons root]: all builds run in $W/data, then move to $W/data-<name>
build() {
    local name=$1 cli=$2 loader=$3 how=$4 root=${5:-$CS} D=$W/data
    rm -rf "$D" "$W/data-$name" "$W/src"; mkdir -p "$D/baseline" "$W/src"
    cp -al "$SEED/." "$D/baseline/"
    addon "$W/src/core" "$root/classic-seasons" split
    for y in "${YEARS[@]}"; do addon "$W/src/s$y" "$root/classic-seasons-$y" "$how"; done
    local log=$W/data-$name.log
    (
        export SWOFTY_USAGE=0
        if [[ -n $loader ]]; then export SWOFTY_LOADER_DIR=$loader; else unset SWOFTY_LOADER_DIR; fi
        "${PIN[@]}" "$cli" --data-dir "$D" addons install "$W/src/core"
        for y in "${YEARS[@]}"; do "${PIN[@]}" "$cli" --data-dir "$D" addons install "$W/src/s$y"; done
        find "$D/addons" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +
        "${PIN[@]}" "$cli" --data-dir "$D" --profile cmp addons enable swofty.classic-seasons
        for y in "${YEARS[@]}"; do "${PIN[@]}" "$cli" --data-dir "$D" --profile cmp addons enable "swofty.classic-seasons-$y"; done
        "${PIN[@]}" "$cli" --data-dir "$D" --profile cmp build --game "$GAME"
    ) > "$log" 2>&1 || { echo "build $name failed: $log" >&2; tail -20 "$log" >&2; exit 2; }
    mv "$D" "$W/data-$name"
}

overlay() { ls -d "$W/data-$1"/cache/overlay/cmp/*/ | head -1; }

build a "$CLI15" "" one
build a15 "$CLI15" "" split
if [[ -n ${BASE_REF:-} ]]; then
    rm -rf "$W/ref"; mkdir -p "$W/ref"
    git -C "$REPO" archive "$BASE_REF" addons/classicseasons | tar -x -C "$W/ref"
    build a15ref "$CLI15" "" split "$W/ref/addons/classicseasons"
fi
build b16 "$CLI16" "$LOADER16" split
args=()
[[ -f $CS/classic-seasons/registry/drivers.json ]] && args+=("swofty.classic-seasons" "$CS/classic-seasons/registry/drivers.json")
for y in "${YEARS[@]}"; do args+=("swofty.classic-seasons-$y" "$CS/classic-seasons-$y/registry/content.json"); done
"$TOOL" --registries "$REPO/packages/sdk/std/registries" --db "$STOCK" --edits "$(overlay b16)swofty/assets.json" \
    --out "$W/core.json" --summary "$W/core.summary.json" "${args[@]}" > "$W/core.log" 2>&1 || { cat "$W/core.log"; exit 1; }
tail -1 "$W/core.log"

status=0
echo "== ${YEARS[0]}-$YEAR: launcher 1.5 builds the seasons as they are exactly as it builds 0.6.x"
A=$(overlay a) B=$(overlay a15)
files() { (cd "$1" && find . -type f ! -name built.json ! -name plan.json -exec sha256sum {} + | sort -k2
           sed "s/$(basename "$1")/BUILD/g" plan.json | sha256sum | sed 's/-$/plan.json/'); }
if diff <(files "$A") <(files "$B") > "$W/a15.diff"; then
    echo "ok: $(files "$A" | wc -l) files the same (build hashes $(basename "$A") and $(basename "$B"))"
else
    echo "DIFF: $W/a15.diff"; status=1
fi
if [[ -n ${BASE_REF:-} ]]; then
    echo "== ${YEARS[0]}-$YEAR: launcher 1.5 builds the seasons as it built $BASE_REF's"
    R=$(overlay a15ref)
    if [[ $(basename "$R") == $(basename "$B") ]] && diff <(files "$R") <(files "$B") > "$W/a15ref.diff"; then
        echo "ok: build hash $(basename "$B") for both"
    else
        echo "DIFF: build hash $(basename "$R") against $(basename "$B"); $W/a15ref.diff"; status=1
    fi
fi
echo "== ${YEARS[0]}-$YEAR: launcher 1.6 leaves db/records.json to the core"
n=$(grep -c "db/records.json is applied by the core" "$W/data-b16.log" || true)
if [[ $n == "${#YEARS[@]}" ]]; then echo "ok: $n season(s)"; else echo "DIFF: $n of ${#YEARS[@]} season(s)"; status=1; fi
echo "== ${YEARS[0]}-$YEAR: launcher 1.6 plus the core's rows against 0.6.x, rows matched by label"
LABELS=(--by-label entrants.vehicles.vehicle_N --by-label entrants.vehicles.vehicle_packages.vehicle_package_N
        --by-label audio.audio_bundle_settings.audio_bundle_settings_N --by-label car_setup.presets.vehicle_package_setups_N)
python3 "$CS/test/rows.py" "$W/data-a" "$W/core.json" "${LABELS[@]}" --show 40 || status=1
echo "== ${YEARS[0]}-$YEAR: the liveries' records put the archives the 1.5 operations put"
python3 - "$CS" "$W/core.summary.json" "${YEARS[@]}" <<'PY' || status=1
import json, sys
from pathlib import Path
cs, summary, years = Path(sys.argv[1]), json.loads(Path(sys.argv[2]).read_text()), sys.argv[3:]
lines = " ".join(summary["lines"].get("livery", []))
bad = 0
for y in years:
    f = cs / f"classic-seasons-{y}" / "assets" / "liveries.json"
    if not f.exists():
        print(f"{y}: no liveries.json; the season's cars stay on the 1.5 operations")
        continue
    targets = [o["target"] for o in json.loads(f.read_text())["ops"] if o["op"] == "file.put"]
    missing = [t for t in targets if f"({t})" not in lines]
    print(f"{y}: {len(targets) - len(missing)} of {len(targets)} archives are put by the records" + (f"; missing {missing[:3]}" if missing else ""))
    bad += bool(missing)
raise SystemExit(1 if bad else 0)
PY
exit $status
