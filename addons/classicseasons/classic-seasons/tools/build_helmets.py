#!/usr/bin/env python3
"""Season helmets (data/helmets.json): mods' helmet textures on F1 25 helmet archives, built into the shared addon's
helmets/ (not committed) with helmets/meta.json.

F1 25's helmet shells (Bell HP7, Arai GP-6, Schuberth SF2 Pro, Stilo ST5 and ST6) share one texture layout, so a
helmet texture fits every shell; an entry's base is an F1 25 look whose shell is the brand on the helmet. The base's
helmet archive is relabelled from the base look to cs<yy>-<the base look without its year> (the same length), and its
helmet_d, helmet_n and helmet_s take the mod's textures, every level embedded with no sidecar. The helmet group lists
the base archive first, which holds the shell, the wing and the visor parts the copy names.

A mod's textures come from a zip in --downloads ("zip", "root") or an extracted folder under --mods ("dir"):

- DDS textures ("colour", "spec", "normal"): helmet_d is resized to the base's full size, helmet_n and helmet_s keep
  the mod's levels. A square texture (F1 2016 to F1 2018) holds the helmet in its top half, which is cropped; a cropped
  helmet_s is encoded as BC1.
- An archive ("archive", "textures": the prefix of its helmet_ surfaces) of F1 22 (surface version 8) or F1 23
  (version 9): each texture's archive levels and the levels of a `.mipmaps` sidecar the mod folder holds. F1 23
  helmet shells are byte for byte F1 25's.
- A game folder ("game", under --games) of F1 2016 to F1 22 and an entry's "look", the game's driver look folder
  (old_helmets.py): helmet_d's largest level resized to the base's full size, and helmet_n's levels, the top half of
  each when square, from the largest the base's helmet_n size holds; helmet_s the same with "spec": "own" (F1 2020
  and later, whose helmet_s has F1 25's channels), else generic. A square helmet_d holds the helmet in its top half
  ("square": "top", F1 2016 to F1 2018) or stretched over the whole square ("whole").

"generic" for normal is GENERIC's helmet_n; for spec, the per-texel median at 1024 by 512 of the helmet_s of the
F1 25 looks in data/stock_looks.json "looks" (the 2024 grid's), encoded as BC1. A texture an archive holds only in part
(its sidecar is not in the mod) is generic too. An entry with "off" is not built.

    python3 tools/build_helmets.py [--downloads ~/Downloads] [--mods <folder holding the extracted mods>]
        [--games <folder holding the old game folders>] [--game <F1 25>] [--jobs <n>]
"""

import argparse
import json
import os
import re
import shutil
import struct
import sys
import zipfile
from concurrent.futures import ProcessPoolExecutor
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path[:0] = [str(HERE), str(ROOT.parents[2] / "tools")]

import bcn
import erpw
import f1_23_suits
import old_helmets
import suit_sources
from build_real_suits import relabel
from build_suits import put_texture

DATA = json.loads((ROOT / "data" / "helmets.json").read_text())
STOCK = json.loads((ROOT / "data" / "stock_looks.json").read_text())
OUT = ROOT / "helmets"
DRIVERS = "character_package/drivers/male"
DXGI = {72: bcn.BC1[1], 71: bcn.BC1[0], 83: bcn.BC5[0]}
FOURCC = {b"DXT1": bcn.BC1[0], b"ATI2": bcn.BC5[0], b"BC5U": bcn.BC5[0]}
GENERIC = "helmets_2016"
ZSTD = (0x10, 0x11)


def label_of(entry):
    base = entry["base"]
    if not re.match(r"^20\d\d-", base):
        raise SystemExit(f"{entry['driver']}: base {base} has no year to relabel")
    return f"cs{entry['season'] % 100:02d}-{base[5:]}"


def paths(entry):
    base, label = entry["base"], label_of(entry)
    stem = base.split("_", 1)[1]
    return {"label": label, "stem": stem,
            "target": f"2025_asset_groups/{DRIVERS}/{label}/{stem}_helmet.erp",
            "group": f"asset_groups/{DRIVERS}/{label}/{stem}_helmet.assetgroup",
            "base_erp": f"2025_asset_groups/{DRIVERS}/{base}/{stem}_helmet.erp",
            "template": f"asset_groups/{DRIVERS}/{base}/{stem}_helmet.assetgroup"}


def dds(data):
    h, w, levels = struct.unpack_from("<III", data, 12)[0], struct.unpack_from("<I", data, 16)[0], \
        max(1, struct.unpack_from("<I", data, 28)[0])
    fourcc = data[84:88]
    if fourcc == b"DX10":
        fmt, at = DXGI.get(struct.unpack_from("<I", data, 128)[0]), 148
    else:
        fmt, at = FOURCC.get(fourcc), 128
    if fmt is None:
        raise SystemExit(f"a DDS of format {fourcc} {data[128:132].hex()}")
    out = []
    for size in bcn.level_sizes(fmt, w, h, levels):
        out.append(data[at:at + size])
        at += size
    return fmt, w, h, out


