import json
import tempfile
import unittest
from pathlib import Path

from compat import snapshot

DATA = "/scratch/run/work/s0000/data"


class Normalising(unittest.TestCase):
    def setUp(self):
        self.norm = snapshot.Normaliser({"data": DATA, "loader": "/opt/swofty/loader", "game": "/games/F1 25"})

    def test_posix_and_wine_paths(self):
        self.assertEqual(self.norm.text(f"{DATA}/state/x.json"), "<data>/state/x.json")
        self.assertEqual(self.norm.text("Z:\\scratch\\run\\work\\s0000\\data\\state\\x.json"), "<data>\\state\\x.json")
        self.assertEqual(self.norm.text("loader: 8 file(s) from /opt/swofty/loader"), "loader: 8 file(s) from <loader>")
        self.assertEqual(self.norm.text("/games/F1 25/F1_25.exe"), "<game>/F1_25.exe")

    def test_json_escaped_wine_paths_in_file_bytes(self):
        raw = json.dumps({"requests": "Z:\\scratch\\run\\work\\s0000\\data\\state\\requests\\compat.json"}).encode()
        self.assertEqual(self.norm.bytes(raw), b'{"requests": "<data>\\\\state\\\\requests\\\\compat.json"}')

    def test_overlay_dirs_lose_their_hash(self):
        self.assertEqual(
            self.norm.text(f"{DATA}/cache/overlay/compat/244773424b397ac3/swofty/assets.json"),
            "<overlay>/swofty/assets.json",
        )

    def test_longest_root_wins(self):
        norm = snapshot.Normaliser({"home": "/scratch", "data": DATA})
        self.assertEqual(norm.text(f"{DATA}/x"), "<data>/x")
        self.assertEqual(norm.text("/scratch/y"), "<home>/y")

    def test_timestamps_are_dropped(self):
        self.assertEqual(
            self.norm.json({"report": {"built_at": 1793155333.8, "order": ["a"]}, "path": f"{DATA}/a"}),
            {"report": {"order": ["a"]}, "path": "<data>/a"},
        )


class Parsing(unittest.TestCase):
    def test_stdout(self):
        out = snapshot.parse_stdout(
            [
                "profile compat: swofty.core -> kizi.pitlane",
                "runtime: swofty/assets.json with 0 asset group(s), 1 resource(s), 374 database edit(s), 1 language table(s), 0 overlay file(s)",
                "warning: something odd",
                "overlay 244773424b397ac3: <overlay>",
            ]
        )
        self.assertEqual(out["order"], ["swofty.core", "kizi.pitlane"])
        self.assertEqual(out["buildhash"], "244773424b397ac3")
        self.assertEqual(out["warnings"], ["something odd"])
        self.assertEqual(out["runtime"], [0, 1, 374, 1, 0])

    def test_source_classes(self):
        self.assertEqual(snapshot.source_class("<loader>/version.dll"), "loader")
        self.assertEqual(snapshot.source_class("<overlay>/swofty/assets.json"), "overlay")
        self.assertEqual(snapshot.source_class("<data>/addons/x/native/x.dll"), "addon")
        self.assertEqual(snapshot.addon_of("swofty/addons/x/native/x.dll", "<data>/addons/x/native/x.dll"), "x")
        self.assertIsNone(snapshot.addon_of("version.dll", "<loader>/version.dll"))


