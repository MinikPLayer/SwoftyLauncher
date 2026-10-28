"""Period engine sounds.

A car's engine sound is picked by the `switchName` of the AudioF1Car component in its <folder>-carmodel.layer
(`\\nswitchName\\x00<len><label>\\x00`, f1_m_<team> on the 2025 cars the season cars are built on). The game uses the
label twice: its Wwise ID (FNV-1 32 with A-Z lowered) is the engine switch state, and it reads the
audio.audio_car_data.<label>.* records (case kept), whose bundle_name it loads from audio/2025/bundles/ when
audio.audio_bundle_settings lists it. The layer's bundleName does not pick the sound and stays the built car's.

Each season car gets its donor label (the layer's label in lower case) in other letter case: the label's letters are
numbered from its first, and letter i is upper case when bit i of n is set, n the season less 2015 (1 to 9) and n + 16
for the season's extra car (2016 Manor on the Sauber slot): 2016 McLaren F1_m_mclaren, 2017 f1_M_mclaren. The length
and the Wwise switch state stay the donor's, so the layer is rewritten byte for byte, and the label is the car's own
key in audio.audio_car_data: sounds.py copies the donor's 43 records under it from the stock database (meta.json) with
bundle_name the season bundle of the team's `engine` in data/seasons.json. data/sounds.json names each maker's bundle
in the game of the season (F1 25 loads an old game's `bndl` version 0x6a bundles) or a stock F1 25 bundle
("stock"). A season bundle that is not stock is copied to the season addon's sounds/ (not committed) as
cs<yy>_<maker>.bdl. F1 25's part reader (FUN_144524b30) reads 16 variation layers in every part, with no version
check: 21 curves of 1032 bytes (16 layer curves, then 5 global ones) and 16 layer masks. F1 2016 to F1 2018 parts
(0x71) hold 8 layers (13 curves, 8 masks), so F1 25 read 8 layer curves, the 5 global curves and all 16 masks from
the part's samples. widen() gives those parts 16 layers, layer i + 8 a copy of layer i, and sets the part's block
size; the game then picks among the same layer curves as the old game did. gen.py ships the bundles with file.put to
audio/2025/bundles/, adds each one's audio.audio_bundle_settings row (a copy of its maker's 2025 row) and puts each
car's records. build_cars.py sets the labels when it builds a season; run on its own, the tool sets them in the built
cars (only the season's own cs_/cx_ package layers), copies the bundles and writes meta.json. A rerun changes nothing.

    python3 tools/sounds.py --games <folder holding the old game folders> --f1-23 <F1 23> [--game <F1 25>]
        [--years 2016-2024]
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import erpw
import gamedb
from layout import ROOT, cars_dir, year_dir

DATA = json.loads((ROOT / "data" / "seasons.json").read_text())
SOURCES = json.loads((ROOT / "data" / "sounds.json").read_text())["seasons"]
SWITCH = re.compile(rb"\nswitchName\x00(.)", re.DOTALL)
STOCK_BUNDLES = "audio/2025/bundles"
PART = re.compile(rb"ego[a-z]")
LAYERS = 16
GLOBAL_CURVES = 5
CURVE_BYTES = 1032
BLOCK_HEAD = 24
DB = "game_package/gamedatabases/gamedatabases.2025.bin"
CAR_DATA = "audio.audio_car_data"
FIELDS = [
    "_metadata_", "bundle_name", "detonations_category", "safety_car", "cabin_radio", "left_right_panning",
    "frontwing_offset_x", "frontwing_offset_y", "frontwing_offset_z",
    "rearwing_offset_x", "rearwing_offset_y", "rearwing_offset_z",
    "windscreen_offset_x", "windscreen_offset_y", "windscreen_offset_z",
    "engine_offset_x", "engine_offset_y", "engine_offset_z", "engine_rotation_y",
    "exhaust_offset_x", "exhaust_offset_y", "exhaust_offset_z", "exhaust_rotation_y",
    "cabin_offset_x", "cabin_offset_y", "cabin_offset_z", "cabin_rotation_y",
    "gearbox_offset_x", "gearbox_offset_y", "gearbox_offset_z", "gearbox_rotation_y",
    "braking_slowdown", "engine_braking_scale", "exhaust_braking_scale",
    "engine_downshift_load_lag", "exhaust_downshift_load_lag",
    "engine_torque_throttle_mixer", "exhaust_torque_throttle_mixer",
    "judder_wobble_scale", "surface_load_wobble_scale",
    "off_load_decell_attenuation_idle", "off_load_decell_attenuation_redline", "throttle_blend_power_base",
]


def bundle(year, maker):
    stock = SOURCES[str(year)].get("stock")
    return stock.format(maker=maker) if stock else f"cs{year % 100:02d}_{maker}"


def wwise_id(label):
    h = 0x811c9dc5
    for c in label.encode():
        if 0x41 <= c <= 0x5a:
            c += 0x20
        h = ((h * 0x1000193) & 0xffffffff) ^ c
    return h


def label(donor, year, extra=False):
    n = year - 2015 + (16 if extra else 0)
    letters = [i for i, c in enumerate(donor) if c.isalpha()]
    if n >> len(letters):
        raise SystemExit(f"{donor}: {len(letters)} letters hold no case variant {n}")
    out = list(donor)
    for bit, i in enumerate(letters):
        if n >> bit & 1:
            out[i] = out[i].upper()
    new = "".join(out)
    if new == donor or wwise_id(new) != wwise_id(donor):
        raise SystemExit(f"{new} does not keep the Wwise switch of {donor}")
    return new


def cars(year):
    season = next(s for s in DATA["seasons"] if s["year"] == year)
    out = []
    for team in season["teams"]:
        folder = team["folder"]
        sub = Path("extra") if "car" in team else Path()
        out.append((cars_dir(year) / sub / "teams" / folder / "wep" / f"{folder}.erp", team.get("key", folder),
                    team["engine"], "car" in team))
    return out


def engines(year):
    season = next(s for s in DATA["seasons"] if s["year"] == year)
    return {team.get("key", team["folder"]): team["engine"] for team in season["teams"]}


def layers(e, year):
    prefixes = (f"eaid://cs_{year}_vehicle_package/", f"eaid://cx_{year}_vehicle_package/")
    return [r for r in e.res if r.name.startswith(prefixes) and r.name.endswith("-carmodel.layer")]


def switch_name(data):
    hits = list(SWITCH.finditer(data))
    if len(hits) != 1:
        raise SystemExit(f"{len(hits)} switchName entries in a carmodel layer")
    m = hits[0]
    return m, data[m.end():m.end() + data[m.end() - 1]].decode()


def apply(e, year, extra=False):
    changes = []
    for r in layers(e, year):
        data = e.frag_data(r, 0)
        m, old = switch_name(data)
        donor = old.lower()
        new = label(donor, year, extra)
        if new != old:
            e.set_frag(r, 0, data[:m.end()] + new.encode() + data[m.end() + len(old):])
        changes.append((r.name, donor, old, new))
    if not changes:
        raise SystemExit(f"{year}: a car with no -carmodel.layer")
    return changes


def name_car(path, year, extra):
    e = erpw.Erp(str(path))
    changes = apply(e, year, extra)
    if any(old != new for _, _, old, new in changes):
        tmp = path.with_suffix(".erp.tmp")
        e.to_file(str(tmp))
        os.replace(tmp, path)
    return changes


def source(year, maker, games, f1_23, game):
    src = SOURCES[str(year)]
    if "stock" in src:
        return Path(game) / STOCK_BUNDLES / f"{bundle(year, maker)}.bdl"
    root = Path(f1_23) if src["game"] == "F1 23" else Path(games) / src["game"]
    return root / src["bundle"].format(maker=maker)


def layer_count(size):
    layers, rest = divmod(size - BLOCK_HEAD - GLOBAL_CURVES * CURVE_BYTES, CURVE_BYTES + 4)
    return layers if not rest and layers > 0 else None


def parts(data):
    version, flags = struct.unpack_from("<i", data, 4)[0], 3
    if version > 0x73:
        flags = 7 if version == 0x74 else struct.unpack_from("<I", data, 0x40)[0]
    first = struct.unpack_from("<i", data, 0x10)[0] + 0x3c
    starts = []
    for m in PART.finditer(data, first):
        p = m.start()
        if p + 72 > len(data):
            break
        ver, zero, rate, size = (struct.unpack_from("<i", data, p + o)[0] for o in (4, 8, 52, 68))
        if 0x60 <= ver <= 0x7f and zero == 0 and rate == 48000 and layer_count(size):
            starts.append(p)
    if not starts or starts[0] != first or len(starts) != 2 * bin(flags & 7).count("1"):
        raise SystemExit(f"bundle parts at {starts}, part flags {flags}")
    return starts


def widen(data):
    out, at = bytearray(), 0
    for p in parts(data):
        size = struct.unpack_from("<i", data, p + 68)[0]
        layers = layer_count(size)
        if layers == LAYERS:
            continue
        if LAYERS % layers:
            raise SystemExit(f"a bundle part with {layers} layers")
        block = data[p + 72:p + 72 + size]
        curves = BLOCK_HEAD + (layers + GLOBAL_CURVES) * CURVE_BYTES
        layer = [block[BLOCK_HEAD + i * CURVE_BYTES:BLOCK_HEAD + (i + 1) * CURVE_BYTES] for i in range(layers)]
        mask = [block[curves + i * 4:curves + i * 4 + 4] for i in range(layers)]
        new = (block[:BLOCK_HEAD] + b"".join(layer[i % layers] for i in range(LAYERS))
               + block[BLOCK_HEAD + layers * CURVE_BYTES:curves] + b"".join(mask[i % layers] for i in range(LAYERS)))
        out += data[at:p + 68] + struct.pack("<i", len(new)) + new
        at = p + 72 + size
    out += data[at:]
    out = bytes(out)
    if any(layer_count(struct.unpack_from("<i", out, p + 68)[0]) != LAYERS for p in parts(out)):
        raise SystemExit("a bundle part kept its old layer count")
    return out


def copy_bundles(year, makers, games, f1_23, game):
    out = year_dir(year) / "sounds"
    meta, keep = [], set()
    for maker in sorted(makers):
        name = bundle(year, maker)
        src = source(year, maker, games, f1_23, game)
        if not src.is_file():
            raise SystemExit(f"{year} {maker}: no {src}")
        if "stock" in SOURCES[str(year)]:
            continue
        target = out / f"{name}.bdl"
        target.parent.mkdir(parents=True, exist_ok=True)
        data = widen(src.read_bytes())
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        keep.add(target.name)
        meta.append({"name": name, "maker": maker, "game": SOURCES[str(year)]["game"],
                     "source": SOURCES[str(year)]["bundle"].format(maker=maker),
                     "source_sha256": hashlib.sha256(src.read_bytes()).hexdigest(), "bytes": target.stat().st_size,
                     "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
    if out.exists():
        for stale in out.glob("*.bdl"):
            if stale.name not in keep:
                stale.unlink()
    return meta


def car_records(by, donor, new, bundle_name):
    records = []
    for field in FIELDS:
        src = by.get(gamedb.fnv1a64(f"{CAR_DATA}.{donor}.{field}"))
        if src is None:
            raise SystemExit(f"{donor}: no {CAR_DATA} {field}")
        key = f"{CAR_DATA}.{new}.{field}"
        if gamedb.fnv1a64(key) in by:
            raise SystemExit(f"{key} is in the stock database")
        value = {"_metadata_": new.encode(), "bundle_name": f"{bundle_name}.bdl".encode()}.get(field, src.value)
        records.append({"key": key, "type": src.type, "value": value.hex()})
    return records


def parse_years(text):
    a, _, b = text.partition("-")
    return range(int(a), int(b or a) + 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--years", default="2016-2024")
    ap.add_argument("--games", required=True, help="the folder holding the F1 2016 to F1 2022 game folders")
    ap.add_argument("--f1-23", required=True, help="the F1 23 folder")
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25",
                    help="the F1 25 folder with the stock database")
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()
    by = {r.key: r for r in gamedb.read((Path(args.game) / DB).read_bytes())}
    labels = {}
    for year in parse_years(args.years):
        todo = cars(year)
        with ProcessPoolExecutor(args.jobs) as pool:
            done = list(pool.map(name_car, [t[0] for t in todo], [year] * len(todo), [t[3] for t in todo]))
        out = []
        for (path, key, maker, extra), changes in zip(todo, done):
            for layer, donor, old, new in changes:
                if new in labels:
                    raise SystemExit(f"{new}: the label of {labels[new]} and of {year} {key}")
                labels[new] = f"{year} {key}"
                out.append({"key": key, "maker": maker, "donor": donor, "label": new, "bundle": bundle(year, maker),
                            "records": car_records(by, donor, new, bundle(year, maker))})
                print(f"{year} {key}: switchName {old} -> {new}, {bundle(year, maker)}", flush=True)
        meta = copy_bundles(year, {c["maker"] for c in out}, args.games, args.f1_23, args.game)
        folder = year_dir(year) / "sounds"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "meta.json").write_text(json.dumps({"bundles": meta, "cars": out}, indent=1) + "\n")
        print(f"{year}: {len(meta)} bundle(s), {sum(m['bytes'] for m in meta)} bytes", flush=True)


if __name__ == "__main__":
    main()
