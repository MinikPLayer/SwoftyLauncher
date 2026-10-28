#!/usr/bin/env python3
"""Build the season car packs from fanfreluche's "F1 <year> cars for F1 25" zips.

Each zip replaces the stock 2025 cars in place, so its archives name their resources
`eaid://f1_2025_vehicle_package/...`. The build renames them byte for byte to `eaid://cs_<year>_vehicle_package/...`
(the same length, so no payload offset moves). A zip without its own teams/common.erp keeps the cars' shared
references on the stock f1_2025 package, whose common.erp the season's asset groups then list. Bare `.mipmaps`
sidecar paths that the zip ships as loose files move to the season's folder with the files; every other sidecar path
stays on the stock files. A season with its own common.erp renames its tyre set (shared/tyres/) with the rest, and
every car and showroom model layer gets a Tyres override naming it (tyres.py): the car templates name the stock
tyre set, whose resources share their names with the zip's tyre set and its larger buffers. Each car then gets the
frame-rate pass (carperf: shadow casters rebuilt from a lower LOD, non-livery textures and shipped sidecars capped
at 1024 px), and its handling files take the wheel positions from themselves instead of the model (handling.py) their aero and mass
from the team's real qualifying gap (pace.py), and their power unit the stock 2025 one (powerunit.py), and each model layer's switchName becomes the car's own engine sound label of the season (sounds.py). The build checks that every renamed reference resolves inside the season's archives, writes
them to the season addon's cars/ folder (layout.py), whose asset ops gen.py writes. A team with its own car archive in data/seasons.json ("car", a file in the downloads
folder, shipped in the slot of the team named by "folder") gets the same passes in the season's second package
(layout.py), with its shared references on the season's shared archive.
"""

import argparse
import glob
import json
import os
import re
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parents[2]
sys.path.insert(0, str(REPO / "tools"))

import erpw

sys.path.insert(0, str(ROOT / "tools"))
import cameras
import carperf
import handling
import pace
import powerunit
import sounds
import tyres
from layout import FOLDERS, SOURCE, STOCK_COMMON, cars_dir, extra_package, package

OLD = f"eaid://{SOURCE}/"
ANY_EAID = re.compile(rb"eaid://[\x21-\x7e]+")
SIDECAR = re.compile(rb"f1_20\d\d_vehicle_package/[\x21-\x7e]*?\.mipmaps")
SEASONS = {s["year"]: s for s in json.loads((ROOT / "data" / "seasons.json").read_text())["seasons"]}


def find_zip(downloads, year):
    short = str(year)[2:]
    pats = [f"F1 {year} [Cc]ars*F1 25*.zip", f"F1 {short} [Cc]ars*F1 25*.zip"]
    hits = sorted(p for pat in pats for p in glob.glob(os.path.join(downloads, pat)))
    if not hits:
        raise SystemExit(f"{year}: no zip in {downloads}")
    return hits[0]


def rewrite(e, pattern, repl):
    renamed = rewritten = 0
    for r in e.res:
        n = pattern.sub(repl, r.name.encode()).decode()
        if n != r.name:
            r.name = n
            renamed += 1
        for i in range(len(r.frags)):
            d = e.frag_data(r, i)
            nd = pattern.sub(repl, d)
            if nd != d:
                e.set_frag(r, i, nd)
                rewritten += 1
    return renamed, rewritten


def references(e):
    refs = set()
    for r in e.res:
        for i in range(len(r.frags)):
            for m in ANY_EAID.finditer(e.frag_data(r, i)):
                refs.add(m.group(0).split(b"?")[0].decode("latin-1"))
    return refs


def sidecars(e):
    found = set()
    for r in e.res:
        for i in range(len(r.frags)):
            found.update(m.group(0).decode() for m in SIDECAR.finditer(e.frag_data(r, i)))
    return found


