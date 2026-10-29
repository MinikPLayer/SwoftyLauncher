#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# register.cpp on the host: built for x86_64-linux-musl against the recording stand-in (fake_core.cpp),
# run on the committed data/years.json, compared record by record and field by field with years.json
# (compare.py), and every captured record validated against the SDK schemas (tools/std/validate.py).
# Also: an older core (API 8) gets no calls, a missing years.json adds nothing, and compare.py fails on
# years.json copies with one value changed.
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if command -v zig >/dev/null; then
    ZIG=(zig)
else
    ZIG=(nix run nixpkgs#zig --)
fi

REPO=$(readlink -f ../../../../..)
ADDON=$REPO/addons/progressive
OUT=${PG_TEST_DIR:-${TMPDIR:-/tmp}/progressive-test}/register
rm -rf "$OUT/records" "$OUT/empty" "$OUT/mutated"
mkdir -p "$OUT/empty" "$OUT/mutated"
export ZIG_GLOBAL_CACHE_DIR=${ZIG_GLOBAL_CACHE_DIR:-$OUT/zig-cache} ZIG_LOCAL_CACHE_DIR=${ZIG_LOCAL_CACHE_DIR:-$OUT/zig-cache}
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)

"${PIN[@]}" "${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Werror -Wno-nullability-completeness \
    -Wno-missing-designated-field-initializers -I"$REPO/packages/sdk/include" \
    -o "$OUT/fake_core" fake_core.cpp ../../src/register.cpp

"$OUT/fake_core" "$ADDON" "$OUT/captured.json"
python3 compare.py "$OUT/captured.json" "$ADDON/data/years.json" "$OUT/records" "$REPO/packages/sdk/std/registries"

python3 "$REPO/tools/std/validate.py" --repo "$REPO" --schema "$REPO/packages/sdk/std/registries/career.season/1.0.json" \
    "$OUT"/records/career.season/*.json >"$OUT/validate-season.txt" || { cat "$OUT/validate-season.txt"; exit 1; }
python3 "$REPO/tools/std/validate.py" --repo "$REPO" --schema "$REPO/packages/sdk/std/registries/career.driver_ratings/1.0.json" \
    "$OUT"/records/career.driver_ratings/*.json >"$OUT/validate-ratings.txt" || { cat "$OUT/validate-ratings.txt"; exit 1; }
python3 "$REPO/tools/std/validate.py" --repo "$REPO" --data "$OUT/records/seasons.data.json"
CORE=$REPO/native/src/core/registry
"${PIN[@]}" "${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -w -I"$REPO/packages/sdk/include" \
    -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine -o "$OUT/records_test" "$REPO/native/test/registry/progressive_records_test.cpp" \
    "$CORE"/store.cpp "$CORE"/table.cpp "$CORE"/validate.cpp "$CORE"/schema.cpp "$CORE"/engine.cpp "$CORE"/older.cpp
"$OUT/records_test" "$OUT/captured.json"
"${PIN[@]}" "${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -w -I"$REPO/packages/sdk/include" \
    -DSWOFTY_FAKE_CORE_ENGINE=swofty_registry_engine -o "$OUT/start_teams_test" "$REPO/native/test/registry/progressive_start_teams_test.cpp" \
    "$CORE"/store.cpp "$CORE"/table.cpp "$CORE"/validate.cpp "$CORE"/schema.cpp "$CORE"/engine.cpp "$CORE"/older.cpp
"$OUT/start_teams_test" "$OUT/captured.json" "$ADDON/data/years.json" none >"$OUT/start-none.txt" || { cat "$OUT/start-none.txt"; exit 1; }
"$OUT/start_teams_test" "$OUT/captured.json" "$ADDON/data/years.json" none >"$OUT/start-none-again.txt"
cmp -s "$OUT/start-none.txt" "$OUT/start-none-again.txt" || { echo "the no-layer start plan differs between two runs"; exit 1; }
"$OUT/start_teams_test" "$OUT/captured.json" "$ADDON/data/years.json" layer >"$OUT/start-layer.txt" || { cat "$OUT/start-layer.txt"; exit 1; }
tail -n 1 "$OUT/start-none.txt"
tail -n 1 "$OUT/start-layer.txt"
echo "validate: $(grep -c '^ok' "$OUT/validate-season.txt") career.season and $(grep -c '^ok' "$OUT/validate-ratings.txt") career.driver_ratings records valid"

"$OUT/fake_core" "$ADDON" "$OUT/api8.json" 8 2>/dev/null
"$OUT/fake_core" "$OUT/empty" "$OUT/missing.json" 2>/dev/null
python3 - "$OUT" <<'EOF'
import json, sys
out = sys.argv[1]
old = json.load(open(f"{out}/api8.json"))
assert not old["declare"] and not old["add"] and not old["storage"], "an API 8 core got registry or storage calls"
missing = json.load(open(f"{out}/missing.json"))
assert not missing["declare"] and not missing["add"], "a missing years.json still added records"
assert any("not read" in line for line in missing["log"]), "a missing years.json was not logged"
print("register: an API 8 core gets no calls; a missing years.json adds nothing and says so")
EOF

python3 - "$ADDON/data/years.json" "$OUT/mutated" <<'EOF'
import copy, json, sys
years = json.load(open(sys.argv[1]))
out = sys.argv[2]
def write(name, fn):
    y = copy.deepcopy(years)
    fn(y)
    json.dump(y, open(f"{out}/{name}.json", "w"))
write("rating", lambda y: y["ratings"]["years"]["2019"]["lewis_hamilton"].__setitem__("focus", y["ratings"]["years"]["2019"]["lewis_hamilton"]["focus"] + 0.1))
write("seat", lambda y: y["seasons"]["2017"]["teams"][3]["drivers"][1].__setitem__("driver", "jenson_button"))
write("change", lambda y: y["seasons"]["2016"]["changes"][2].__setitem__("circuit", "sakhir"))
write("pool", lambda y: y["pool"][0]["looks"]["2018"].__setitem__("stat_id", 1))
write("pace", lambda y: y["ratings"]["years"]["2023"]["lando_norris"].__setitem__("pace", y["ratings"]["years"]["2023"]["lando_norris"]["pace"] + 0.1))
write("kind", lambda y: y["seasons"]["2016"]["changes"][0].__setitem__("kind", "replace"))
write("calendar", lambda y: y["seasons"]["2024"]["calendar"].pop())
EOF
for m in rating pace seat change kind pool calendar; do
    if python3 compare.py "$OUT/captured.json" "$OUT/mutated/$m.json" "$OUT/mutated/records-$m" >"$OUT/mutated/$m.txt"; then
        echo "FAIL compare.py accepted years.json with one $m value changed"
        exit 1
    fi
done
echo "compare: rejects years.json with one rating, pace, seat, change, kind, pool or calendar value changed"
