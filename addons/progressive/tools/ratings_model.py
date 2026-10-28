#!/usr/bin/env python3
"""A driver ratings model from real results, calibrated on EA's launch ratings, for data/ratings_model.json.

    python3 tools/ratings_model.py --jolpica /mnt/work/.progressive/jolpica

Per driver-season 2016-2025 (his team: the constructor he raced most for) the features come from the Jolpica
cache (qualifying-<year>-p*.json, results-<year>-p*.json, driverStandings-<year>.json):
    q_gap       median qualifying gap to his team-mate in % (best of Q1-Q3, rounds both set a time; < 0 faster)
    q_h2h       share of those rounds he out-qualified his team-mate
    pole_gap    median gap of his best qualifying time to pole in % (driver and car together)
    team_gap    median gap of his team's best qualifying time to pole in % (the car)
    r_h2h       share of races he finished ahead of his team-mate, both classified
    pts_share   his share of his team's points
    champ       championship position, 0 for the champion to 1 for the last
    wins, podiums, dnfs   per race start (dnfs: not classified)
    gain        mean places gained from the grid in classified finishes (a pit-lane start counts as 20th)
    exp         log(1 + race starts before the season)
Every feature but exp is BLEND x the season's + (1 - BLEND) x the previous season's when the driver raced then (the
launch ratings weigh the season before too; 2015 is not cached, so 2016 is its own season only).
racecraft, awareness and pace are ridge regressions (standardised features, LAMBDA) fitted to the official
launch ratings of the same season's game 2020-2025 (data/official_ratings.json; the game's ratings at the start of
that season); experience is the monotone fit (pool-adjacent-violators) of the official experience over race starts
before the season. The fit error is leave-one-out. Predictions cover every driver-season 2016-2025.
"""
import argparse
import json
import math
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import gen  # noqa: E402
import ratings  # noqa: E402

OUT = ROOT / "data" / "ratings_model.json"
OFFICIAL = ROOT / "data" / "official_ratings.json"
YEARS = range(2016, 2026)
FEATURES = ("q_gap", "q_h2h", "pole_gap", "team_gap", "r_h2h", "pts_share", "champ", "wins", "podiums", "dnfs",
            "gain", "exp")
TARGETS = ("racecraft", "awareness", "pace")
LAMBDA = 10.0
BLEND = 0.6


def seconds(t):
    if not t:
        return None
    m, _, s = t.rpartition(":")
    try:
        return int(m or 0) * 60 + float(s)
    except ValueError:
        return None


def pages(folder, stem):
    out = []
    for p in sorted(folder.glob(f"{stem}-p*.json"), key=lambda p: int(p.stem.rsplit("-p", 1)[1])):
        out += json.loads(p.read_text())["MRData"]["RaceTable"]["Races"]
    return out


def label(jid):
    return gen.JOLPICA_DRIVERS.get(jid)


def classified(status):
    return status == "Finished" or status.startswith("+") or status == "Lapped"


