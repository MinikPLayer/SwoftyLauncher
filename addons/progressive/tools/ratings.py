"""Per-year driver ratings for data/years.json (`ratings`): every driver of a season, the five statistics the game
keeps per driver (experience, racecraft, awareness, pace, focus) and the overall rating they make.

    2026        the stock gamedb's 2026 dataset at the start of a career (x = 0; `default` without one)
    2025        the stock default dataset at x = 0, which is EA's F1 25 launch ratings
    2020-2024   EA/Codemasters' launch ratings of that season's game (data/official_ratings.json,
                tools/official_ratings.py); a driver the launch table leaves out (stand-ins, 2020 McLaren) gets the
                model's
    2016-2019   the model (data/ratings_model.json, tools/ratings_model.py): racecraft, awareness and pace from real
                results, calibrated on the 2020-2025 launch ratings; experience from race starts
focus is the driver's stock value (sources give none; a driver without stock statistics gets the stock median). A
driver neither has (none so far) falls back to Classic Seasons' season pace with the other statistics moved from his
stock 2025 ones. overall is the game's weighted mean (driver_statistics_info weighting_factor: experience 0.25,
racecraft 1, awareness 0.25, pace 2, focus 0); `official_overall` keeps EA's published overall where there is one.
"""
import re
import statistics

HOLDERS = "driver_statistics.driver_statistics"
INFO = "driver_statistics.driver_statistics.driver_statistics_info"
STATS = ("experience", "racecraft", "awareness", "pace", "focus")
LIMITS = (0.0, 99.0)
STOCK_YEARS = (2025, 2026)
PRE_2016_STARTS = {
    "lewis_hamilton": 167, "sebastian_vettel": 158, "fernando_alonso": 252, "kimi_raikkonen": 229,
    "felipe_massa": 229, "jenson_button": 284, "nico_rosberg": 185, "daniel_ricciardo": 88, "sergio_perez": 94,
    "nico_hulkenberg": 96, "romain_grosjean": 83, "valtteri_bottas": 56, "daniil_kvyat": 37, "kevin_magnussen": 19,
    "marcus_ericsson": 35, "esteban_gutierrez": 38, "carlos_sainz-jr": 19, "max_verstappen": 19, "felipe_nasr": 19,
    "robert_kubica": 76,
}


def curve_start(text):
    """The game's curve evaluator (Career_Eval_Curve) at x = 0: the y of the last point with x <= 0, scaled."""
    body = text.split("@", 2)[-1]
    head, points = {}, []
    for part in body.split(","):
        k, v = part.split(":")
        if k in ("minx", "maxx", "miny", "maxy"):
            head[k] = float(v)
        else:
            points.append((float(k), float(v)))
    y = [p[1] for p in points if p[0] <= 0][-1]
    return y * (head["maxy"] - head["miny"]) + head["miny"]


def stock_stats(db):
    out = {}
    for h in db.rows(HOLDERS, "DriverStatisticsHolder"):
        driver = db.get(f"{h}.driver")
        sets = {}
        for ds in db.rows(h, "StatisticsDataSet"):
            vals = {}
            for st in db.rows(ds, "Statistic"):
                curve = db.get(f"{st}.evolution")
                if curve and re.match(r"@v2@", curve):
                    vals[db.get(f"{st}._metadata_")] = round(curve_start(curve), 1)
            if all(s in vals for s in STATS):
                sets[db.get(f"{ds}._metadata_")] = vals
        if driver and "default" in sets:
            out[driver] = sets
    return out


def stat_infos(db):
    infos = []
    for r in db.rows(INFO, "StatisticInfo"):
        infos.append({"name": db.get(f"{r}._metadata_"), "stat_id": int(db.get(f"{r}.stat_id")),
                      "weight": float(db.get(f"{r}.weighting_factor"))})
    names = [i["name"] for i in infos]
    if sorted(names) != sorted(STATS):
        raise SystemExit(f"driver_statistics_info lists {names}, expected {list(STATS)}")
    return sorted(infos, key=lambda i: STATS.index(i["name"]))


def overall(values, infos):
    total = sum(i["weight"] for i in infos)
    return round(sum(values[i["name"]] * i["weight"] for i in infos) / total, 1)


def starts_by_year(rounds):
    out = {}
    for year, rs in rounds["years"].items():
        for r in rs:
            for entries in r["entries"].values():
                for driver, _ in entries:
                    if not driver.startswith("missing:"):
                        out.setdefault(driver, {}).setdefault(int(year), 0)
                        out[driver][int(year)] += 1
    return out


def starts_before(starts, driver, year):
    return PRE_2016_STARTS.get(driver, 0) + sum(n for y, n in starts.get(driver, {}).items() if y < year)


def monotone_fit(points):
    """Pool-adjacent-violators over (x, y) (equal x pooled first): a non-decreasing fit, as (x, y) knots."""
    by_x = {}
    for x, y in points:
        by_x.setdefault(x, []).append(y)
    blocks = []
    for x in sorted(by_x):
        blocks.append([x, x, sum(by_x[x]) / len(by_x[x]), len(by_x[x])])
        while len(blocks) > 1 and blocks[-2][2] > blocks[-1][2]:
            b = blocks.pop()
            a = blocks[-1]
            a[1] = b[1]
            a[2] = (a[2] * a[3] + b[2] * b[3]) / (a[3] + b[3])
            a[3] += b[3]
    return [((a + b) / 2, y) for a, b, y, _ in blocks]


