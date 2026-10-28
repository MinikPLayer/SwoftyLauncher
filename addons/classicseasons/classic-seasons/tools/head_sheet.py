#!/usr/bin/env python3
"""Contact sheet of the face map (head_d) of every driver whose head data/seasons.json sets: a shipped head from
heads/ (tools/heads.py), or a stock generic head from the game folder. With --generic, every stock male generic head
(male_ls<n>) instead, with its hair texture set and its hair colour swatch. With --donors, every stock scanned male
head (heads/male_driver/) of no driver of a Classic Seasons season.

usage: head_sheet.py --out <png> [--generic | --donors] [--game <F1 25>] [--baseline <launcher baseline>/<build>]
"""

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bcn
import heads
from heads import erpw
from stock import Stock

TILE = 320
COLS = 5


def drivers():
    seasons = json.loads((heads.ROOT / "data" / "seasons.json").read_text())
    wears = {**seasons["head_overrides"], **{k: d["head"] for k, d in seasons["new_drivers"].items()}}
    years = {}
    for season in seasons["seasons"]:
        for team in season["teams"]:
            for driver, _ in team["drivers"]:
                years.setdefault(driver, []).append(season["year"])
    return sorted(((d, heads.worn(h), years.get(d, [])) for d, h in wears.items()), key=lambda x: (x[2][:1], x[0]))


def archive(head, stock):
    if head in heads.FACES:
        return heads.OUT / f"2025_asset_groups/{heads.SCENES}/heads/{heads.label(head)}.erp"
    if head in heads.shipped():
        return heads.OUT / heads.archives(head)[0]
    return stock.path(f"2025_asset_groups/{heads.SCENES}/heads/{head}.erp")


def face(path, game):
    e = erpw.Erp(str(path))
    folders = {r.name.split("/")[5] for r in e.res if r.name.endswith("?model") and "/heads/male_" in r.name
               and "/kitpart_" not in r.name}
    images = [r for r in e.res if r.name.endswith("_head_d.tif.image") and "/heads/male_" in r.name]
    own = [r for r in images if r.name.split("/")[5] in folders and r.name.split("/")[4] != "head_template"]
    best = None
    for r in own if any(r.name.rsplit("/", 1)[-1].startswith(r.name.split("/")[5]) for r in own) else images:
        img = bcn.top_level([heads.frag(r, i) for i in range(len(r.frags))], [heads.OUT, game])
        if best is None or img.shape[0] > best[1].shape[0]:
            best = (r.name.rsplit("/", 1)[-1], img)
    if best is None:
        sys.exit(f"{path}: no head_d surface")
    return best


def hair(path, game):
    e = erpw.Erp(str(path))
    res = {r.name: r for r in e.res}
    mat = [r for r in e.res if re.search(r"/heads/male_ea/[^/]+/materials/hair\.material$", r.name)]
    if not mat:
        return "-", "-", None
    refs = re.findall(rb"eaid://[\x21-\x7e]+", heads.frag(mat[0], 0))
    names = [r.decode() for r in refs]
    style = next((n.rsplit("/", 1)[-1][:-6] for n in names if n.endswith("_d.tif") and "hair" in n), "-")
    swatch = next((n for n in names if "et_swatches" in n), None)
    if swatch is None or swatch + ".image" not in res:
        return style, "-", None
    r = res[swatch + ".image"]
    img = bcn.top_level([heads.frag(r, i) for i in range(len(r.frags))], game)
    return style, swatch.rsplit("/", 1)[-1][len("hair_swatch_"):-len("_et.tif")], img


def small_face(path, stock, want=512):
    e = erpw.Erp(str(path))
    for r in e.res:
        if not r.name.endswith("_head_d.tif.image") or "/heads/male_" not in r.name:
            continue
        frags = [heads.frag(r, i) for i in range(len(r.frags))]
        fmt, w, h, _ = bcn.Surface.header(frags[0])
        if len(frags) > 2 and frags[2] and frags[2][0]:
            side_path, side = bcn.sidecar(frags[2])
            n = len(side)
            k = next((k for k in range(n) if (w << (n - k)) <= want), None)
            if k is not None:
                full = stock.path(side_path)
                return bcn.decode(fmt, bcn.read_level(full, side[k]), w << (n - k), h << (n - k))
        return bcn.decode(fmt, frags[1], w, h)
    sys.exit(f"{path}: no head_d surface")


