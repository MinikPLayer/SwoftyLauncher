#!/usr/bin/env python3
"""In-season development curves for data/development.json, from real qualifying (Jolpica F1 API).

    python3 tools/development.py [--cache /mnt/work/.progressive/jolpica] [--fetch]

Per year 2016-2025 and round: a team's gap is its faster car's best lap over Q1-Q3 against pole, in percent (the
Classic Seasons performance.py measure behind each year's car). Rounds whose gaps sit far from the season's order
(wet or mixed sessions: the median team deviation from the season medians over WET_SPREAD times the typical one) are
dropped, as is a team's round more than OUTLIER points slower than its season median (no representative time). The
season is cut into CHECKPOINTS equal parts by round; a checkpoint is the median of the team's gaps in its part, at the
part's middle (`at`, 0 = round 1, 1 = the last round), taken relative to the mean of the team's checkpoints so the
curve averages 0 and the year's car keeps the year's pace. delta_pct > 0 is slower than the year's car. Career slots
follow team lineage (SLOT_OF_CONSTRUCTOR); 10-team years leave slot 11 without a curve.

--fetch downloads missing pages (qualifying-<year>-p<n>.json, raw API responses, 100 results a page) into --cache.
"""

import argparse
import json
import statistics
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "development.json"
API = "https://api.jolpi.ca/ergast/f1"
YEARS = range(2016, 2026)
CHECKPOINTS = 5
OUTLIER = 1.5
WET_SPREAD = 2.5
SLOT_OF_CONSTRUCTOR = {
    "mercedes": 1, "ferrari": 2, "red_bull": 3, "mclaren": 4,
    "force_india": 5, "racing_point": 5, "aston_martin": 5,
    "renault": 6, "alpine": 6, "williams": 7,
    "toro_rosso": 8, "alphatauri": 8, "rb": 8,
    "sauber": 9, "alfa": 9, "haas": 10, "manor": 11,
}


def pages(cache, year, fetch):
    out, offset, page = [], 0, 0
    while True:
        path = cache / f"qualifying-{year}-p{page}.json"
        if not path.exists():
            if not fetch:
                raise SystemExit(f"{path} is missing (run with --fetch)")
            url = f"{API}/{year}/qualifying.json?limit=100&offset={offset}"
            with urllib.request.urlopen(url, timeout=60) as r:
                path.write_bytes(r.read())
            time.sleep(0.6)
        data = json.loads(path.read_text())["MRData"]
        out.append(data)
        offset += int(data["limit"])
        page += 1
        if offset >= int(data["total"]):
            return out


def seconds(t):
    if not t:
        return None
    m, _, s = t.rpartition(":")
    return int(m or 0) * 60 + float(s)


def rounds_of(year_pages):
    races = {}
    for data in year_pages:
        for race in data["RaceTable"]["Races"]:
            r = races.setdefault(int(race["round"]), {"name": race["raceName"], "results": []})
            r["results"].extend(race["QualifyingResults"])
    return dict(sorted(races.items()))


def round_gaps(results):
    best = {}
    for q in results:
        slot = SLOT_OF_CONSTRUCTOR.get(q["Constructor"]["constructorId"])
        times = [t for t in (seconds(q.get(s)) for s in ("Q1", "Q2", "Q3")) if t]
        if slot is None or not times:
            continue
        best[slot] = min(best.get(slot, 1e9), min(times))
    if not best:
        return {}
    pole = min(best.values())
    return {slot: (t / pole - 1) * 100 for slot, t in best.items()}


def season(year_pages):
    rounds = rounds_of(year_pages)
    total = max(rounds)
    gaps = {r: g for r, g in ((r, round_gaps(v["results"])) for r, v in rounds.items()) if g}
    slots = sorted({s for g in gaps.values() for s in g})
    median = {s: statistics.median(g[s] for g in gaps.values() if s in g) for s in slots}
    spread = {r: statistics.median(abs(g[s] - median[s]) for s in g) for r, g in gaps.items() if g}
    typical = statistics.median(spread.values())
    dropped = {r: rounds[r]["name"] for r, v in spread.items() if v > WET_SPREAD * typical}
    teams = {}
    for s in slots:
        series = [(r, g[s]) for r, g in gaps.items() if r not in dropped and s in g and g[s] <= median[s] + OUTLIER]
        parts = []
        for k in range(CHECKPOINTS):
            lo, hi = k / CHECKPOINTS, (k + 1) / CHECKPOINTS
            xs = [v for r, v in series if lo <= (r - 1) / (total - 1) < hi or (k == CHECKPOINTS - 1 and r == total)]
            parts.append(statistics.median(xs) if xs else None)
        if any(v is None for v in parts):
            continue
        mean = statistics.fmean(parts)
        teams[str(s)] = {
            "rounds": len(series),
            "checkpoints": [{"at": round((k + 0.5) / CHECKPOINTS, 3), "delta_pct": round(v - mean, 3)}
                            for k, v in enumerate(parts)],
        }
    return {"rounds": total, "dropped_rounds": {str(r): n for r, n in sorted(dropped.items())}, "teams": teams}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=Path("/mnt/work/.progressive/jolpica"))
    ap.add_argument("--fetch", action="store_true")
    args = ap.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    doc = {
        "schema_version": 1,
        "generated_by": "addons/progressive/tools/development.py",
        "method": f"best qualifying lap of the faster car vs pole in %, wet rounds dropped (median team deviation "
                  f"> {WET_SPREAD}x typical), team rounds > {OUTLIER} points over the team's season median dropped, "
                  f"median per fifth of the season, relative to the mean of the five; delta_pct > 0 is slower than "
                  f"the year's car",
        "years": {},
    }
    for year in YEARS:
        doc["years"][str(year)] = season(pages(args.cache, year, args.fetch))
        y = doc["years"][str(year)]
        print(year, f"{y['rounds']} rounds, dropped {list(y['dropped_rounds'].values())}")
        for s, t in y["teams"].items():
            print(f"  slot {s:>2}: " + " ".join(f"{c['delta_pct']:+.2f}" for c in t["checkpoints"]))
    OUT.write_text(json.dumps(doc, indent=1) + "\n")


if __name__ == "__main__":
    main()
