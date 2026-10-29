import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
sys.path.insert(0, str(TOOLS))


class TempRegistries(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="std-schema-"))
        self.root = self.tmp / "registries"
        shutil.copytree(FIXTURES / "registries", self.root)
        self.reg = self.root / "career.season"

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def schema(self, version):
        return json.loads((self.reg / f"{version}.json").read_text())

    def write(self, version, schema, folder=None):
        (folder or self.reg).joinpath(f"{version}.json").write_text(json.dumps(schema, indent=2) + "\n")

    def edit(self, version, fn):
        s = self.schema(version)
        fn(s)
        self.write(version, s)
        return s
