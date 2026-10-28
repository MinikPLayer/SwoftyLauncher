import hashlib
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

import heads
from heads import erpw, gfx_material

SHADER = "eaid://shader_package/character/skin.nefx2"
F1_25 = gfx_material.write25(gfx_material.Material(25, SHADER, [
    gfx_material.Param("me_tint", gfx_material.CONST, struct.pack("<f", 1.0), 3, 1, 0, 0),
    gfx_material.Param("me_baseTex", gfx_material.TEXTURE, "eaid://character_package/x_d.tif", 2, 0, 0xffffffff, 0),
], [["PerMaterial", 4]]))
F1_23 = gfx_material.write23(gfx_material.Material(23, SHADER, [
    gfx_material.Param("me_tint", gfx_material.CONST, struct.pack("<f", 1.0)),
    gfx_material.Param("me_baseTex", gfx_material.TEXTURE, "eaid://character_package/x_d.tif"),
    gfx_material.Param("me_base", gfx_material.SAMPLER, "eaid://character_package/x_d.tif.sampler"),
]))
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"


def write_erp(path, entries):
    e = erpw.Erp.__new__(erpw.Erp)
    e.hdr, e.ver, e.res = b"ERPK" + struct.pack("<I", 3) + bytes(0x28), 3, []
    for name, kind, version, data, comp in entries:
        r, f = erpw.Res(), erpw.Frag()
        f.name, f.flags, f.comp, f.size = b"temp", 0, comp, len(data)
        f.raw = erpw.zstd_c(data) if comp in heads.ZSTD_CODES else data
        f.packed = len(f.raw)
        r.name, r.type, r.unk, r.frags, r.hash = name, kind.ljust(16, b"\0"), struct.pack("<I", version), [f], bytes(16)
        e.res.append(r)
    e.to_file(str(path))


def version_offsets(path):
    b = path.read_bytes()
    out, pos = {}, 0x38
    for r in erpw.Erp(str(path)).res:
        name_len = struct.unpack_from("<H", b, pos + 4)[0]
        out[r.name] = pos + 6 + name_len + 16
        pos += 4 + struct.unpack_from("<I", b, pos)[0]
    return out