def season_features(folder, year, starts):
    quali = {}
    for race in pages(folder, f"qualifying-{year}"):
        rnd = quali.setdefault(int(race["round"]), [])
        for q in race["QualifyingResults"]:
            t = [seconds(q.get(k)) for k in ("Q1", "Q2", "Q3")]
            t = [x for x in t if x]
            rnd.append((label(q["Driver"]["driverId"]), q["Constructor"]["constructorId"], min(t) if t else None))
    results = {}
    for race in pages(folder, f"results-{year}"):
        rnd = results.setdefault(int(race["round"]), [])
        for r in race["Results"]:
            rnd.append((label(r["Driver"]["driverId"]), r["Constructor"]["constructorId"], int(r["grid"]),
                        int(r["position"]), r["status"], float(r["points"])))
    st = json.loads((folder / f"driverStandings-{year}.json").read_text())["MRData"]["StandingsTable"]
    table = st["StandingsLists"][0]["DriverStandings"]
    champ = {label(x["Driver"]["driverId"]): i / max(1, len(table) - 1) for i, x in enumerate(table)}

    per = {}
    team_count = {}
    for rnd, rows in results.items():
        for d, c, *_ in rows:
            if d:
                team_count.setdefault(d, {}).setdefault(c, 0)
                team_count[d][c] += 1
    team_of = {d: max(cs, key=cs.get) for d, cs in team_count.items()}
    team_points = {}
    for rows in results.values():
        for d, c, g, pos, status, pts in rows:
            team_points[c] = team_points.get(c, 0) + pts
    for d, team in team_of.items():
        f = {"q_gaps": [], "q_wins": 0, "q_n": 0, "pole": [], "team": [], "r_wins": 0, "r_n": 0, "starts": 0,
             "points": 0.0, "wins": 0, "podiums": 0, "dnfs": 0, "gains": []}
        per[d] = f
    for rnd, rows in quali.items():
        times = [t for _, _, t in rows if t]
        if not times:
            continue
        pole = min(times)
        best = {}
        for d, c, t in rows:
            if t:
                best[c] = min(best.get(c, t), t)
        for d, c, t in rows:
            if d not in per or not t or team_of.get(d) != c:
                continue
            f = per[d]
            f["pole"].append((t / pole - 1) * 100)
            f["team"].append((best[c] / pole - 1) * 100)
            mates = [tm for dm, cm, tm in rows if cm == c and dm != d and tm]
            if mates:
                gap = (t / mates[0] - 1) * 100
                if abs(gap) < 3:
                    f["q_gaps"].append(gap)
                f["q_n"] += 1
                f["q_wins"] += t < mates[0]
    for rnd, rows in results.items():
        for d, c, g, pos, status, pts in rows:
            if d not in per or team_of.get(d) != c:
                continue
            f = per[d]
            f["starts"] += 1
            f["points"] += pts
            f["wins"] += pos == 1
            f["podiums"] += pos <= 3
            ok = classified(status)
            f["dnfs"] += not ok
            if ok:
                f["gains"].append((g if g > 0 else 20) - pos)
            mates = [(pm, sm) for dm, cm, gm, pm, sm, _ in rows if cm == c and dm != d]
            if mates and ok and classified(mates[0][1]):
                f["r_n"] += 1
                f["r_wins"] += pos < mates[0][0]
    out = {}
    for d, f in per.items():
        n = max(1, f["starts"])
        out[d] = {
            "team": team_of[d],
            "starts": f["starts"],
            "q_gap": statistics.median(f["q_gaps"]) if f["q_gaps"] else 0.0,
            "q_h2h": f["q_wins"] / f["q_n"] if f["q_n"] else 0.5,
            "pole_gap": statistics.median(f["pole"]) if f["pole"] else 3.0,
            "team_gap": statistics.median(f["team"]) if f["team"] else 3.0,
            "r_h2h": f["r_wins"] / f["r_n"] if f["r_n"] else 0.5,
            "pts_share": f["points"] / team_points[team_of[d]] if team_points.get(team_of[d]) else 0.5,
            "champ": champ.get(d, 1.0),
            "wins": f["wins"] / n, "podiums": f["podiums"] / n, "dnfs": f["dnfs"] / n,
            "gain": statistics.mean(f["gains"]) if f["gains"] else 0.0,
            "exp": math.log1p(ratings.starts_before(starts, d, year)),
            "starts_before": ratings.starts_before(starts, d, year),
        }
    return out


def blended(f, prev):
    if not prev:
        return dict(f)
    out = dict(f)
    for k in FEATURES:
        if k != "exp":
            out[k] = BLEND * f[k] + (1 - BLEND) * prev[k]
    return out


def solve(a, b):
    n = len(a)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(m[r][i]))
        m[i], m[p] = m[p], m[i]
        for r in range(n):
            if r != i and m[i][i]:
                k = m[r][i] / m[i][i]
                m[r] = [x - k * y for x, y in zip(m[r], m[i])]
    return [m[i][n] / m[i][i] for i in range(n)]


