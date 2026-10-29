#!/usr/bin/env python3
"""Write data/stat_ids.json: the stat ids Classic Seasons 0.6.4's rows have, read from a launcher 1.5 build of the
shared addon and every season in year order (swofty build's swofty/assets.json). The 1.5 launcher gives a copied
vehicle, vehicle package and entrant list row the table's highest stat id plus one; gen.py pins each season's records to
these ids, so the 1.6 core writes the same rows whatever else is installed."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TABLES = {"vehicle": ("entrants.vehicles", "vehicle"),
          "vehicle.package": ("entrants.vehicles.vehicle_packages", "vehicle_package"),
          "content.package": ("entrants.entrant_lists", "entrant_list")}


def fnv(text, h=0xCBF29CE484222325):
    for c in text.encode():
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def main():
    build = Path(sys.argv[1])
    found = sorted(build.glob("cache/overlay/*/*/swofty/assets.json"))
    if not found:
        raise SystemExit(f"{build}: no cache/overlay/*/*/swofty/assets.json")
    edits = {e["hash"]: e["value"] for e in json.loads(found[0].read_text())["db_edits"]}
    out = {}
    for registry, (table, row) in TABLES.items():
        ids = {}
        for i in range(4096):
            label = edits.get(fnv(f"{table}.{row}_{i}._metadata_"))
            stat = edits.get(fnv(f"{table}.{row}_{i}.stat_id"))
            if label and stat is not None and (label.startswith(("cs", "cx"))):
                ids[label] = int(stat)
        out[registry] = ids
    (ROOT / "data" / "stat_ids.json").write_text(json.dumps(out, indent=1) + "\n")
    print(", ".join(f"{r} {len(v)} ({min(v.values())}-{max(v.values())})" for r, v in out.items()))


if __name__ == "__main__":
    main()
