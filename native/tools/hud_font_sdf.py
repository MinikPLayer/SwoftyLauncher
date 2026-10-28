#!/usr/bin/env python3
"""Generate the overlay's signed distance field font from Titillium Web SemiBold (SIL OFL 1.1).

  nix-shell -p "python3.withPackages(p: [p.freetype-py p.numpy p.scipy p.fonttools p.pillow])" \\
      --run "python3 native/tools/hud_font_sdf.py [--preview out.png]"

Downloads the font and its licence from google/fonts at a pinned commit (cached in native/build/fonts/),
checks their sha256, and writes native/src/core/hud_font_data.hpp (metrics and kerning),
native/src/core/hud_font_atlas.hpp (the atlas as an 8-bit greyscale PNG), native/build/fonts/atlas.png
and native/third_party/titillium-web/OFL.txt. --preview renders sample text from the atlas at 12-72 px.

Atlas texels hold 0.5 + d / (2 * SPREAD), d the signed distance to the outline in atlas pixels (positive
inside), sampled at each texel centre from an exact distance transform of an 8x supersampled raster.
"""
import argparse, hashlib, io, os, sys, urllib.request

import freetype
import numpy as np
from fontTools.ttLib import TTFont
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
COMMIT = "e7db6ca603fd4862010383ff17ffd6cc4ffac1c0"
BASE = f"https://raw.githubusercontent.com/google/fonts/{COMMIT}/ofl/titilliumweb/"
FILES = {
    "TitilliumWeb-SemiBold.ttf": "7c92f15302d7ac1836015cb9cf4068796406667b304a061b75df8e11f1648c81",
    "OFL.txt": "c0562e066174197902a4a10606673d189f067970ef89c6013943b9cf182aadbc",
}
NAME = "Titillium Web SemiBold"
EM = 48
SPREAD = 8
SUPER = 8
GUTTER = 1
MIN_KERN = 0.004
EXTRA = [0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A,
         0x20AC, 0x2122, 0x0152, 0x0153, 0x0160, 0x0161, 0x0178, 0x017D, 0x017E, 0x2190, 0x2191, 0x2192, 0x2193,
         0x25B2, 0x25BC, 0x2212]
ZERO_WIDTH = {0x00AD}


def fetch(name):
    cache = os.path.join(NATIVE, "build", "fonts")
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, name)
    if not os.path.exists(path) or hashlib.sha256(open(path, "rb").read()).hexdigest() != FILES[name]:
        data = urllib.request.urlopen(BASE + name).read()
        if hashlib.sha256(data).hexdigest() != FILES[name]:
            sys.exit(f"{name}: sha256 mismatch from {BASE + name}")
        open(path, "wb").write(data)
    return path


def codepoints(cmap):
    want = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + EXTRA
    have = [c for c in want if c in cmap or c in ZERO_WIDTH]
    return sorted(set(have)), [c for c in want if c not in cmap and c not in ZERO_WIDTH]


def raster(face, cp, upm, bbox):
    """The glyph's atlas cell (h x w texels) and its rect in atlas pixels relative to the pen, y down."""
    xmin, ymin, xmax, ymax = bbox
    s = EM / upm
    left = int(np.floor(xmin * s)) - SPREAD
    right = int(np.ceil(xmax * s)) + SPREAD
    top = int(np.floor(-ymax * s)) - SPREAD
    bottom = int(np.ceil(-ymin * s)) + SPREAD
    w, h = right - left, bottom - top
    face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_NO_HINTING)
    g = face.glyph
    bm = g.bitmap
    src = np.array(bm.buffer, dtype=np.uint8).reshape(bm.rows, bm.pitch)[:, :bm.width] if bm.rows else None
    canvas = np.zeros((h * SUPER, w * SUPER), dtype=bool)
    if src is not None:
        ox = g.bitmap_left - left * SUPER
        oy = -g.bitmap_top - top * SUPER
        if ox < 0 or oy < 0 or ox + bm.width > canvas.shape[1] or oy + bm.rows > canvas.shape[0]:
            sys.exit(f"U+{cp:04X}: raster outside its cell")
        canvas[oy:oy + bm.rows, ox:ox + bm.width] = src >= 128
    inside = ndimage.distance_transform_edt(canvas) - 0.5
    outside = ndimage.distance_transform_edt(~canvas) - 0.5
    d = np.where(canvas, inside, -outside) / SUPER
    d = d.reshape(h, SUPER, w, SUPER).mean(axis=(1, 3))
    cell = np.clip(np.rint((0.5 + d / (2 * SPREAD)) * 255), 0, 255).astype(np.uint8)
    return cell, (left, top, right, bottom)


