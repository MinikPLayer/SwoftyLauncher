#!/usr/bin/env python3
"""Generate Progressive Career's data from Classic Seasons and the stock database.

    python3 tools/gen.py [--db <stock gamedatabases.2025.bin>] [--jolpica <Classic Seasons build/jolpica dir>]

Writes data/years.json (per year and career team slot: line-up, vehicle row, looks, names, pace; the driver pool),
db/career.json (mode, championship, shared components, data groups, schedule, official lists, content set),
db/grid.json (career entrant list, driver pool list and entrants, per-year Choose Driver lists, pool looks) and
lang/career.json. --jolpica refreshes data/participants.json, every driver who qualified per year and constructor,
from Classic Seasons' qualifying cache. --standings refreshes data/standings.json, the final constructors' and drivers'
standings of 2015-2025 from the Jolpica cache (fetch_standings.py); each season gets the previous year's final order
(`previous_standings`). Each real team of 2016-2025 also gets its in-season development curve from
data/development.json (tools/development.py).

Line-ups 2016-2024 are Classic Seasons' data/seasons.json; its emitted classic-seasons-<year>/db/season.json gives the
vehicle rows and the look stat ids. 2025 and 2026 are the stock f1_2025 and f1_2026 lists.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parents[1]
CS = REPO / "addons" / "classicseasons"
CS_DATA = CS / "classic-seasons" / "data"
CS_LOGOS = CS / "classic-seasons" / "erp" / "logos.json"
LOGO_ARRAYS = ("team_logos.Contents.f1_2023", "team_logos.Contents.driver_career_2024_team_select",
               "teams.Contents.mp_team_select_logos", "teams.Contents.driver_select_team_logos",
               "teams.Contents.badges_broadcast", "broadcast_ui_generic.Contents.grid_team_logos",
               "osd.Contents.timing_tower.Contents.team_badges_hud",
               "osd.Contents.timing_tower.Contents.team_badges_broadcast")
sys.path.insert(0, str(HERE))
import ratings  # noqa: E402
import stockdb  # noqa: E402

YEARS = range(2016, 2027)
CS_YEARS = range(2016, 2025)
SEASONS = 20
DATA_GROUP = "f1_career_progressive"
SUFFIX = "PG"
MODE = "driver_career_progressive"
CHAMPIONSHIP = "F1_driver_career_progressive"
EVENT_LIST = "F1_career_progressive"
SHARED = "championships.shared_components"
SHARED_ROW = "shared_driver_career_progressive_components"
POOL_LIST = "f1_career_progressive_driver_pool"
YEAR_LIST = "pg_f1_{year}"
KIT_YEARS = "entrants.team_customisation"
KITS = f"{KIT_YEARS}.team_customisation_for_year[_metadata_=2026]"
CONTENT_SET = "2026"

MODE_STAT_ID = 82
CHAMPIONSHIP_STAT_ID = 36
LIST_STAT_IDS = range(120, 136)
LOOK_STAT_IDS = range(700, 800)
CS_DRIVER_STAT_BASE = 225

STOCK_LISTS = {2025: "f1_2025", 2026: "f1_2026"}
LISTS = "entrants.entrant_lists"
ENTRANTS = "entrants.entrant_lists.entrants"
LOOK_FOLDER = "entrants.driver_customisations.folder_1"
LOOKS = "entrants.driver_customisations.folder[_metadata_=official_driver_customisations]"
LOOK_WORD = "preloaded_driver_customisation_properties"
LOOK_TEMPLATE = "2026-mclaren_lando_norris"
ENTRANT_TEMPLATE = "jarno_opmeer"
LIST_TEMPLATE = "f1_2026"
DATA_GROUP_SOURCE = "f1_career_2026"
DATA_GROUP_TABLES = [
    ("career.career_setup", "CareerSetupDataGroup"),
    ("accolades", "AccoladeDataGroup"),
    ("career.leverage", "LeverageDataGroup"),
    ("career.my_team_renown_and_form", "MyTeamRenownAndFormDataGroup"),
    ("career.my_team_sponsors", "MyTeamSponsorsDataGroup"),
    ("recognition", "RecognitionDataGroup"),
    ("resource_points", "data_group"),
    ("specialist_manager", "dataGroup"),
]
OFFICIAL = "official_entrant_list"

SLOTS = [
    {"slot": 1, "key": "mercedes", "team": "mercedes_26", "vehicle": "mercedes_2026", "folder": "mercedes",
     "stock_2025": "mercedes", "vehicle_2025": "mercedes_2025", "lineage": {"mercedes": YEARS}},
    {"slot": 2, "key": "ferrari", "team": "ferrari_26", "vehicle": "ferrari_2026", "folder": "ferrari",
     "stock_2025": "ferrari", "vehicle_2025": "ferrari_2025", "lineage": {"ferrari": YEARS}},
    {"slot": 3, "key": "redbull", "team": "redbull_26", "vehicle": "redbull_2026", "folder": "redbull",
     "stock_2025": "redbull", "vehicle_2025": "redbull_2025", "lineage": {"redbull": YEARS}},
    {"slot": 4, "key": "mclaren", "team": "mclaren_26", "vehicle": "mclaren_2026", "folder": "mclaren",
     "stock_2025": "mclaren", "vehicle_2025": "mclaren_2025", "lineage": {"mclaren": YEARS}},
    {"slot": 5, "key": "aston_martin", "team": "forceindia_26", "vehicle": "force_india_2026", "folder": "force_india",
     "stock_2025": "forceindia", "vehicle_2025": "force_india_2025", "lineage": {"force_india": YEARS}},
    {"slot": 6, "key": "alpine", "team": "renault_26", "vehicle": "lotus_2026", "folder": "lotus",
     "stock_2025": "renault", "vehicle_2025": "lotus_2025", "lineage": {"lotus": YEARS}},
    {"slot": 7, "key": "williams", "team": "williams_26", "vehicle": "williams_2026", "folder": "williams",
     "stock_2025": "williams", "vehicle_2025": "williams_2025", "lineage": {"williams": YEARS}},
    {"slot": 8, "key": "racing_bulls", "team": "tororosso_26", "vehicle": "toro_rosso_2026", "folder": "toro_rosso",
     "stock_2025": "tororosso", "vehicle_2025": "toro_rosso_2025", "lineage": {"toro_rosso": YEARS}},
    {"slot": 9, "key": "audi", "team": "sauber_26", "vehicle": "sauber_2026", "folder": "sauber",
     "stock_2025": "sauber", "vehicle_2025": "sauber_2025", "lineage": {"sauber": YEARS}},
    {"slot": 10, "key": "haas", "team": "haas_26", "vehicle": "haas_2026", "folder": "haas",
     "stock_2025": "haas", "vehicle_2025": "haas_2025", "lineage": {"haas": YEARS}},
    {"slot": 11, "key": "cadillac", "team": "gm_cadillac_26", "vehicle": "gm_cadillac_2026", "folder": None,
     "stock_2025": None, "vehicle_2025": None, "lineage": {"manor": [2016]}},
]
SLOT_OF_CS_KEY = {"mercedes": 1, "ferrari": 2, "redbull": 3, "mclaren": 4, "force_india": 5, "lotus": 6,
                  "williams": 7, "toro_rosso": 8, "sauber": 9, "haas": 10, "manor": 11}
SLOT_OF_STOCK_TEAM = {"mercedes": 1, "ferrari": 2, "redbull": 3, "mclaren": 4, "forceindia": 5, "renault": 6,
                      "williams": 7, "tororosso": 8, "sauber": 9, "haas": 10, "gm_cadillac": 11}
NAMES_STOCK = {
    2025: {1: ("Mercedes", "MER"), 2: ("Ferrari", "FER"), 3: ("Red Bull Racing", "RBR"), 4: ("McLaren", "MCL"),
           5: ("Aston Martin", "AST"), 6: ("Alpine", "ALP"), 7: ("Williams", "WIL"), 8: ("Racing Bulls", "RB"),
           9: ("Kick Sauber", "SAU"), 10: ("Haas", "HAS")},
    2026: {1: ("Mercedes", "MER"), 2: ("Ferrari", "FER"), 3: ("Red Bull Racing", "RBR"), 4: ("McLaren", "MCL"),
           5: ("Aston Martin", "AST"), 6: ("Alpine", "ALP"), 7: ("Williams", "WIL"), 8: ("Racing Bulls", "RB"),
           9: ("Audi", "SAU"), 10: ("Haas", "HAS"), 11: ("Cadillac", "GMC")},
}
FILLER_YEARS = range(2017, 2026)
FILLER_FROM = (2016, "manor")
JOLPICA_DRIVERS = {
    "aitken": "jack_aitken", "albon": "alex_albon", "alonso": "fernando_alonso", "bearman": "oliver_bearman",
    "bottas": "valtteri_bottas", "brendon_hartley": "brendon_hartley", "button": "jenson_button",
    "colapinto": "franco_colapinto", "de_vries": "nyck_de-vries", "doohan": "jack_doohan",
    "ericsson": "marcus_ericsson", "gasly": "pierre_gasly", "giovinazzi": "antonio_giovinazzi",
    "grosjean": "romain_grosjean", "gutierrez": "esteban_gutierrez", "hamilton": "lewis_hamilton",
    "haryanto": "rio_haryanto", "hulkenberg": "nico_hulkenberg", "jolyon_palmer": "jolyon_palmer",
    "kevin_magnussen": "kevin_magnussen", "kubica": "robert_kubica", "kvyat": "daniil_kvyat",
    "latifi": "nicholas_latifi", "lawson": "liam_lawson", "leclerc": "charles_leclerc", "massa": "felipe_massa",
    "max_verstappen": "max_verstappen", "mazepin": "nikita_mazepin", "mick_schumacher": "mick_schumacher",
    "nasr": "felipe_nasr", "norris": "lando_norris", "ocon": "esteban_ocon", "perez": "sergio_perez",
    "piastri": "oscar_piastri", "pietro_fittipaldi": None, "raikkonen": "kimi_raikkonen", "resta": None,
    "ricciardo": "daniel_ricciardo", "rosberg": "nico_rosberg", "russell": "george_russell",
    "sainz": "carlos_sainz-jr", "sargeant": "logan_sargeant", "sirotkin": "sergey_sirotkin",
    "stroll": "lance_stroll", "tsunoda": "yuki_tsunoda", "vandoorne": "stoffel_vandoorne",
    "vettel": "sebastian_vettel", "wehrlein": "pascal_wehrlein", "zhou": "guanyu_zhou",
}
JOLPICA_TEAMS = {
    "mercedes": "mercedes", "red_bull": "redbull", "ferrari": "ferrari", "williams": "williams",
    "force_india": "force_india", "racing_point": "force_india", "aston_martin": "force_india",
    "renault": "lotus", "alpine": "lotus", "toro_rosso": "toro_rosso", "alphatauri": "toro_rosso",
    "rb": "toro_rosso", "sauber": "sauber", "alfa": "sauber", "mclaren": "mclaren", "haas": "haas",
    "manor": "manor", "lotus_f1": "lotus",
}
JOLPICA_DRIVERS.update({"antonelli": "andrea-kimi_antonelli", "bortoleto": "gabriel_bortoleto",
                        "hadjar": "isack_hadjar"})
NOT_IN_GAME = {"pietro_fittipaldi": "Pietro Fittipaldi", "resta": "Paul di Resta"}
CIRCUIT_TRACKS = {
    "albert_park": 0, "ricard": 1, "shanghai": 2, "bahrain": 3, "catalunya": 4, "monaco": 5, "villeneuve": 6,
    "silverstone": 7, "hungaroring": 9, "spa": 10, "monza": 11, "marina_bay": 12, "suzuka": 13, "yas_marina": 14,
    "americas": 15, "interlagos": 16, "red_bull_ring": 17, "sochi": 18, "rodriguez": 19, "baku": 20,
    "zandvoort": 26, "imola": 27, "portimao": 28, "jeddah": 29, "miami": 30, "vegas": 31, "losail": 32,
    "madring": 42, "hockenheimring": None, "sepang": None, "mugello": None, "nurburgring": None, "istanbul": None,
}
MIDSEASON_YEARS = range(2016, 2026)
STANDINGS_YEARS = range(2015, 2026)
PARTICIPANTS_2025 = {
    "redbull": {"max_verstappen": {"sessions": 24, "number": 1}, "yuki_tsunoda": {"sessions": 22, "number": 22},
                "liam_lawson": {"sessions": 2, "number": 30}},
    "toro_rosso": {"isack_hadjar": {"sessions": 24, "number": 6}, "liam_lawson": {"sessions": 22, "number": 30},
                   "yuki_tsunoda": {"sessions": 2, "number": 22}},
    "lotus": {"pierre_gasly": {"sessions": 24, "number": 10}, "franco_colapinto": {"sessions": 18, "number": 43},
              "jack_doohan": {"sessions": 6, "number": 7}},
}


def cs_logo_keys():
    """The team logo keys Classic Seasons adds to every array Choose Driver and Team Select read."""
    doc = json.loads(CS_LOGOS.read_text())
    per = {a: set() for a in LOGO_ARRAYS}
    for op in doc["ops"]:
        for e in op.get("edits", []):
            for a in LOGO_ARRAYS:
                if e.get("op") == "clone-entry" and e.get("at") == f"overrides.Contents.{a}.Contents":
                    per[a].add(e["as"])
    keys = set.intersection(*per.values())
    if not keys:
        raise SystemExit(f"{CS_LOGOS}: no team logo keys in {', '.join(LOGO_ARRAYS)}")
    return sorted(keys)


def write_json(path, doc):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


def participants_from_jolpica(folder):
    out = {}
    for year in CS_YEARS:
        rounds = json.loads((folder / f"qualifying-{year}.json").read_text())
        teams = {}
        for _, results in sorted(rounds.items(), key=lambda kv: int(kv[0])):
            for q in results:
                team = JOLPICA_TEAMS[q["Constructor"]["constructorId"]]
                jid = q["Driver"]["driverId"]
                if jid not in JOLPICA_DRIVERS:
                    raise SystemExit(f"{year}: Jolpica driver {jid} has no game label in JOLPICA_DRIVERS")
                label = JOLPICA_DRIVERS[jid] or f"missing:{jid}"
                entry = teams.setdefault(team, {}).setdefault(label, {"sessions": 0, "number": int(q["number"])})
                entry["sessions"] += 1
        out[str(year)] = teams
    out["2025"] = PARTICIPANTS_2025
    return {"source": "Classic Seasons build/jolpica qualifying-<year>.json (qualifying sessions per constructor); "
                      "2025 by hand",
            "not_in_game": NOT_IN_GAME, "years": out}


def rounds_from_jolpica(folder):
    out = {}
    for year in MIDSEASON_YEARS:
        cal = json.loads((folder / f"calendar-{year}.json").read_text())["MRData"]["RaceTable"]["Races"]
        races = {}
        for page in sorted(folder.glob(f"results-{year}-p*.json"), key=lambda p: int(p.stem.rsplit("-p", 1)[1])):
            for race in json.loads(page.read_text())["MRData"]["RaceTable"]["Races"]:
                races.setdefault(int(race["round"]), []).extend(race["Results"])
        rounds = []
        for race in cal:
            k = int(race["round"])
            cid = race["Circuit"]["circuitId"]
            if cid not in CIRCUIT_TRACKS:
                raise SystemExit(f"{year} round {k}: circuit {cid} has no entry in CIRCUIT_TRACKS")
            entries = {}
            for res in races.get(k, []):
                team = JOLPICA_TEAMS[res["Constructor"]["constructorId"]]
                jid = res["Driver"]["driverId"]
                if jid not in JOLPICA_DRIVERS:
                    raise SystemExit(f"{year}: Jolpica driver {jid} has no game label in JOLPICA_DRIVERS")
                label = JOLPICA_DRIVERS[jid] or f"missing:{jid}"
                entries.setdefault(team, []).append([label, int(res["number"])])
            rounds.append({"round": k, "circuit": cid, "race": race["raceName"], "date": race["date"],
                           "entries": {t: sorted(e) for t, e in sorted(entries.items())}})
        out[str(year)] = rounds
    return {"source": "Jolpica F1 API /<year>.json (calendar) and /<year>/results.json (race entries per round and "
                      "constructor), cached under /mnt/work/.progressive/jolpica",
            "not_in_game": NOT_IN_GAME, "years": out}


def standings_from_jolpica(folder):
    out, skipped = {}, {}
    for year in STANDINGS_YEARS:
        c = json.loads((folder / f"constructorStandings-{year}.json").read_text())["MRData"]
        d = json.loads((folder / f"driverStandings-{year}.json").read_text())["MRData"]
        cl = c["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"]
        dl = d["StandingsTable"]["StandingsLists"][0]["DriverStandings"]
        teams = []
        for x in cl:
            cid = x["Constructor"]["constructorId"]
            if cid not in JOLPICA_TEAMS:
                raise SystemExit(f"{year}: Jolpica constructor {cid} has no entry in JOLPICA_TEAMS")
            teams.append(JOLPICA_TEAMS[cid])
        drivers = []
        for x in dl:
            jid = x["Driver"]["driverId"]
            label = JOLPICA_DRIVERS.get(jid)
            if label:
                drivers.append(label)
            else:
                skipped.setdefault(str(year), []).append(jid)
        out[str(year)] = {"constructors": teams, "drivers": drivers}
    return {"source": "Jolpica F1 API /<year>/constructorStandings.json and /<year>/driverStandings.json (final "
                      "standings), cached under /mnt/work/.progressive/jolpica (fetch_standings.py); drivers not in "
                      "the game are left out", "not_in_game": skipped, "years": out}


def add_previous_standings(years, standings):
    for year in YEARS:
        prev = standings["years"].get(str(year - 1))
        if not prev:
            continue
        slots = []
        for key in prev["constructors"]:
            slot = SLOT_OF_CS_KEY[key]
            if slot not in slots:
                slots.append(slot)
        years[str(year)]["previous_standings"] = {"year": year - 1, "slots": slots, "drivers": prev["drivers"]}


def look_index(stock):
    """Every look a driver has for a career slot in a year: (year, slot, driver) -> (label, stat_id)."""
    out = {}
    for year in CS_YEARS:
        _, looks, _ = season_ops(year)
        for label, row in looks.items():
            rest = label.split("-", 1)[1]
            for key, slot in SLOT_OF_CS_KEY.items():
                if rest.startswith(f"cs_{key}_"):
                    out[(year, slot, rest[len(f"cs_{key}_"):])] = (label, int(row["stat_id"]))
    for label in stock.looks:
        head, _, rest = label.partition("-")
        if not head.isdigit() or not (2016 <= int(head) <= 2026):
            continue
        for team, slot in sorted(SLOT_OF_STOCK_TEAM.items(), key=lambda kv: -len(kv[0])):
            if rest.startswith(f"{team}_"):
                driver = rest[len(team) + 1:]
                if driver in stock.drivers and stock.stat(stock.looks, label) is not None:
                    out.setdefault((int(head), slot, driver), (label, stock.stat(stock.looks, label)))
                break
    return out


def change_look(looks, pool_by_driver, year, slot, driver):
    """The look a driver who comes in mid-season wears: his look for that team and year, else for that team in the
    nearest year, else his look that year at another team (each year away costs 1, another team 2.5)."""
    best = None
    for (y, s, d), (label, stat) in looks.items():
        if d != driver:
            continue
        score = abs(y - year) + (0 if s == slot else 2.5)
        key = (score, -y, label)
        if best is None or key < best[0]:
            best = (key, label, stat, "same team and year" if score == 0 else
                    f"{'same team' if s == slot else 'slot ' + str(s)} {y}")
    if best:
        return {"label": best[1], "stat_id": best[2], "from": best[3]}
    p = pool_by_driver.get(driver)
    if p:
        return {"label": p["default_look"]["label"], "stat_id": p["default_look"]["stat_id"], "from": "default look"}
    return None


def season_timeline(year, teams, rounds):
    """Per career slot and seat, the holder at each real round from the year's race entries. The season's line-up
    (teams[].drivers by position) is the start; a holder missing from a round keeps the seat unless a new driver
    raced for the team, and a driver not in the game never takes it."""
    seats, skipped = {}, []
    for t in teams:
        if t["kind"] != "real":
            continue
        seats[t["slot"]] = [d["driver"] for d in sorted(t["drivers"], key=lambda d: d["position"])]
    key_of = {t["slot"]: t["cs_key"] or SLOTS[t["slot"] - 1]["folder"] for t in teams if t["kind"] == "real"}
    holders = {slot: [list(base)] for slot, base in seats.items()}
    numbers = {}
    for r in rounds:
        for slot, base in seats.items():
            now = list(holders[slot][-1])
            entries = r["entries"].get(key_of[slot], [])
            raced = [d for d, _ in entries]
            for d, n in entries:
                numbers[(r["round"], d)] = n
            new = [d for d in raced if d not in now]
            gone = [i for i, d in enumerate(now) if d not in raced]
            for i in gone:
                if not new:
                    break
                back = [d for d in new if any(h[i] == d for h in holders[slot])]
                pick = back[0] if back else new[0]
                new.remove(pick)
                if pick.startswith("missing:"):
                    skipped.append({"round": r["round"], "circuit": r["circuit"], "slot": slot, "seat": i,
                                    "out": now[i], "not_in_game": NOT_IN_GAME[pick[len("missing:"):]],
                                    "reason": "not in the game; the seat keeps its driver"})
                    continue
                now[i] = pick
            holders[slot].append(now)
    by_round = {}
    for slot, hs in holders.items():
        for k in range(1, len(hs)):
            for d in hs[k]:
                if d in by_round.setdefault(k, {}):
                    raise SystemExit(f"{year} round {k}: {d} sits twice ({by_round[k][d]} and slot {slot})")
                by_round[k][d] = slot
    return holders, numbers, skipped


def season_changes(year, teams, rounds, looks, pool_by_driver):
    holders, numbers, skipped = season_timeline(year, teams, rounds)
    n = len(rounds)
    circuit = {r["round"]: r["circuit"] for r in rounds}
    seated = {k: {d for hs in holders.values() for d in hs[k]} for k in range(1, n + 1)}
    changes = []
    for slot in sorted(holders):
        hs = holders[slot]
        for seat in range(len(hs[0])):
            line = [h[seat] for h in hs]
            k = 1
            while k <= n:
                if line[k] == line[k - 1]:
                    k += 1
                    continue
                out, new = line[k - 1], line[k]
                m = k + 1
                while m <= n and line[m] == new:
                    m += 1
                until = m if m <= n and line[m] == out else None
                absent = all(out not in seated[j] for j in range(k, m))
                kind = "stand_in" if until and absent else "move" if any(
                    out in seated[j] for j in range(k, m)) else "replace"
                ch = {"id": f"{year}-r{k}-s{slot}{'ab'[seat]}-{new}", "round": k, "real_round_count": n,
                      "circuit": circuit[k], "track": CIRCUIT_TRACKS[circuit[k]], "slot": slot, "seat": seat,
                      "out": out, "in": new, "number": numbers[(k, new)], "kind": kind,
                      "look": change_look(looks, pool_by_driver, year, slot, new)}
                if until:
                    ch["until_round"] = until
                    ch["until_circuit"] = circuit[until]
                    ch["until_track"] = CIRCUIT_TRACKS[circuit[until]]
                else:
                    ch["permanent"] = True
                changes.append(ch)
                k = m + 1 if until else m
    changes.sort(key=lambda c: (c["round"], c["slot"], c["seat"]))
    return changes, skipped


def midseason(years, rounds_doc, stock, pool):
    looks = look_index(stock)
    pool_by_driver = {p["driver"]: p for p in pool}
    for year in MIDSEASON_YEARS:
        rounds = rounds_doc["years"][str(year)]
        y = years[str(year)]
        changes, skipped = season_changes(year, y["teams"], rounds, looks, pool_by_driver)
        y["calendar"] = [{"round": r["round"], "circuit": r["circuit"], "track": CIRCUIT_TRACKS[r["circuit"]]}
                         for r in rounds]
        y["changes"] = changes
        y["changes_skipped"] = skipped
    return years


def season_ops(year):
    doc = json.loads((CS / f"classic-seasons-{year}" / "db" / "season.json").read_text())
    vehicles, looks, packages = {}, {}, {}
    for o in doc["ops"]:
        if o["op"] != "add-row":
            continue
        table, row = o["table"], o["set"]
        if table == "entrants.vehicles":
            vehicles[row["_metadata_"]] = row
        elif table == "entrants.vehicles.vehicle_packages":
            packages[row["_metadata_"]] = row
        elif table.startswith("entrants.driver_customisations"):
            looks[row["_metadata_"]] = row
    return vehicles, looks, packages


class Stock:
    def __init__(self, db):
        self.db = db
        self.entrants = {db.get(f"{r}._metadata_"): r for r in db.rows(ENTRANTS, "entrant")}
        self.drivers = {db.get(f"{r}._metadata_"): r for r in db.rows("entrants.drivers", "driver")}
        self.looks = {db.get(f"{r}._metadata_"): r for r in db.rows(LOOK_FOLDER, LOOK_WORD)}
        self.teams = {db.get(f"{r}._metadata_"): r for r in db.rows("entrants.teams", "team")}
        self.vehicles = {db.get(f"{r}._metadata_"): r for r in db.rows("entrants.vehicles", "vehicle")}

    def stat(self, rows, label, field="stat_id"):
        r = rows.get(label)
        v = self.db.get(f"{r}.{field}") if r else None
        return int(v) if v not in (None, "") else None

    def lineup(self, list_label):
        r = self.db.row(LISTS, "entrant_list", list_label)
        out = []
        for e in self.db.rows(r, "entrant"):
            ref = self.db.get(f"{e}.entrant_reference")
            pool = self.entrants[ref]
            out.append({"team": self.db.get(f"{e}.team"), "vehicle": self.db.get(f"{e}.vehicle"),
                        "position": int(self.db.get(f"{e}.team_position")), "entrant": ref,
                        "driver": self.db.get(f"{pool}.driver"), "look": self.db.get(f"{pool}.driver_customisation"),
                        "number": int(self.db.get(f"{pool}.car_number"))})
        return out

    def team_names(self, label):
        r = self.teams[label]
        return {k: self.db.get(f"{r}.{k}") for k in ("display_name", "display_name_short", "display_name_super_short",
                                                    "display_name_code", "livery_name", "timing_tower_texture_path")}


def performance():
    perf = json.loads((CS_DATA / "performance.json").read_text())
    slots_2025 = json.loads((CS_DATA / "ai_slots.json").read_text())["slots"]
    stock_pace = json.loads((CS_DATA / "stock_pace.json").read_text())
    return perf, slots_2025, stock_pace


def ranked(gaps):
    order = sorted(gaps, key=lambda k: gaps[k])
    return {k: order.index(k) + 1 for k in gaps}


stand_in_numbers = {}


def new_driver_ids():
    data = json.loads((CS_DATA / "seasons.json").read_text())
    return {d: n for n, d in enumerate(data["new_drivers"], CS_DRIVER_STAT_BASE)}


def build_years(stock, participants):
    data = json.loads((CS_DATA / "seasons.json").read_text())
    new_drivers = set(data["new_drivers"])
    perf, slots_2025, stock_pace = performance()
    seasons = {s["year"]: s for s in data["seasons"]}
    out_years = {}
    raced = {}
    stand_in_numbers.clear()

    def look_entry(label, stat_id, source):
        return {"label": label, "stat_id": stat_id, "source": source}

    for year in YEARS:
        teams = []
        if year in CS_YEARS:
            vehicles, looks, packages = season_ops(year)
            season = seasons[year]
            p = perf[str(year)]
            ranks = ranked(p["teams"])
            yy = year % 100
            by_slot = {}
            for team in season["teams"]:
                key = team.get("key", team["folder"])
                slot = SLOT_OF_CS_KEY[key]
                label = f"cs{yy:02d}_{key}"
                v = vehicles[label]
                drivers = []
                for position, (driver, number) in enumerate(team["drivers"]):
                    look = f"{year}-cs_{key}_{driver}"
                    drivers.append({"driver": driver, "number": number, "position": position,
                                    "look": look_entry(look, int(looks[look]["stat_id"]), f"classic-seasons-{year}"),
                                    "rating": p["pace"].get(driver)})
                    raced.setdefault(driver, {})[year] = slot
                by_slot[slot] = {
                    "slot": slot, "kind": "real", "cs_key": key, "cs_team": label,
                    "name": team["name"], "code": team["code"], "lang": f"lng_cs_team_{yy:02d}_{key}",
                    "vehicle": {"label": label, "stat_id": None, "package": v["vehicle_package"],
                                "asset_filename": v.get("asset_filename", team["folder"]),
                                "performance_tier": int(v["performance_tier"]), "source": f"classic-seasons-{year}"},
                    "pace": {"gap_pct": p["teams"][key], "rank": ranks[key], "source": "classic-seasons performance.json"},
                    "drivers": drivers,
                }
            for s in SLOTS:
                if s["slot"] in by_slot:
                    teams.append(by_slot[s["slot"]])
        else:
            lineup = stock.lineup(STOCK_LISTS[year])
            by_slot = {}
            for e in lineup:
                base = e["team"][:-3] if e["team"].endswith("_26") else e["team"]
                slot = SLOT_OF_STOCK_TEAM[base]
                by_slot.setdefault(slot, []).append(e)
            gaps = {}
            for s in SLOTS:
                if s["slot"] in by_slot and s["folder"]:
                    gaps[s["slot"]] = slots_2025[s["folder"]]
            if year == 2026:
                gaps[11] = max(gaps.values())
            ranks = ranked(gaps)
            for s in SLOTS:
                if s["slot"] not in by_slot:
                    continue
                es = sorted(by_slot[s["slot"]], key=lambda e: e["position"])
                team_row = es[0]["team"]
                name, code = NAMES_STOCK[year][s["slot"]]
                drivers = []
                for e in es:
                    drivers.append({"driver": e["driver"], "number": e["number"], "position": e["position"],
                                    "look": look_entry(e["look"], stock.stat(stock.looks, e["look"]), "stock"),
                                    "rating": stock_pace.get(e["driver"])})
                    raced.setdefault(e["driver"], {})[year] = s["slot"]
                vehicle = es[0]["vehicle"]
                teams.append({
                    "slot": s["slot"], "kind": "real", "cs_key": None, "cs_team": None, "stock_team": team_row,
                    "name": name, "code": code, "lang": stock.team_names(team_row)["display_name"],
                    "vehicle": {"label": vehicle, "stat_id": stock.stat(stock.vehicles, vehicle),
                                "package": STOCK_LISTS[year], "asset_filename": stock.db.get(
                                    f"{stock.vehicles[vehicle]}.asset_filename"),
                                "performance_tier": int(stock.db.get(f"{stock.vehicles[vehicle]}.performance_tier")),
                                "source": "stock"},
                    "pace": {"gap_pct": gaps[s["slot"]], "rank": ranks[s["slot"]],
                             "source": "stock AI slot gap (classic-seasons ai_slots.json, 2025 cars)"
                             + ("; Cadillac set to the slowest" if s["slot"] == 11 else "")},
                    "drivers": drivers,
                })
        if year in FILLER_YEARS:
            fy, fkey = FILLER_FROM
            teams.append({"slot": 11, "kind": "filler", "cs_key": fkey, "cs_team": f"cs{fy % 100:02d}_{fkey}",
                          "name": "Manor", "code": "MRT", "lang": f"lng_cs_team_{fy % 100:02d}_{fkey}",
                          "vehicle": {"label": f"cs{fy % 100:02d}_{fkey}", "stat_id": None,
                                      "package": f"cs_{fy}_{fkey}", "asset_filename": None,
                                      "source": f"classic-seasons-{fy}"},
                          "pace": None, "drivers": []})
        teams.sort(key=lambda t: t["slot"])
        part = participants["years"].get(str(year), {})
        stand_ins = []
        for t in teams:
            if t["kind"] != "real":
                continue
            key = t["cs_key"] or SLOTS[t["slot"] - 1]["folder"]
            regular = {d["driver"] for d in t["drivers"]}
            for driver, entry in sorted(part.get(key, {}).items()):
                if driver not in regular:
                    missing = driver.startswith("missing:")
                    stand_ins.append({"slot": t["slot"], "driver": None if missing else driver,
                                      "not_in_game": NOT_IN_GAME[driver[len("missing:"):]] if missing else None,
                                      "sessions": entry["sessions"], "number": entry["number"]})
                    if not driver.startswith("missing:"):
                        raced.setdefault(driver, {}).setdefault(year, t["slot"])
        out_years[str(year)] = {"teams": teams, "stand_ins": stand_ins}
    for y in out_years.values():
        for st in y["stand_ins"]:
            if st["driver"]:
                stand_in_numbers.setdefault(st["driver"], {})[None] = st["number"]
    return out_years, raced, new_drivers


def add_development(years):
    dev = json.loads((ROOT / "data" / "development.json").read_text())["years"]
    for year, y in years.items():
        d = dev.get(year, {})
        for t in y["teams"]:
            curve = d.get("teams", {}).get(str(t["slot"])) if t["kind"] == "real" else None
            t["development"] = None if curve is None else {
                "checkpoints": curve["checkpoints"], "rounds": d["rounds"], "rounds_used": curve["rounds"],
                "source": "tools/development.py (Jolpica qualifying)"}


def pool_rows(stock, years, raced, new_drivers):
    grid_2026 = {e["driver"]: e for e in stock.lineup("f1_2026")}
    pool = []
    look_ids = iter(LOOK_STAT_IDS)
    for driver in sorted(raced, key=lambda d: (min(raced[d]), d)):
        looks, numbers = {}, {}
        for year, y in years.items():
            for t in y["teams"]:
                for d in t["drivers"]:
                    if d["driver"] == driver:
                        looks[year] = d["look"]
                        numbers[year] = d["number"]
        last_slot = raced[driver][max(raced[driver])]
        if driver in grid_2026:
            entrant, look, own = grid_2026[driver]["entrant"], grid_2026[driver]["look"], None
        else:
            stock_entrant = stock.entrants.get(driver)
            entrant = f"pg-{driver}"
            look = next((lab for lab in (
                [f"2025-{t}_{driver}" for t in SLOT_OF_STOCK_TEAM] + [f"2024-{t}_{driver}" for t in SLOT_OF_STOCK_TEAM])
                if lab in stock.looks), None)
            if look is None and stock_entrant and stock.db.get(f"{stock_entrant}.driver") == driver:
                look = stock.db.get(f"{stock_entrant}.driver_customisation")
            own = None
            if look is None:
                look = f"pg-{driver}"
                own = next(look_ids)
        number = numbers[max(numbers)] if numbers else stand_in_numbers.get(driver, {}).get(None)
        cs_ids = new_driver_ids()
        pool.append({
            "driver": driver,
            "stat_id": cs_ids[driver] if driver in cs_ids else stock.stat(stock.drivers, driver),
            "source": "classic-seasons shared package" if driver in new_drivers else "stock",
            "years": {str(y): s for y, s in sorted(raced[driver].items())},
            "on_2026_grid": driver in grid_2026,
            "entrant": entrant,
            "default_look": {"label": look, "stat_id": own if own is not None else stock.stat(stock.looks, look),
                             "own_row": own is not None, "seat_template": seat_look(stock, last_slot)},
            "looks": looks,
            "numbers": numbers,
            "number": number,
        })
    return pool


def seat_look(stock, slot):
    team = SLOTS[slot - 1]["stock_2025"] or "sauber"
    for lab in sorted(stock.looks):
        if lab.startswith(f"2025-{team}_"):
            return lab
    return LOOK_TEMPLATE


class Ops:
    def __init__(self, stock):
        self.stock = stock
        self.ops = []
        self.fields = {}

    def clone(self, container, word, label, values, by="_metadata_"):
        row = self.stock.db.row(container, word, label, by)
        self.stock.db.learn(values)
        fields = self.stock.db.fields(row)
        missing = [k for k in values if k not in fields]
        if missing:
            raise SystemExit(f"{container}.{word}[{by}={label}]: fields not on the source: {missing}")
        key = f"{container}.{word}"
        if self.fields.setdefault(key, fields) != fields:
            raise SystemExit(f"{key}: two sources with different fields")
        unsigned = self.stock.db.unsigned_t12(row, [f for f in fields if f not in values])
        self.add(container, f"{container}.{word}[{by}={label}]", {**unsigned, **values})

    def set(self, select, field, value):
        self.ops.append({"op": "set", "select": select, "field": field, "value": str(value)})

    def add(self, table, source, values, **extra):
        op = {"op": "add-row", "table": table, "from": source, "set": {k: str(v) for k, v in values.items()}}
        op.update(extra)
        self.ops.append(op)

    def append(self, select, field, value):
        self.ops.append({"op": "list-append", "select": select, "field": field, "value": value})


def career_ops(stock):
    db = stock.db
    o = Ops(stock)
    for table, word in DATA_GROUP_TABLES:
        o.clone(table, word, DATA_GROUP_SOURCE, {"_metadata_": DATA_GROUP})

    ev_table = "schedule.event_lists"
    ev = db.row(ev_table, "event_list", "F1_career_26")
    stock_seasons = db.count(ev, "season")
    o.clone(ev_table, "event_list", "F1_career_26", {"_metadata_": EVENT_LIST})
    sel = f"{ev_table}.event_list[_metadata_={EVENT_LIST}]"
    for n in range(stock_seasons, SEASONS):
        o.add(sel, f"{sel}.season[_metadata_=season_1]", {"_metadata_": f"season_{n}"})

    o.clone(SHARED, "SharedComponents", "shared_driver_career_26_components", {
        "_metadata_": SHARED_ROW, "ComAccolades_0.data_group": DATA_GROUP,
        "ComResourcePoints_0.data_group": DATA_GROUP, "ComSchedule_0.event_list": EVENT_LIST,
        "ComUpgrades_0.upgrades_path": "upgrades.2026",
    })
    champ = db.row("championships", "Championship", "F1_driver_career_26")
    shared = db.get(f"{champ}.shared_components").replace("shared_driver_career_26_components", SHARED_ROW)
    o.clone("championships", "Championship", "F1_driver_career_26", {
        "_metadata_": CHAMPIONSHIP, "statId": CHAMPIONSHIP_STAT_ID, "event_list": EVENT_LIST,
        "ComCareerSetup_0.entrant_list": DATA_GROUP, "ComCareerSetup_0.setup_data": DATA_GROUP,
        "ComInfluence_0.data_group": DATA_GROUP, "ComSpecialists_0.specialist_data": DATA_GROUP,
        "shared_components": shared,
    })
    setup = "championship_setups.ChampionshipSetupList_0.ChampionshipSetup_0"
    o.clone("game_modes", "ModeChampionship", "driver_career_26", {
        "_metadata_": MODE, "statId": MODE_STAT_ID, "save_data.name": MODE, "event_list": EVENT_LIST,
        f"{setup}.championship": CHAMPIONSHIP, f"{setup}.event_list": EVENT_LIST,
        "championships.ChampionshipReference_0.Championship": CHAMPIONSHIP,
    })

    cs = f"content_sets.content_set[_metadata_={CONTENT_SET}]"
    o.append(cs, "available_game_modes_0.game_mode_championships", MODE)
    official = [(f"F1CAREER_{SUFFIX}", "F1CAREER_26", "f1_2026"), (f"F2CAREER_{SUFFIX}", "F2CAREER_26", None),
                (f"ICONS_{SUFFIX}", "ICONS_26", POOL_LIST)]
    for year in YEARS:
        yy = f"{year % 100:02d}"
        grid = "f1_2026" if year == 2026 else YEAR_LIST.format(year=year)
        official += [(f"F1CAREER_{SUFFIX}{yy}", "F1CAREER_26", grid), (f"F2CAREER_{SUFFIX}{yy}", "F2CAREER_26", None),
                     (f"ICONS_{SUFFIX}{yy}", "ICONS_26", POOL_LIST)]
    for name, source, entrant_list in official:
        row = {"name": name}
        if entrant_list:
            row["entrant_list"] = entrant_list
        o.add(cs, f"{cs}.{OFFICIAL}[name={source}]", row)
    content = db.row("content_sets", "content_set", CONTENT_SET)
    o.fields[f"{cs}.{OFFICIAL}"] = db.fields(db.row(content, OFFICIAL, "F1CAREER_26", by="name"))
    return {"schema_version": 1, "fields": o.fields, "ops": o.ops}


def list_ids():
    names = ["career", "pool"] + [str(y) for y in YEARS if y != 2026]
    if len(names) > len(LIST_STAT_IDS):
        raise SystemExit("not enough reserved entrant list stat ids")
    return dict(zip(names, LIST_STAT_IDS))


def grid_ops(stock, years, pool):
    db = stock.db
    o = Ops(stock)
    ids = list_ids()
    list_from = f"{LISTS}.entrant_list[_metadata_={LIST_TEMPLATE}]"
    template_seats = db.count(db.row(LISTS, "entrant_list", LIST_TEMPLATE), "entrant")
    blank = {"badge_customisation": "", "vehicle_customisation": "", "driver": "", "driver_customisation": ""}

    for p in pool:
        if p["default_look"]["own_row"]:
            seat = stock.looks[p["default_look"]["seat_template"]]
            o.add(LOOKS, f"{LOOKS}.{LOOK_WORD}[_metadata_={LOOK_TEMPLATE}]", {
                "_metadata_": p["default_look"]["label"], "stat_id": p["default_look"]["stat_id"],
                "helmet": db.get(f"{seat}.helmet"), "suit": db.get(f"{seat}.suit"), "gloves": db.get(f"{seat}.gloves"),
                "linked_driver_stat_id": p["driver"], "available": "False",
            })
        if p["on_2026_grid"] or not p["entrant"].startswith("pg-"):
            continue
        o.add(ENTRANTS, f"{ENTRANTS}.entrant[_metadata_={ENTRANT_TEMPLATE}]", {
            "_metadata_": p["entrant"], "badge_customisation": "", "car_number": p["number"] or 255,
            "driver": p["driver"], "driver_customisation": p["default_look"]["label"], "entrant_reference": "",
            "team": "", "team_position": -1, "vehicle": "", "vehicle_customisation": "",
        })

    def fill(sel, rows):
        for n, values in enumerate(rows):
            if n < template_seats:
                for f, v in values.items():
                    o.set(f"{sel}.entrant_{n}", f, v)
            else:
                o.add(sel, f"{sel}.entrant_0", values)
        if len(rows) < template_seats:
            o.set(sel, "_entrant_instancecount_", len(rows))

    off_grid = [p for p in pool if not p["on_2026_grid"]]
    o.add(LISTS, list_from, {"_metadata_": POOL_LIST, "stat_id": ids["pool"], "entrant_list_references": "",
                             "display_name": "lng_pg_driver_pool"})
    fill(f"{LISTS}.entrant_list[_metadata_={POOL_LIST}]", [
        {"_metadata_": f"pg_pool_{p['driver']}", "entrant_reference": p["entrant"], "team": "", "vehicle": "",
         "team_position": -1, "car_number": 255, **blank} for p in off_grid])

    o.add(LISTS, list_from, {
        "_metadata_": DATA_GROUP, "stat_id": ids["career"], "display_name": "lng_pg_career",
        "entrant_list_references": f"f1_2026,{POOL_LIST},f1_career_2026_reserve_drivers,career_f2_2025_for_f1_2026",
        "previous_season_team_standings": "f1_teams_2025", "previous_season_driver_standings": "f1_drivers_2025",
        "leaderboard": "", "ui_path": "f1_2023", "_entrant_instancecount_": 0,
    })

    entrant_of = {p["driver"]: p["entrant"] for p in pool}
    for year in YEARS:
        if year == 2026:
            continue
        label = YEAR_LIST.format(year=year)
        o.add(LISTS, list_from, {"_metadata_": label, "stat_id": ids[str(year)], "display_name": f"lng_pg_f1_{year}",
                                 "available": "True"})
        rows = []
        for t in years[str(year)]["teams"]:
            if t["kind"] != "real":
                continue
            s = SLOTS[t["slot"] - 1]
            for d in t["drivers"]:
                rows.append({"_metadata_": f"pg{year % 100:02d}_{s['key']}_driver_0{d['position'] + 1}",
                             "team": s["team"], "vehicle": t["vehicle"]["label"],
                             "entrant_reference": entrant_of[d["driver"]], "team_position": d["position"],
                             "car_number": d["number"], **blank, "driver_customisation": d["look"]["label"]})
        fill(f"{LISTS}.entrant_list[_metadata_={label}]", rows)

    # The game loads an entrant list when its row has available = True (the per-year lists above) or when something
    # references it. The career list is referenced by the championship's ComCareerSetup, which must also pass the
    # content set's whitelist, row 0 of available_entrant_lists (the only row read); the pool comes through the
    # career list's references.
    cs = f"content_sets.content_set[_metadata_={CONTENT_SET}].available_entrant_lists_0"
    o.append(cs, "entrant_lists", f"{DATA_GROUP},{POOL_LIST}")
    return {"schema_version": 1, "fields": o.fields, "ops": o.ops}, ids


def look_of(pool_by_driver, years, year, driver):
    for t in years[str(year)]["teams"]:
        for d in t["drivers"]:
            if d["driver"] == driver:
                return d["look"]["label"]
    looks = {int(y): v for y, v in pool_by_driver[driver]["looks"].items()}
    if year in looks:
        return looks[year]["label"]
    for dist in range(1, YEARS.stop - YEARS.start):
        for y in (year - dist, year + dist):
            if y in looks:
                return looks[y]["label"]
    return pool_by_driver[driver]["default_look"]["label"]


def kit_looks(years, pool):
    by_driver = {p["driver"]: p for p in pool}
    out = {s["slot"]: [] for s in SLOTS}

    def add(slot, label):
        if label not in out[slot]:
            out[slot].append(label)

    for year in YEARS:
        if year == 2026:
            continue
        teams = years[str(year)]["teams"]
        seated = {d["driver"] for t in teams for d in t["drivers"]}
        for t in teams:
            for d in t["drivers"]:
                add(t["slot"], d["look"]["label"])
            if t["kind"] == "filler":
                near = {d["driver"] for y in (year - 1, year + 1) if str(y) in years
                        for tt in years[str(y)]["teams"] for d in tt["drivers"]}
                for driver in sorted(near - seated):
                    add(t["slot"], look_of(by_driver, years, year, driver))
    for p in pool:
        for slot in sorted(set(p["years"].values())):
            add(slot, p["default_look"]["label"])
    for year in YEARS:
        for ch in years[str(year)].get("changes", []):
            if ch["look"]:
                add(ch["slot"], ch["look"]["label"])
    return out


def kit_ops(stock, years, pool):
    db = stock.db
    folder = db.row(KIT_YEARS, "team_customisation_for_year", "2026")
    kit_of = {db.get(f"{r}.linked_team"): db.get(f"{r}._metadata_") for r in db.rows(folder, "team_customisation")}
    o = Ops(stock)
    for slot, labels in kit_looks(years, pool).items():
        kit = kit_of[SLOTS[slot - 1]["team"]]
        row = db.row(folder, "team_customisation", kit)
        stock_list = (db.get(f"{row}.official_driver_customisations") or "").split(",")
        new = [lab for lab in labels if lab not in stock_list]
        if new:
            o.append(f"{KITS}.team_customisation[_metadata_={kit}]", "official_driver_customisations", ",".join(new))
    return o.ops


def lang_ops():
    ops = [
        ("lng_pg_career_content", "Progressive Career"),
        ("lng_pg_career_content_description",
         "Start in any season from 2016 to 2026 as a real driver. Every new season brings that year's cars, "
         "liveries, helmets and line-ups; after 2026 the grid stays 2026."),
        ("lng_pg_career", "Progressive Career"),
        ("lng_pg_driver_pool", "Progressive Career drivers"),
    ] + [(f"lng_pg_f1_{y}", f"F1 {y}") for y in YEARS] + [(f"lng_pg_resume_{y}", f"Progressive - {y}") for y in YEARS]
    return {"schema_version": 1, "ops": [{"op": "lang.set", "language": "*", "key": k, "value": v} for k, v in ops]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=str(stockdb.DEFAULT))
    ap.add_argument("--jolpica", type=Path)
    ap.add_argument("--rounds", type=Path, help="Jolpica cache with calendar-<year>.json and results-<year>-p<n>.json "
                                                "(fetch_results.py); refreshes data/rounds.json")
    ap.add_argument("--standings", type=Path, help="Jolpica cache with constructorStandings-<year>.json and "
                                                   "driverStandings-<year>.json (fetch_standings.py); refreshes "
                                                   "data/standings.json")
    args = ap.parse_args()
    if args.jolpica:
        write_json(ROOT / "data" / "participants.json", participants_from_jolpica(args.jolpica))
    if args.rounds:
        write_json(ROOT / "data" / "rounds.json", rounds_from_jolpica(args.rounds))
    if args.standings:
        write_json(ROOT / "data" / "standings.json", standings_from_jolpica(args.standings))
    participants = json.loads((ROOT / "data" / "participants.json").read_text())
    rounds = json.loads((ROOT / "data" / "rounds.json").read_text())
    stock = Stock(stockdb.Db(args.db))
    years, raced, new_drivers = build_years(stock, participants)
    add_development(years)
    add_previous_standings(years, json.loads((ROOT / "data" / "standings.json").read_text()))
    pool = pool_rows(stock, years, raced, new_drivers)
    midseason(years, rounds, stock, pool)
    grid, ids = grid_ops(stock, years, pool)
    grid["ops"] += kit_ops(stock, years, pool)
    reserved = {
        "mode_statId": MODE_STAT_ID, "championship_statId": CHAMPIONSHIP_STAT_ID,
        "entrant_list_stat_ids": {**{DATA_GROUP: ids["career"], POOL_LIST: ids["pool"]},
                                  **{YEAR_LIST.format(year=y): ids[str(y)] for y in YEARS if y != 2026}},
        "entrant_list_range": [LIST_STAT_IDS.start, LIST_STAT_IDS.stop - 1],
        "look_stat_range": [LOOK_STAT_IDS.start, LOOK_STAT_IDS.stop - 1],
    }
    slots = []
    for s in SLOTS:
        slots.append({"slot": s["slot"], "key": s["key"], "team": s["team"],
                      "team_stat_id": stock.stat(stock.teams, s["team"]), "vehicle_2026": s["vehicle"],
                      "vehicle_2026_stat_id": stock.stat(stock.vehicles, s["vehicle"]),
                      "names_2026": stock.team_names(s["team"]), "cs_folder": s["folder"]})
    doc = {
        "schema_version": 1,
        "generated_by": "addons/progressive/tools/gen.py",
        "data_group": DATA_GROUP, "suffix": SUFFIX, "mode": MODE, "seasons_in_schedule": SEASONS,
        "years": list(YEARS), "slots": slots, "reserved": reserved,
        "notes": {
            "vehicle.stat_id": "null for Classic Seasons rows: the launcher gives them the next free id at build; "
                               "resolve the label with swofty::db at run time",
            "look.stat_id": "Classic Seasons sets look stat ids explicitly (420-601), stock looks are stock ids",
            "filler": "slot 11 in 2017-2025: the 2016 Manor car, drivers chosen at run time (DESIGN D7)",
            "logo_keys": "Classic Seasons team logo keys present in every team logo and badge array it extends "
                         "(tools/gen.py LOGO_ARRAYS); a season team's cs_team outside this list keeps its stock logo",
            "rating": "Classic Seasons performance.json pace (2016-2024), stock_pace.json (2025-2026); null if none",
            "development": "in-season pace of a real team relative to the year's car: checkpoints {at: 0..1 of the "
                           "season, delta_pct: qualifying gap % minus the team's season mean, > 0 slower}; null "
                           "without data (2026, the filler)",
            "changes": "per year 2016-2025, the real mid-season line-up changes from the Jolpica race entries "
                       "(data/rounds.json): from real round `round` (of `real_round_count`, at `circuit`, game track "
                       "`track`) `in` races `seat` (team position) of career `slot` in place of `out`, with his "
                       "real `number` and `look` (that team and year, else that team's nearest year, else his look "
                       "that year); `until_round` when `out` gets the seat back, else `permanent`. kind: stand_in "
                       "(`out` did not race meanwhile), move (`out` raced for another team), replace (`out` left). "
                       "The season's line-up (`teams`) is the start; `calendar` lists the real rounds",
            "changes_skipped": "real changes whose incoming driver is not in the game; the seat keeps its driver",
            "previous_standings": "the previous year's final standings (data/standings.json, Jolpica): `slots` the "
                                  "career team slots in constructors' order, `drivers` the drivers in game labels; "
                                  "the season-start order of the career's standings",
            "ratings": "per year, every driver of the season (line-up, stand-ins, mid-season changes): the base value "
                       "of each driver statistic (`stats`: name, the game's stat_id and overall weight) and the "
                       "overall rating they make, written into the career at each season's first Team HQ "
                       "(tools/ratings.py: 2026 the stock 2026 dataset, 2025 the stock default = EA's F1 25 launch "
                       "ratings, 2020-2024 EA's launch ratings of that season's game, 2016-2019 and drivers the "
                       "launch tables leave out the model of tools/ratings_model.py; focus stock)",
        },
        "seasons": years,
        "pool": pool,
        "logo_keys": cs_logo_keys(),
        "ratings": ratings.build(stock.db, years, performance()[0], rounds,
                                 json.loads((ROOT / "data" / "official_ratings.json").read_text())["years"],
                                 json.loads((ROOT / "data" / "ratings_model.json").read_text())),
    }
    write_json(ROOT / "data" / "years.json", doc)
    write_json(ROOT / "db" / "career.json", career_ops(stock))
    write_json(ROOT / "db" / "grid.json", grid)
    write_json(ROOT / "lang" / "career.json", lang_ops())
    own = sum(1 for p in pool if p["default_look"]["own_row"])
    print(f"years.json: {len(YEARS)} years, {len(pool)} pool drivers ({own} own looks), "
          f"career.json {len(career_ops(stock)['ops'])} ops, grid.json {len(grid['ops'])} ops")


if __name__ == "__main__":
    main()
