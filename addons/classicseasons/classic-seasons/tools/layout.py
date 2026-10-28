"""Where each season's addon lives, and the asset ops that ship its cars and suits.

The core addon (this folder) holds what every season shares. Each season is its own addon, a sibling folder
classic-seasons-<year>, whose cars/ holds the built archives (not committed). A team whose car comes from its own
archive instead of the season zip (a season's eleventh team, shipped in another team's slot) goes in a second package,
cx_<year>_vehicle_package, under that slot's folder, so its per-track AI data is the slot's. A season of 2016 to 2021
also ships its team suits (build_suits.py) from suits/ (not committed): each suit archive gets an asset group that
lists its stock base suit's archive first, which holds the shared body and glove meshes. A real driver suit's group
(the core's suits/) depends on its base driver's stock team group, which holds the body and glove models: without it
the grid's driver-in-car shot misses the glove model and the race crashes on the grid. The core addon ships the season
helmets of data/helmets.json (build_helmets.py) from its helmets/ (not committed), each group listing its base
helmet's archive first: a driver of any season can wear another season's helmet of his. A season ships the engine sound
bundles of its cars (sounds.py) from sounds/ (not committed) to audio/2025/bundles/.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = "f1_2025_vehicle_package"
STOCK_COMMON = f"2025_asset_groups/{SOURCE}/teams/common.erp"
SUIT_ROOT = "character_package/team_livery"
SUIT_YEARS = range(2016, 2022)
FOLDERS = ("mercedes", "redbull", "ferrari", "williams", "force_india", "lotus", "toro_rosso", "sauber", "mclaren",
           "haas")


def year_dir(year):
    return ROOT.parent / f"classic-seasons-{year}"


def cars_dir(year):
    return year_dir(year) / "cars"


def package(year):
    return f"cs_{year}_vehicle_package"


def extra_package(year):
    return f"cx_{year}_vehicle_package"


def suits(year):
    path = year_dir(year) / "suits" / "meta.json"
    if not path.exists():
        return {}
    built = json.loads(path.read_text())["suits"]
    return {k: v for k, v in built.items() if (year_dir(year) / "suits" / f"{v['label']}.erp").exists()}


def suit_paths(label):
    return (f"2025_asset_groups/{SUIT_ROOT}/{label}/{label}.erp", f"asset_groups/{SUIT_ROOT}/{label}/{label}.assetgroup")


def sounds(year):
    path = year_dir(year) / "sounds" / "meta.json"
    if not path.exists():
        return []
    return [m for m in json.loads(path.read_text())["bundles"] if (year_dir(year) / "sounds" / f"{m['name']}.bdl").exists()]


def sound_cars(year):
    path = year_dir(year) / "sounds" / "meta.json"
    return json.loads(path.read_text())["cars"] if path.exists() else []


def sound_asset_ops(year):
    return {"schema_version": 1, "ops": [{"op": "file.put", "source": f"sounds/{m['name']}.bdl",
                                          "target": f"audio/2025/bundles/{m['name']}.bdl"} for m in sounds(year)]}


def suit_asset_ops(year):
    ops = []
    for m in suits(year).values():
        target, group = suit_paths(m["label"])
        base, template = suit_paths(m["base"])
        ops.append({"op": "file.put", "source": f"suits/{m['label']}.erp", "target": target})
        ops.append({"op": "group.register", "group": group, "erps": [base, target], "template": template})
    return {"schema_version": 1, "ops": ops}


def helmets():
    path = ROOT / "helmets" / "meta.json"
    if not path.exists():
        return {}
    built = json.loads(path.read_text())["helmets"]
    return {(m["season"], m["driver"]): m for m in built if (ROOT / m["source"]).exists()}


def helmet_asset_ops():
    ops = []
    for m in helmets().values():
        ops.append({"op": "file.put", "source": m["source"], "target": m["target"]})
        ops.append({"op": "group.register", "group": m["group"], "erps": [m["base_erp"], m["target"]],
                    "template": m["template"]})
    return {"schema_version": 1, "ops": ops}


def real_suits():
    path = ROOT / "suits" / "meta.json"
    if not path.exists():
        return []
    return [m for m in json.loads(path.read_text())["suits"] if (ROOT / m["source"]).exists()]


def real_suit_asset_ops():
    ops = []
    for m in real_suits():
        for key in ("", "helmet_") if m["helmet"] else ("",):
            ops.append({"op": "file.put", "source": m[key + "source"], "target": m[key + "target"]})
            group = {"op": "group.register", "group": m[key + "group"], "erps": [m[key + "base_erp"], m[key + "target"]],
                     "template": m[key + "template"]}
            if "driver" in m and not key:
                team = m["base"].split("_", 1)[0]
                group["deps"] = [f"asset_groups/{SUIT_ROOT}/{team}/{team}.assetgroup"]
            ops.append(group)
        for rel in m["sidecars"]:
            ops.append({"op": "file.put", "source": f"suits/sidecars/{rel}", "target": rel})
    return {"schema_version": 1, "ops": ops}


def meta(year):
    path = cars_dir(year) / "meta.json"
    return json.loads(path.read_text()) if path.exists() else None


def built(year):
    m = meta(year)
    if not m:
        return None
    teams = cars_dir(year) / "teams"
    if m["common"] == "own" and not (teams / "common.erp").exists():
        return None
    if not all((teams / f / "wep" / f"{f}.erp").exists() for f in FOLDERS):
        return None
    return m


def asset_ops(year):
    pkg = package(year)
    m = meta(year)
    ops = []
    if m["common"] == "own":
        common = f"2025_asset_groups/{pkg}/teams/common.erp"
        ops.append({"op": "file.put", "source": "cars/teams/common.erp", "target": common})
    else:
        common = STOCK_COMMON
    for rel in m["sidecars"]:
        ops.append({"op": "file.put", "source": f"cars/sidecars/{rel}", "target": rel})
    if (cars_dir(year) / "incardriver" / "incardriver_f1.erp").exists():
        scenes = f"animation_package/scenes/incardriver/cs{year % 100:02d}/incardriver_f1"
        ops.append({"op": "file.put", "source": "cars/incardriver/incardriver_f1.erp",
                    "target": f"2025_asset_groups/{scenes}.erp"})
        ops.append({"op": "group.register", "group": f"asset_groups/{scenes}.assetgroup",
                    "erps": [f"2025_asset_groups/{scenes}.erp"],
                    "template": "asset_groups/animation_package/scenes/incardriver/2025/incardriver_f1.assetgroup"})
    for folder in FOLDERS:
        target = f"2025_asset_groups/{pkg}/teams/{folder}/wep/{folder}.erp"
        ops.append({"op": "file.put", "source": f"cars/teams/{folder}/wep/{folder}.erp", "target": target})
        ops.append({"op": "group.register", "group": f"asset_groups/{pkg}/teams/{folder}/wep/{folder}.assetgroup",
                    "erps": [common, target],
                    "template": f"asset_groups/{SOURCE}/teams/{folder}/wep/{folder}.assetgroup"})
    extra = extra_package(year)
    for folder in m.get("extra", []):
        target = f"2025_asset_groups/{extra}/teams/{folder}/wep/{folder}.erp"
        ops.append({"op": "file.put", "source": f"cars/extra/teams/{folder}/wep/{folder}.erp", "target": target})
        ops.append({"op": "group.register", "group": f"asset_groups/{extra}/teams/{folder}/wep/{folder}.assetgroup",
                    "erps": [common, target],
                    "template": f"asset_groups/{SOURCE}/teams/{folder}/wep/{folder}.assetgroup"})
    return {"schema_version": 1, "ops": ops}
