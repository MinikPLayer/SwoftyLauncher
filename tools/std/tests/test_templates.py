import io
import json
import shutil
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from helpers import FIXTURES

import templates


class Templates(unittest.TestCase):
    def setUp(self):
        self.repo = Path(tempfile.mkdtemp(prefix="std-templates-"))
        shutil.copytree(FIXTURES / "registries", self.repo / "packages/sdk/std/registries")
        self.season = json.loads((FIXTURES / "registries/career.season/examples/2016.json").read_text())

    def tearDown(self):
        shutil.rmtree(self.repo, ignore_errors=True)

    def addon(self, name, files):
        root = self.repo / "templates/std" / name
        for rel, content in files.items():
            f = root / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(content if isinstance(content, str) else json.dumps(content))
        return root

    def run_main(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            code = templates.main(["--repo", str(self.repo), *args])
        return code, out.getvalue()

    def test_no_templates_is_a_no_op(self):
        code, out = self.run_main()
        self.assertEqual(code, 0)
        self.assertIn("nothing to build", out)

    def test_valid_data_files(self):
        self.addon("whatif", {
            "swofty.json": {"id": "example.whatif"},
            "registry/seasons.json": {"career.season": {"what_if": self.season}},
            "registry/layers.json": {"career.season": {"swofty.progressive/2017": {"teams": [{"slot": 1, "seats": []}]}}},
            "registry/tags.json": {"c/f1_cars": ["not", "a", "record"]},
        })
        code, out = self.run_main("--no-build")
        self.assertEqual(code, 0, out)
        self.assertIn("templates: 1 addons, 0 failed", out)

    def test_variant_base_in_the_addon(self):
        self.addon("whatif", {
            "swofty.json": {"id": "example.whatif"},
            "registry/seasons.json": {"career.season": {"base": self.season}},
            "registry/variants.json": {"career.season": {"v": {"extends": "example.whatif/base", "teams": [{"slot": 7}]}}},
        })
        code, out = self.run_main("--no-build")
        self.assertEqual(code, 1)
        self.assertIn("is required (merged onto example.whatif/base)", out)

    def test_invalid_layer(self):
        self.addon("whatif", {
            "swofty.json": {"id": "example.whatif"},
            "registry/layers.json": {"career.season": {"swofty.progressive/2017": {"year": None}}},
        })
        code, out = self.run_main("--no-build")
        self.assertEqual(code, 1)
        self.assertIn("null removes a required field", out)

    def test_missing_manifest(self):
        self.addon("broken", {"registry/x.json": {}})
        code, out = self.run_main("--no-build")
        self.assertEqual(code, 1)
        self.assertIn("no swofty.json", out)

    def test_build_runs(self):
        root = self.addon("sound", {"swofty.json": {"id": "example.sound"}, "native/build.sh": "touch \"$(dirname \"$0\")/built\"\n"})
        code, out = self.run_main()
        self.assertEqual(code, 0, out)
        self.assertTrue((root / "native/built").exists())

    def test_build_failure(self):
        self.addon("sound", {"swofty.json": {"id": "example.sound"}, "native/build.sh": "exit 3\n"})
        code, out = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("native/build.sh failed", out)


if __name__ == "__main__":
    unittest.main()
