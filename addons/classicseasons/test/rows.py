#!/usr/bin/env python3
"""Compare two built databases row for row: the db_edits of two `swofty build` outputs (swofty/assets.json), each
the records the core puts on top of the stock database file. Float records compare by value."""

import argparse
import json
import struct
import sys
from pathlib import Path


def edits(path):
    p = Path(path)
    if p.is_dir():
        found = sorted(p.glob("cache/overlay/*/*/swofty/assets.json"))
        if not found:
            raise SystemExit(f"{path}: no cache/overlay/*/*/swofty/assets.json")
        p = found[0]
    data = json.loads(p.read_text())
    return {e["hash"]: (e["type"], e["value"]) for e in data["db_edits"]}


def same(a, b):
    if a == b:
        return True
    if a[0] == b[0] == 1:
        try:
            return struct.pack("<f", float(a[1])) == struct.pack("<f", float(b[1]))
        except ValueError:
            return False
    return False


def compare(a, b):
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    changed = sorted(k for k in set(a) & set(b) if not same(a[k], b[k]))
    floats = sum(1 for k in set(a) & set(b) if a[k] != b[k] and same(a[k], b[k]))
    return only_a, only_b, changed, floats


CATALOG = Path(__file__).resolve().parents[3] / "tools" / "gamedb" / "catalog" / "keys.tsv.gz"


def fnv(text, h=0xCBF29CE484222325):
    for c in text.encode():
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def row_names(tables, rows=600):
    """hash -> (row prefix, index, field) for every catalogue field of the named indexed tables."""
    import gzip
    out = {}
    with gzip.open(CATALOG, "rt", encoding="utf-8") as f:
        next(f)
        for line in f:
            key, table = line.split("\t", 2)[:2]
            if table not in tables or key.count("_N") != 1:
                continue
            prefix, field = key.split("_N.", 1)
            for i in range(rows):
                out["%016x" % fnv(f"{prefix}_{i}.{field}")] = (prefix, i, field)
    return out


def canonical(db, names):
    """The records with each indexed row named by its label instead of its index."""
    labels = {}
    for h, (prefix, i, field) in names.items():
        if field == "_metadata_" and h in db:
            labels[prefix, i] = db[h][1]
    out = {}
    for h, v in db.items():
        n = names.get(h)
        if n and (n[0], n[1]) in labels:
            out[f"{n[0]}[{labels[n[0], n[1]]}].{n[2]}"] = v
        else:
            out[h] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--names", help="a names.tsv (hash<TAB>key) to print keys by name")
    ap.add_argument("--show", type=int, default=20)
    ap.add_argument("--by-label", action="append", default=[],
                    help="an indexed table (catalogue form, e.g. entrants.vehicles.vehicle_N) whose rows match by "
                         "label, not index")
    args = ap.parse_args()
    a, b = edits(args.a), edits(args.b)
    if args.by_label:
        names = row_names(set(args.by_label))
        a, b = canonical(a, names), canonical(b, names)
    names = {}
    if args.names:
        for line in Path(args.names).read_text().splitlines():
            h, _, n = line.partition("\t")
            names[h] = n
    only_a, only_b, changed, floats = compare(a, b)
    print(f"a {len(a)} records, b {len(b)} records: {len(only_a)} only in a, {len(only_b)} only in b, "
          f"{len(changed)} differ, {floats} float records equal by value with other text")
    for label, keys in (("only in a", only_a), ("only in b", only_b), ("differ", changed)):
        for k in keys[: args.show]:
            print(f"  {label}: {names.get(k, k)} a={a.get(k)} b={b.get(k)}")
    return 0 if not (only_a or only_b or changed) else 1


if __name__ == "__main__":
    sys.exit(main())
