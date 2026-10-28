"""Helmet textures of F1 2016 to F1 22 game folders, for build_helmets.py.

F1 2020 and later ship each driver look's helmet archive (<yy>_asset_groups/character_package/drivers/male/<look>/
<stem>_helmet.erp): surface version 8 (zstd fragments 0x10, stored 0x90, or zlib 1), whose third fragment names a
`.mipmaps` sidecar under the game folder. The version 8 descriptor is `u8 n, path[n], u32 count`, then count times
`{u8 compression, u64 offset, u64 packed, u64 size}`; version 9 has a `u8 0` after the path and a second u8 per level.
Compression 0 is stored, 1 zlib and 3 an LZ4 block.

F1 2016 to F1 2019 hold the archives in game.dat; only the sidecars are loose
(character_package/drivers/male/<look>/textures/<stem>_helmet_<part>.<tga|tif.2019>.mipmaps), so a texture's
format, size and levels are read from the sidecar alone: its levels follow each other from the largest, each an LZ4
block or stored. A shape fits when every level decodes or is stored and the levels end with the file; of two shapes
of the same byte size (BC1 at twice the area of BC7) the one whose largest level decodes smoother wins.

helmet_d of F1 2016 to F1 2020 is square with the helmet in its top half (the bottom half holds gloves, boots and
visor parts); helmet_n of F1 2016 to F1 2018 too. A square texture is cropped to its top half.
"""

import struct
import zlib
from pathlib import Path

import numpy as np

import bcn
import erpw
import f1_23_suits

BC1, BC5, BC7 = bcn.BC1[1], bcn.BC5[0], bcn.BC7[1]
SHAPES = [(4096, 4096), (4096, 2048), (2048, 2048), (2048, 1024), (1024, 1024), (1024, 512), (512, 512)]
DRIVERS = "character_package/drivers/male"