def build_season(year, src_zip, out_dir, game):
    pkg = package(year)
    new = f"eaid://{pkg}/"
    report = {"year": year, "zip": os.path.basename(src_zip), "cars": {}}
    with zipfile.ZipFile(src_zip) as z, tempfile.TemporaryDirectory(dir=out_dir) as tmp:
        names = z.namelist()
        loose = {}
        for n in names:
            m = re.search(r"(f1_20\d\d_vehicle_package/.*\.mipmaps)$", n)
            if m and not n.endswith("/"):
                loose[m.group(1)] = n

        def member(rel):
            hits = [n for n in names if n.endswith("/2025_asset_groups/" + rel) or n == "2025_asset_groups/" + rel]
            if len(hits) > 1:
                raise SystemExit(f"{year}: {rel}: {len(hits)} matches in {src_zip}")
            return z.extract(hits[0], tmp) if hits else None

        own_common = member(f"{SOURCE}/teams/common.erp")
        prefix = OLD if own_common else OLD + "teams/"
        pattern = re.compile(re.escape(prefix.encode()) + rb"[^\x00]*")
        target = (new if own_common else new + "teams/").encode()
        repl = lambda m: target + m.group(0)[len(prefix):]

        archives = {f: f"{SOURCE}/teams/{f}/wep/{f}.erp" for f in FOLDERS}
        if own_common:
            archives = {"common": None, **archives}
        loaded = {}
        for key, rel in archives.items():
            path = own_common if key == "common" else member(rel)
            if not path:
                raise SystemExit(f"{year}: {rel} missing from {src_zip}")
            e = erpw.Erp(path)
            renamed, rewritten = rewrite(e, pattern, repl)
            loaded[key] = e
            report["cars"][key] = {"resources": len(e.res), "renamed": renamed, "rewritten": rewritten}

        if own_common:
            report["tyre_overrides"] = sum(tyres.override_layers(loaded[f], pkg) for f in FOLDERS)

        camera = SEASONS.get(year, {}).get("cockpit_camera")
        if camera:
            for folder in FOLDERS:
                cameras.move_cockpit(loaded[folder], pkg, camera["up"], camera["forward"])
            report["cockpit_camera"] = camera
        pod = SEASONS.get(year, {}).get("pod_camera")
        if pod:
            for folder in FOLDERS:
                cameras.pod_near_clip(loaded[folder], pkg, pod["near_clip"])
            report["pod_camera"] = pod
        report["handling_positions"] = sum(handling.use_handling_positions(loaded[f]) for f in FOLDERS)
        report["pace"] = {key: pace.apply(loaded[folder], pace.values(year, key))
                          for folder, key in pace.teams(year)[0].items()}
        parts = powerunit.stock(game)
        report["power_unit"] = sum(powerunit.apply(loaded[f], parts) for f in FOLDERS)
        report["sounds"] = {key: [new for _, _, _, new in sounds.apply(loaded[folder], year)]
                            for folder, key in pace.teams(year)[0].items()}

        shipped = set()
        for key, e in loaded.items():
            moved = {s for s in sidecars(e) if s in loose}
            if moved:
                swap = re.compile(b"|".join(re.escape(s.encode()) for s in sorted(moved, key=len, reverse=True)))
                rewrite(e, swap, lambda m: (pkg + m.group(0).decode()[m.group(0).index(b"/"):]).encode())
            shipped |= moved
            report["cars"][key]["sidecars_moved"] = len(moved)
        side = {pkg + s[s.index("/"):]: z.read(loose[s]) for s in shipped}
        trimmed, capped = carperf.cap_sidecars(loaded.values(), side)
        side.update(trimmed)
        report["sidecars"] = len(shipped)
        report["sidecars_capped"] = len(capped["capped"])
        for rel, data in sorted(side.items()):
            dest = out_dir / "sidecars" / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)

        known = set()
        for e in loaded.values():
            known |= {r.name.split("?")[0] for r in e.res}
        if not own_common:
            known |= {r.name.split("?")[0] for r in erpw.Erp(str(game / STOCK_COMMON)).res}
        for folder in FOLDERS:
            e = loaded[folder]
            world = f"{new}teams/{folder}/wep/{folder}.wep"
            if world not in known:
                raise SystemExit(f"{year} {folder}: no World {world}")
            refs = references(e)
            missing = sorted(r for r in refs - known if r.startswith((new, OLD)))
            foreign = sorted(r for r in refs if "_vehicle_package/" in r and not r.startswith((new, OLD))
                             and r not in known)
            missing_sidecars = sorted(s for s in sidecars(e) if s.startswith("f1_")
                                      and not (game / s).exists())
            report["cars"][folder].update(unresolved=missing[:20], unresolved_count=len(missing),
                                          foreign=foreign[:20], foreign_count=len(foreign),
                                          missing_sidecars=missing_sidecars[:20])

        for key, e in loaded.items():
            perf = carperf.optimise(e)
            try:
                shadow = carperf.check(e)
            except ValueError as err:
                raise SystemExit(f"{year} {key}: {err}")
            report["cars"][key].update(shadow_before=perf["shadow_before"], shadow_triangles=shadow,
                                       shadows_rebuilt=len(perf["shadows"]), textures_capped=len(perf["textures"]),
                                       textures_left=len(perf["textures_left"]))

        for key, e in loaded.items():
            rel = "teams/common.erp" if key == "common" else f"teams/{key}/wep/{key}.erp"
            out = out_dir / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            e.to_file(str(out))
            report["cars"][key]["bytes"] = out.stat().st_size
    (out_dir / "meta.json").write_text(json.dumps({"common": "own" if own_common else "stock",
                                                   "sidecars": sorted(str(p.relative_to(out_dir / "sidecars"))
                                                                      for p in (out_dir / "sidecars").rglob("*")
                                                                      if p.is_file())
                                                   if (out_dir / "sidecars").exists() else []}, indent=1) + "\n")
    return report


