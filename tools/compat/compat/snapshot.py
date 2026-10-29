import hashlib
import json
import re
from pathlib import Path

SCHEMA = 1
VOLATILE_KEYS = frozenset({"built_at"})
NORMALISE_LIMIT = 64 << 20
OVERLAY = re.compile(r"<data>([/\\]+)cache\1+overlay\1+[^/\\\"]+\1+[0-9a-f]{16}")
RUNTIME_LINE = re.compile(
    r"^runtime: \S+ with (\d+) asset group\(s\), (\d+) resource\(s\), (\d+) database edit\(s\), "
    r"(\d+) language table\(s\), (\d+) overlay file\(s\)"
)
ORDER_LINE = re.compile(r"^profile \S+: (.*)$")
HASH_LINE = re.compile(r"^overlay ([0-9a-f]+): ")


def _variants(path):
    p = str(path).rstrip("/")
    if not p:
        return []
    out = [p]
    if p.startswith("/"):
        win = "Z:" + p.replace("/", "\\")
        out += [win, win.replace("\\", "\\\\"), "Z:" + p, win.lower(), win.lower().replace("\\", "\\\\")]
    return out


class Normaliser:
    def __init__(self, roots):
        pairs = []
        for name, path in roots.items():
            if path:
                for v in _variants(path):
                    pairs.append((v, f"<{name}>"))
        pairs.sort(key=lambda p: -len(p[0]))
        seen, self.pairs = set(), []
        for v, name in pairs:
            if v not in seen:
                seen.add(v)
                self.pairs.append((v, name))
        self.byte_pairs = [(v.encode(), n.encode()) for v, n in self.pairs]

    def text(self, s):
        for v, name in self.pairs:
            if v in s:
                s = s.replace(v, name)
        if "<data>" in s:
            s = OVERLAY.sub("<overlay>", s)
        return s

    def bytes(self, b):
        for v, name in self.byte_pairs:
            if v in b:
                b = b.replace(v, name)
        if b"<data>" in b:
            b = re.sub(OVERLAY.pattern.encode(), b"<overlay>", b)
        return b

    def json(self, value):
        if isinstance(value, str):
            return self.text(value)
        if isinstance(value, list):
            return [self.json(v) for v in value]
        if isinstance(value, dict):
            return {self.text(k): self.json(v) for k, v in value.items() if k not in VOLATILE_KEYS}
        return value


def hash_file(path, norm):
    path = Path(path)
    try:
        size = path.stat().st_size
    except OSError as e:
        return {"missing": f"{type(e).__name__}"}
    raw = hashlib.sha256()
    if size <= NORMALISE_LIMIT:
        data = path.read_bytes()
        raw.update(data)
        normalised = norm.bytes(data)
        out = {"size": size, "raw": raw.hexdigest()}
        out["sha256"] = out["raw"] if normalised == data else hashlib.sha256(normalised).hexdigest()
        if normalised != data:
            out["normalised"] = True
        return out
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            raw.update(b)
    return {"size": size, "raw": raw.hexdigest(), "sha256": raw.hexdigest()}


def source_class(source):
    if source.startswith("<loader>"):
        return "loader"
    if source.startswith("<overlay>"):
        return "overlay"
    if source.startswith("<data>/addons/") or source.startswith("<data>\\addons\\"):
        return "addon"
    if source.startswith("<game>") or source.startswith("<data>/baseline"):
        return "game"
    return "other"


def addon_of(rel, source):
    for text in (source, rel):
        m = re.match(r"^(?:<data>/addons/|swofty/addons/)([^/]+)/", text)
        if m:
            return m.group(1)
    return None


def parse_stdout(lines):
    out = {"order": None, "buildhash": None, "warnings": [], "runtime": None}
    for line in lines:
        m = ORDER_LINE.match(line)
        if m and out["order"] is None:
            out["order"] = [x.strip() for x in m.group(1).split("->")]
            continue
        m = HASH_LINE.match(line)
        if m:
            out["buildhash"] = m.group(1)
            continue
        if line.startswith("warning: "):
            out["warnings"].append(line[len("warning: "):])
            continue
        m = RUNTIME_LINE.match(line)
        if m:
            out["runtime"] = [int(x) for x in m.groups()]
    return out


def db_counts(lines, runtime, assets):
    out = {}
    if runtime:
        keys = ("asset_groups", "resources", "db_edits", "language_tables", "overlay_files")
        out["runtime"] = dict(zip(keys, runtime))
    out["database"] = [l for l in lines if l.startswith("database: ")]
    if isinstance(assets, dict):
        edits = assets.get("db_edits")
        if isinstance(edits, list):
            out["assets_db_edits"] = len(edits)
    return out


def _read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None


def _lines(text, norm):
    return [norm.text(l) for l in (text or "").splitlines() if l.strip()]


def extract(data_dir, profile, steps, build, norm, meta=None):
    data_dir = Path(data_dir)
    stdout = _lines(build.get("stdout"), norm)
    stderr = _lines(build.get("stderr"), norm)
    parsed = parse_stdout(stdout)
    snap = {
        "schema": SCHEMA,
        **(meta or {}),
        "steps": [
            {"step": s["step"], "rc": s["rc"], "out": _lines(s.get("stdout"), norm) + _lines(s.get("stderr"), norm)}
            for s in steps
        ],
        "build": {"rc": build.get("rc"), "stderr": stderr, "stdout": stdout},
        "buildhash": parsed["buildhash"],
        "order": parsed["order"],
        "files": {},
        "plan": None,
        "lines": None,
        "warnings": parsed["warnings"],
        "db": {},
        "parts": {},
        "conflicts": None,
        "waiting": None,
        "registry_override": None,
    }
    hashdir = None
    if parsed["buildhash"]:
        hashdir = data_dir / "cache" / "overlay" / profile / parsed["buildhash"]
    plan = _read_json(hashdir / "plan.json") if hashdir else None
    built = _read_json(hashdir / "built.json") if hashdir else None
    if plan is not None:
        files = {}
        mapping = {}
        for kind in ("replace", "add", "loader"):
            for rel, src in sorted((plan.get(kind) or {}).items()):
                source = norm.text(src)
                mapping[rel] = {"kind": kind, "source": source}
                entry = {"kind": kind, "source": source, "class": source_class(source)}
                addon = addon_of(rel, source)
                if addon:
                    entry["addon"] = addon
                entry.update(hash_file(src, norm))
                files[rel] = entry
        snap["files"] = files
        snap["plan"] = {"tracks": norm.json(plan.get("tracks")), "files": mapping}
    if built is not None:
        snap["lines"] = norm.json(built.get("lines") or [])
        snap["warnings"] = norm.json(built.get("warnings") or [])
        snap["conflicts"] = norm.json({"conflicts": built.get("conflicts"), "report": built.get("report")})
        snap["waiting"] = norm.json(built.get("waiting"))
        snap["registry_override"] = built.get("registry_override")
    lines = snap["lines"] if snap["lines"] is not None else stdout
    assets = None
    if plan is not None:
        src = (plan.get("loader") or {}).get("swofty/assets.json")
        assets = _read_json(src) if src else None
    snap["db"] = db_counts(lines, parsed["runtime"], assets)
    fingerprint = _read_json(data_dir / "state" / "fingerprint-outputs.json") or {}
    for addon, rec in sorted((fingerprint.get(profile) or {}).items()):
        snap["parts"][addon] = dict(sorted((rec.get("parts") or {}).items()))
    return snap
