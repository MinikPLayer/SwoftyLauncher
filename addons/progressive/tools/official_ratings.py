#!/usr/bin/env python3
"""EA/Codemasters' published launch driver ratings of each season's game, for data/official_ratings.json.

    python3 tools/official_ratings.py [--cache /mnt/work/.progressive/ratings-sources]

Reads the cached pages (fetched with curl from SOURCES; the cache keeps each page under its URL with the scheme
dropped and / ? = & as _) and writes, per season, the game, the source URLs and per driver (game label) overall,
experience, racecraft, awareness and pace. A season's launch ratings are the season start; later in-season updates
are not used. Drivers a table leaves out (2020: McLaren's) get the model's ratings (tools/ratings_model.py).
"""
import argparse
import html
import json
import re
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "official_ratings.json"
CACHE = Path("/mnt/work/.progressive/ratings-sources")
STATS = ("overall", "experience", "racecraft", "awareness", "pace")
SOURCES = {
    "2020": {"game": "F1 2020", "format": "gpfans", "urls": [
        "https://www.gpfans.com/en/f1-news/53235/f1-2020-first-driver-ratings-revealed/",
        "https://www.gpfans.com/en/f1-news/53262/f1-2020-lewis-hamilton-driver-rating-revealed",
        "https://www.gpfans.com/en/f1-news/53294/f1-2020-ferrari-driver-ratings-revealed"],
        "note": "Codemasters' launch ratings as published team by team (June 2020); McLaren's were not found"},
    "2021": {"game": "F1 2021", "format": "o e r a p", "urls": ["https://boxthislap.org/f1-2021-ratings-revealed/"],
             "note": "launch ratings (July 2021)"},
    "2022": {"game": "F1 22", "format": "e r a p o", "urls": [
        "https://traxion.gg/f1-22-driver-ratings-unveiled-verstappen-hamilton-tied-at-the-top/"],
        "note": "launch ratings (24 June 2022, data through Monaco)"},
    "2023": {"game": "F1 23", "format": "o e r a p", "urls": [
        "https://traxion.gg/verstappen-leads-alonso-hamilton-in-initial-f1-23-driver-ratings/"],
        "note": "initial ratings (June 2023)"},
    "2024": {"game": "F1 24", "format": "e r a p o", "urls": [
        "https://clutchpoints.com/gaming/f1-24-roster-driver-ratings-max-verstappen-leads-the-charge"],
        "note": "launch ratings (May 2024)"},
    "2025": {"game": "F1 25", "format": "t o e r a p", "urls": [
        "https://insider-gaming.com/f1-25-ratings-all-driver-ratings-listed/",
        "https://traxion.gg/verstappen-heads-f1-25-games-driver-ratings-piastri-eighth/"],
        "note": "launch ratings (May 2025); the same as the stock gamedb's default dataset"},
}
SURNAMES = {
    "verstappen": "max_verstappen", "hamilton": "lewis_hamilton", "bottas": "valtteri_bottas",
    "leclerc": "charles_leclerc", "vettel": "sebastian_vettel", "norris": "lando_norris", "sainz": "carlos_sainz-jr",
    "ricciardo": "daniel_ricciardo", "ocon": "esteban_ocon", "gasly": "pierre_gasly", "kvyat": "daniil_kvyat",
    "perez": "sergio_perez", "stroll": "lance_stroll", "albon": "alex_albon", "raikkonen": "kimi_raikkonen",
    "giovinazzi": "antonio_giovinazzi", "magnussen": "kevin_magnussen", "grosjean": "romain_grosjean",
    "russell": "george_russell", "latifi": "nicholas_latifi", "alonso": "fernando_alonso",
    "tsunoda": "yuki_tsunoda", "schumacher": "mick_schumacher", "mazepin": "nikita_mazepin",
    "zhou": "guanyu_zhou", "hulkenberg": "nico_hulkenberg", "hulkenburg": "nico_hulkenberg",
    "piastri": "oscar_piastri", "sargeant": "logan_sargeant", "vries": "nyck_de-vries", "lawson": "liam_lawson",
    "bearman": "oliver_bearman", "bortoleto": "gabriel_bortoleto", "doohan": "jack_doohan",
    "antonelli": "andrea-kimi_antonelli", "hadjar": "isack_hadjar",
}


def cache_path(cache, url):
    name = re.sub(r"[/?=&]", "_", re.sub(r"^https?://", "", url))[:120]
    return cache / f"{name}.html"


def text_lines(path):
    t = path.read_text(encoding="utf-8", errors="replace")
    t = re.sub(r"<script.*?</script>|<style.*?</style>", " ", t, flags=re.S)
    t = re.sub(r"</t[dh]>", " | ", t)
    t = re.sub(r"</tr>", "\n", t)
    t = re.sub(r"<[^>]+>", " ", t)
    t = html.unescape(t)
    return [re.sub(r"\s+", " ", line).strip() for line in t.split("\n")]


def label_of(name):
    plain = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode().lower()
    words = re.findall(r"[a-z]+", plain)
    hits = {SURNAMES[w] for w in words if w in SURNAMES}
    if len(hits) != 1:
        raise SystemExit(f"official ratings: no single driver for {name!r} ({sorted(hits)})")
    return hits.pop()


def parse_gpfans(lines):
    text = " ".join(lines)
    rx = re.compile(r"((?:[A-Z][a-zA-Z]+ ){1,3})Overall: (\d+) - Experience: (\d+), Racecraft: (\d+), "
                    r"Awareness: (\d+), Pace: (\d+)")
    out = {}
    for m in rx.finditer(text):
        out[label_of(m.group(1))] = dict(zip(STATS, map(int, m.groups()[1:])))
    return out


def parse_table(lines, fmt):
    cols = fmt.split()
    rx = re.compile(r"^([^|]+?) \| " + r" \| ".join(r"([^|]+?)" if c == "t" else r"(\d{2})" for c in cols) + r" \|")
    names = {"o": "overall", "e": "experience", "r": "racecraft", "a": "awareness", "p": "pace"}
    out = {}
    for line in lines:
        m = rx.match(line)
        if not m:
            continue
        vals = [v for c, v in zip(cols, m.groups()[1:]) if c != "t"]
        out[label_of(m.group(1))] = {names[c]: int(v) for c, v in zip([c for c in cols if c != "t"], vals)}
    return out


def build(cache):
    years = {}
    for year, src in SOURCES.items():
        drivers = {}
        for url in src["urls"]:
            lines = text_lines(cache_path(cache, url))
            got = parse_gpfans(lines) if src["format"] == "gpfans" else parse_table(lines, src["format"])
            for label, v in got.items():
                if label in drivers and drivers[label] != v:
                    raise SystemExit(f"official ratings {year}: {label} differs between sources")
                drivers[label] = v
        if len(drivers) < 18:
            raise SystemExit(f"official ratings {year}: only {len(drivers)} drivers parsed")
        years[year] = {"game": src["game"], "sources": src["urls"], "note": src["note"],
                       "drivers": {k: drivers[k] for k in sorted(drivers)}}
    return {"generated_by": "addons/progressive/tools/official_ratings.py",
            "cache": "pages fetched with curl into /mnt/work/.progressive/ratings-sources (2026-10-27)",
            "years": years}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=CACHE)
    args = ap.parse_args()
    doc = build(args.cache)
    OUT.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(", ".join(f"{y}: {len(v['drivers'])}" for y, v in doc["years"].items()))


if __name__ == "__main__":
    main()