def donors(game, stock):
    seasons = json.loads((heads.ROOT / "data" / "seasons.json").read_text())
    grid = {d for s in seasons["seasons"] for team in s["teams"] for d, _ in team["drivers"]}
    folder = Path(game) / f"2025_asset_groups/{heads.SCENES}/heads"
    out = []
    for p in sorted(folder.glob("*.erp")):
        label = p.stem
        if label.startswith(("male_", "female_", "cs_")) or label in grid or label.endswith("_malerig"):
            continue
        names = {r.name for r in erpw.Erp(str(stock.path(f"2025_asset_groups/{heads.SCENES}/heads/{p.name}"))).res}
        if any(n.startswith(f"eaid://character_package/heads/male_driver/{label}/idf/") and n.endswith("?model")
               for n in names):
            out.append(label)
    return out


def donor_sheet(out, game, stock):
    labels = donors(game, stock)
    rows = (len(labels) + COLS - 1) // COLS
    sheet = Image.new("RGB", (TILE * COLS, (TILE + 20) * rows), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for n, head in enumerate(labels):
        img = small_face(stock.path(f"2025_asset_groups/{heads.SCENES}/heads/{head}.erp"), stock)
        x, y = TILE * (n % COLS), (TILE + 20) * (n // COLS)
        sheet.paste(Image.fromarray(np.asarray(img)[..., :3]).resize((TILE, TILE), Image.LANCZOS), (x, y + 20))
        draw.text((x + 4, y + 3), head, fill=(255, 255, 255))
        print(head)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def generic_sheet(out, game, stock):
    folder = Path(game) / f"2025_asset_groups/{heads.SCENES}/heads"
    labels = sorted((p.stem for p in folder.glob("male_ls*.erp")), key=lambda h: int(h[7:]))
    rows = (len(labels) + COLS - 1) // COLS
    sheet = Image.new("RGB", (TILE * COLS, (TILE + 34) * rows), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for n, head in enumerate(labels):
        src = stock.path(f"2025_asset_groups/{heads.SCENES}/heads/{head}.erp")
        _, img = face(src, game)
        style, colour, swatch = hair(src, game)
        x, y = TILE * (n % COLS), (TILE + 34) * (n // COLS)
        sheet.paste(Image.fromarray(np.asarray(img)[..., :3]).resize((TILE, TILE), Image.LANCZOS), (x, y + 34))
        if swatch is not None:
            chip = Image.fromarray(np.asarray(swatch)[..., :3]).resize((64, 28))
            sheet.paste(chip, (x + TILE - 68, y + 3))
        draw.text((x + 4, y + 3), f"{head}  hair {style}", fill=(255, 255, 255))
        draw.text((x + 4, y + 18), f"swatch {colour}", fill=(200, 200, 120))
        print(f"{head:11} {style:26} {colour}")
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def span(years):
    if not years:
        return "-"
    return str(years[0]) if len(years) == 1 else f"{years[0]}-{years[-1]}" if years == list(
        range(years[0], years[-1] + 1)) else ", ".join(map(str, years))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25")
    ap.add_argument("--baseline", default=heads.BASELINE)
    ap.add_argument("--generic", action="store_true", help="every stock male generic head instead")
    ap.add_argument("--donors", action="store_true", help="every stock scanned male head of no season driver instead")
    a = ap.parse_args()
    baseline = Path(a.baseline).expanduser()
    stock = Stock(a.game, baseline if (baseline / "baseline.json").is_file() else None)
    if a.generic:
        return generic_sheet(a.out, a.game, stock)
    if a.donors:
        return donor_sheet(a.out, a.game, stock)
    rows = drivers()
    sheet = Image.new("RGB", (TILE * COLS, (TILE + 34) * ((len(rows) + COLS - 1) // COLS)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for n, (driver, head, years) in enumerate(rows):
        name, img = face(archive(head, stock), a.game)
        x, y = TILE * (n % COLS), (TILE + 34) * (n // COLS)
        sheet.paste(Image.fromarray(np.asarray(img)[..., :3]).resize((TILE, TILE), Image.LANCZOS), (x, y + 34))
        label = heads.label(head) if head in heads.shipped() else head
        draw.text((x + 4, y + 3), f"{driver} ({span(years)})", fill=(255, 255, 255))
        draw.text((x + 4, y + 18), f"{label}: {name} {img.shape[1]}px", fill=(200, 200, 120))
        print(f"{driver:20} {span(years):22} {label:22} {name} {img.shape[1]}x{img.shape[0]}")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(a.out)


if __name__ == "__main__":
    main()
