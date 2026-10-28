#!/usr/bin/env python3
"""Which stock 2025 team's per-track AI data each season team drives with.

The AI reads a car's per-track data from `environment_package/tracks/<track>/ai/<ai_package_folder>/<asset_filename>/`:
its braking, cornering and throttle tables, the virtual-performance lap times of simulated sessions, and its tyre
wear, fuel and ERS use. A season car's vehicle row is a copy of the stock team's in its slot, asset_filename
included, so it drove with that 2025 team's data: a 2016 McLaren with the 2025 McLaren's, the fastest of the ten.
`measure` writes data/ai_slots.json: each slot's mean lap-time gap, in percent, to the fastest slot at each track, from
the dry, full-grip virtual-performance sets at MASS kg, between the masses each team's sets hold (its own car's, 110 kg
more and about 577 kg): a car's mass is the season's own (pace.py), not the slot's. It also writes the lap time each
kilogram costs, in percent, between a team's own mass and 110 kg more, the mean over every slot and track. `assign` gives a season's teams the slots in pace
order: the team with the smallest real qualifying gap (data/performance.json) the fastest slot, the next team the
next slot, and teams beyond ten the slowest. gen.py points a team whose slot isn't its own folder at the slot's data
and gives every team the rank's performance tier and tyre wear scale (`ranked`), which the vehicle row would
otherwise copy from the stock team in its slot; pace.py takes the slot's gap off the team's handling penalty.

    python3 tools/aislots.py [--game <F1 25 folder>]
"""

import argparse
import json
import statistics
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "tools"), str(ROOT.parents[2] / "tools")]

from layout import FOLDERS

DATA = ROOT / "data" / "ai_slots.json"
TRACKS = Path("2025_asset_groups/environment_package/tracks")
AI = "f1_2025_vehicle_package"
MASS = 800.0
REFERENCE = (0.0, 1.0, 1.0, 0.0, 0.0)
TOP_TYRE_SCALE = 1.04
PERFORMANCE = json.loads((ROOT / "data" / "performance.json").read_text())


def laps(path):
    """(mass, lap time) of the dry, full-grip virtual-performance sets, lightest first."""
    import erpw

    e = erpw.Erp(str(path))
    b = e.frag_data(next(r for r in e.res if r.name.endswith(".vp_times")), 0)
    count, o, points = struct.unpack_from("<I", b, 8)[0], 12, []
    for _ in range(count):
        params = struct.unpack_from("<7f", b, o)
        n = struct.unpack_from("<I", b, o + 28)[0]
        if params[:5] == REFERENCE and params[6] == 1.0:
            points.append((params[5], struct.unpack_from("<f", b, o + 32 + 4 * (n - 1))[0]))
        o += 32 + 4 * n
    return sorted(points)


def lap_at(points, mass=MASS):
    (m0, t0), (m1, t1) = next(((a, b) for a, b in zip(points, points[1:]) if mass <= b[0]), points[-2:])
    return t0 + (t1 - t0) * (mass - m0) / (m1 - m0)


def measure(game):
    gaps = {f: [] for f in FOLDERS}
    per_kg, tracks = [], []
    for track in sorted(p for p in (Path(game) / TRACKS).iterdir() if (p / "ai" / AI).is_dir()):
        paths = {f: track / "ai" / AI / f / f"{f}.erp" for f in FOLDERS}
        if not all(p.exists() for p in paths.values()):
            continue
        points = {f: laps(p) for f, p in paths.items()}
        times = {f: lap_at(p) for f, p in points.items()}
        best = min(times.values())
        for folder, t in times.items():
            gaps[folder].append(100 * (t / best - 1))
            (m0, t0), (m1, t1) = points[folder][-2:]
            per_kg.append(100 * (t1 - t0) / (m1 - m0) / t)
        tracks.append(track.name)
    return {"mass": MASS, "percent_per_kg": round(statistics.mean(per_kg), 5), "tracks": tracks,
            "slots": {f: round(statistics.mean(g), 3) for f, g in gaps.items()}}


def slots():
    return json.loads(DATA.read_text())["slots"]


def percent_per_kg():
    """Lap time, in percent, each kilogram over a car's own mass costs in the AI's tables."""
    return json.loads(DATA.read_text())["percent_per_kg"]


def assign(year):
    """{team key: slot folder} for a season."""
    order = sorted(slots().items(), key=lambda kv: kv[1])
    teams = sorted(PERFORMANCE[str(year)]["teams"].items(), key=lambda kv: kv[1])
    return {key: order[min(i, len(order) - 1)][0] for i, (key, _) in enumerate(teams)}


def ranked(year, key):
    """The vehicle row's rank-based values: `performance_tier` (the stock grid's four teams at 0, three at 1, the
    rest at 2) and `tyre_wear_scale` (TOP_TYRE_SCALE for the fastest team, 0.01 less per place, down to 0.95), as
    the stock 2025 cars have them by their pace."""
    order = sorted(PERFORMANCE[str(year)]["teams"].items(), key=lambda kv: kv[1])
    rank = [k for k, _ in order].index(key)
    tier = 0 if rank < 4 else 1 if rank < 7 else 2
    return {"performance_tier": str(tier), "tyre_wear_scale": f"{max(TOP_TYRE_SCALE - 0.01 * rank, 0.95):.2f}"}


def head_start(year, key):
    """How much slower, in percent, the team's slot drives than the season's fastest team's slot."""
    pace, given = slots(), assign(year)
    top = min(PERFORMANCE[str(year)]["teams"].items(), key=lambda kv: kv[1])[0]
    return pace[given[key]] - pace[given[top]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25")
    args = ap.parse_args()
    result = measure(args.game)
    DATA.write_text(json.dumps(result, indent=1) + "\n")
    for folder, gap in sorted(result["slots"].items(), key=lambda kv: kv[1]):
        print(f"{folder:12s} +{gap:.3f} %")
    print(f"{len(result['tracks'])} tracks, {result['percent_per_kg']} % per kg")


if __name__ == "__main__":
    main()
