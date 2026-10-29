#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import stdschema as S

REGISTRIES = "packages/sdk/std/registries"
FOLDER_FILES = {"status.json", "CHANGES.md", "README.md"}


class Violation:
    def __init__(self, rule, file, message, where=""):
        self.rule = rule
        self.file = str(file)
        self.where = where
        self.message = message

    def __str__(self):
        where = f" {self.where}:" if self.where else ""
        return f"{self.file}:{where} {self.message} [{self.rule}]"


class Checker:
    def __init__(self, root, display_root=None):
        self.root = Path(root)
        self.display_root = Path(display_root) if display_root else self.root
        self.violations = []
        self.schemas = {}

    def rel(self, path):
        try:
            return str(self.display_root / Path(path).relative_to(self.root))
        except ValueError:
            return str(path)

    def add(self, rule, file, message, where=""):
        self.violations.append(Violation(rule, self.rel(file) if isinstance(file, Path) else file, message, where))

    def run(self, released=None):
        if not self.root.is_dir():
            return self.violations
        for folder in sorted(p for p in self.root.iterdir() if p.is_dir()):
            self.check_registry(folder, released or {})
        if released:
            self.check_released(released)
        return self.violations

    def check_registry(self, folder, released):
        name = folder.name
        if not S.REGISTRY_NAME.match(name):
            self.add("layout", folder, f"{name!r} is not a registry name (dotted, lowercase)")
        versions = {}
        for f in sorted(folder.iterdir()):
            if f.is_dir():
                if f.name != "examples":
                    self.add("layout", f, "a registry folder holds only examples/ and its files")
                continue
            m = S.VERSION_FILE.match(f.name)
            if m:
                major, minor = int(m.group(1)), int(m.group(2))
                if major == 0:
                    self.add("layout", f, "majors start at 1")
                    continue
                versions[(major, minor)] = f
            elif f.suffix == ".json" and f.name not in FOLDER_FILES:
                self.add("layout", f, "is not a schema file name (<major>.<minor>.json)")
        if not versions:
            self.add("layout", folder, "has no schema file (1.0.json)")
            return

        loaded = {}
        for version, f in sorted(versions.items()):
            try:
                schema = S.load(f)
            except S.JsonError as e:
                self.add("json", f, str(e))
                continue
            if self.check_schema(name, version, f, schema):
                loaded[version] = schema
        self.schemas[name] = loaded

        self.check_sequence(folder, versions)
        released_here = {v for (n, v) in released if n == name}
        majors = sorted({v[0] for v in versions})
        for major in majors:
            minors = sorted(v[1] for v in versions if v[0] == major)
            for prev, cur in zip(minors, minors[1:]):
                old, new = (major, prev), (major, cur)
                if new in released_here or old not in loaded or new not in loaded:
                    continue
                for where, message in compare(loaded[old], loaded[new]):
                    self.add("minor", versions[new], f"{message}; a {S.version_name(new)} may only add to "
                             f"{S.version_name(old)}, so this needs {major + 1}.0.json with an adapter", where)
            first = (major, minors[0])
            if major > 1 and first in loaded and first not in released_here:
                self.check_adapter(name, first, versions[first], loaded[first])

        self.check_status(folder, majors)
        self.check_changes(folder, versions)

    def check_sequence(self, folder, versions):
        majors = sorted({v[0] for v in versions})
        for expected, major in enumerate(majors, start=1):
            if major != expected:
                self.add("sequence", folder, f"major {expected} is missing; a new major keeps every older one "
                         f"({expected}.0.json and its minors)")
                break
        for major in majors:
            minors = sorted(v[1] for v in versions if v[0] == major)
            for expected, minor in enumerate(minors):
                if minor != expected:
                    self.add("sequence", folder, f"{major}.{expected}.json is missing; minors of major {major} "
                             f"go up by one from {major}.0")
                    break

    def check_schema(self, name, version, f, schema):
        if not isinstance(schema, dict):
            self.add("subset", f, "a schema is a JSON object")
            return False
        start = len(self.violations)
        if schema.get("x-registry") != name:
            self.add("ids", f, f"x-registry is {schema.get('x-registry')!r}, expected {name!r} (the folder name)")
        declared = S.parse_version(schema.get("x-version"))
        if declared is None:
            self.add("meta", f, f"x-version {schema.get('x-version')!r} is not \"<major>.<minor>\"")
        elif declared != version:
            self.add("meta", f, f"x-version is {S.version_name(declared)} but the file is {S.version_name(version)}.json")
        if "x-applies" in schema and schema["x-applies"] not in S.APPLIES:
            self.add("meta", f, f"x-applies {schema['x-applies']!r} is not one of {', '.join(S.APPLIES)}")
        if "x-snapshot" in schema and not isinstance(schema["x-snapshot"], bool):
            self.add("meta", f, "x-snapshot is true or false")
        budget = schema.get("x-snapshot-budget")
        if budget is not None and not (isinstance(budget, int) and not isinstance(budget, bool) and budget >= 0):
            self.add("meta", f, "x-snapshot-budget is a whole number of bytes")
        shared = schema.get("x-shared")
        if "x-shared" in schema:
            if not isinstance(shared, dict) or set(shared) != {"provider_registry", "provides", "resolved"}:
                self.add("meta", f, "x-shared is {\"provider_registry\", \"provides\", \"resolved\"}")
            else:
                if not all(isinstance(shared[k], str) and shared[k] for k in shared):
                    self.add("meta", f, "x-shared fields are non-empty strings")
                elif not (shared["provides"].startswith("/") and shared["resolved"].startswith("/")):
                    self.add("meta", f, "x-shared provides and resolved are JSON pointers")
        if "x-adapter" in schema and version[0] == 1:
            self.add("meta", f, "x-adapter belongs on the first file of a later major")
        if S.types_of(schema) != ["object"]:
            self.add("subset", f, "a record schema has \"type\": \"object\"")
        walker = SubsetWalker(schema)
        walker.walk(schema, [], top=True)
        for where, message in walker.problems:
            self.add("subset", f, message, where)
        return len(self.violations) == start

    def check_adapter(self, name, version, f, schema):
        want = f"{name}@{version[0] - 1}"
        got = schema.get("x-adapter")
        if got != want:
            self.add("major", f, f"major {version[0]} declares no adapter from the previous major: set "
                     f"\"x-adapter\": \"{want}\" and register swofty::adapter for it" if got is None else
                     f"x-adapter is {got!r}, expected {want!r}")

    def check_status(self, folder, majors):
        f = folder / "status.json"
        if not f.exists():
            return
        try:
            status = S.load(f)
        except S.JsonError as e:
            self.add("status", f, str(e))
            return
        if not isinstance(status, dict):
            self.add("status", f, "status.json is an object")
            return
        kind = status.get("status")
        if kind not in S.STATUSES:
            self.add("status", f, f"status {kind!r} is not one of {', '.join(S.STATUSES)}")
        if not (isinstance(status.get("since"), str) and S.SEMVER.match(status["since"])):
            self.add("status", f, "since is the launcher version that added it, e.g. \"1.6.0\"")
        if kind == "experimental" and not isinstance(status.get("feature"), str):
            self.add("status", f, "an experimental registry names its feature (\"feature\")")
        if kind == "superseded":
            if not (isinstance(status.get("superseded"), str) and S.SEMVER.match(status["superseded"])):
                self.add("status", f, "a superseded registry says when (\"superseded\": \"<launcher version>\")")
            if not isinstance(status.get("by"), str):
                self.add("status", f, "a superseded registry names what replaces it (\"by\")")

    def check_changes(self, folder, versions):
        f = folder / "CHANGES.md"
        if not f.exists():
            return
        text = f.read_text(encoding="utf-8")
        for version in sorted(versions):
            name = S.version_name(version)
            if not re.search(rf"(?<![0-9.]){re.escape(name)}(?![0-9]|\.[0-9])", text):
                self.add("changes", f, f"has no line for {name}")

    def check_released(self, released):
        for (name, version), (path, content) in sorted(released.items()):
            f = self.root / name / f"{S.version_name(version)}.json"
            if not f.exists():
                self.add("released", path, f"was released and is gone; released schema files are never removed")
                continue
            now = f.read_bytes().replace(b"\r\n", b"\n")
            if now != content.replace(b"\r\n", b"\n"):
                self.add("released", f, "was released and has changed; released schema files are never edited "
                         "(add a new minor or major instead)")