def build_extra(year, team, downloads, out_dir, game):
    folder, pkg, extra = team["folder"], package(year), extra_package(year)
    own_common = json.loads((out_dir / "meta.json").read_text())["common"] == "own"
    e = erpw.Erp(str(downloads / team["car"]))
    renamed, rewritten = rewrite(e, re.compile(re.escape(f"{OLD}teams/".encode())),
                                 lambda m: f"eaid://{extra}/teams/".encode())
    if own_common:
        rewrite(e, re.compile(re.escape(OLD.encode())), lambda m: f"eaid://{pkg}/".encode())
        tyres.override_layers(e, extra, pkg)
    season = SEASONS.get(year, {})
    if season.get("cockpit_camera"):
        cameras.move_cockpit(e, extra, season["cockpit_camera"]["up"], season["cockpit_camera"]["forward"])
    if season.get("pod_camera"):
        cameras.pod_near_clip(e, extra, season["pod_camera"]["near_clip"])
    handling.use_handling_positions(e)
    powerunit.apply(e, powerunit.stock(game))
    pace.apply(e, pace.values(year, team["key"]))
    sounds.apply(e, year, extra=True)
    known = {r.name.split("?")[0] for r in e.res}
    common = out_dir / "teams" / "common.erp" if own_common else game / STOCK_COMMON
    known |= {r.name.split("?")[0] for r in erpw.Erp(str(common)).res}
    world = f"eaid://{extra}/teams/{folder}/wep/{folder}.wep"
    if world not in known:
        raise SystemExit(f"{year} {team['key']}: no World {world}")
    missing = sorted(r for r in references(e) - known if r.startswith((f"eaid://{extra}/", f"eaid://{pkg}/", OLD)))
    if missing:
        raise SystemExit(f"{year} {team['key']}: {len(missing)} unresolved reference(s), such as {missing[:5]}")
    perf = carperf.optimise(e)
    shadow = carperf.check(e)
    out = out_dir / "extra" / "teams" / folder / "wep" / f"{folder}.erp"
    out.parent.mkdir(parents=True, exist_ok=True)
    e.to_file(str(out))
    return {"car": team["car"], "folder": folder, "package": extra, "renamed": renamed, "rewritten": rewritten,
            "shadow_triangles": shadow, "shadows_rebuilt": len(perf["shadows"]),
            "textures_capped": len(perf["textures"]), "bytes": out.stat().st_size}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--downloads", default=os.path.expanduser("~/Downloads"))
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25")
    ap.add_argument("--years", default="2016-2024")
    args = ap.parse_args()
    lo, _, hi = args.years.partition("-")
    for year in range(int(lo), int(hi or lo) + 1):
        src = find_zip(args.downloads, year)
        out = cars_dir(year)
        if out.exists():
            for p in sorted(out.rglob("*"), reverse=True):
                p.unlink() if p.is_file() else p.rmdir()
        out.mkdir(parents=True, exist_ok=True)
        r = build_season(year, src, out, Path(args.game))
        extras = [t for t in SEASONS.get(year, {}).get("teams", []) if "car" in t]
        if extras:
            r["extra"] = {t["key"]: build_extra(year, t, Path(args.downloads), out, Path(args.game)) for t in extras}
            meta = json.loads((out / "meta.json").read_text())
            meta["extra"] = [t["folder"] for t in extras]
            (out / "meta.json").write_text(json.dumps(meta, indent=1) + "\n")
        (out / "report.json").write_text(json.dumps(r, indent=1) + "\n")
        bad = {k: v["unresolved_count"] for k, v in r["cars"].items() if v.get("unresolved_count")}
        foreign = {k: v["foreign_count"] for k, v in r["cars"].items() if v.get("foreign_count")}
        miss = {k: len(v["missing_sidecars"]) for k, v in r["cars"].items() if v.get("missing_sidecars")}
        print(f"{year}: {len(r['cars'])} archives from {r['zip']}, {r['sidecars']} sidecar(s) moved, "
              f"unresolved {bad or 'none'}, foreign {foreign or 'none'}, missing sidecars {miss or 'none'}"
              + "".join(f", {k} from {v['car']} in {v['package']}" for k, v in r.get("extra", {}).items()), flush=True)


if __name__ == "__main__":
    main()