def pack(sizes, width):
    x = y = row = 0
    spots = []
    for w, h in sizes:
        if x + w > width:
            x, y, row = 0, y + row + GUTTER, 0
        spots.append((x, y))
        x += w + GUTTER
        row = max(row, h)
    return spots, y + row


def kerning(font, cmap, cps):
    names = {cp: cmap[cp] for cp in cps if cp in cmap}
    by_name = {}
    for cp, n in names.items():
        by_name.setdefault(n, []).append(cp)
    pairs = {}
    gpos = font["GPOS"].table if "GPOS" in font else None
    lookups = set()
    if gpos and gpos.ScriptList and gpos.FeatureList:
        feats = set()
        for sr in gpos.ScriptList.ScriptRecord:
            if sr.ScriptTag not in ("DFLT", "latn"):
                continue
            systems = [sr.Script.DefaultLangSys] + [ls.LangSys for ls in sr.Script.LangSysRecord]
            for ls in systems:
                if ls:
                    feats.update(ls.FeatureIndex)
        for i in feats:
            fr = gpos.FeatureList.FeatureRecord[i]
            if fr.FeatureTag == "kern":
                lookups.update(fr.Feature.LookupListIndex)
    glyphs = list(by_name)
    for li in sorted(lookups):
        lookup = gpos.LookupList.Lookup[li]
        subs = []
        for st in lookup.SubTable:
            if lookup.LookupType == 9:
                if st.ExtensionLookupType != 2:
                    continue
                st = st.ExtSubTable
            elif lookup.LookupType != 2:
                continue
            subs.append(st)
        prepared = []
        for st in subs:
            cov = {g: i for i, g in enumerate(st.Coverage.glyphs)}
            if st.Format == 1:
                sets = {}
                for g, i in cov.items():
                    sets[g] = {r.SecondGlyph: r for r in st.PairSet[i].PairValueRecord}
                prepared.append((1, cov, sets))
            else:
                c1 = st.ClassDef1.classDefs if st.ClassDef1 else {}
                c2 = st.ClassDef2.classDefs if st.ClassDef2 else {}
                prepared.append((2, cov, (c1, c2, st.Class1Record)))
        for a in glyphs:
            for b in glyphs:
                for fmt, cov, data in prepared:
                    if a not in cov:
                        continue
                    if fmt == 1:
                        rec = data[a].get(b)
                        if rec is None:
                            continue
                    else:
                        c1, c2, records = data
                        rec = records[c1.get(a, 0)].Class2Record[c2.get(b, 0)]
                    v1 = getattr(rec, "Value1", None)
                    v2 = getattr(rec, "Value2", None)
                    adj = (getattr(v1, "XAdvance", 0) or 0) if v1 else 0
                    adj += (getattr(v2, "XPlacement", 0) or 0) if v2 else 0
                    if adj:
                        pairs[(a, b)] = pairs.get((a, b), 0) + adj
                    break
    if not lookups and "kern" in font:
        for t in font["kern"].kernTables:
            for (a, b), v in t.kernTable.items():
                if a in by_name and b in by_name:
                    pairs[(a, b)] = pairs.get((a, b), 0) + v
    upm = font["head"].unitsPerEm
    out = {}
    for (a, b), v in pairs.items():
        if abs(v / upm) < MIN_KERN:
            continue
        for ca in by_name[a]:
            for cb in by_name[b]:
                out[ca << 16 | cb] = v / upm
    return out


def num(v):
    s = f"{v:.6g}"
    if "e" not in s and "." not in s:
        s += ".0"
    return s + "f"


