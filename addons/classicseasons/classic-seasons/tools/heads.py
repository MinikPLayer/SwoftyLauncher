#!/usr/bin/env python3
"""Ship the real driver heads of data/heads.json: copy each head's files out of its mod zip into heads/ (not committed)
and write assets/heads.json, the asset ops that put them in the game folder.

The stock database has an entrants.animation.driver_heads.driver_head row for each of these heads, and its
asset_group_path_format and face_morph_asset_group_path_format pick the groups
animation_package/scenes/cinematics/drivers/heads/<label> and .../morph_poses/<label> of the row's label. The stock
manifest lists those groups, so the core never registers an addon's group under them, and a player's game folder may
hold other files at their archive paths. Each head therefore ships under its own label cs_<head> (label()): gen.py adds
a cs_<head> row copied from the stock <head> row, whose model_path,
eaid://[CHARACTER_PACKAGE]/heads/male_driver/<head>/idf/<head>_rig_head_morph_v1, the heads archive holds. The two
archives go to heads/cs_<head>.erp and morph_poses/cs_<head>.erp, and both groups are registered with group.register,
with lando_norris's groups as templates. A head whose textures stream also has .mipmaps files under
character_package/heads/male_driver/<head>/textures/, put at their own paths.

A head with `repair_from` in data/heads.json takes, for each of its materials that names a .sampler resource or a
heads/male_driver/ resource its archive lacks, the same-named material of the donor head, with the donor's texture
folder code (male_driver/serps_<code>/textures, the same length in every More Driver Models head) renamed to the head's.

Every material in F1 25's layout gets resource version 2. F1 25 parses a material of any other version as F1 23's
layout, and More Driver Models labels its F1 25 eye, head and head_cutscene materials version 1, as the repair leaves
the materials it copies: the parse then copies 256 KB from a 6 KB head.material and crashes the session load (memcpy at
F1_25.exe+0x53784bb, called from the material parser 0x143c0dc80). The relabel changes the version and no fragment: the
game decompresses every fragment of compression code 3 to 0x3f as zstd, More Driver Models writes code 0x10, and
erpw.set_frag stores a 0x10 fragment uncompressed, on which the game's decompressor spins and the session load never ends.

The tool fails when --game is not the F1 25 folder or a target exists in it that the launcher's placed.json does not
list, and when a head's archives name a .sampler resource (an older format F1 25 1.0.141 has none of) or a resource
under heads/male_driver/ or heads/male_ea/cs_ they do not hold: the team picker's head preview crashes in
Ne_LinkResolver_ResolveLinks on such a link. A head must be the head of a
driver in data/seasons.json (head_overrides or new_drivers).

A head with no stock row of its own label (`row_from` in data/heads.json) gets its row from the named stock head, with
the model_path of its heads archive, has_morph_data True and available_for_customisation False.

A head of `faces` is a stock head (`base`: a scanned head under heads/male_driver/, or a generic one under
heads/male_ea/) with another face map: the head_d surface of `face` in another archive (`archive`), or an F1 2016
colour map (`mipmaps`: raw BC1 levels 2048 to 256, largest first, to which the tool adds 128 to 4): both of the base's
archives, read from the game folder (the baseline's copy of a file the launcher changed), with `base` renamed byte
for byte to `code` (the same length) in every resource name and fragment. A surface's sidecar description (fragment 2)
keeps naming the stock .mipmaps file. The face archive's head_d surface, 2048 px with all 10 levels in the archive and
no sidecar, replaces the copy's under the copy's name, and the head_d view takes its format and 10 levels. gen.py copies
the row of a generic base, or male_ls222's for a scanned base, with the copy's model_path.

A head of `off` ships nothing; its drivers wear the head it names.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT.parents[2] / "tools"))

import erpw
import gfx_material
from stock import Stock

EAID = re.compile(rb"eaid://[\x21-\x7e]+")
CODE = re.compile(rb"/heads/male_driver/(serps_[A-Za-z0-9]+)/textures/")
OUT = ROOT / "heads"
OPS = ROOT / "assets" / "heads.json"
SCENES = "animation_package/scenes/cinematics/drivers"
TEMPLATE = "lando_norris"
VERSION_25 = struct.pack("<I", 2)
ZSTD_CODES = range(3, 0x40)
DATA = json.loads((ROOT / "data" / "heads.json").read_text())
FACES = DATA.get("faces", {})
OFF = DATA.get("off", {})
SURFACE = b"GfxSurfaceRes"
SRV = b"GfxSRVResource"
FACE_LEVELS = 10
FACE_FORMAT = 54
ROW_TEMPLATE = "male_ls222"
BASELINE = "~/.local/share/swofty/baseline/25251167-d873a1c8d204"
PLACED = "~/.local/share/swofty/state/placed.json"


def label(head):
    return f"cs_{head}"


def shipped():
    return [h for h in [*DATA["heads"], *FACES] if h not in OFF]


def worn(head):
    return OFF.get(head, head)


def generic(label):
    return label.startswith("male_ls")


def model_path(head):
    if head in FACES:
        code = FACES[head]["code"]
        folder = "male_ea" if generic(FACES[head]["base"]) else "male_driver"
        return f"eaid://[CHARACTER_PACKAGE]/heads/{folder}/{code}/idf/{code}_rig_head_morph_v1"
    return f"eaid://[CHARACTER_PACKAGE]/heads/male_driver/{head}/idf/{head}_rig_head_morph_v1"


def row_op(table, head):
    if head in FACES:
        base = FACES[head]["base"] if generic(FACES[head]["base"]) else ROW_TEMPLATE
    else:
        base = DATA.get("row_from", {}).get(head, head)
    op = {"op": "add-row", "table": table, "from": f"{table}.driver_head[_metadata_={base}]",
          "set": {"_metadata_": label(head)}}
    if base != head:
        op["set"].update({"model_path": model_path(head), "has_morph_data": "True",
                          "available_for_customisation": "False"})
    return op


def archives(head):
    return [f"2025_asset_groups/{SCENES}/{kind}/{head}.erp" for kind in ("heads", "morph_poses")]


def target(rel, head):
    if head in FACES:
        return rel
    return rel if rel not in archives(head) else rel.replace(f"/{head}.erp", f"/{label(head)}.erp")


def wanted(rel, head):
    if rel in archives(head):
        return True
    parts = rel.split("/")
    return len(parts) > 4 and parts[:2] == ["character_package", "heads"] and parts[3] == head


def used_heads():
    seasons = json.loads((ROOT / "data" / "seasons.json").read_text())
    return set(seasons["head_overrides"].values()) | {d["head"] for d in seasons["new_drivers"].values()}


def open_zip(downloads, source, head):
    found = [Path(d).expanduser() / source["zip"] for d in downloads]
    found = [p for p in found if p.exists()]
    if not found:
        sys.exit(f"{head}: {source['zip']} is in none of {', '.join(map(str, downloads))}")
    z = zipfile.ZipFile(found[0])
    return zipfile.ZipFile(z.open(source["inner"])) if source.get("inner") else z


def extract(downloads, source, head):
    root = source["root"].rstrip("/") + "/" if source["root"] else ""
    files = []
    with open_zip(downloads, source, head) as z:
        path = Path(z.filename or source["inner"])
        for info in z.infolist():
            if info.is_dir() or not info.filename.startswith(root):
                continue
            rel = info.filename[len(root):]
            if not wanted(rel, head):
                continue
            target = OUT / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)
            files.append(rel)
    missing = [a for a in archives(head) if a not in files]
    if missing:
        sys.exit(f"{head}: {path.name} has no {', '.join(missing)} under {root}")
    return sorted(files)


def frag(r, i):
    f = r.frags[i]
    return erpw.zstd_d(f.raw) if f.comp in ZSTD_CODES else f.raw


def f1_25_layout(b):
    try:
        gfx_material.parse25(b, strict=False)
    except (ValueError, IndexError, struct.error):
        return False
    return True


def bad_refs(e, r, held):
    out = set()
    for i in range(len(r.frags)):
        for m in EAID.finditer(e.frag_data(r, i)):
            ref = m.group(0).decode()
            own = "/heads/male_driver/" in ref or "/heads/male_ea/cs_" in ref
            if ref.endswith(".sampler") or (own and ref.split("?")[0] not in held):
                out.add(ref)
    return out


def broken_links(path):
    e = erpw.Erp(str(path))
    held = {r.name.split("?")[0] for r in e.res}
    return sorted(set().union(*(bad_refs(e, r, held) for r in e.res)))


def texture_code(e):
    codes = {m.group(1) for r in e.res for m in [CODE.search(r.name.encode())] if m}
    if len(codes) != 1:
        sys.exit(f"{e.path}: texture folder codes {sorted(codes)}")
    return codes.pop()


def repair(head, donor):
    path = OUT / archives(head)[0]
    e, d = erpw.Erp(str(path)), erpw.Erp(str(OUT / archives(donor)[0]))
    mine, theirs = texture_code(e), texture_code(d)
    if len(mine) != len(theirs):
        sys.exit(f"{head}: texture folder {mine.decode()} and {donor}'s {theirs.decode()} differ in length")
    held = {r.name.split("?")[0] for r in e.res}
    materials = {r.name.rsplit("/", 1)[-1]: r for r in d.res if r.name.endswith(".material")}
    swapped = []
    for r in e.res:
        if not r.name.endswith(".material") or not bad_refs(e, r, held):
            continue
        src = materials.get(r.name.rsplit("/", 1)[-1])
        if src is None:
            sys.exit(f"{head}: {donor} has no {r.name.rsplit('/', 1)[-1]}")
        data = [d.frag_data(src, i).replace(theirs, mine) for i in range(len(src.frags))]
        r.frags = r.frags[:len(data)]
        for i, b in enumerate(data):
            e.set_frag(r, i, b)
        swapped.append(r.name.rsplit("/", 1)[-1])
    e.to_file(str(path))
    return swapped


def relabel(path):
    e = erpw.Erp(str(path))
    wrong = [r for r in e.res if r.type.rstrip(b"\0") == b"GfxMaterialRes" and r.unk[:4] != VERSION_25
             and f1_25_layout(frag(r, 0))]
    for r in wrong:
        r.unk = VERSION_25 + r.unk[4:]
    if wrong:
        e.to_file(str(path))
    return [r.name.rsplit("/", 1)[-1] for r in wrong]


def put_frag(r, i, data):
    f = r.frags[i]
    f.raw = erpw.zstd_c(data) if f.comp in ZSTD_CODES else data
    f.size, f.packed = len(data), len(f.raw)
    r.hash = hashlib.md5(b"".join(frag(r, j) for j in range(len(r.frags)))).digest()


def rename(e, old, new):
    for r in e.res:
        r.name = r.name.replace(old.decode(), new.decode())
        surface = r.type.rstrip(b"\0") == SURFACE
        for i in range(len(r.frags)):
            if surface and i == 2:
                continue
            data = frag(r, i)
            if old in data:
                put_frag(r, i, data.replace(old, new))


def f1_2016_chain(downloads, face, head):
    import bcn
    with open_zip(downloads, DATA["sources"][face["source"]], head) as z:
        raw = z.read(face["mipmaps"])
    sizes = bcn.level_sizes(FACE_FORMAT, 2048, 2048, 4)
    if len(raw) != sum(sizes):
        sys.exit(f"{head}: {face['mipmaps']} is {len(raw)} bytes, not BC1 levels 2048 to 256")
    level = bcn.decode(FACE_FORMAT, raw[sum(sizes[:3]):], 256, 256)
    chain = [raw]
    while level.shape[0] > 4:
        level = bcn.downsample(level)
        chain.append(bcn.encode(FACE_FORMAT, level))
    return b"".join(chain)


def embedded_surface(template, chain):
    r = copy_res(template)
    r.frags = r.frags[:2]
    header = bytearray(frag(template, 0))
    struct.pack_into("<3I", header, 8, FACE_FORMAT, 2048, 2048)
    struct.pack_into("<I", header, 24, FACE_LEVELS)
    put_frag(r, 0, bytes(header))
    put_frag(r, 1, chain)
    return r


def face_surface(downloads, face, head):
    with open_zip(downloads, DATA["sources"][face["source"]], head) as z, tempfile.TemporaryDirectory() as d:
        path = Path(d) / "face.erp"
        path.write_bytes(z.read(face["archive"]))
        e = erpw.Erp(str(path))
    images = [r for r in e.res if r.name.endswith(f"/{face['face']}_head_d.tif.image")]
    if len(images) != 1:
        sys.exit(f"{head}: {face['archive']} holds {len(images)} {face['face']} head_d surfaces")
    r = images[0]
    header = struct.unpack_from("<10I", frag(r, 0))
    w, h, levels = header[3], header[4], header[6]
    if (w, h, levels) != (2048, 2048, FACE_LEVELS) or len(r.frags) != 2:
        sys.exit(f"{head}: {face['archive']} head_d is {w}x{h}, {levels} levels, {len(r.frags)} fragments")
    r.hash = hashlib.md5(b"".join(frag(r, i) for i in range(len(r.frags)))).digest()
    return r


def build_face(stock, downloads, head):
    face = FACES[head]
    base, code = face["base"].encode(), face["code"].encode()
    if len(base) != len(code):
        sys.exit(f"{head}: {face['code']} is not as long as {face['base']}")
    image = face_surface(downloads, face, head) if "archive" in face else None
    chain = f1_2016_chain(downloads, face, head) if "mipmaps" in face else None
    levels = FACE_LEVELS
    files = []
    placed = 0
    for kind in ("heads", "morph_poses"):
        src = f"2025_asset_groups/{SCENES}/{kind}/{face['base']}.erp"
        e = erpw.Erp(str(stock.path(src)))
        if any(code in r.name.encode() for r in e.res):
            sys.exit(f"{head}: {src} already names {face['code']}")
        rename(e, base, code)
        for i, r in enumerate(e.res):
            if r.name.endswith(f"/{face['code']}_head_d.tif.image") and r.type.rstrip(b"\0") == SURFACE:
                copy = copy_res(image) if image else embedded_surface(r, chain)
                copy.name = r.name
                e.res[i] = copy
                placed += 1
        fmt = struct.unpack_from("<10I", frag(image, 0))[2] if image else FACE_FORMAT
        for r in e.res:
            if r.name.endswith(f"/{face['code']}_head_d.tif") and r.type.rstrip(b"\0") == SRV:
                srv = bytearray(frag(r, 0))
                struct.pack_into("<I", srv, 4, fmt)
                struct.pack_into("<I", srv, 12, levels)
                put_frag(r, 0, bytes(srv))
        rel = f"2025_asset_groups/{SCENES}/{kind}/{label(head)}.erp"
        (OUT / rel).parent.mkdir(parents=True, exist_ok=True)
        e.to_file(str(OUT / rel))
        files.append(rel)
    if not placed:
        sys.exit(f"{head}: {face['base']} has no head_d surface")
    return files


def copy_res(r):
    out = erpw.Res()
    out.name, out.type, out.unk, out.hash = r.name, r.type, r.unk, r.hash
    out.frags = []
    for f in r.frags:
        g = erpw.Frag()
        g.name, g.flags, g.comp, g.size, g.packed, g.raw = f.name, f.flags, f.comp, f.size, f.packed, f.raw
        out.frags.append(g)
    return out


def face_leftovers(head, game=None):
    face = FACES[head]
    base = face["base"].encode()
    out = []
    for kind in ("heads", "morph_poses"):
        e = erpw.Erp(str(OUT / f"2025_asset_groups/{SCENES}/{kind}/{label(head)}.erp"))
        for r in e.res:
            surface = r.type.rstrip(b"\0") == SURFACE
            for i in range(len(r.frags)):
                data = frag(r, i)
                if surface and i == 2 and data and data[0]:
                    side = data[1:1 + data[0]].rstrip(b"\0").decode()
                    if game and face["base"] in side and not (Path(game) / side).is_file():
                        out.append(side)
                elif base in data or base in r.name.encode() or face.get("face", face["base"]).encode() in r.name.encode():
                    out.append(r.name)
            if r.name.endswith(f"/{face['code']}_head_d.tif.image") and len(r.frags) != 2:
                out.append(f"{r.name} (a sidecar)")
    return sorted(set(out))


def asset_ops(heads):
    ops = []
    for head, files in heads.items():
        ops += [{"op": "file.put", "source": f"heads/{rel}", "target": target(rel, head)} for rel in files]
        for kind in ("heads", "morph_poses"):
            ops.append({"op": "group.register", "group": f"asset_groups/{SCENES}/{kind}/{label(head)}.assetgroup",
                        "erp": f"2025_asset_groups/{SCENES}/{kind}/{label(head)}.erp",
                        "template": f"asset_groups/{SCENES}/{kind}/{TEMPLATE}.assetgroup"})
    return {"schema_version": 1, "ops": ops}


def placed_files(path):
    path = Path(path).expanduser()
    return set(json.loads(path.read_text())["files"]) if path.is_file() else set()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--downloads", action="append", help="folder holding the mod zips; repeat for more (default ~/Downloads)")
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25")
    ap.add_argument("--baseline", default=BASELINE, help="launcher baseline folder holding baseline.json")
    ap.add_argument("--placed", default=PLACED, help="the launcher's placed.json: files it put in the game folder")
    args = ap.parse_args()
    downloads = args.downloads or [os.path.expanduser("~/Downloads")]
    game = Path(args.game)
    if not (game / "F1_25.exe").is_file():
        sys.exit(f"--game {game} is not the F1 25 folder")
    baseline = Path(args.baseline).expanduser()
    stock = Stock(game, baseline if (baseline / "baseline.json").is_file() else None)
    unused = sorted(set(shipped()) - used_heads())
    if unused:
        sys.exit(f"no driver in data/seasons.json has the head {', '.join(unused)}")
    if OUT.exists():
        shutil.rmtree(OUT)
    heads = {head: extract(downloads, DATA["sources"][DATA["heads"][head]], head)
             for head in shipped() if head not in FACES}
    for head, donor in DATA.get("repair_from", {}).items():
        print(f"{head}: materials from {donor}: {', '.join(repair(head, donor))}")
    for head in shipped():
        if head in FACES:
            heads[head] = build_face(stock, downloads, head)
            left = face_leftovers(head, game)
            if left:
                sys.exit(f"{head}: {FACES[head]['base']} is still named in {', '.join(left[:3])}")
            print(f"{head}: {FACES[head]['base']} as {FACES[head]['code']}, face from {FACES[head].get('archive') or FACES[head]['mipmaps']}")
        paths = [OUT / rel for rel in heads[head] if rel.endswith(".erp")]
        relabelled = [name for path in paths for name in relabel(path)]
        if relabelled:
            print(f"{head}: resource version 2 for {', '.join(relabelled)}")
        bad = [ref for path in paths for ref in broken_links(path)]
        if bad:
            sys.exit(f"{head}: its archives name {len(bad)} resource(s) F1 25 does not have: {', '.join(bad[:3])}")
    placed = placed_files(args.placed)
    clash = [target(rel, head) for head, files in heads.items() for rel in files
             if (game / target(rel, head)).exists() and target(rel, head) not in placed]
    if clash:
        sys.exit(f"these replace game files: {', '.join(clash)}")
    OPS.parent.mkdir(parents=True, exist_ok=True)
    OPS.write_text(json.dumps(asset_ops(heads), indent=1) + "\n")
    size = sum((OUT / rel).stat().st_size for files in heads.values() for rel in files)
    print(f"heads: {len(heads)} heads, {sum(map(len, heads.values()))} files, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
