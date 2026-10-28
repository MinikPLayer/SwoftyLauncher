#!/usr/bin/env python3
"""Write data/titles.json: the stock event and season titles that carry the year 2026.

The race weekend titles (lng_track_<code>_gp_title_26 and _gp_title_multiline_26) and the season
titles (lng_f1_2026_championship_season, lng_f1_championship_season_26_loading_screen) of the 2026
content hold the year in their text. progressive.dll sets them with the career's year in a
Progressive career below 2026. The text comes from the stock language files the launcher extracts
into its baseline (language_<code>.lng); "all" holds the English text, "per" the languages whose
text differs from it.

  titles.py [--lang-dir DIR]
"""

import argparse
import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parents[1]
BASELINE = Path.home() / ".local" / "share" / "swofty" / "baseline"
KB_BUILD = "d873a1c8"
YEAR = "2026"
SEASON_KEYS = ("lng_f1_2026_championship_season", "lng_f1_championship_season_26_loading_screen")


def load_lng():
    spec = importlib.util.spec_from_file_location("check_modules", REPO / "tools" / "gamedb" / "check_modules.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.load_lng


def default_lang_dir():
    dirs = sorted(BASELINE.glob(f"*{KB_BUILD}*/lang")) or sorted(BASELINE.glob("*/lang"))
    if not dirs:
        raise SystemExit(f"no extracted language files under {BASELINE}; pass --lang-dir")
    return dirs[0]


def wanted(key):
    return key in SEASON_KEYS or (key.startswith("lng_track_") and
                                  (key.endswith("_gp_title_26") or key.endswith("_gp_title_multiline_26")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang-dir", type=Path)
    args = ap.parse_args()
    lang_dir = args.lang_dir or default_lang_dir()
    load = load_lng()
    tables = {}
    for path in sorted(lang_dir.glob("language_*.lng")):
        code = path.stem.split("_", 1)[1]
        tables[code] = {k: v for k, v in load(str(path)).items() if wanted(k) and YEAR in v}
    if "eng" not in tables or not tables["eng"]:
        raise SystemExit(f"{lang_dir}: no English titles with {YEAR}")
    eng = tables["eng"]
    per = {}
    for code, t in sorted(tables.items()):
        diff = {k: v for k, v in sorted(t.items()) if eng.get(k) != v}
        if diff:
            per[code] = diff
    doc = {
        "schema_version": 1,
        "generated_by": "addons/progressive/tools/titles.py",
        "source": f"stock language_<code>.lng of the {KB_BUILD} build",
        "year": YEAR,
        "all": dict(sorted(eng.items())),
        "per": per,
    }
    out = ROOT / "data" / "titles.json"
    out.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"titles.json: {len(eng)} key(s), {len(tables)} language(s), {sum(len(v) for v in per.values())} "
          f"per-language text(s) from {lang_dir}", file=sys.stderr)


if __name__ == "__main__":
    main()