class SubsetWalker:
    def __init__(self, root):
        self.root = root
        self.problems = []

    def problem(self, path, message):
        self.problems.append((S.pointer(path), message))

    def walk(self, node, path, top=False):
        if isinstance(node, bool):
            return
        if not isinstance(node, dict):
            self.problem(path, "a schema is a JSON object")
            return
        for k in node:
            if k not in S.KEYWORDS:
                self.problem(path, f"keyword {k!r} is outside the schema subset")
            elif k in S.TOP_ONLY and not top:
                self.problem(path, f"{k} is only allowed at the top of the schema")
        if "$ref" in node:
            try:
                S.deref(self.root, node)
            except KeyError as e:
                self.problem(path, e.args[0])
        types = S.types_of(node)
        if types is not None:
            if not types or any(t not in S.TYPES for t in types) or len(set(types)) != len(types):
                self.problem(path + ["type"], f"type {node['type']!r} is not one or more of {', '.join(S.TYPES)}")
        for k in ("minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum"):
            if k in node and not S._is_number(node[k]):
                self.problem(path + [k], "is a number")
        for k in ("minItems", "maxItems", "minLength", "maxLength"):
            if k in node and not (S._is_int(node[k]) and node[k] >= 0):
                self.problem(path + [k], "is a whole number")
        if "pattern" in node:
            try:
                re.compile(node["pattern"])
            except (re.error, TypeError) as e:
                self.problem(path + ["pattern"], f"is not a regular expression: {e}")
        if "enum" in node and not (isinstance(node["enum"], list) and node["enum"]):
            self.problem(path + ["enum"], "is a non-empty list")
        if "required" in node:
            req = node["required"]
            if not (isinstance(req, list) and all(isinstance(r, str) for r in req)):
                self.problem(path + ["required"], "is a list of field names")
            elif "properties" in node:
                for r in req:
                    if r not in node["properties"]:
                        self.problem(path + ["required"], f"{r!r} is required but not in properties")
        if "items" in node and isinstance(node["items"], list):
            self.problem(path + ["items"], "items is one schema; tuple items are outside the subset")
        if "x-key" in node:
            self.check_key(node, path)
        if "x-ref" in node:
            self.check_ref(node, path)
        elif "x-missing" in node:
            self.problem(path + ["x-missing"], "only goes with x-ref")

        for k in ("properties", "$defs", "definitions"):
            if k in node:
                if not isinstance(node[k], dict):
                    self.problem(path + [k], "is an object of schemas")
                    continue
                for name, sub in node[k].items():
                    self.walk(sub, path + [k, name])
        for k in ("items", "additionalProperties"):
            if isinstance(node.get(k), dict):
                self.walk(node[k], path + [k])
        for k in ("anyOf", "oneOf", "allOf"):
            if k in node:
                if not (isinstance(node[k], list) and node[k]):
                    self.problem(path + [k], "is a non-empty list of schemas")
                    continue
                for i, sub in enumerate(node[k]):
                    self.walk(sub, path + [k, i])

    def check_key(self, node, path):
        key = node["x-key"]
        if not (isinstance(key, str) and key or isinstance(key, list) and key and all(isinstance(k, str) and k for k in key)):
            self.problem(path + ["x-key"], "is a field name or a list of field names")
            return
        fields = S.key_fields(key)
        if len(set(fields)) != len(fields):
            self.problem(path + ["x-key"], "names a field twice")
        types = S.types_of(node)
        if types != ["array"]:
            self.problem(path, "x-key marks a list (\"type\": \"array\")")
        try:
            items = S.deref(self.root, node.get("items", {}))
        except KeyError:
            return
        if not isinstance(items, dict) or S.types_of(items) != ["object"]:
            self.problem(path + ["items"], "a keyed list holds objects (\"type\": \"object\")")
            return
        props = items.get("properties", {})
        required = items.get("required", [])
        for k in fields:
            if k not in props:
                self.problem(path + ["x-key"], f"key field {k!r} is not a field of the items")
            elif k not in required:
                self.problem(path + ["x-key"], f"key field {k!r} must be required in the items")

    def check_ref(self, node, path):
        target = node["x-ref"]
        if not (isinstance(target, str) and S.REGISTRY_NAME.match(target.split("@", 1)[0])):
            self.problem(path + ["x-ref"], f"{target!r} is not a registry name")
        types = S.types_of(node)
        if types is not None and "string" not in types:
            self.problem(path, "x-ref marks a string (a record id)")
        policy = node.get("x-missing")
        if policy is None:
            return
        if not isinstance(policy, dict):
            self.problem(path + ["x-missing"], "is an object")
            return
        unknown = set(policy) - {"default", "allow", "drop", "to"}
        if unknown:
            self.problem(path + ["x-missing"], f"has unknown fields {sorted(unknown)}")
        default = policy.get("default", "required")
        if default not in S.MISSING_POLICIES:
            self.problem(path + ["x-missing", "default"], f"is one of {', '.join(S.MISSING_POLICIES)}")
        allow = policy.get("allow")
        if allow is not None:
            if not (isinstance(allow, list) and all(a in S.MISSING_POLICIES for a in allow)):
                self.problem(path + ["x-missing", "allow"], f"lists some of {', '.join(S.MISSING_POLICIES)}")
            elif default not in allow:
                self.problem(path + ["x-missing", "allow"], f"doesn't include the default {default!r}")