def top_half(fmt, w, h, levels):
    img = bcn.decode(fmt, levels[0], w, h)
    return img[:h // 2] if w == h else img


def colour_image(fmt, w, h, levels, size):
    return np.asarray(Image.fromarray(top_half(fmt, w, h, levels)).resize(size, Image.LANCZOS))


class Source:
    def __init__(self, mod, downloads, mods):
        self.mod = mod
        self.zip = zipfile.ZipFile(Path(downloads) / mod["zip"]) if "zip" in mod else None
        self.dir = Path(mods) / mod["dir"] if "dir" in mod else None

    def read(self, rel):
        if self.zip:
            return self.zip.read(f"{self.mod['root']}/{rel}")
        return (self.dir / rel).read_bytes()


def frag(e, r, i):
    fr = r.frags[i]
    return erpw.zstd_d(fr.raw) if fr.comp in ZSTD else fr.raw


def archive_chain(e, r, folder):
    fmt, w, h, n = bcn.Surface.header(frag(e, r, 0))
    emb = frag(e, r, 1)
    levels, at = [], 0
    for size in bcn.level_sizes(fmt, w, h, n):
        levels.append(emb[at:at + size])
        at += size
    if at > len(emb):
        raise SystemExit(f"{r.name}: {len(emb)} archive bytes for {n} levels of {w}x{h}")
    f2 = frag(e, r, 2) if len(r.frags) > 2 else b""
    if not (f2 and f2[0]):
        return fmt, w, h, levels
    if r.unk[0] != 9:
        raise SystemExit(f"{r.name}: a sidecar of surface version {r.unk[0]}")
    path, entries = bcn.sidecar(f2)
    if not entries:
        return fmt, w, h, levels
    full = folder / Path(path).name
    if not full.exists():
        return None
    k = len(entries)
    side = [bcn.read_level(full, lv) for lv in entries]
    if [len(s) for s in side] != bcn.level_sizes(fmt, w << k, h << k, k):
        raise SystemExit(f"{full}: levels of the wrong size")
    return fmt, w << k, h << k, side + levels


def stock_spec(game, label):
    stem = label.split("_", 1)[1]
    e = erpw.Erp(str(Path(game) / f"2025_asset_groups/{DRIVERS}/{label}/{stem}_helmet.erp"))
    r = f1_23_suits.surfaces(e)[f"eaid://{DRIVERS}/{label}/textures/{stem}_helmet_s.tif.image"]
    img = bcn.top_level([e.frag_data(r, i) for i in range(len(r.frags))], game)
    return np.asarray(Image.fromarray(img).resize((1024, 512), Image.LANCZOS))


@lru_cache(None)
def generic(part, downloads, game):
    if part == "normal":
        mod = DATA["mods"][GENERIC]
        with zipfile.ZipFile(Path(downloads) / mod["zip"]) as z:
            return "chain", dds(z.read(f"{mod['root']}/{mod['normal']}"))
    labels = sorted({look["helmet"] for look in STOCK["looks"].values() if look["helmet"].startswith("2024-")})
    stack = np.stack([stock_spec(game, label) for label in labels])
    print(f"generic helmet_s: the median of {len(labels)} stock helmets", flush=True)
    return "image", (np.median(stack, axis=0).astype(np.uint8), bcn.BC1[0])


def dds_part(src, mod, key, driver, downloads, game):
    if mod[key] == "generic":
        return generic(key, downloads, game)
    fmt, w, h, levels = dds(src.read(mod[key].format(driver=driver)))
    if w == h:
        return "image", (top_half(fmt, w, h, levels), bcn.BC1[0])
    return "chain", (fmt, w, h, levels)


def game_textures(entry, mod, games, downloads, game, sizes):
    tx = old_helmets.textures(Path(games) / mod["game"], int(mod["game"][-4:]), entry["look"])
    fmt, w, h, levels = tx["d"]
    img = top_half(fmt, w, h, levels) if mod.get("square", "top") == "top" else bcn.decode(fmt, levels[0], w, h)
    out = {"d": ("image", (np.asarray(Image.fromarray(img).resize(sizes["d"][1:], Image.LANCZOS)), None)),
           "s": generic("spec", downloads, game)}
    if mod.get("spec") == "own" and "s" in tx:
        out["s"] = ("chain", old_helmets.capped(*old_helmets.top_half(*tx["s"]), sizes["s"][1]))
    if "n" in tx:
        out["n"] = ("chain", old_helmets.capped(*old_helmets.top_half(*tx["n"]), sizes["n"][1]))
    else:
        out["n"] = generic("normal", downloads, game)
    return out


def textures(entry, src, downloads, game, size):
    mod = DATA["mods"][entry["mod"]]
    if "archive" in mod:
        e = erpw.Erp(str(src.dir / mod["archive"]))
        found = f1_23_suits.surfaces(e)
        out = {}
        for part, key in (("d", "colour"), ("n", "normal"), ("s", "spec")):
            r = found.get(f"{mod['textures']}{part}.tif.image")
            got = archive_chain(e, r, src.dir) if r is not None and mod.get(key) != "generic" else None
            if got is None and part == "d":
                raise SystemExit(f"{entry['driver']}: {mod['archive']} has no whole helmet_d")
            out[part] = ("chain", got) if got else generic(key, downloads, game)
        return out
    colour = dds(src.read(mod["colour"].format(driver=entry["driver"])))
    return {"d": ("image", (colour_image(*colour, size), None)),
            "n": dds_part(src, mod, "normal", entry["driver"], downloads, game),
            "s": dds_part(src, mod, "spec", entry["driver"], downloads, game)}


def put(e, r, how, value, base_fmt):
    if how == "chain":
        f1_23_suits.put_chain(e, r, *value)
        return
    img, fmt = value
    fmt = fmt or base_fmt
    if img.shape[2] == 3:
        img = np.concatenate([img, np.full(img.shape[:2] + (1,), 255, np.uint8)], 2)
    head, data, n = bcn.surface_frags(e.frag_data(r, 0), fmt, img)
    put_texture(e, r.name, head, data, n, fmt)


def build_one(entry, p, game, downloads, mods, games):
    report = []
    mod = DATA["mods"][entry["mod"]]
    base_erp = Path(game) / p["base_erp"]
    e, _ = relabel(base_erp, base_erp, entry["base"], p["label"], None, {}, game, report)
    prefix = f"eaid://{DRIVERS}/{p['label']}/textures/{p['stem']}_helmet_"
    surfaces = f1_23_suits.surfaces(e)
    fmt25, w25, h25 = f1_23_suits.full_size(e, surfaces[prefix + "d.tif.image"])
    if "game" in mod:
        sizes = {part: f1_23_suits.full_size(e, surfaces[prefix + f"{part}.tif.image"]) for part in "dns"}
        parts = game_textures(entry, mod, games, downloads, game, sizes)
    else:
        parts = textures(entry, Source(mod, downloads, mods), downloads, game, (w25, h25))
    for part, (how, value) in parts.items():
        put(e, surfaces[prefix + f"{part}.tif.image"], how, value, fmt25)
    missing = f1_23_suits.unresolved(e, erpw.Erp(str(base_erp)))
    if missing:
        raise SystemExit(f"{p['label']}: names {len(missing)} resource(s) neither archive holds: {missing[:4]}")
    source = f"helmets/{p['label']}/{p['stem']}_helmet.erp"
    (ROOT / source).parent.mkdir(parents=True, exist_ok=True)
    e.to_file(str(ROOT / source))
    print(f"{p['label']}: {entry['season']} {entry['driver']} from {mod['name']} on {entry['base']}", flush=True)
    return source, report


def build(game, downloads, mods, only=None, games=None, jobs=1):
    made = {}
    suit_labels = {e["label"] for e in suit_sources.real()}
    db = f1_23_suits.Db(game)
    if OUT.exists() and not only:
        shutil.rmtree(OUT)
    todo = []
    for entry in DATA["helmets"]:
        if entry.get("off") and not only:
            continue
        if only and f"{entry['season']}-{entry['driver']}" not in only:
            continue
        p = paths(entry)
        if p["label"] in made or p["label"] in suit_labels:
            raise SystemExit(f"{p['label']}: the helmet of {entry['driver']} and of {made.get(p['label'], 'a suit')}")
        made[p["label"]] = entry["driver"]
        todo.append((entry, p))
    with ProcessPoolExecutor(jobs) as pool:
        done = list(pool.map(build_one, *zip(*todo), *([x] * len(todo) for x in (game, downloads, mods, games))))
    built = [{**entry, **p, "source": source, "helmet_colours": db.helmet_colours(entry["base"])}
             for (entry, p), (source, _) in zip(todo, done)]
    if not only:
        (OUT / "meta.json").write_text(json.dumps({"helmets": built}, indent=1) + "\n")
    for _, report in done:
        for line in report:
            print(line)
    return built


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--downloads", default=os.path.expanduser("~/Downloads"))
    ap.add_argument("--mods", default=os.path.expanduser("~/Downloads"))
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25")
    ap.add_argument("--games", help="the folder holding the F1 2016 to F1 22 game folders")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--only", help="<season>-<driver>,...: build these entries, \"off\" ones too, without meta.json")
    args = ap.parse_args()
    if not (Path(args.game) / "F1_25.exe").is_file():
        sys.exit(f"--game {args.game} is not the F1 25 folder")
    build(args.game, args.downloads, args.mods, set(args.only.split(",")) if args.only else None, args.games, args.jobs)


if __name__ == "__main__":
    main()