def write_data(path, metrics, glyphs, kerns, w, h):
    o = ["#pragma once", "#include <cstdint>", "",
         "// Generated by native/tools/hud_font_sdf.py from Titillium Web SemiBold (SIL OFL 1.1, "
         "native/third_party/titillium-web/OFL.txt). Do not edit.", "",
         "namespace hud::font {", "",
         f'constexpr const char *NAME = "{NAME}";',
         f"constexpr int ATLAS_W = {w}, ATLAS_H = {h};",
         f"constexpr float ATLAS_EM = {EM};",
         f"constexpr float SPREAD = {SPREAD};",
         "constexpr float ASCENDER = %s, DESCENDER = %s, CAP_HEIGHT = %s, X_HEIGHT = %s, LINE_GAP = %s;" % tuple(
             num(metrics[k]) for k in ("ascender", "descender", "cap", "x", "gap")), "",
         "struct Glyph {", "    uint32_t cp;", "    float advance;", "    float x0, y0, x1, y1;",
         "    uint16_t u0, v0, u1, v1;", "};", "",
         "constexpr Glyph GLYPHS[] = {"]
    for g in glyphs:
        o.append("    {0x%04x, %s, %s, %s, %s, %s, %d, %d, %d, %d}," % (
            g["cp"], num(g["advance"]), *(num(v) for v in g["quad"]), *g["uv"]))
    o += ["};", "constexpr uint32_t GLYPH_COUNT = sizeof(GLYPHS) / sizeof(GLYPHS[0]);", "",
          "struct Kern {", "    uint32_t pair;", "    float adjust;", "};", ""]
    if kerns:
        o.append("constexpr Kern KERNS[] = {")
        for pair in sorted(kerns):
            o.append("    {0x%08x, %s}," % (pair, num(kerns[pair])))
        o += ["};", "constexpr uint32_t KERN_COUNT = sizeof(KERNS) / sizeof(KERNS[0]);"]
    else:
        o += ["constexpr Kern KERNS[] = {{0xffffffff, 0.0f}};", "constexpr uint32_t KERN_COUNT = 0;"]
    o += ["", "}", ""]
    open(path, "w").write("\n".join(o))


def write_atlas(path, png):
    o = ["#pragma once", "",
         "// Generated by native/tools/hud_font_sdf.py: the signed distance field atlas of hud_font_data.hpp as an "
         "8-bit greyscale PNG. Do not edit.", "",
         "namespace hud::font {", "",
         "alignas(16) inline constexpr unsigned char ATLAS_PNG[] = {"]
    for i in range(0, len(png), 24):
        o.append("    " + ", ".join("0x%02x" % b for b in png[i:i + 24]) + ",")
    o += ["};", "inline constexpr unsigned ATLAS_PNG_SIZE = sizeof(ATLAS_PNG);", "", "}", ""]
    open(path, "w").write("\n".join(o))


