#!/usr/bin/env python3
"""Each season car's pace, set in its handling file (.vtf) from the team's real qualifying gap.

Outside career, a car's pace comes from its handling file and its per-track AI data (aislots.py). The AI's tables,
and the lap times of simulated sessions, are kept per mass, so mass is the lever the AI follows in both driven and
simulated sessions, where lower lift or more drag only slow the car's physics. Every season car gets the same aero:
the stock teams' mean wing coefficients (NEUTRAL) and body drag (DRAG), and lift multipliers of LIFT. Its Chassis
Mass is TOP_MASS plus its team's median qualifying gap to the season's fastest team (data/performance.json), less the
head start of its AI slot, at the lap time a kilogram costs in the AI's tables (data/ai_slots.json). build_cars.py
sets them on every build; run on its own, the tool sets them on built cars.

    python3 tools/pace.py [--years 2016-2024]
"""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "tools"), str(ROOT.parents[2] / "tools")]

import aislots
import erpw
import vtf
from layout import cars_dir

SEASONS = {s["year"]: s for s in json.loads((ROOT / "data" / "seasons.json").read_text())["seasons"]}
PERFORMANCE = json.loads((ROOT / "data" / "performance.json").read_text())
LIFT_VALUE = 0.95
TOP_MASS = 798
DRAG = {("F1Aero", "BodyDragCoeffMin"): 0.4491, ("F1Aero", "BodyDragCoeffMax"): 0.641}
NEUTRAL = {
    ("F1Aero", "FrontWingCoL1"): "0.008254",
    ("F1Aero", "FrontWingCoD1"): "0.0026",
    ("F1Aero", "RearWingCoL1"): "0.007424",
    ("F1Aero", "RearWingCoD1"): "0.003755",
}
LIFT = (("F1Aero", "FrontCoeffLiftMultiplier"), ("F1Aero", "RearCoeffLiftMultiplier"))
MASS = ("Chassis", "Mass")


def gap(year, key):
    """The team's real gap left for the handling file to make, in percent, after its AI slot's head start."""
    return max(PERFORMANCE[str(year)]["teams"][key] - aislots.head_start(year, key), 0.0)


def mass(year, key):
    return round(TOP_MASS + gap(year, key) / aislots.percent_per_kg())


def values(year, key):
    v = dict(NEUTRAL)
    for field in LIFT:
        v[field] = f"{LIFT_VALUE:g}"
    v[MASS] = f"{mass(year, key):d}"
    for field, mean in DRAG.items():
        v[field] = f"{mean:.4f}"
    return v


def teams(year):
    """The season's main-package teams by folder, and its own-archive team (a "car"), if any."""
    teams = SEASONS[year]["teams"]
    return ({t["folder"]: t.get("key", t["folder"]) for t in teams if "car" not in t},
            next((t for t in teams if "car" in t), None))


def apply(e, wanted):
    changed = 0
    for r in e.res:
        if not r.name.endswith(".vtf"):
            continue
        root = vtf.parse(e.frag_data(r, 0))
        tops = [root] if root.name == b"VTF" else root.find(b"VTF")
        hit = False
        for top in tops:
            for (block, field), value in wanted.items():
                for node in top.find(block.encode()):
                    for leaf in node.find(field.encode()):
                        if leaf.attr(b"value") != value.encode():
                            leaf.set_attr(b"value", value.encode())
                            changed += 1
                            hit = True
        if hit:
            e.set_frag(r, 0, vtf.write(root))
    return changed


def parse_years(text):
    lo, _, hi = text.partition("-")
    return range(int(lo), int(hi or lo) + 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--years", default=f"{min(SEASONS)}-{max(SEASONS)}")
    args = ap.parse_args()
    for year in parse_years(args.years):
        main_teams, extra = teams(year)
        cars = [(cars_dir(year) / "teams" / f / "wep" / f"{f}.erp", key) for f, key in main_teams.items()]
        if extra:
            f = extra["folder"]
            cars.append((cars_dir(year) / "extra" / "teams" / f / "wep" / f"{f}.erp", extra["key"]))
        for path, key in cars:
            e = erpw.Erp(str(path))
            changed = apply(e, values(year, key))
            if changed:
                e.to_file(str(path))
            v = values(year, key)
            print(f"{year} {key}: lift {v[LIFT[0]]}, mass {v[MASS]}, drag {v[('F1Aero', 'BodyDragCoeffMax')]}, "
                  f"{changed} value(s) set", flush=True)


if __name__ == "__main__":
    main()