class Heads(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name) / "head.erp"

    def tearDown(self):
        self.dir.cleanup()

    def test_f1_25_materials_get_resource_version_2_and_nothing_else_changes(self):
        write_erp(self.path, [
            ("a/materials/head.material", b"GfxMaterialRes", 1, F1_25, erpw.STORED),
            ("a/materials/eye.material", b"GfxMaterialRes", 1, F1_25, 0x10),
            ("a/materials/arm.material", b"GfxMaterialRes", 1, F1_25, erpw.ZSTD),
            ("a/materials/lashes.material", b"GfxMaterialRes", 1, F1_23, 0x10),
            ("b/materials/head.material", b"GfxMaterialRes", 2, F1_25, erpw.ZSTD),
            ("a/textures/x_d.tif", b"GfxSurfaceRes", 1, b"surface", erpw.STORED),
        ])
        before = self.path.read_bytes()
        self.assertEqual(heads.relabel(self.path), ["head.material", "eye.material", "arm.material"])
        after = self.path.read_bytes()
        self.assertEqual(len(after), len(before))
        at = version_offsets(self.path)
        changed = {at[n] for n in ("a/materials/head.material", "a/materials/eye.material", "a/materials/arm.material")}
        self.assertEqual({i for i in range(len(after)) if after[i] != before[i]}, changed)
        self.assertEqual([after[i:i + 4] for i in sorted(changed)], [heads.VERSION_25] * 3)
        self.assertEqual(heads.relabel(self.path), [])

    def test_a_relabelled_zstd_fragment_stays_a_zstd_frame(self):
        write_erp(self.path, [("a/materials/eye.material", b"GfxMaterialRes", 1, F1_25, 0x10)])
        heads.relabel(self.path)
        f = erpw.Erp(str(self.path)).res[0].frags[0]
        self.assertEqual((f.comp, f.raw[:4], erpw.zstd_d(f.raw)), (0x10, ZSTD_MAGIC, F1_25))

    @unittest.skipUnless(heads.OUT.is_dir(), "heads/ is not built")
    def test_built_heads_hold_zstd_frames_and_version_2_f1_25_materials(self):
        for path in sorted(heads.OUT.rglob("*.erp")):
            for r in erpw.Erp(str(path)).res:
                for f in r.frags:
                    if f.comp in heads.ZSTD_CODES:
                        self.assertEqual(f.raw[:4], ZSTD_MAGIC, f"{path.name}: {r.name}")
                if r.type.rstrip(b"\0") == b"GfxMaterialRes" and heads.f1_25_layout(heads.frag(r, 0)):
                    self.assertEqual(r.unk[:4], heads.VERSION_25, f"{path.name}: {r.name}")

    def test_rename_renames_names_and_fragments_but_not_a_sidecar_description(self):
        side = b"\x40character_package_shared/heads/male_ea/male_ls356/textures/male_ls356_head_n.tif.mipmaps\0"
        write_erp(self.path, [
            ("eaid://character_package/heads/male_ea/male_ls356/materials/head.material", b"GfxMaterialRes", 2,
             b"eaid://character_package_shared/heads/male_ea/male_ls356/textures/male_ls356_head_d.tif", erpw.ZSTD),
            ("eaid://character_package_shared/heads/male_ea/male_ls356/textures/male_ls356_head_n.tif.image",
             b"GfxSurfaceRes", 9, b"head", erpw.STORED),
        ])
        e = erpw.Erp(str(self.path))
        image = e.res[1]
        for data in (b"male_ls356", side):
            f = erpw.Frag()
            f.name, f.flags, f.comp, f.size, f.raw = b"temp", 0, erpw.ZSTD, len(data), erpw.zstd_c(data)
            f.packed = len(f.raw)
            image.frags.append(f)
        heads.rename(e, b"male_ls356", b"cs_pal_356")
        self.assertEqual([r.name for r in e.res], [
            "eaid://character_package/heads/male_ea/cs_pal_356/materials/head.material",
            "eaid://character_package_shared/heads/male_ea/cs_pal_356/textures/cs_pal_356_head_n.tif.image"])
        self.assertEqual(heads.frag(e.res[0], 0),
                         b"eaid://character_package_shared/heads/male_ea/cs_pal_356/textures/cs_pal_356_head_d.tif")
        self.assertEqual(heads.frag(image, 1), b"cs_pal_356")
        self.assertEqual(heads.frag(image, 2), side)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "numpy is not installed")
    def test_an_f1_2016_colour_map_gets_its_small_levels(self):
        import bcn
        import numpy as np
        import zipfile
        img = np.zeros((2048, 2048, 4), np.uint8)
        img[...] = (200, 120, 60, 255)
        raw = b"".join(bcn.encode(heads.FACE_FORMAT, lv) for lv in bcn.mip_chain(img)[:4])
        zpath = Path(self.dir.name) / "face.zip"
        with zipfile.ZipFile(zpath, "w") as z:
            z.writestr("x_head_d.tga.mipmaps", raw)
        heads.DATA["sources"]["test_face"] = {"zip": "face.zip", "root": ""}
        try:
            chain = heads.f1_2016_chain([self.dir.name], {"source": "test_face", "mipmaps": "x_head_d.tga.mipmaps"}, "x")
        finally:
            del heads.DATA["sources"]["test_face"]
        sizes = bcn.level_sizes(heads.FACE_FORMAT, 2048, 2048, heads.FACE_LEVELS)
        self.assertEqual(len(chain), sum(sizes))
        self.assertEqual(chain[:len(raw)], raw)
        smallest = bcn.decode(heads.FACE_FORMAT, chain[-sizes[-1]:], 4, 4)
        self.assertTrue(np.all(np.abs(smallest[..., :3].astype(int) - (200, 120, 60)) <= 8))

    def test_head_rows(self):
        table = "entrants.animation.driver_heads"
        self.assertEqual(heads.row_op(table, "kimi_raikkonen"), {
            "op": "add-row", "table": table, "from": f"{table}.driver_head[_metadata_=kimi_raikkonen]",
            "set": {"_metadata_": "cs_kimi_raikkonen"}})
        for head, f in heads.FACES.items():
            face = heads.row_op(table, head)
            code = f["code"]
            scanned = not heads.generic(f["base"])
            folder = "male_driver" if scanned else "male_ea"
            template = heads.ROW_TEMPLATE if scanned else f["base"]
            self.assertEqual(face["from"], f"{table}.driver_head[_metadata_={template}]")
            self.assertEqual(face["set"], {
                "_metadata_": heads.label(head),
                "model_path": f"eaid://[CHARACTER_PACKAGE]/heads/{folder}/{code}/idf/{code}_rig_head_morph_v1",
                "has_morph_data": "True", "available_for_customisation": "False"})
        rio = heads.row_op(table, "rio_haryanto")
        self.assertEqual(rio["from"], f"{table}.driver_head[_metadata_={heads.DATA['row_from']['rio_haryanto']}]")
        self.assertEqual(rio["set"]["model_path"],
                         "eaid://[CHARACTER_PACKAGE]/heads/male_driver/rio_haryanto/idf/rio_haryanto_rig_head_morph_v1")

    def test_an_off_head_ships_nothing_and_its_drivers_wear_its_stand_in(self):
        for head, stand_in in heads.OFF.items():
            self.assertNotIn(head, heads.shipped())
            self.assertEqual(heads.worn(head), stand_in)
            self.assertNotIn(stand_in, heads.shipped())
        self.assertEqual(heads.worn("kimi_raikkonen"), "kimi_raikkonen")

    def test_face_codes_are_as_long_as_their_base_and_unique(self):
        codes = [f["code"] for f in heads.FACES.values()]
        self.assertEqual(len(set(codes)), len(codes))
        for head, f in heads.FACES.items():
            self.assertEqual(len(f["code"]), len(f["base"]), head)
            self.assertTrue(f["code"].startswith("cs_"), head)

    def test_no_face_base_is_a_driver_of_any_season(self):
        seasons = json.loads((heads.ROOT / "data" / "seasons.json").read_text())
        grid = {d for s in seasons["seasons"] for team in s["teams"] for d, _ in team["drivers"]}
        for head, f in heads.FACES.items():
            self.assertNotIn(f["base"], grid, head)

    def test_no_two_drivers_of_a_season_wear_the_same_head_or_generic_base(self):
        seasons = json.loads((heads.ROOT / "data" / "seasons.json").read_text())
        wears = {**seasons["head_overrides"], **{k: d["head"] for k, d in seasons["new_drivers"].items()}}

        def shape(head):
            head = heads.worn(head)
            return heads.FACES[head]["base"] if head in heads.FACES else head

        for season in seasons["seasons"]:
            grid = [d for team in season["teams"] for d, _ in team["drivers"] if d in wears]
            worn = [shape(wears[d]) for d in grid]
            self.assertEqual(len(set(worn)), len(worn), f"{season['year']}: {dict(zip(grid, worn))}")

    @unittest.skipUnless(heads.OUT.is_dir(), "heads/ is not built")
    def test_built_heads_hold_their_row_model(self):
        for head in heads.shipped():
            rel = (f"2025_asset_groups/{heads.SCENES}/heads/{heads.label(head)}.erp" if head in heads.FACES
                   else heads.archives(head)[0])
            names = {r.name for r in erpw.Erp(str(heads.OUT / rel)).res}
            model = heads.model_path(head).replace("[CHARACTER_PACKAGE]", "character_package") + ".idf?model"
            self.assertIn(model, names, head)

    @unittest.skipUnless(heads.OUT.is_dir(), "heads/ is not built")
    def test_built_faces_name_their_base_only_in_sidecar_descriptions(self):
        for head, f in heads.FACES.items():
            self.assertEqual(heads.face_leftovers(head), [], head)
            e = erpw.Erp(str(heads.OUT / f"2025_asset_groups/{heads.SCENES}/heads/{heads.label(head)}.erp"))
            image = [r for r in e.res if r.name.endswith(f"/{f['code']}_head_d.tif.image")]
            self.assertEqual(len(image), 1, head)
            header = struct.unpack_from("<10I", heads.frag(image[0], 0))
            self.assertEqual((header[3], header[4], header[6]), (2048, 2048, heads.FACE_LEVELS), head)
            for r in e.res:
                data = b"".join(heads.frag(r, i) for i in range(len(r.frags)))
                self.assertEqual(r.hash, hashlib.md5(data).digest(), f"{head}: {r.name}")


if __name__ == "__main__":
    unittest.main()