def interp(knots, x):
    if x <= knots[0][0]:
        return knots[0][1]
    for (x0, y0), (x1, y1) in zip(knots, knots[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0) if x1 > x0 else y1
    return knots[-1][1]


def clamp(v):
    return round(min(LIMITS[1], max(LIMITS[0], v)), 1)


def season_drivers(y):
    out = []
    for t in y["teams"]:
        for d in t["drivers"]:
            out.append(d["driver"])
    for s in y.get("stand_ins", []):
        if s["driver"]:
            out.append(s["driver"])
    for c in y.get("changes", []):
        out += [c["out"], c["in"]]
    return list(dict.fromkeys(out))


def build(db, years, performance, rounds, official, model):
    stock = stock_stats(db)
    infos = stat_infos(db)
    starts = starts_by_year(rounds)
    grid_2025 = season_drivers(years["2025"])
    exp_points = [(starts_before(starts, d, 2025), stock[d]["default"]["experience"]) for d in grid_2025 if d in stock]
    knots = monotone_fit(exp_points)
    own = sorted(stock)
    offsets = {s: statistics.median(stock[d]["default"][s] - stock[d]["default"]["pace"] for d in own
                                    if d in grid_2025) for s in ("racecraft", "awareness")}
    generic_focus = statistics.median(stock[d]["default"]["focus"] for d in own if d in grid_2025)
    pace_by_driver = {}
    for y, p in performance.items():
        for d, v in p["pace"].items():
            pace_by_driver.setdefault(d, {})[int(y)] = v
    model_error = model["fit_error_loo"]["overall"]["mae"]

    def focus_of(driver):
        return stock[driver]["default"]["focus"] if driver in stock else generic_focus

    def season_pace(driver, year):
        mine = pace_by_driver.get(driver, {})
        if year in mine:
            return mine[year], "Classic Seasons pace"
        if mine:
            near = min(mine, key=lambda y: (abs(y - year), y > year))
            return mine[near], f"Classic Seasons pace of {near}"
        if driver in stock:
            return stock[driver]["default"]["pace"], "stock 2025 pace"
        season = performance.get(str(year), {}).get("pace")
        if season:
            return min(season.values()), "no pace data: the season's slowest Classic Seasons pace"
        return None, None

    def fallback(driver, year):
        pace, how = season_pace(driver, year)
        if pace is None:
            return None, None
        n = starts_before(starts, driver, year)
        if driver in stock:
            base = stock[driver]["default"]
            delta = pace - base["pace"]
            return ({"experience": base["experience"] + interp(knots, n) - interp(
                knots, starts_before(starts, driver, 2025)), "racecraft": base["racecraft"] + delta,
                "awareness": base["awareness"] + delta, "pace": pace, "focus": base["focus"]},
                f"fallback: {how}; others from stock 2025 moved by {delta:+.1f} pace and {n} starts")
        return ({"experience": interp(knots, n), "racecraft": pace + offsets["racecraft"],
                 "awareness": pace + offsets["awareness"], "pace": pace, "focus": generic_focus},
                f"fallback: {how}; no stock statistics: others from pace and {n} starts")

    out = {}
    counts = {}
    for key in sorted(years, key=int):
        year = int(key)
        rows = {}
        launch = official.get(key)
        predicted = model["years"].get(key, {})
        for driver in season_drivers(years[key]):
            if driver.startswith("missing:"):
                continue
            extra = {}
            if year in STOCK_YEARS:
                sets = stock.get(driver)
                if not sets:
                    continue
                use_2026 = year == 2026 and "2026" in sets
                v = dict(sets["2026"] if use_2026 else sets["default"])
                source = "stock 2026 dataset" if use_2026 else "stock default dataset (EA's F1 25 launch ratings)"
                if launch and driver in launch["drivers"]:
                    extra["official_overall"] = launch["drivers"][driver]["overall"]
                kind = "stock"
            elif launch and driver in launch["drivers"]:
                o = launch["drivers"][driver]
                v = {k: o[k] for k in ("experience", "racecraft", "awareness", "pace")}
                v["focus"] = focus_of(driver)
                source = f"EA {launch['game']} launch ratings ({launch['sources'][0]})"
                extra["official_overall"] = o["overall"]
                kind = "official"
            elif driver in predicted:
                m = predicted[driver]
                v = {k: m[k] for k in ("experience", "racecraft", "awareness", "pace")}
                v["focus"] = focus_of(driver)
                source = (f"model from {year} results (tools/ratings_model.py, leave-one-out overall error "
                          f"{model_error} on the 2020-2025 launch ratings)")
                kind = "model"
            else:
                v, source = fallback(driver, year)
                if v is None:
                    continue
                kind = "fallback"
            counts[kind] = counts.get(kind, 0) + 1
            v = {s: clamp(v[s]) for s in STATS}
            rows[driver] = {**v, "overall": overall(v, infos), **extra, "source": source}
        out[key] = rows
    return {"stats": infos, "sources": {k: {"game": v["game"], "urls": v["sources"], "note": v["note"]}
                                        for k, v in official.items()},
            "model": {"fit_error_loo": model["fit_error_loo"], "calibration": model["calibration"]},
            "counts": counts, "years": out}
