import json
import math
import re
from pathlib import Path

REGISTRY_NAME = re.compile(r"^[a-z0-9][a-z0-9_-]*(\.[a-z0-9][a-z0-9_-]*)*$")
VERSION_FILE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.json$")
DATA_KEY = re.compile(r"^(?P<name>[a-z0-9][a-z0-9_.-]*?)(?:@(?P<major>[1-9][0-9]*))?$")
PINNED_FILE = re.compile(r"^(?P<stem>.+?)@(?P<major>[1-9][0-9]*)(?:\.(?P<minor>0|[1-9][0-9]*))?\.json$")
RECORD_ID = re.compile(r"^[a-z0-9][a-z0-9._-]*/[^\s|?*]+$")
RECORD_KEY = re.compile(r"^[^\s/?|*$][^\s?|*]*$")
APPLIES = ("boot", "content", "session", "read")
MISSING_POLICIES = ("required", "optional", "fallback")
STATUSES = ("experimental", "stable", "superseded")
SEMVER = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")

ANNOTATIONS = frozenset({"$comment", "title", "description", "examples", "deprecated"})
TOP_ONLY = frozenset({"$schema", "x-registry", "x-version", "x-applies", "x-snapshot", "x-snapshot-budget", "x-adapter", "x-shared"})
KEYWORDS = ANNOTATIONS | TOP_ONLY | frozenset({
    "$id", "$ref", "$defs", "definitions", "default",
    "type", "enum", "const",
    "properties", "required", "additionalProperties",
    "items", "minItems", "maxItems", "uniqueItems",
    "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum",
    "minLength", "maxLength", "pattern",
    "anyOf", "oneOf", "allOf",
    "x-key", "x-ref", "x-missing",
})
TYPES = ("object", "array", "string", "integer", "number", "boolean", "null")


class JsonError(Exception):
    pass


def _pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise JsonError(f"duplicate key {key!r}")
        out[key] = value
    return out


def _constant(name):
    raise JsonError(f"{name} is not JSON")


def loads(text):
    try:
        return json.loads(text, object_pairs_hook=_pairs, parse_constant=_constant)
    except json.JSONDecodeError as e:
        raise JsonError(f"line {e.lineno} column {e.colno}: {e.msg}") from None


def load(path):
    try:
        text = Path(path).read_text(encoding="utf-8")
    except UnicodeDecodeError:
        raise JsonError("not UTF-8") from None
    return loads(text)


def pointer(parts):
    return "/" + "/".join(str(p).replace("~", "~0").replace("/", "~1") for p in parts) if parts else "/"


def parse_version(value):
    if isinstance(value, str):
        m = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value)
        if m:
            return int(m.group(1)), int(m.group(2))
    if isinstance(value, dict) and set(value) == {"major", "minor"}:
        major, minor = value["major"], value["minor"]
        if _is_int(major) and _is_int(minor) and major >= 0 and minor >= 0:
            return int(major), int(minor)
    return None


def version_name(version):
    return f"{version[0]}.{version[1]}"


def key_fields(value):
    if isinstance(value, str):
        return [value]
    return list(value)


def _is_int(v):
    return isinstance(v, int) and not isinstance(v, bool) or isinstance(v, float) and v.is_integer()