def preview(path, atlas, glyphs, kerns, text="Swofty Launcher 1:23.456 AVATAR Ünïcödé "
            "ÆØÅ ß €", sizes=(12, 18, 24, 36, 48, 72)):
    by_cp = {g["cp"]: g for g in glyphs}
    a = atlas.astype(np.float32) / 255
    rows = []
    for px in sizes:
        scale = px / EM
        width = int(sum(by_cp.get(ord(c), by_cp[0x3F])["advance"] for c in text) * px) + 40
        height = int(px * 1.6) + 10
        img = np.zeros((height, width), dtype=np.float32)
        pen, base = 20.0, 5 + px * 1.15
        prev = None
        for ch in text:
            g = by_cp.get(ord(ch), by_cp[0x3F])
            if prev is not None:
                pen += kerns.get(prev << 16 | g["cp"], 0) * px
            x0, y0, x1, y1 = g["quad"]
            u0, v0, u1, v1 = g["uv"]
            if u1 > u0:
                qx0, qx1, qy0, qy1 = pen + x0 * px, pen + x1 * px, base + y0 * px, base + y1 * px
                ix0, ix1 = int(np.floor(qx0)), int(np.ceil(qx1))
                iy0, iy1 = int(np.floor(qy0)), int(np.ceil(qy1))
                xs = np.arange(ix0, ix1) + 0.5
                ys = np.arange(iy0, iy1) + 0.5
                ax = u0 + (xs - qx0) / (qx1 - qx0) * (u1 - u0) - 0.5
                ay = v0 + (ys - qy0) / (qy1 - qy0) * (v1 - v0) - 0.5
                inx = (xs >= qx0) & (xs < qx1)
                iny = (ys >= qy0) & (ys < qy1)
                ax = np.clip(ax, 0, a.shape[1] - 1.001)
                ay = np.clip(ay, 0, a.shape[0] - 1.001)
                fx, fy = np.floor(ax).astype(int), np.floor(ay).astype(int)
                tx, ty = ax - fx, ay - fy
                s00 = a[fy[:, None], fx[None, :]]
                s01 = a[fy[:, None], fx[None, :] + 1]
                s10 = a[fy[:, None] + 1, fx[None, :]]
                s11 = a[fy[:, None] + 1, fx[None, :] + 1]
                v = (s00 * (1 - tx) + s01 * tx) * (1 - ty)[:, None] + (s10 * (1 - tx) + s11 * tx) * ty[:, None]
                wdt = 0.7 / (2 * SPREAD * scale)
                t = np.clip((v - (0.5 - wdt)) / (2 * wdt), 0, 1)
                cov = t * t * (3 - 2 * t) * (iny[:, None] & inx[None, :])
                sub = img[iy0:iy1, ix0:ix1]
                np.maximum(sub, cov[:sub.shape[0], :sub.shape[1]], out=sub)
            pen += g["advance"] * px
            prev = g["cp"]
        rows.append(img)
    w = max(r.shape[1] for r in rows)
    full = np.concatenate([np.pad(r, ((0, 0), (0, w - r.shape[1]))) for r in rows])
    out = 40 + full * (255 - 40)
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "L").save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", help="also render sample text from the atlas to this PNG")
    args = ap.parse_args()
    ttf = fetch("TitilliumWeb-SemiBold.ttf")
    ofl = fetch("OFL.txt")
    lic = os.path.join(NATIVE, "third_party", "titillium-web", "OFL.txt")
    os.makedirs(os.path.dirname(lic), exist_ok=True)
    open(lic, "wb").write(open(ofl, "rb").read())

    font = TTFont(ttf)
    upm = font["head"].unitsPerEm
    cmap = font.getBestCmap()
    glyf = font["glyf"]
    hmtx = font["hmtx"]
    os2 = font["OS/2"]
    hhea = font["hhea"]
    metrics = {"ascender": hhea.ascent / upm, "descender": hhea.descent / upm, "gap": hhea.lineGap / upm,
               "cap": os2.sCapHeight / upm, "x": os2.sxHeight / upm}
    cps, missing = codepoints(cmap)
    face = freetype.Face(ttf)
    face.set_pixel_sizes(0, EM * SUPER)

    glyphs, cells = [], []
    for cp in cps:
        if cp in ZERO_WIDTH and cp not in cmap:
            glyphs.append({"cp": cp, "advance": 0.0, "quad": (0, 0, 0, 0), "uv": (0, 0, 0, 0)})
            continue
        name = cmap[cp]
        g = glyf[name]
        adv = hmtx[name][0] / upm
        if g.numberOfContours == 0 or not hasattr(g, "xMin"):
            glyphs.append({"cp": cp, "advance": adv, "quad": (0, 0, 0, 0), "uv": (0, 0, 0, 0)})
            continue
        g.recalcBounds(glyf)
        cell, (l, t, r, b) = raster(face, cp, upm, (g.xMin, g.yMin, g.xMax, g.yMax))
        glyphs.append({"cp": cp, "advance": adv, "quad": (l / EM, t / EM, r / EM, b / EM), "cell": len(cells)})
        cells.append(cell)

    order = sorted(range(len(cells)), key=lambda i: -cells[i].shape[0])
    sizes = [(cells[i].shape[1], cells[i].shape[0]) for i in order]
    best = None
    for width in (512, 1024):
        spots, used = pack(sizes, width)
        height = 1
        while height < used:
            height *= 2
        if height > 2048:
            continue
        key = (width * height, max(width, height))
        if best is None or key < best[0]:
            best = (key, width, height, spots)
    if best is None:
        sys.exit("the glyphs do not fit a 1024 x 2048 atlas")
    _, aw, ah, spots = best
    atlas = np.zeros((ah, aw), dtype=np.uint8)
    place = {}
    for (x, y), i in zip(spots, order):
        c = cells[i]
        atlas[y:y + c.shape[0], x:x + c.shape[1]] = c
        place[i] = (x, y, x + c.shape[1], y + c.shape[0])
    for g in glyphs:
        if "cell" in g:
            g["uv"] = place.pop(g["cell"])
            del g["cell"]

    kerns = kerning(font, cmap, cps)
    buf = io.BytesIO()
    Image.fromarray(atlas, "L").save(buf, "PNG", optimize=True, compress_level=9)
    png = buf.getvalue()
    build = os.path.join(NATIVE, "build", "fonts")
    open(os.path.join(build, "atlas.png"), "wb").write(png)
    write_data(os.path.join(NATIVE, "src", "core", "hud_font_data.hpp"), metrics, glyphs, kerns, aw, ah)
    write_atlas(os.path.join(NATIVE, "src", "core", "hud_font_atlas.hpp"), png)
    if args.preview:
        preview(args.preview, atlas, glyphs, kerns)
    print(f"{NAME}: {len(glyphs)} glyphs, {len(kerns)} kern pairs, atlas {aw}x{ah}, PNG {len(png)} bytes")
    if missing:
        print("not in the font: " + " ".join(f"U+{c:04X}" for c in missing))


if __name__ == "__main__":
    main()