def lz4_prefix(src, pos, n):
    out = bytearray()
    end = len(src)
    try:
        while pos < end:
            token = src[pos]
            pos += 1
            lit = token >> 4
            if lit == 15:
                while True:
                    b = src[pos]
                    pos += 1
                    lit += b
                    if b != 255:
                        break
            out += src[pos:pos + lit]
            pos += lit
            if len(out) >= n:
                return (bytes(out), pos) if len(out) == n else (None, pos)
            off = src[pos] | (src[pos + 1] << 8)
            pos += 2
            if off == 0 or off > len(out):
                return None, pos
            ml = (token & 15) + 4
            if (token & 15) == 15:
                while True:
                    b = src[pos]
                    pos += 1
                    ml += b
                    if b != 255:
                        break
            if len(out) + ml > n:
                return None, pos
            start = len(out) - off
            if off >= ml:
                out += out[start:start + ml]
            else:
                out += (out[start:] * (ml // off + 1))[:ml]
    except IndexError:
        return None, pos
    return None, pos


def frag(e, r, i):
    fr = r.frags[i]
    if fr.comp in (0x10, 0x11):
        return erpw.zstd_d(fr.raw)
    if fr.comp == 1:
        return zlib.decompress(fr.raw)
    return fr.raw


def descriptor(desc, version):
    n = desc[0]
    path = desc[1:1 + n].rstrip(b"\0").decode().replace("\\", "/")
    at = 1 + n + (1 if version >= 9 else 0)
    count = struct.unpack_from("<I", desc, at)[0]
    at += 4
    entries = []
    for _ in range(count):
        if version >= 9:
            at += 1
        comp, off, packed, size = struct.unpack_from("<BQQQ", desc, at)
        at += 25
        entries.append((comp, off, packed, size))
    return path, entries


def read_level(full, entry):
    comp, off, packed, size = entry
    with open(full, "rb") as f:
        f.seek(off)
        raw = f.read(packed)
    if comp == 3:
        import lz4.block
        return lz4.block.decompress(raw, uncompressed_size=size)
    if comp == 1:
        return zlib.decompress(raw)
    if comp != 0:
        raise SystemExit(f"{full}: level compression {comp}")
    return raw


def archive_texture(game_dir, e, r):
    fmt, w, h, n = bcn.Surface.header(frag(e, r, 0))
    emb = frag(e, r, 1)
    levels, at = [], 0
    for size in bcn.level_sizes(fmt, w, h, n):
        levels.append(emb[at:at + size])
        at += size
    f2 = frag(e, r, 2) if len(r.frags) > 2 else b""
    if not (f2 and f2[0]):
        return fmt, w, h, levels
    path, entries = descriptor(f2, r.unk[0])
    full = Path(game_dir) / path
    if not entries:
        return fmt, w, h, levels
    if not full.exists():
        raise SystemExit(f"{full}: not downloaded")
    k = len(entries)
    side = [read_level(full, lv) for lv in entries]
    if [len(s) for s in side] != bcn.level_sizes(fmt, w << k, h << k, k):
        raise SystemExit(f"{full}: levels of the wrong size")
    return fmt, w << k, h << k, side + levels


def archive_textures(game_dir, year, look):
    stem = look.split("_", 1)[1]
    path = Path(game_dir) / f"{year}_asset_groups" / DRIVERS / look / f"{stem}_helmet.erp"
    e = erpw.Erp(str(path))
    out = {}
    for name, r in f1_23_suits.surfaces(e).items():
        for part in "dns":
            if f"/{look}/textures/" in name and name.endswith(f"_helmet_{part}.tif.image"):
                out[part] = archive_texture(game_dir, e, r)
    return out


def chain_of(src, fmt, w, h):
    levels, pos = [], 0
    for size in bcn.level_sizes(fmt, w, h, 16):
        if pos == len(src):
            break
        data, end = lz4_prefix(src, pos, size)
        if data is None:
            if pos + size > len(src):
                return None
            data, end = src[pos:pos + size], pos + size
        levels.append(data)
        pos = end
        if max(1, w >> len(levels)) < 4 and max(1, h >> len(levels)) < 4:
            break
    return levels if pos == len(src) and levels else None


def roughness(fmt, w, h, data):
    k = 0
    while (w >> k) > 256:
        k += 1
    img = bcn.decode(fmt, data, w, h)[::1 << k, ::1 << k, :3].astype(np.int32)
    return np.abs(np.diff(img, axis=0)).mean() + np.abs(np.diff(img, axis=1)).mean()


def sidecar_texture(path, formats):
    src = Path(path).read_bytes()
    fits = []
    for fmt in formats:
        for w, h in SHAPES:
            levels = chain_of(src, fmt, w, h)
            if levels:
                fits.append((fmt, w, h, levels))
    if not fits:
        raise SystemExit(f"{path}: no shape fits its {len(src)} bytes")
    if len(fits) > 1:
        fits.sort(key=lambda f: roughness(f[0], f[1], f[2], f[3][0]))
    return fits[0]


def sidecar_textures(game_dir, look):
    folder = Path(game_dir) / DRIVERS / look / "textures"
    stem = look.split("_", 1)[1]
    out = {}
    for part, formats in (("d", (BC1, BC7)), ("n", (BC5,))):
        hits = sorted(folder.glob(f"{stem}_helmet_{part}.*mipmaps"))
        if hits:
            out[part] = sidecar_texture(hits[0], formats)
    return out


def textures(game_dir, year, look):
    return archive_textures(game_dir, year, look) if year >= 2020 else sidecar_textures(game_dir, look)


def top_half(fmt, w, h, levels):
    if w != h:
        return fmt, w, h, levels
    out = []
    for k, data in enumerate(levels):
        lh = max(1, h >> k)
        if lh // 2 < 4:
            break
        out.append(data[:len(data) // 2])
    return fmt, w, h // 2, out


def capped(fmt, w, h, levels, width):
    while w > width and len(levels) > 1:
        w, h, levels = w // 2, h // 2, levels[1:]
    return fmt, w, h, levels
