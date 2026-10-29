#!/usr/bin/env python3
"""Run the content lowering on each Classic Seasons season's registry/content.json and check that it makes the
same operations as the season's db/records.json, the build operations its records cover (as multisets: the core
applies records by id, the build in team order). The stock audio records the cars copy come from a small database
file made from db/records.json itself, so no game install is needed."""

import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import gamedb  # noqa: E402

SEASONS = ROOT / "addons" / "classicseasons"


def fake_stock(records, content):
    switches = {v["audio_switch"]: v["audio_from"] for v in content.get("vehicle", {}).values() if "audio_switch" in v}
    recs = []
    for op in records["ops"]:
        if op["op"] != "put-record":
            continue
        _, _, switch, field = op["key"].split(".", 3)
        key = f"audio.audio_car_data.{switches[switch]}.{field}"
        recs.append(gamedb.Record(gamedb.fnv1a64(key), op["type"], bytes.fromhex(op["value"])))
    return gamedb.write(recs)


FLOAT_FIELDS = {"tyre_wear_scale"}
STAT_IDS = json.loads((SEASONS / "classic-seasons" / "data" / "stat_ids.json").read_text())
STAT_TABLES = {"entrants.vehicles": "vehicle", "entrants.vehicles.vehicle_packages": "vehicle.package",
               "entrants.entrant_lists": "content.package"}


def canon(op):
    """One operation as text. A float field's text compares by value: the record holds 1.0, which the core writes as
    "1", where the build wrote "1.00"; the game parses both to the same float. The stat_id the core sets on a copy
    is checked on its own (pinned_ids): the build operations leave it to the launcher."""
    if op.get("op") == "add-row":
        op = {**op, "set": {k: (repr(float(v)) if k in FLOAT_FIELDS else v) for k, v in op["set"].items()
                            if not (k == "stat_id" and op["table"] in STAT_TABLES)}}
    return json.dumps(op, sort_keys=True)


def pinned_ids(lowered):
    """The copies whose stat_id is not the one data/stat_ids.json pins for their label."""
    bad = []
    for op in lowered["ops"]:
        registry = STAT_TABLES.get(op.get("table")) if op.get("op") == "add-row" else None
        if registry:
            label = op["set"]["_metadata_"]
            if op["set"].get("stat_id") != str(STAT_IDS[registry].get(label)):
                bad.append(f"{label} stat_id {op['set'].get('stat_id')}, pinned {STAT_IDS[registry].get(label)}")
    return bad


def main():
    tool, years = sys.argv[1], [int(y) for y in sys.argv[2:]] or range(2016, 2025)
    bad = 0
    for year in years:
        folder = SEASONS / f"classic-seasons-{year}"
        records = json.loads((folder / "db" / "records.json").read_text())
        content = json.loads((folder / "registry" / "content.json").read_text())
        with tempfile.TemporaryDirectory() as tmp:
            stock = Path(tmp) / "stock.bin"
            stock.write_bytes(fake_stock(records, content))
            out = Path(tmp) / "ops.json"
            r = subprocess.run([tool, str(ROOT / "packages/sdk/std/registries"), str(stock), str(out),
                                f"swofty.classic-seasons-{year}", str(folder / "registry" / "content.json")],
                               capture_output=True, text=True)
            if r.returncode:
                print(f"{year}: lower_tool failed ({r.returncode})\n{r.stdout}{r.stderr}")
                bad += 1
                continue
            lowered = json.loads(out.read_text())
        want, got = Counter(map(canon, records["ops"])), Counter(map(canon, lowered["ops"]))
        missing, extra = want - got, got - want
        fields_ok = all(lowered["fields"].get(t) == records["fields"].get(t) for t in lowered["fields"])
        unpinned = pinned_ids(lowered)
        ok = not missing and not extra and fields_ok and not unpinned
        bad += not ok
        print(f"{year}: {'ok' if ok else 'DIFF'}: {sum(got.values())} operations from {sum(map(len, content.values()))} "
              f"records, {sum(missing.values())} missing, {sum(extra.values())} extra, fields {'match' if fields_ok else 'differ'}, "
              f"{len(unpinned)} copies off their pinned stat id")
        for line in unpinned[:5]:
            print("  stat id", line)
        for op in list(missing)[:5]:
            print("  missing", op)
        for op in list(extra)[:5]:
            print("  extra  ", op)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