class Extracting(unittest.TestCase):
    def test_a_built_overlay(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "data"
            loader = Path(tmp) / "loader"
            hashdir = data / "cache/overlay/compat/0123456789abcdef"
            (hashdir / "swofty").mkdir(parents=True)
            loader.mkdir()
            (loader / "version.dll").write_bytes(b"MZ proxy")
            (data / "addons/x/native").mkdir(parents=True)
            (data / "addons/x/native/x.dll").write_bytes(b"MZ addon")
            settings = {"requests": "Z:" + str(data / "state/requests/compat.json").replace("/", "\\")}
            (hashdir / "swofty/settings.json").write_text(json.dumps(settings))
            (hashdir / "swofty/assets.json").write_text(json.dumps({"db_edits": [{"hash": "1"}, {"hash": "2"}]}))
            plan = {
                "profile": "compat",
                "buildhash": "0123456789abcdef",
                "tracks": [],
                "replace": {},
                "add": {},
                "loader": {
                    "version.dll": str(loader / "version.dll"),
                    "swofty/settings.json": str(hashdir / "swofty/settings.json"),
                    "swofty/assets.json": str(hashdir / "swofty/assets.json"),
                    "swofty/addons/x/native/x.dll": str(data / "addons/x/native/x.dll"),
                },
            }
            (hashdir / "plan.json").write_text(json.dumps(plan))
            built = {
                "lines": [f"loader: 1 file(s) from {loader}", "database: unchanged"],
                "warnings": [],
                "conflicts": [],
                "waiting": [],
                "report": {"profile": "compat", "built_at": 12.5, "pairs": [], "order": ["swofty.core", "x"]},
                "registry_override": True,
            }
            (hashdir / "built.json").write_text(json.dumps(built))
            (data / "state").mkdir()
            (data / "state/fingerprint-outputs.json").write_text(
                json.dumps({"compat": {"x": {"made": "m", "parts": {"native": "n"}}}})
            )
            norm = snapshot.Normaliser({"data": data, "loader": loader})
            stdout = f"profile compat: swofty.core -> x\noverlay 0123456789abcdef: {hashdir}\n"
            snap = snapshot.extract(data, "compat", [], {"rc": 0, "stdout": stdout, "stderr": ""}, norm)
            self.assertEqual(snap["buildhash"], "0123456789abcdef")
            self.assertEqual(snap["order"], ["swofty.core", "x"])
            self.assertEqual(snap["lines"], ["loader: 1 file(s) from <loader>", "database: unchanged"])
            self.assertEqual(snap["files"]["version.dll"]["class"], "loader")
            self.assertEqual(snap["files"]["swofty/addons/x/native/x.dll"]["addon"], "x")
            self.assertTrue(snap["files"]["swofty/settings.json"]["normalised"])
            self.assertEqual(snap["db"]["assets_db_edits"], 2)
            self.assertEqual(snap["db"]["database"], ["database: unchanged"])
            self.assertEqual(snap["parts"], {"x": {"native": "n"}})
            self.assertNotIn("built_at", json.dumps(snap["conflicts"]))
            self.assertEqual(snap["plan"]["files"]["swofty/assets.json"]["source"], "<overlay>/swofty/assets.json")

    def test_a_failed_build_keeps_its_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            norm = snapshot.Normaliser({"data": tmp})
            snap = snapshot.extract(
                tmp, "compat", [{"step": "enable x", "rc": 1, "stdout": "", "stderr": f"error: {tmp}/addons/x missing"}],
                {"rc": 1, "stdout": "", "stderr": "swofty: menus: route group swofty_settings exists already\n"}, norm,
            )
            self.assertIsNone(snap["buildhash"])
            self.assertEqual(snap["build"]["stderr"], ["swofty: menus: route group swofty_settings exists already"])
            self.assertEqual(snap["steps"][0]["out"], ["error: <data>/addons/x missing"])
            self.assertEqual(snap["files"], {})

    def test_large_files_hash_raw(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "big.erp"
            p.write_bytes(tmp.encode() + b"\0" * 64)
            norm = snapshot.Normaliser({"data": tmp})
            small = snapshot.hash_file(p, norm)
            self.assertTrue(small["normalised"])
            old = snapshot.NORMALISE_LIMIT
            snapshot.NORMALISE_LIMIT = 8
            try:
                big = snapshot.hash_file(p, norm)
            finally:
                snapshot.NORMALISE_LIMIT = old
            self.assertEqual(big["sha256"], big["raw"])
            self.assertEqual(snapshot.hash_file(Path(tmp) / "gone", norm), {"missing": "FileNotFoundError"})


if __name__ == "__main__":
    unittest.main()