def _is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def type_of(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "boolean"
    if isinstance(v, int):
        return "integer"
    if isinstance(v, float):
        return "integer" if v.is_integer() else "number"
    if isinstance(v, str):
        return "string"
    if isinstance(v, list):
        return "array"
    if isinstance(v, dict):
        return "object"
    raise TypeError(type(v))


def has_type(v, t):
    actual = type_of(v)
    return actual == t or (t == "number" and actual == "integer")


def json_equal(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    if _is_number(a) and _is_number(b):
        return a == b
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(json_equal(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(json_equal(a[k], b[k]) for k in a)
    return type(a) is type(b) and a == b


def canonical(v):
    if _is_number(v) and not isinstance(v, bool) and float(v).is_integer():
        v = int(v)
    return json.dumps(v, sort_keys=True, separators=(",", ":"))


def show(v):
    text = json.dumps(v, separators=(", ", ": "))
    return text if len(text) <= 60 else text[:57] + "..."


def types_of(schema):
    t = schema.get("type")
    if t is None:
        return None
    return [t] if isinstance(t, str) else list(t)


def resolve_ref(root, ref):
    if not isinstance(ref, str) or not ref.startswith("#"):
        raise KeyError(f"$ref {ref!r} is not local to the file")
    node = root
    if ref in ("#", "#/"):
        return node
    if not ref.startswith("#/"):
        raise KeyError(f"$ref {ref!r} is not a JSON Pointer")
    for raw in ref[2:].split("/"):
        part = raw.replace("~1", "/").replace("~0", "~")
        if isinstance(node, dict) and part in node:
            node = node[part]
        elif isinstance(node, list) and part.isdigit() and int(part) < len(node):
            node = node[int(part)]
        else:
            raise KeyError(f"$ref {ref!r} does not resolve")
    return node


def deref(root, schema):
    seen = set()
    while isinstance(schema, dict) and "$ref" in schema:
        ref = schema["$ref"]
        if ref in seen:
            raise KeyError(f"$ref {ref!r} loops")
        seen.add(ref)
        target = resolve_ref(root, ref)
        rest = {k: v for k, v in schema.items() if k != "$ref" and k not in ANNOTATIONS and k != "default"}
        if rest:
            raise KeyError(f"$ref {ref!r} has sibling keywords {sorted(rest)}")
        schema = target
    return schema


def parse_ref(text):
    optional = text.endswith("?")
    body = text[:-1] if optional else text
    alternatives = body.split("|")
    if not all(RECORD_ID.match(a) for a in alternatives):
        return None
    return alternatives, optional


def missing_allowed(policy):
    if not isinstance(policy, dict):
        return set(MISSING_POLICIES)
    allow = policy.get("allow")
    if allow is None:
        return {policy.get("default", "required")} | {"required"}
    return set(allow) | {"required"}


class Validator:
    def __init__(self, schema):
        self.root = schema

    def errors(self, instance, patch=False):
        out = []
        self._check(self.root, instance, [], out, patch)
        return out

    def _fail(self, out, path, message):
        out.append((pointer(path), message))

    def _check(self, schema, v, path, out, patch):
        try:
            schema = deref(self.root, schema)
        except KeyError as e:
            self._fail(out, path, f"schema error: {e.args[0]}")
            return
        if schema is True:
            return
        if schema is False:
            self._fail(out, path, "no value is allowed here")
            return

        types = types_of(schema)
        if types is not None and not any(has_type(v, t) for t in types):
            self._fail(out, path, f"{show(v)} is {type_of(v)}, expected {' or '.join(types)}")
            return

        if "enum" in schema and not any(json_equal(v, e) for e in schema["enum"]):
            self._fail(out, path, f"{show(v)} is not one of {show(schema['enum'])}")
        if "const" in schema and not json_equal(v, schema["const"]):
            self._fail(out, path, f"{show(v)} is not {show(schema['const'])}")

        if _is_number(v):
            if "minimum" in schema and v < schema["minimum"]:
                self._fail(out, path, f"{show(v)} is below the minimum {show(schema['minimum'])}")
            if "maximum" in schema and v > schema["maximum"]:
                self._fail(out, path, f"{show(v)} is above the maximum {show(schema['maximum'])}")
            if "exclusiveMinimum" in schema and v <= schema["exclusiveMinimum"]:
                self._fail(out, path, f"{show(v)} is not above {show(schema['exclusiveMinimum'])}")
            if "exclusiveMaximum" in schema and v >= schema["exclusiveMaximum"]:
                self._fail(out, path, f"{show(v)} is not below {show(schema['exclusiveMaximum'])}")

        if isinstance(v, str):
            if "minLength" in schema and len(v) < schema["minLength"]:
                self._fail(out, path, f"{show(v)} is shorter than {schema['minLength']} characters")
            if "maxLength" in schema and len(v) > schema["maxLength"]:
                self._fail(out, path, f"{show(v)} is longer than {schema['maxLength']} characters")
            if "pattern" in schema and not re.search(schema["pattern"], v):
                self._fail(out, path, f"{show(v)} does not match {schema['pattern']}")

        if "x-ref" in schema:
            self._check_ref(schema, v, path, out)

        if isinstance(v, dict):
            self._check_object(schema, v, path, out, patch)
        if isinstance(v, list):
            self._check_array(schema, v, path, out, patch)

        for sub in schema.get("allOf", ()):
            self._check(sub, v, path, out, patch)
        if "anyOf" in schema:
            if not any(not self._sub_errors(sub, v, path, patch) for sub in schema["anyOf"]):
                self._fail(out, path, f"{show(v)} matches none of the anyOf alternatives")
        if "oneOf" in schema:
            matches = sum(1 for sub in schema["oneOf"] if not self._sub_errors(sub, v, path, patch))
            if matches != 1:
                self._fail(out, path, f"{show(v)} matches {matches} of the oneOf alternatives, expected exactly 1")

    def _sub_errors(self, schema, v, path, patch):
        out = []
        self._check(schema, v, path, out, patch)
        return out

    def _check_ref(self, schema, v, path, out):
        if v is None:
            return
        if not isinstance(v, str):
            self._fail(out, path, f"{show(v)} is a reference to {schema['x-ref']} and must be a record id string")
            return
        parsed = parse_ref(v)
        if parsed is None:
            self._fail(out, path, f"{show(v)} is not a record id (<namespace>/<key>, with an optional '?' or '|' fallbacks)")
            return
        alternatives, optional = parsed
        allowed = missing_allowed(schema.get("x-missing"))
        if optional and "optional" not in allowed:
            self._fail(out, path, f"{show(v)} is optional ('?'), which {schema['x-ref']} references here don't allow")
        if len(alternatives) > 1 and "fallback" not in allowed:
            self._fail(out, path, f"{show(v)} has fallbacks ('|'), which {schema['x-ref']} references here don't allow")

    def _check_object(self, schema, v, path, out, patch):
        props = schema.get("properties", {})
        if not patch:
            for name in schema.get("required", ()):
                if name not in v:
                    self._fail(out, path + [name], "is required")
        extra = schema.get("additionalProperties", True)
        for name, value in v.items():
            if patch and value is None:
                if name in schema.get("required", ()):
                    self._fail(out, path + [name], "null removes a required field")
                continue
            if name in props:
                self._check(props[name], value, path + [name], out, patch)
            elif extra is False:
                self._fail(out, path + [name], "is not a field of this record")
            elif isinstance(extra, dict):
                self._check(extra, value, path + [name], out, patch)

    def _check_array(self, schema, v, path, out, patch):
        if not patch or "x-key" not in schema:
            if "minItems" in schema and len(v) < schema["minItems"]:
                self._fail(out, path, f"has {len(v)} items, fewer than {schema['minItems']}")
            if "maxItems" in schema and len(v) > schema["maxItems"]:
                self._fail(out, path, f"has {len(v)} items, more than {schema['maxItems']}")
        if schema.get("uniqueItems"):
            seen = {}
            for i, item in enumerate(v):
                c = canonical(item)
                if c in seen:
                    self._fail(out, path + [i], f"repeats item {seen[c]}")
                seen.setdefault(c, i)
        items = schema.get("items")
        if "x-key" in schema:
            self._check_keyed(schema, items, v, path, out, patch)
            return
        if items is not None:
            for i, item in enumerate(v):
                self._check(items, item, path + [i], out, False)

    def _check_keyed(self, schema, items, v, path, out, patch):
        fields = key_fields(schema["x-key"])
        seen = {}
        for i, item in enumerate(v):
            if not isinstance(item, dict):
                self._fail(out, path + [i], f"is {type_of(item)}; a keyed list holds objects keyed by {', '.join(fields)}")
                continue
            missing = [f for f in fields if f not in item or item[f] is None]
            if missing:
                self._fail(out, path + [i], f"has no key field {', '.join(missing)}")
                continue
            key = ",".join(f"{f}={canonical(item[f])}" for f in fields)
            if key in seen:
                self._fail(out, path + [i], f"repeats key {key} of item {seen[key]}")
            seen.setdefault(key, i)
            element = item
            if "$delete" in item:
                if not patch:
                    self._fail(out, path + [i, "$delete"], "is only allowed in a layer")
                elif item["$delete"] is not True:
                    self._fail(out, path + [i, "$delete"], "must be true")
                elif set(item) - set(fields) - {"$delete"}:
                    self._fail(out, path + [i], "a $delete element holds only its key fields")
                continue
            if items is not None:
                self._check(items, element, path + [i], out, patch)


def valid_key(key):
    return bool(RECORD_KEY.match(key)) and not key.endswith("/") and "//" not in key


class MergeError(Exception):
    pass


def _object_schema(root, schema, name):
    try:
        schema = deref(root, schema)
    except KeyError:
        return {}
    if not isinstance(schema, dict):
        return {}
    return schema.get("properties", {}).get(name, {})


def merge_patch(root, schema, base, patch):
    """RFC 7396 with keyed lists, as swofty::json::merge_patch does it with the schema's keys: a keyed
    list is found through properties and the items of keyed lists. Raises MergeError, changing nothing,
    when an element of a keyed list lacks a key field or has a "$delete" that isn't a bool."""
    if not isinstance(patch, dict):
        return patch
    out = dict(base) if isinstance(base, dict) else {}
    for name, value in patch.items():
        if value is None:
            out.pop(name, None)
            continue
        sub = _object_schema(root, schema, name)
        try:
            sub = deref(root, sub)
        except KeyError:
            sub = {}
        if isinstance(sub, dict) and "x-key" in sub and isinstance(value, list):
            out[name] = merge_keyed(root, sub, out.get(name), value)
        else:
            out[name] = merge_patch(root, sub, out.get(name), value)
    return out


def _scalar_key(v):
    return isinstance(v, (str, int, float, bool)) and not (isinstance(v, float) and not math.isfinite(v))


def merge_keyed(root, schema, base, patch):
    """A keyed list merges element by element. An empty patch list changes nothing (a missing list
    becomes an empty one), "$delete": true removes the element with that key, and an element without
    all its key fields is a MergeError."""
    fields = key_fields(schema["x-key"])
    items = schema.get("items", {})
    out = [dict(e) if isinstance(e, dict) else e for e in base] if isinstance(base, list) else []

    def key_of(e):
        if not isinstance(e, dict) or any(f not in e or not _scalar_key(e[f]) for f in fields):
            return None
        return tuple(canonical(e[f]) for f in fields)

    for i, element in enumerate(patch):
        k = key_of(element)
        if k is None:
            raise MergeError(f"element {i} of a keyed list needs its key fields ({', '.join(fields)})")
        if "$delete" in element and not isinstance(element["$delete"], bool):
            raise MergeError(f"element {i} has a \"$delete\" that isn't true or false")
    for element in patch:
        k = key_of(element)
        if element.get("$delete") is True:
            out = [e for e in out if key_of(e) != k]
            continue
        at = next((i for i, e in enumerate(out) if key_of(e) == k), None)
        if at is None:
            out.append(merge_patch(root, items, {}, element))
            at = len(out) - 1
        else:
            out[at] = merge_patch(root, items, out[at], element)
        out[at].pop("$delete", None)
    return out


def registry_schemas(root):
    root = Path(root)
    out = {}
    if not root.is_dir():
        return out
    for folder in sorted(p for p in root.iterdir() if p.is_dir()):
        versions = {}
        for f in folder.iterdir():
            m = VERSION_FILE.match(f.name)
            if m and f.is_file():
                versions[(int(m.group(1)), int(m.group(2)))] = f
        out[folder.name] = dict(sorted(versions.items()))
    return out