def compare(old, new):
    out = []
    Comparer(old, new, out).node(old, new, [], top=True)
    return out


ORDER_KEYS = (
    ("minimum", "below"), ("exclusiveMinimum", "below"), ("minItems", "below"), ("minLength", "below"),
    ("maximum", "above"), ("exclusiveMaximum", "above"), ("maxItems", "above"), ("maxLength", "above"),
)
SKIP = S.ANNOTATIONS | {"$schema", "$id", "$defs", "definitions", "x-version", "x-adapter", "$ref"}


class Comparer:
    def __init__(self, old_root, new_root, out):
        self.old_root = old_root
        self.new_root = new_root
        self.out = out
        self.seen = set()

    def fail(self, path, message):
        self.out.append((S.pointer(path), message))

    def node(self, o, n, path, top=False):
        try:
            o = S.deref(self.old_root, o)
            n = S.deref(self.new_root, n)
        except KeyError as e:
            self.fail(path, e.args[0])
            return
        if (id(o), id(n)) in self.seen:
            return
        self.seen.add((id(o), id(n)))
        if isinstance(o, bool) or isinstance(n, bool):
            if o is not n and o is not False and n is not True:
                self.fail(path, f"changed from {o!r} to {n!r}")
            return

        keys = (set(o) | set(n)) - SKIP
        for k in sorted(keys):
            if k in o and k in n and S.json_equal(o[k], n[k]) and k not in ("properties", "items", "additionalProperties", "anyOf", "oneOf", "allOf"):
                continue
            handler = getattr(self, "k_" + k.replace("-", "_").replace("$", ""), None)
            if handler:
                handler(o, n, path)
            else:
                self.k_generic(k, o, n, path)

    def k_generic(self, k, o, n, path):
        if k not in o:
            self.fail(path, f"adds {k} {S.show(n[k])}")
        elif k not in n:
            self.fail(path, f"drops {k} {S.show(o[k])}")
        else:
            self.fail(path, f"changes {k} from {S.show(o[k])} to {S.show(n[k])}")

    def k_type(self, o, n, path):
        ot, nt = S.types_of(o), S.types_of(n)
        if nt is None:
            return
        if ot is None:
            self.fail(path, f"adds type {S.show(n['type'])}, which rejects values 1.0 allowed")
            return
        lost = [t for t in ot if t not in nt and not (t == "integer" and "number" in nt)]
        if lost:
            self.fail(path, f"changes type from {S.show(o['type'])} to {S.show(n['type'])}")

    def k_enum(self, o, n, path):
        if "enum" not in n:
            return
        if "enum" not in o:
            self.fail(path, f"adds enum {S.show(n['enum'])}")
            return
        lost = [v for v in o["enum"] if not any(S.json_equal(v, w) for w in n["enum"])]
        if lost:
            self.fail(path, f"removes enum value{'s' if len(lost) > 1 else ''} {', '.join(S.show(v) for v in lost)}")

    def k_const(self, o, n, path):
        if "const" in n:
            self.k_generic("const", o, n, path)

    def k_required(self, o, n, path):
        old = set(o.get("required", ()))
        props = o.get("properties", {})
        for name in n.get("required", ()):
            if name in old:
                continue
            if name in props:
                self.fail(path + [name], "was optional and is now required")
            else:
                self.fail(path + [name], "is a new required field; a new field in a minor is optional")

    def k_properties(self, o, n, path):
        op, np_ = o.get("properties", {}), n.get("properties", {})
        extra = n.get("additionalProperties", True)
        for name, sub in op.items():
            if name not in np_:
                if extra is False or isinstance(extra, dict):
                    self.fail(path + [name], "is removed (or renamed)")
                else:
                    self.fail(path + [name], "is removed (or renamed); fields are never removed in a minor")
                continue
            self.node(sub, np_[name], path + [name])
        if "additionalProperties" in o and isinstance(o["additionalProperties"], dict):
            for name in np_:
                if name not in op:
                    self.node(o["additionalProperties"], np_[name], path + [name])

    def k_additionalProperties(self, o, n, path):
        oa, na = o.get("additionalProperties", True), n.get("additionalProperties", True)
        if oa is False or na is True:
            return
        if oa is True:
            self.fail(path, f"adds additionalProperties {S.show(na)}, which rejects fields 1.0 kept")
            return
        if na is False:
            self.fail(path, "sets additionalProperties to false")
            return
        self.node(oa, na, path + ["additionalProperties"])

    def k_items(self, o, n, path):
        if "items" not in n:
            return
        if "items" not in o:
            self.fail(path, "adds items")
            return
        self.node(o["items"], n["items"], path + ["items"])

    def k_uniqueItems(self, o, n, path):
        if n.get("uniqueItems") and not o.get("uniqueItems"):
            self.fail(path, "adds uniqueItems")

    def k_pattern(self, o, n, path):
        if "pattern" in n:
            self.k_generic("pattern", o, n, path)

    def k_default(self, o, n, path):
        self.k_generic("default", o, n, path)

    def k_x_key(self, o, n, path):
        if "x-key" not in o:
            self.fail(path, f"adds x-key {S.show(n['x-key'])}; adding a key to a list is a major change")
        elif "x-key" not in n:
            self.fail(path, f"drops x-key {S.show(o['x-key'])}")
        elif S.key_fields(o["x-key"]) != S.key_fields(n["x-key"]):
            self.fail(path, f"changes x-key from {S.show(o['x-key'])} to {S.show(n['x-key'])}")

    def k_x_ref(self, o, n, path):
        self.k_generic("x-ref", o, n, path)

    def k_x_missing(self, o, n, path):
        om = o.get("x-missing") or {}
        nm = n.get("x-missing") or {}
        if "x-missing" not in o:
            self.fail(path, f"adds x-missing {S.show(nm)}")
            return
        if om.get("default", "required") != nm.get("default", "required"):
            self.fail(path + ["x-missing"], f"changes the default from {om.get('default', 'required')!r} "
                      f"to {nm.get('default', 'required')!r}")
        lost = S.missing_allowed(om) - S.missing_allowed(nm)
        if lost:
            self.fail(path + ["x-missing"], f"no longer allows {', '.join(sorted(lost))}")
        for k in ("drop", "to"):
            if om.get(k) != nm.get(k):
                self.fail(path + ["x-missing", k], f"changes from {S.show(om.get(k))} to {S.show(nm.get(k))}")

    def k_x_registry(self, o, n, path):
        self.k_generic("x-registry", o, n, path)

    def k_x_applies(self, o, n, path):
        self.k_generic("x-applies", o, n, path)

    def k_x_shared(self, o, n, path):
        self.k_generic("x-shared", o, n, path)

    def k_x_snapshot(self, o, n, path):
        self.k_generic("x-snapshot", o, n, path)

    def k_x_snapshot_budget(self, o, n, path):
        ob, nb = o.get("x-snapshot-budget"), n.get("x-snapshot-budget")
        if ob is not None and (nb is None or nb < ob):
            self.fail(path, f"shrinks x-snapshot-budget from {ob} to {nb}")
        elif ob is None:
            self.fail(path, f"adds x-snapshot-budget {nb}")

    def _alternatives(self, k, o, n, path):
        ol, nl = o.get(k), n.get(k)
        if ol is None:
            self.fail(path, f"adds {k}")
            return
        if nl is None:
            return
        if k == "allOf" and len(nl) != len(ol):
            self.fail(path, f"changes allOf from {len(ol)} to {len(nl)} schemas")
            return
        if len(nl) < len(ol):
            self.fail(path, f"removes {k} alternatives ({len(ol)} to {len(nl)})")
            return
        for i, sub in enumerate(ol):
            self.node(sub, nl[i], path + [k, i])

    def k_anyOf(self, o, n, path):
        self._alternatives("anyOf", o, n, path)

    def k_oneOf(self, o, n, path):
        self._alternatives("oneOf", o, n, path)

    def k_allOf(self, o, n, path):
        self._alternatives("allOf", o, n, path)


