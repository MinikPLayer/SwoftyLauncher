#!/usr/bin/env python3
"""Generate the Classic Seasons addons from data/seasons.json.

The shared addon gets the rows every season shares (new drivers and their statistics, head overrides) in db/core.json
and lang/core.json, and native/src/seasons.hpp, the boxes its plugin registers. Each season's addon (layout.py) gets
its manifest, its rows in db/season.json (with its entrant list appended to swofty.core's swofty_f1 host setup mode
and the content sets), its text in lang/season.json and its cars' asset ops in assets/cars.json.

The game looks up the AI and player car setups by vehicle package name in car_setup.presets, under
<row>.<ai|player>.<variant>.<slider>, and a package without a row gets setups that ground the cars. Each season copies
the f1_2025 presets under its own package name; data/setup_preset_fields.json lists the keys, which the database
catalogue does not name.

A track's f1 world has ten garages; the eleventh is in the f1_26 world. With a season of more than ten teams, the shared
addon moves swofty_f1 from the f1 track layer to a copy of the f1_26 one for the 2023 to 2025 seasons, which every
classic entrant list uses, so every season races on the f1_26 worlds.

The pit lane reads a team's pit stop times from the pit_lane.pit_times row whose team names it, so each season team
copies its stock team's row.

A vehicle's asset_filename names both its car folder ([FILENAME] in its package's resource_path) and its per-track AI
data. A team whose AI slot (aislots.assign) isn't its own folder gets a vehicle package of its own, cs_<year>_<key>,
whose resource_path names its car's archive outright, with a car_setup.presets row, and its vehicle's asset_filename
names the slot. Every vehicle sets performance_tier and tyre_wear_scale by its team's rank (aislots.ranked) instead of
keeping the stock team's.

Engine sounds (sounds.py, the season's sounds/meta.json): a season ships its bundles that the game lacks in
assets/sounds.json, adds an audio.audio_bundle_settings row for each, a copy of its maker's 2025 row, and puts each
car's audio.audio_car_data records under the case variant of its donor label that its carmodel layer's switchName
holds, with bundle_name the season bundle of the team's engine maker.

Suits of 2016 to 2023 (suit_sources.py): each driver's look wears the suit and gloves suit_sources.pick chooses, a
real suit (a mod's or F1 23's, built by build_real_suits.py into this addon's suits/, or a stock F1 25 one) or, for
2016 to 2021, the team's generated suit (build_suits.py, the season's suits/meta.json). A 2022 or 2023 look with no
built F1 23 suit keeps its seat's stock kit. A driver with a stock look of the season (<year>-<stock team>_<driver>,
the 2024 grid) wears that look's suit and gloves. The core addon copies
each built mod suit's suit, gloves and helmet rows from its base under its label, with item ids REAL_SUIT_IDS and
HELMET_IDS, and ships its archives (assets/suits.json). A season copies its generated suits' rows; every team-season of
SUIT_YEARS takes the next id of SUIT_IDS, in season and team order, whether its suit is generated or not. The suits of
mods converted by suit_transfer.py (a "transfer" mod) take TRANSFER_SUIT_IDS, the ids after SUIT_IDS, in
data/real_suits.json order. The stock suit and gloves ids are 448 to 973, the helmet ids 315 to 867. The build fails
when two suit, gloves or helmet copies share an id, or one takes a stock id (check_item_ids).

Every customisation item row holds colour_1_default to colour_4_default (tag 11), which the catalogue does not name.
Game_Build_CustomisationCatalogue reads all four at boot, and a copy without them crashes the game in
Sess_Apply_ContentAvailability. The files with copies list them in ITEM_FIELDS, and the suit and gloves copies set
each to "0": the 2025 suit and gloves rows hold "" in all four, and launchers up to 1.4.0 skip a copied "" for an int.
A helmet copy sets the base helmet's values (data/real_suits.json "helmet_colours"), with "0" for an empty one.

Helmets: a period helmet is a helmet of a driver's own from one season: a stock look of the season
(<year>-<stock team>_<driver>, the 2024 grid), a built mod or F1 23 driver suit's helmet (its season and driver in
data/real_suits.json) or a built season helmet of data/helmets.json (build_helmets.py; the last of these wins).
Each driver's look of a season wears his period helmet of the nearest season that has one, the earlier season on a
tie, else his own stock F1 25 helmet (data/stock_looks.json driver_looks), else his design of data/helmets.json
"designs": a copy cs-<driver> of a stock customisable helmet design (a livery_package tier design, no diffuse; its
nb_colours masks paint it) with colour_1_default to colour_3_default set to his helmet's colours and colour_4_default
"0". The build fails for a driver with none of these. Season helmets come from mods and from the F1 2016 to F1 22
game folders (build_helmets.py, old_helmets.py). The core addon holds the season helmets' rows and archives
(assets/helmets.json), then the rows of the designs a look wears. Item ids, in data/helmets.json order, an "off"
entry's id unused: the first FIRST_SEASON_HELMETS season helmets from HELMET_IDS.stop, then the designs, then the later
season helmets.
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import aislots
import heads
import ratings
import suit_sources
from layout import (ROOT, SUIT_YEARS, asset_ops, built, extra_package, helmet_asset_ops, helmets, real_suit_asset_ops,
                    real_suits, sound_asset_ops, sound_cars, sounds, suit_asset_ops, suits, year_dir)

DATA = json.loads((ROOT / "data" / "seasons.json").read_text())
STOCK = json.loads((ROOT / "data" / "stock_looks.json").read_text())
DRIVER_FIELDS = json.loads((ROOT / "data" / "driver_fields.json").read_text())
SETUP_PRESET_FIELDS = json.loads((ROOT / "data" / "setup_preset_fields.json").read_text())
TEAM_COLOURS = json.loads((ROOT / "data" / "team_colours.json").read_text())
REAL_HEADS = heads.shipped()
HEAD_ROWS = "entrants.animation.driver_heads"

CATEGORY = "options_system.options.multiplayer.options_folder_2.option[_metadata_=category]"
CATEGORY_FIELDS = ["_metadata_", "confirmation_dialogue", "description_override", "game_value", "is_default", "loc_tag",
                   "loc_tag_alternate", "vr_is_default"]
LOOKS = "entrants.driver_customisations.folder[_metadata_=official_driver_customisations]"
LOOK_ROW = LOOKS + ".preloaded_driver_customisation_properties"
SCENARIO_FIELDS = ["_metadata_", "display_name", "entrant_list", "game_setup_mode", "race_class", "statId",
                   "use_custom_vehicle_spec"]
F1_LISTS = ("f1_2024_story_prologue,f1_2024_story,f1_2025_story,f1_2025,f1_2025_with_konnersport,"
            "f1_2024_story_zenith_prologue,f1_2024_story_zenith_a,f1_2024_story_zenith_b,f1_2025_equal_performance,"
            "f1_custom_team_2025_equal_performance,f1_2025_with_zenith,f1_2026,f1_2026_equal_performance")
CONTENT_SETS = ("2025", "2026")
LOOK_STAT_BASE = 420
DRIVER_STAT_BASE = 225
SCENARIO_STAT_BASE = 15
TRACK_LAYER_STAT = 20
HOST_MODE = "swofty_f1"
SETUP_ROW = "entrants.entrant_setup_modes.entrant_setup_mode"
SETUP_PRESETS = "car_setup.presets.vehicle_package_setups"
SEASON_LAUNCHER = ">=1.5.1"
SEASON_VERSION = "0.6.4"
SEASON_CORE = "0.6.1"
DIRECT_SUITS = [e for e in suit_sources.REAL["suits"] if "transfer" not in suit_sources.REAL["mods"][e["mod"]]]
TRANSFERRED_SUITS = [e for e in suit_sources.REAL["suits"] if "transfer" in suit_sources.REAL["mods"][e["mod"]]]
REAL_SUIT_IDS = range(974, 974 + len(DIRECT_SUITS))
SUIT_IDS = range(REAL_SUIT_IDS.stop, REAL_SUIT_IDS.stop + sum(len(s["teams"]) for s in json.loads(
    (ROOT / "data" / "seasons.json").read_text())["seasons"] if s["year"] in SUIT_YEARS))
TRANSFER_SUIT_IDS = range(SUIT_IDS.stop, SUIT_IDS.stop + len(TRANSFERRED_SUITS))
HELMET_IDS = range(868, 868 + sum(1 for e in suit_sources.REAL["suits"] if e.get("helmet")))
HELMETS = "customisation.custom_avatar.helmets"
AUDIO_BUNDLES = "audio.audio_bundle_settings.audio_bundle_settings"
AUDIO_BUNDLE_FIELDS = ["_metadata_", "bundle_name", "era", "engine", "exhaust", "cabin"]
SEASON_HELMETS = json.loads((ROOT / "data" / "helmets.json").read_text())["helmets"]
FIRST_SEASON_HELMETS = 26
DESIGNS = json.loads((ROOT / "data" / "helmets.json").read_text())["designs"]
DESIGN_HELMET_IDS = {d: n for n, d in enumerate(DESIGNS, HELMET_IDS.stop + FIRST_SEASON_HELMETS)}
SEASON_HELMET_IDS = {(h["season"], h["driver"]): n + (len(DESIGNS) if i >= FIRST_SEASON_HELMETS else 0)
                     for i, (n, h) in enumerate(enumerate(SEASON_HELMETS, HELMET_IDS.stop)) if not h.get("off")}
STOCK_ITEM_IDS = {"suits": range(448, 974), "gloves": range(448, 974), "helmets": range(315, 868)}
COLOUR_FIELDS = [f"colour_{n}_default" for n in range(1, 5)]
SUITS = "customisation.custom_avatar.suits"
GLOVES = "customisation.custom_avatar.gloves"
ITEM_FIELDS = {f"{SUITS}.Suit": COLOUR_FIELDS, f"{GLOVES}.Gloves": COLOUR_FIELDS,
               "customisation.custom_avatar.helmets.Helmet": COLOUR_FIELDS}


def team_label(year, folder):
    return f"cs{year % 100:02d}_{folder}"


def look_label(year, folder, driver):
    return f"{year}-cs_{folder}_{driver}"


def placeholder_look(folder, position, driver, year):
    team = DATA["folders"][folder]["team"]
    period = STOCK["looks"].get(f"{year}-{team}_{driver}")
    if period:
        return f"{year}-{team}_{driver}", period["helmet"], period["suit"], period["gloves"]
    seat = STOCK["seat_looks"][team][str(position)]
    own = STOCK["driver_looks"].get(driver)
    helmet_from = STOCK["looks"][own] if own else STOCK["looks"][seat]
    suit_from = STOCK["looks"][seat]
    return seat, helmet_from["helmet"], suit_from["suit"], suit_from["gloves"]


def head_label(head):
    head = heads.worn(head)
    return heads.label(head) if head in REAL_HEADS else head


def colour(hex_rgb):
    return str((0xFF000000 | int(hex_rgb[1:], 16)) - (1 << 32))


def team_colours(team, label):
    wear = TEAM_COLOURS[label]["team_wear"]
    return {"primary_colour": colour(team["colour"]), "ui_team_colour": colour(team["colour"]),
            "secondary_colour": colour(wear[1]), "tertiary_colour": colour(wear[2]),
            **{f"team_wear_{slot}_colour": colour(c) for slot, c in zip(("primary", "secondary", "tertiary"), wear)}}


def lang(key, value):
    return {"op": "lang.set", "language": "*", "key": key, "value": value}


def write_native_seasons(years):
    rows = "\n".join(f'    {{"cs_f1_{year}", "lng_cs_f1_{year}", "#overrides.home_screen.tiles_solo_6x6[cs_f1_{year}]", {year}}},'
                     for year in years)
    (ROOT / "native" / "src" / "seasons.hpp").write_text(
        "// Generated by tools/gen.py from data/seasons.json.\n#pragma once\n\n#include <cstdint>\n\n"
        "namespace classic_seasons {\n\n"
        "struct Season {\n    const char *entrant_list;\n    const char *title;\n    const char *image;\n"
        "    int32_t order;\n};\n\n"
        f"inline constexpr Season kSeasons[] = {{\n{rows}\n}};\n\n}}\n")


def check_item_ids(files):
    seen = {}
    for name, ops in files:
        for o in ops:
            table = o.get("table", "")
            if o["op"] != "add-row" or not table.startswith("customisation.custom_avatar."):
                continue
            kind, item_id, label = table.rsplit(".", 1)[1], o["set"]["id"], o["set"]["_metadata_"]
            if int(item_id) in STOCK_ITEM_IDS.get(kind, ()):
                raise SystemExit(f"{kind} id {item_id} of {label} ({name}) is a stock id")
            if (kind, item_id) in seen:
                raise SystemExit(f"{kind} id {item_id}: {seen[kind, item_id]} and {label} ({name})")
            seen[kind, item_id] = f"{label} ({name})"
    return len(seen)


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")


def suit_rows(m, item_id):
    label, base = m["label"], m["base"]
    colours = {f: "0" for f in COLOUR_FIELDS}
    pattern = f"eaid://[CHARACTER_PACKAGE]/team_livery/{label}/idf/{label}"
    group = f"asset_groups/[CHARACTER_PACKAGE]/team_livery/{label}/{label}.assetgroup"
    return [{"op": "add-row", "table": SUITS, "from": f"{SUITS}.Suit[_metadata_={base}]",
             "set": {"_metadata_": label, "asset_group": group, "feet_model": f"{pattern}_male",
                     "logos_model_override_pattern": f"{pattern}_body_logos_[GENDER]", "material_override": label,
                     "id": str(item_id), **colours}},
            {"op": "add-row", "table": GLOVES, "from": f"{GLOVES}.Gloves[_metadata_={base}]",
             "set": {"_metadata_": label, "asset_group": group, "logos_model": f"{pattern}_glove_logos_[GENDER]",
                     "material_override": label, "id": str(item_id), **colours}}]


def real_rows(m, item_id, helmet_id):
    label, base = m["label"], m["base"]
    if "driver" not in m:
        return suit_rows(m, item_id)
    colours = {f: "0" for f in COLOUR_FIELDS}
    folder = f"[CHARACTER_PACKAGE]/drivers/male/{label}"
    stem = m["stem"]
    logos = {k: f"eaid://[CHARACTER_PACKAGE]/{m[k]}" for k in ("logos_model", "glove_logos_model") if k in m}
    ops = [{"op": "add-row", "table": SUITS, "from": f"{SUITS}.Suit[_metadata_={base}]",
            "set": {"_metadata_": label, "asset_group": f"asset_groups/{folder}/{stem}.assetgroup",
                    "feet_model": f"eaid://{folder}/idf/{stem}",
                    "logos_model_override_pattern": logos.get("logos_model", f"eaid://{folder}/idf/{stem}_body_logos"),
                    "material_override": label, "id": str(item_id), **colours}},
           {"op": "add-row", "table": GLOVES, "from": f"{GLOVES}.Gloves[_metadata_={base}]",
            "set": {"_metadata_": label, "asset_group": f"asset_groups/{folder}/{stem}.assetgroup",
                    "logos_model": logos.get("glove_logos_model", f"eaid://{folder}/idf/{stem}_glove_logos"),
                    "material_override": label,
                    "id": str(item_id), **colours}}]
    if m["helmet"]:
        ops.append({"op": "add-row", "table": HELMETS, "from": f"{HELMETS}.Helmet[_metadata_={base}]",
                    "set": {"_metadata_": label, "asset_group": f"asset_groups/{folder}/{stem}_helmet.assetgroup",
                            "attachments_model": f"eaid://{folder}/idf/{stem}_helmet_attachments",
                            "material_override": label, "id": str(helmet_id),
                            **dict(zip(COLOUR_FIELDS, m["helmet_colours"]))}})
    return ops


def helmet_row(m):
    folder = f"[CHARACTER_PACKAGE]/drivers/male/{m['label']}"
    return {"op": "add-row", "table": HELMETS, "from": f"{HELMETS}.Helmet[_metadata_={m['base']}]",
            "set": {"_metadata_": m["label"], "asset_group": f"asset_groups/{folder}/{m['stem']}_helmet.assetgroup",
                    "attachments_model": f"eaid://{folder}/idf/{m['stem']}_helmet_attachments",
                    "material_override": m["label"], "id": str(SEASON_HELMET_IDS[m["season"], m["driver"]]),
                    **dict(zip(COLOUR_FIELDS, m["helmet_colours"]))}}


def period_helmets(made, season_helmets):
    out = {}
    for season in DATA["seasons"]:
        for team in season["teams"]:
            stock = DATA["folders"][team["folder"]]["team"]
            for driver, _ in team["drivers"]:
                look = STOCK["looks"].get(f"{season['year']}-{stock}_{driver}")
                if look:
                    out[season["year"], driver] = look["helmet"]
    for e in suit_sources.real():
        if e["kind"] == "mod" and e["helmet_label"] in made and "driver" in e:
            out[e["season"], e["driver"]] = e["helmet_label"]
    for (year, driver), m in season_helmets.items():
        out[year, driver] = m["label"]
    return out


def nearest_helmet(period, year, driver):
    own = sorted((abs(y - year), y, label) for (y, d), label in period.items() if d == driver)
    return own[0][2] if own else None


def design_label(driver):
    return f"cs-{driver}"


def design_row(driver):
    d = DESIGNS[driver]
    return {"op": "add-row", "table": HELMETS, "from": f"{HELMETS}.Helmet[_metadata_={d['design']}]",
            "set": {"_metadata_": design_label(driver), "id": str(DESIGN_HELMET_IDS[driver]),
                    **dict(zip(COLOUR_FIELDS, [colour(c) for c in d["colours"]] + ["0"]))}}


def own_helmet(driver):
    look = STOCK["driver_looks"].get(driver)
    if look:
        return STOCK["looks"][look]["helmet"]
    if driver in DESIGNS:
        return design_label(driver)
    raise SystemExit(f"{driver}: no period helmet, stock helmet or design in data/helmets.json")


def season_manifest(core, year, assets):
    return {
        "schema_version": 1,
        "id": f"{core['id']}-{year}",
        "name": f"F1 {year} GP/TT Addon",
        "version": SEASON_VERSION,
        "author": core["author"],
        "description": f"The F1 {year} season for Classic Seasons: its cars, teams and drivers in Grand Prix and "
                       f"Time Trial. Cars by fanfreluche.",
        "license": core["license"],
        "icon": "icon.png",
        "category": "seasons",
        "kind": ["database", "files"],
        "compat": {**core["compat"], "launcher": SEASON_LAUNCHER},
        "requires": core["requires"] + [{"id": core["id"], "version": f">={SEASON_CORE}"}],
        "operations": {"database": ["db/season.json"], "assets": ["lang/season.json", *assets]},
    }


def build():
    core = json.loads((ROOT / "swofty.json").read_text())
    ops, text = [], []
    look_stat = LOOK_STAT_BASE
    suit_ids = iter(SUIT_IDS)
    seasons = []

    for head in REAL_HEADS:
        ops.append(heads.row_op(HEAD_ROWS, head))

    for driver_stat, (label, d) in enumerate(DATA["new_drivers"].items(), DRIVER_STAT_BASE):
        base = f"lng_cs_driver_{label}"
        ops.append({"op": "add-row", "table": "entrants.drivers",
                    "from": "entrants.drivers.driver[_metadata_=lando_norris]",
                    "set": {"_metadata_": label, "first_name": base + "_first", "last_name": base + "_last",
                            "full_name": base + "_full", "last_name_standard": "", "initials": d["initials"],
                            "nationality": d["nationality"],
                            "head": head_label(d["head"]), "audio_name": "DefaultName", "expression_label": "",
                            "stat_id": str(driver_stat)}})
        text += [lang(base + "_first", d["first"]), lang(base + "_last", d["last"].upper()),
                 lang(base + "_full", f"{d['first']} {d['last'].upper()}")]

    for label, head in DATA["head_overrides"].items():
        ops.append({"op": "set", "select": f"entrants.drivers.driver[_metadata_={label}]", "field": "head",
                    "value": head_label(head)})

    for label in list(DATA["new_drivers"]) + DATA["holders_for"]:
        ops.append({"op": "add-row", "table": "driver_statistics.driver_statistics",
                    "from": "driver_statistics.driver_statistics.DriverStatisticsHolder[_metadata_=nyck_de-vries]",
                    "set": {"_metadata_": label, "driver": label}})
    ops += ratings.dataset_ops()

    if any(len(season["teams"]) > 10 for season in DATA["seasons"]):
        ops.append({"op": "add-row", "table": "track_data.track_layers",
                    "from": "track_data.track_layers.TrackLayer[_metadata_=f1_26]",
                    "set": {"_metadata_": HOST_MODE, "game_setup_modes": HOST_MODE,
                            "seasons": "season_2023,season_2024,season_2025", "statId": str(TRACK_LAYER_STAT)}})
        ops.append({"op": "list-remove", "select": "track_data.track_layers.TrackLayer[_metadata_=f1]",
                    "field": "game_setup_modes", "value": HOST_MODE})

    made = {m["label"]: m for m in real_suits()}
    helmet_ids = iter(HELMET_IDS)
    for item_id, entry in [*zip(REAL_SUIT_IDS, DIRECT_SUITS), *zip(TRANSFER_SUIT_IDS, TRANSFERRED_SUITS)]:
        helmet_id = next(helmet_ids) if entry.get("helmet") else None
        m = made.get(suit_sources.mod_label(entry))
        if m:
            ops += real_rows(m, item_id, helmet_id)
    season_helmets = helmets()
    ops += [helmet_row(m) for m in season_helmets.values()]
    period = period_helmets(made, season_helmets)
    designs = set()

    core_ops, core_text = ops, text
    for index, season in enumerate(DATA["seasons"]):
        year = season["year"]
        yy = year % 100
        list_label = f"cs_f1_{year}"
        standings = f"cs_f1_teams_{year}"
        ops, text = [], []
        seasons.append((year, ops, text))
        text.append(lang(f"lng_cs_f1_{year}", f"F1 {year}"))
        for m in sounds(year):
            ops.append({"op": "add-row", "table": "audio.audio_bundle_settings",
                        "from": f"{AUDIO_BUNDLES}[_metadata_={m['maker']}_2025]",
                        "set": {"_metadata_": m["name"], "bundle_name": m["name"]}})
        for car in sound_cars(year):
            ops += [{"op": "put-record", **record} for record in car["records"]]
        cars = built(year)
        if cars:
            pkg = f"cs_{year}_vehicle_package"
            shared = pkg if cars["common"] == "own" else "f1_2025_vehicle_package"
            animation = "f1_2025"
            if (year_dir(year) / "cars" / "incardriver" / "incardriver_f1.erp").exists():
                animation = f"cs_{year}"
                ops.append({"op": "add-row", "table": "entrants.animation.driver_animation_sets",
                            "from": "entrants.animation.driver_animation_sets.driver_animation_set[_metadata_=f1_2025]",
                            "set": {"_metadata_": animation, "in_car_asset_group":
                                    f"[ANIMATION_PACKAGE]/scenes/incardriver/cs{yy:02d}/incardriver_f1"}})
            ops.append({"op": "add-row", "table": "entrants.vehicles.vehicle_packages",
                        "from": "entrants.vehicles.vehicle_packages.vehicle_package[_metadata_=f1_2025]",
                        "set": {"_metadata_": f"cs_{year}", "resource_path": f"{pkg}/teams/[FILENAME]/wep/[FILENAME]",
                                "blob_shadow_decal": f"eaid://{shared}/shared/ao/materials/blobshadowdecal.material",
                                "driver_animation_set": animation,
                                "has_cockpit_halo": str(season.get("halo", True))}})
            ops.append({"op": "add-row", "table": "car_setup.presets",
                        "from": f"{SETUP_PRESETS}[_metadata_=f1_2025_vehicle_package]", "set": {"_metadata_": pkg}})
            if cars.get("extra"):
                extra = extra_package(year)
                ops.append({"op": "add-row", "table": "entrants.vehicles.vehicle_packages",
                            "from": "entrants.vehicles.vehicle_packages.vehicle_package[_metadata_=f1_2025]",
                            "set": {"_metadata_": f"cx_{year}",
                                    "resource_path": f"{extra}/teams/[FILENAME]/wep/[FILENAME]",
                                    "blob_shadow_decal": f"eaid://{shared}/shared/ao/materials/blobshadowdecal.material",
                                    "driver_animation_set": animation,
                                    "has_cockpit_halo": str(season.get("halo", True))}})
                ops.append({"op": "add-row", "table": "car_setup.presets",
                            "from": f"{SETUP_PRESETS}[_metadata_=f1_2025_vehicle_package]",
                            "set": {"_metadata_": extra}})

        for team in season["teams"]:
            folder = team["folder"]
            key = team.get("key", folder)
            stock = DATA["folders"][folder]
            generated = suit_sources.pick(year, team, made=set(made)) is None
            suit = suits(year).get(key) if generated else None
            item_id = next(suit_ids) if year in SUIT_YEARS else None
            if suit:
                ops += suit_rows(suit, item_id)
            label = team_label(year, key)
            name_key = f"lng_cs_team_{yy:02d}_{key}"
            ops.append({"op": "add-row", "table": "entrants.teams",
                        "from": f"entrants.teams.team[_metadata_={stock['team']}]",
                        "set": {"_metadata_": label, "display_name": name_key, "display_name_short": name_key,
                                "display_name_super_short": name_key, "display_name_code": team["code"],
                                "timing_tower_texture_path": label, **team_colours(team, label)}})
            ops.append({"op": "add-row", "table": "pit_lane.pit_times",
                        "from": f"pit_lane.pit_times.team_pit_times[_metadata_={stock['team']}]",
                        "set": {"_metadata_": label, "team": label}})
            text.append(lang(name_key, team["name"]))
            vehicle = {"_metadata_": label, **aislots.ranked(year, key)}
            if cars:
                vehicle["vehicle_package"] = f"cx_{year}" if "car" in team else f"cs_{year}"
                slot = aislots.assign(year)[key]
                if slot != folder:
                    own = f"cs_{year}_{key}"
                    archive = extra_package(year) if "car" in team else pkg
                    ops.append({"op": "add-row", "table": "entrants.vehicles.vehicle_packages",
                                "from": "entrants.vehicles.vehicle_packages.vehicle_package[_metadata_=f1_2025]",
                                "set": {"_metadata_": own, "resource_path": f"{archive}/teams/{folder}/wep/{folder}",
                                        "blob_shadow_decal":
                                            f"eaid://{shared}/shared/ao/materials/blobshadowdecal.material",
                                        "driver_animation_set": animation,
                                        "has_cockpit_halo": str(season.get("halo", True))}})
                    ops.append({"op": "add-row", "table": "car_setup.presets",
                                "from": f"{SETUP_PRESETS}[_metadata_=f1_2025_vehicle_package]",
                                "set": {"_metadata_": f"{own}_vehicle_package"}})
                    vehicle.update(vehicle_package=own, asset_filename=slot)
            ops.append({"op": "add-row", "table": "entrants.vehicles",
                        "from": f"entrants.vehicles.vehicle[_metadata_={stock['vehicle']}]", "set": vehicle})

            for position, (driver, number) in enumerate(team["drivers"]):
                seat, helmet, suit_look, gloves = placeholder_look(folder, position, driver, year)
                if year in suit_sources.REAL_YEARS:
                    choice = suit_sources.pick(year, team, driver, set(made))
                    if choice:
                        suit_look = gloves = choice["label"]
                    elif suit:
                        suit_look = gloves = suit["label"]
                helmet = nearest_helmet(period, year, driver) or own_helmet(driver)
                if helmet == design_label(driver):
                    designs.add(driver)
                look = look_label(year, key, driver)
                ops.append({"op": "add-row", "table": LOOKS, "from": f"{LOOK_ROW}[_metadata_={seat}]",
                            "set": {"_metadata_": look, "helmet": helmet, "suit": suit_look, "gloves": gloves,
                                    "linked_driver_stat_id": driver, "available": "False",
                                    "stat_id": str(look_stat)}})
                look_stat += 1
                ops.append({"op": "add-row", "table": "entrants.entrant_lists.entrants",
                            "from": "entrants.entrant_lists.entrants.entrant[_metadata_=max_verstappen]",
                            "set": {"_metadata_": f"cs{year}-{driver}", "driver": driver,
                                    "driver_customisation": look, "car_number": str(number)}})

        grid = sum(len(team["drivers"]) for team in season["teams"])
        wide = grid > 20
        ops.append({"op": "add-row", "table": "entrants.standings",
                    "from": f"entrants.standings.standings[_metadata_={'f1_teams_2026' if wide else 'f1_teams_2024'}]",
                    "set": {"_metadata_": standings}})
        for position, team in enumerate(season["teams"]):
            ops.append({"op": "set", "select": f"entrants.standings.standings[_metadata_={standings}].team_reference_{position}",
                        "field": "team", "value": team_label(year, team.get("key", team["folder"]))})

        # Time Trial's stock F1 2025 box loads the cars of every available list on the f1 leaderboard, which with
        # all years installed overflows the engine sound heap; an empty leaderboard crashes Time Trial itself.
        entrant_list = {"_metadata_": list_label, "display_name": f"lng_cs_f1_{year}",
                        "previous_season_team_standings": standings, "previous_season_driver_standings": "",
                        "leaderboard": "classic"}
        if wide:
            entrant_list.update(season="season_2025", ui_path="f1_2023")
            ops.append({"op": "set", "select": f"{SETUP_ROW}[_metadata_={HOST_MODE}]", "field": "max_entrants",
                        "value": str(grid)})
        ops.append({"op": "add-row", "table": "entrants.entrant_lists",
                    "from": f"entrants.entrant_lists.entrant_list[_metadata_={'f1_2026' if wide else 'f1_2025'}]",
                    "set": entrant_list})
        slot = 0
        for team in season["teams"]:
            label = team_label(year, team.get("key", team["folder"]))
            for position, (driver, _number) in enumerate(team["drivers"]):
                row = f"entrants.entrant_lists.entrant_list[_metadata_={list_label}].entrant_{slot}"
                for field, value in (("_metadata_", f"{label}_driver_0{position + 1}"), ("team", label),
                                     ("vehicle", label), ("entrant_reference", f"cs{year}-{driver}"),
                                     ("team_position", str(position)), ("car_number", "255"),
                                     ("driver", ""), ("driver_customisation", "")):
                    ops.append({"op": "set", "select": row, "field": field, "value": value})
                slot += 1
        if slot != (22 if wide else 20):
            raise SystemExit(f"{year}: {slot} entrants, the f1_2025 list has 20 and the f1_2026 list 22")

        ops.append({"op": "add-row", "table": "game_setup.scenarios",
                    "from": "game_setup.scenarios.scenario[_metadata_=f1]",
                    "set": {"_metadata_": list_label, "display_name": f"lng_cs_f1_{year}",
                            "entrant_list": list_label, "game_setup_mode": HOST_MODE,
                            "statId": str(SCENARIO_STAT_BASE + index)}})
        ops.append({"op": "add-row", "table": CATEGORY, "from": f"{CATEGORY}.value[_metadata_=f1_2025]",
                    "set": {"_metadata_": list_label, "game_value": list_label, "loc_tag": f"lng_cs_f1_{year}"}})
        for field in ("entrant_lists", "selectable_entrant_lists"):
            ops.append({"op": "list-append", "select": f"{SETUP_ROW}[_metadata_={HOST_MODE}]", "field": field,
                        "value": list_label})
        for content_set in CONTENT_SETS:
            ops.append({"op": "list-append",
                        "select": f"content_sets.content_set[_metadata_={content_set}].available_entrant_lists_0",
                        "field": "entrant_lists", "value": list_label})

    fields = {"game_setup.scenarios.scenario": SCENARIO_FIELDS, "entrants.drivers.driver": DRIVER_FIELDS,
              CATEGORY + ".value": CATEGORY_FIELDS}
    core_ops += [design_row(driver) for driver in DESIGNS if driver in designs]
    items = check_item_ids([("core", core_ops)] + [(str(year), ops) for year, ops, _ in seasons])
    write_json(ROOT / "db" / "core.json",
               {"schema_version": 1, "fields": {**fields, **(ITEM_FIELDS if made or season_helmets or DESIGNS else {})},
                "ops": core_ops})
    core_assets = [a for a in core["operations"]["assets"] if a not in ("assets/suits.json", "assets/helmets.json")]
    for name, has, write in (("suits", made, real_suit_asset_ops), ("helmets", season_helmets, helmet_asset_ops)):
        if has:
            write_json(ROOT / "assets" / f"{name}.json", write())
            core_assets.append(f"assets/{name}.json")
        elif (ROOT / "assets" / f"{name}.json").exists():
            (ROOT / "assets" / f"{name}.json").unlink()
    if core_assets != core["operations"]["assets"]:
        core["operations"]["assets"] = core_assets
        (ROOT / "swofty.json").write_text(json.dumps(core, indent=2, ensure_ascii=False) + "\n")
    write_json(ROOT / "lang" / "core.json", {"schema_version": 1, "ops": core_text})
    write_native_seasons([year for year, _, _ in seasons])
    print(f"core: {len(core_ops)} db ops, {len(core_text)} strings, looks {LOOK_STAT_BASE}..{look_stat - 1}, "
          f"{len(made)} real suits, suit ids {REAL_SUIT_IDS.start}..{TRANSFER_SUIT_IDS.stop - 1}, {items} item rows with "
          f"unique ids")
    for year, ops, text in seasons:
        folder = year_dir(year)
        assets = ["assets/cars.json"] + (["assets/suits.json"] if suits(year) else []) + \
            (["assets/sounds.json"] if sounds(year) else [])
        write_json(folder / "swofty.json", season_manifest(core, year, assets))
        write_json(folder / "db" / "season.json",
                   {"schema_version": 1, "fields": {**fields, SETUP_PRESETS: SETUP_PRESET_FIELDS,
                                                    **(ITEM_FIELDS if suits(year) else {}),
                                                    **({AUDIO_BUNDLES: AUDIO_BUNDLE_FIELDS} if sounds(year) else {})},
                    "ops": ops})
        write_json(folder / "lang" / "season.json", {"schema_version": 1, "ops": text})
        (folder / ".gitignore").write_text("cars/\nsuits/\nhelmets/\nsounds/\n")
        cars = built(year)
        if cars:
            write_json(folder / "assets" / "cars.json", asset_ops(year))
        if suits(year):
            write_json(folder / "assets" / "suits.json", suit_asset_ops(year))
        elif (folder / "assets" / "suits.json").exists():
            (folder / "assets" / "suits.json").unlink()
        if (folder / "assets" / "helmets.json").exists():
            (folder / "assets" / "helmets.json").unlink()
        if sounds(year):
            write_json(folder / "assets" / "sounds.json", sound_asset_ops(year))
        elif (folder / "assets" / "sounds.json").exists():
            (folder / "assets" / "sounds.json").unlink()
        adds = sum(1 for o in ops if o["op"] == "add-row")
        print(f"{year}: {len(ops)} db ops ({adds} add-row), {len(text)} strings, cars {'built' if cars else 'missing'}, "
              f"{len(suits(year))} suits, {len(sounds(year))} sound bundles")


if __name__ == "__main__":
    build()
