"""Read access to a pristine gamedatabases.2025.bin: values by path, row lookup by label, full field lists for clones.

The field-list machinery is the F1 1990 addon's (tools/career.py Db): the key catalogue plus every known field name,
probed with FNV continuation on the source row and its nested rows.
"""
import gzip
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
sys.path.insert(0, str(REPO / "tools"))
import gamedb  # noqa: E402

CATALOG = REPO / "tools" / "gamedb" / "catalog" / "keys.tsv.gz"
NAME_SOURCES = [REPO / "tools" / "gamedb" / "names.txt", REPO / "tools" / "gamedb" / "catalog" / "exe_names.tsv"]
QUOTED_NAME_SOURCES = [REPO / "addons" / "driver-manager" / "launcher" / "src" / "fields.rs"]
FNV_PRIME = 0x100000001b3
DEFAULT = Path("/mnt/work/.swofty-ai-cache/stock/gamedatabases.2025.bin")


class Db:
    def __init__(self, path=DEFAULT):
        self.d = {r.key: (r.type, r.value.decode("utf-8", "replace"))
                  for r in gamedb.read(Path(path).read_bytes())}
        with gzip.open(CATALOG, "rt", encoding="utf-8") as f:
            self.cat = [line.split("\t", 1)[0] for line in f][1:]
        self.cache = {}
        self.extra_names = set()

    def learn(self, keys):
        before = len(self.extra_names)
        for key in keys:
            for seg in key.split("."):
                self.extra_names.add(re.sub(r"_\d+$", "", seg))
                self.extra_names.add(seg)
        if len(self.extra_names) != before and hasattr(self, "_vocab"):
            del self._vocab
            self.cache.clear()

    def get(self, path):
        v = self.d.get(gamedb.fnv1a64(path.encode()))
        return v[1] if v else None

    def typed(self, path):
        return self.d.get(gamedb.fnv1a64(path.encode()))

    def count(self, container, word):
        v = self.get(f"{container}._{word}_instancecount_")
        return int(v) if v is not None else 0

    def rows(self, container, word):
        return [f"{container}.{word}_{i}" for i in range(self.count(container, word))]

    def row(self, container, word, label, by="_metadata_"):
        for r in self.rows(container, word):
            if self.get(f"{r}.{by}") == label:
                return r
        raise KeyError(f"{container}.{word}[{by}={label}]")

    def find(self, container, word, label, by="_metadata_"):
        try:
            return self.row(container, word, label, by)
        except KeyError:
            return None

    def unsigned_t12(self, row, fields):
        out = {}
        for field in fields:
            v = self.typed(f"{row}.{field}")
            if v and v[0] == 12 and v[1].startswith("-"):
                out[field] = str(int(v[1]) % (1 << 64))
        return out

    def fields(self, row, probe=True):
        if row in self.cache:
            return self.cache[row]
        template = re.sub(r"_\d+(?=\.|$)", "_N", row) + "."
        out = set()
        for key in self.cat:
            if key.startswith(template):
                out.update(self.under(row, key[len(template):]))
        if probe:
            out |= self.probe(row, out)
        self.cache[row] = sorted(out)
        return self.cache[row]

    def values(self, row):
        out = {f: self.get(f"{row}.{f}") for f in self.fields(row)}
        return {f: v for f, v in out.items() if v is not None}

    def stat_ids(self, container, word, field="stat_id"):
        out = set()
        for r in self.rows(container, word):
            v = self.get(f"{r}.{field}")
            if v not in (None, ""):
                out.add(int(v))
        return out

    def vocabulary(self):
        if not hasattr(self, "_vocab"):
            names = set()
            for key in self.cat:
                parts = key.split(".")
                names.update(".".join(parts[i + 1:]) for i, p in enumerate(parts) if p.endswith("_N"))
            for path in NAME_SOURCES:
                with open(path, encoding="utf-8") as f:
                    for line in f:
                        name = line.rstrip("\n").split("\t")[0]
                        names.add(name)
                        names.update(name.split("."))
            for path in QUOTED_NAME_SOURCES:
                for name in re.findall(r'"([A-Za-z0-9_.]+)"', path.read_text(encoding="utf-8")):
                    names.add(name)
                    names.update(name.split("."))
            names |= self.extra_names
            names = sorted(n for n in names if n and "_N" not in n and re.fullmatch(r"[A-Za-z0-9_.]+", n))
            self._vocab = [n.encode() for n in names]
            self._tables = [(n, f"_{n}_instancecount_".encode()) for n in names if "." not in n]
        return self._vocab, self._tables

    def probe(self, row, known):
        vocab, tables = self.vocabulary()
        found = set()
        todo = [""] + sorted({".".join(f.split(".")[:i + 1]) for f in known
                              for i, seg in enumerate(f.split(".")) if re.search(r"_\d+$", seg)})
        seen = set()
        while todo:
            p = todo.pop()
            if p in seen:
                continue
            seen.add(p)
            pre = f"{p}." if p else ""
            h0 = gamedb.fnv1a64(f"{row}.{pre}".encode())
            for name in vocab:
                if self._cont(h0, name) in self.d:
                    found.add(pre + name.decode())
            for word, count_key in tables:
                if self._cont(h0, count_key) in self.d:
                    found.add(pre + count_key.decode())
                    todo += [f"{pre}{word}_{i}" for i in range(int(self.get(f"{row}.{pre}{count_key.decode()}")))]
        return found

    @staticmethod
    def _cont(h, data):
        for c in data:
            h = ((h ^ c) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
        return h

    def under(self, row, rest):
        segs = rest.split(".")
        i = next((k for k, seg in enumerate(segs) if seg.endswith("_N")), None)
        if i is None:
            return [rest] if self.get(f"{row}.{rest}") is not None else []
        pre, word, tail = ".".join(segs[:i]), segs[i][:-2], ".".join(segs[i + 1:])
        parent = f"{row}.{pre}" if pre else row
        n = self.get(f"{parent}._{word}_instancecount_")
        found = []
        for k in range(int(n) if n is not None else 64):
            node = f"{pre}.{word}_{k}" if pre else f"{word}_{k}"
            if not tail:
                if self.get(f"{row}.{node}") is not None:
                    found.append(node)
                continue
            found += [f"{node}.{sub}" for sub in self.under(f"{row}.{node}", tail)]
        return found