def _order(key, direction):
    def handler(self, o, n, path):
        if key not in n:
            return
        if key not in o:
            self.fail(path, f"adds {key} {S.show(n[key])}")
            return
        if direction == "below" and n[key] > o[key] or direction == "above" and n[key] < o[key]:
            self.fail(path, f"tightens {key} from {S.show(o[key])} to {S.show(n[key])}")
    return handler


for _key, _direction in ORDER_KEYS:
    setattr(Comparer, "k_" + _key, _order(_key, _direction))


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True).stdout


def released_files(repo, ref, registries):
    try:
        names = git(repo, "ls-tree", "-r", "--name-only", ref, "--", registries).decode().splitlines()
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"schemacheck: can't read {ref}: {e.stderr.decode().strip()}")
    out = {}
    for path in names:
        parts = Path(path).relative_to(registries).parts
        if len(parts) != 2:
            continue
        m = S.VERSION_FILE.match(parts[1])
        if not m:
            continue
        out[(parts[0], (int(m.group(1)), int(m.group(2))))] = (path, git(repo, "show", f"{ref}:{path}"))
    return out


def last_release(repo, ref):
    try:
        return git(repo, "describe", "--tags", "--abbrev=0", "--match", "v[0-9]*", ref).decode().strip()
    except subprocess.CalledProcessError:
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check the standard library's registry schemas against the versioning rules.")
    ap.add_argument("--repo", default=".", help="the repository root (default: the current directory)")
    ap.add_argument("--registries", default=REGISTRIES, help=f"the registries folder, relative to --repo (default: {REGISTRIES})")
    ap.add_argument("--released", metavar="REF", help="a git ref whose schema files are released: they must exist unchanged")
    ap.add_argument("--released-before", metavar="REF", help="use the last v* tag reachable from REF (e.g. the merge base) as --released")
    args = ap.parse_args(argv)

    repo = Path(args.repo)
    root = repo / args.registries
    ref = args.released
    if args.released_before:
        ref = last_release(repo, args.released_before)
        print(f"schemacheck: released schemas from {ref or 'no release tag (nothing is released yet)'}")
    released = released_files(repo, ref, args.registries) if ref else {}

    checker = Checker(root, Path(args.registries))
    violations = checker.run(released)
    count = sum(len(v) for v in checker.schemas.values())
    for v in violations:
        print(v)
    print(f"schemacheck: {len(checker.schemas)} registries, {count} schema files, {len(released)} released, "
          f"{len(violations)} violation{'s' if len(violations) != 1 else ''}")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