def fit(rows, target, lam):
    xs = [[r["f"][k] for k in FEATURES] for r in rows]
    ys = [r["y"][target] for r in rows]
    mu = [statistics.mean(c) for c in zip(*xs)]
    sd = [statistics.pstdev(c) or 1.0 for c in zip(*xs)]
    z = [[(x - m) / s for x, m, s in zip(row, mu, sd)] for row in xs]
    ym = statistics.mean(ys)
    k = len(FEATURES)
    a = [[sum(r[i] * r[j] for r in z) + (lam if i == j else 0) for j in range(k)] for i in range(k)]
    b = [sum(r[i] * (y - ym) for r, y in zip(z, ys)) for i in range(k)]
    w = solve(a, b)
    return {"mean": mu, "sd": sd, "intercept": ym, "weights": w}


def predict(model, f):
    return model["intercept"] + sum(w * (f[k] - m) / s for w, k, m, s in
                                    zip(model["weights"], FEATURES, model["mean"], model["sd"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jolpica", type=Path, default=Path("/mnt/work/.progressive/jolpica"))
    args = ap.parse_args()
    rounds = json.loads((ROOT / "data" / "rounds.json").read_text())
    starts = ratings.starts_by_year(rounds)
    official = json.loads(OFFICIAL.read_text())["years"]
    own = {y: season_features(args.jolpica, y, starts) for y in YEARS}
    feats = {y: {d: blended(f, own.get(y - 1, {}).get(d)) for d, f in fs.items()} for y, fs in own.items()}
    rows = []
    for y, o in official.items():
        for d, v in o["drivers"].items():
            f = feats.get(int(y), {}).get(d)
            if f:
                rows.append({"year": int(y), "driver": d, "f": f, "y": v})
    models = {t: fit(rows, t, LAMBDA) for t in TARGETS}
    exp_knots = ratings.monotone_fit([(r["f"]["starts_before"], r["y"]["experience"]) for r in rows])
    weights = {"experience": 0.25, "racecraft": 1.0, "awareness": 0.25, "pace": 2.0}
    errors = {t: [] for t in (*TARGETS, "experience", "overall", "overall_from_official_stats")}
    for i, r in enumerate(rows):
        rest = rows[:i] + rows[i + 1:]
        pred = {t: predict(fit(rest, t, LAMBDA), r["f"]) for t in TARGETS}
        pred["experience"] = ratings.interp(ratings.monotone_fit(
            [(x["f"]["starts_before"], x["y"]["experience"]) for x in rest]), r["f"]["starts_before"])
        for t in (*TARGETS, "experience"):
            errors[t].append(abs(pred[t] - r["y"][t]))
        ov = sum(pred[k] * w for k, w in weights.items()) / sum(weights.values())
        errors["overall"].append(abs(ov - r["y"]["overall"]))
        ov_off = sum(r["y"][k] * w for k, w in weights.items()) / sum(weights.values())
        errors["overall_from_official_stats"].append(abs(ov_off - r["y"]["overall"]))
    fit_error = {t: {"mae": round(statistics.mean(e), 2), "max": round(max(e), 1)} for t, e in errors.items()}
    preds = {}
    for y in YEARS:
        out = {}
        for d, f in sorted(feats[y].items()):
            if not d:
                continue
            v = {t: round(min(99.0, max(30.0, predict(models[t], f))), 1) for t in TARGETS}
            v["experience"] = round(ratings.interp(exp_knots, f["starts_before"]), 1)
            out[d] = {**v, "features": {k: round(f[k], 3) for k in (*FEATURES, "starts", "starts_before")},
                      "team": f["team"]}
        preds[str(y)] = out
    doc = {
        "generated_by": "addons/progressive/tools/ratings_model.py",
        "features": list(FEATURES), "lambda": LAMBDA, "blend": BLEND, "calibration": f"{len(rows)} driver-seasons 2020-2025",
        "fit_error_loo": fit_error,
        "models": {t: {"intercept": round(m["intercept"], 4),
                       "weights": {k: round(w, 4) for k, w in zip(FEATURES, m["weights"])},
                       "mean": {k: round(x, 4) for k, x in zip(FEATURES, m["mean"])},
                       "sd": {k: round(x, 4) for k, x in zip(FEATURES, m["sd"])}} for t, m in models.items()},
        "experience_scale": [[round(x, 1), round(v, 2)] for x, v in exp_knots],
        "years": preds,
    }
    OUT.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(fit_error))


if __name__ == "__main__":
    main()
