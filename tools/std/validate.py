#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import stdschema as S

REGISTRIES = "packages/sdk/std/registries"
SELECT_KEYS = {"ids", "where", "tag", "in", "when", "except", "patch"}


def is_layers_name(path):
    stem = Path(path).name.split("@", 1)[0].removesuffix(".json")
    return stem == "layers" or stem.endswith("layer") or stem.endswith("layers")


def file_pin(path):
    m = S.PINNED_FILE.match(Path(path).name)
    if not m:
        return None
    return int(m.group("major")), (int(m.group("minor")) if m.group("minor") is not None else None)


def pick_versions(schemas, name, major, pin, where, problems):
    versions = schemas.get(name)
    if not versions:
        problems.append((where, f"no SDK schema for registry {name!r}"))
        return []
    if pin is not None:
        if pin[0] != major and major is not None:
            problems.append((where, f"the file name pins major {pin[0]} but the data names major {major}"))
            return []
        major = pin[0]
        if pin[1] is not None:
            if (pin[0], pin[1]) not in versions:
                problems.append((where, f"{name} has no schema {pin[0]}.{pin[1]}"))
                return []
            return [(pin[0], pin[1])]
    major = major or 1
    chosen = [v for v in versions if v[0] == major]
    if not chosen:
        problems.append((where, f"{name} has no major {major}"))
    return chosen


class Result:
    def __init__(self):
        self.by_version = {}
        self.problems = []
        self.missing = []

    def add(self, version, errors):
        self.by_version.setdefault(S.version_name(version), []).extend(errors)

    def touch(self, version):
        self.by_version.setdefault(S.version_name(version), [])


def strip_protect(record, path, result):
    if not isinstance(record, dict) or "$protect" not in record:
        return record
    protect = record["$protect"]
    if not (isinstance(protect, list) and all(isinstance(p, str) and p for p in protect)):
        result.problems.append((S.pointer(path + ["$protect"]), "is a list of field names"))
    return {k: v for k, v in record.items() if k != "$protect"}


def validate_record(record, schemas, name, versions, path, result, patch=False):
    for version in versions:
        errors = S.Validator(schemas[name][version]).errors(record, patch=patch)
        result.touch(version)
        result.add(version, [(S.pointer(path) + p if p != "/" else S.pointer(path), m) for p, m in errors]
                   if path else errors)


class Loop(Exception):
    pass


class Corpus:
    def __init__(self, addon=None):
        self.addon = addon
        self.records = {}
        self.docs = []

    def add(self, registry, key, value):
        if isinstance(value, dict):
            self.records.setdefault((registry, key), []).append(value)

    def overlay(self, doc, layers):
        out = Corpus(self.addon)
        out.records = {k: list(v) for k, v in self.records.items()}
        if not any(doc is d for d in self.docs):
            out.add_data(doc, layers)
        return out

    def add_data(self, doc, layers):
        if layers or not isinstance(doc, dict):
            return
        self.docs.append(doc)
        for top, records in doc.items():
            m = S.DATA_KEY.match(top)
            if m and isinstance(records, dict):
                for key, value in records.items():
                    self.add(m.group("name"), key, value)

    def find(self, registry, base_id):
        namespace, _, key = base_id.partition("/")
        if self.addon is None and namespace == "game" or self.addon is not None and namespace != self.addon:
            return None
        found = self.records.get((registry, key), [])
        return found[0] if len(found) == 1 else None

    def effective(self, registry, root, record, seen=()):
        own = {k: v for k, v in record.items() if k not in ("extends", "$protect")}
        base_id = record.get("extends")
        if base_id is None:
            return own
        if not isinstance(base_id, str):
            return None
        if base_id in seen:
            raise Loop(" -> ".join([*seen, base_id]))
        base = self.find(registry, base_id)
        if base is None:
            return None
        base_value = self.effective(registry, root, base, (*seen, base_id))
        if base_value is None:
            return None
        try:
            return S.merge_patch(root, root, base_value, own)
        except S.MergeError:
            return None


def validate_variant(record, schemas, name, versions, path, result, corpus):
    base_id = record["extends"]
    if not (isinstance(base_id, str) and S.RECORD_ID.match(base_id)):
        result.problems.append((S.pointer(path + ["extends"]), "is the full id of the base record (<namespace>/<key>)"))
    patch = {k: v for k, v in record.items() if k != "extends"}
    validate_record(patch, schemas, name, versions, path, result, patch=True)
    if corpus is None or not isinstance(base_id, str):
        return
    for version in versions:
        root = schemas[name][version]
        try:
            merged = corpus.effective(name, root, record, (S.pointer(path),))
        except Loop as e:
            result.problems.append((S.pointer(path + ["extends"]), f"extends loops: {e}"))
            return
        if merged is None:
            continue
        seen = {(w, m) for w, m in result.by_version.get(S.version_name(version), [])}
        prefix = S.pointer(path) if path else ""
        extra = [(prefix + w if w != "/" else (prefix or "/"), f"{m} (merged onto {base_id})")
                 for w, m in S.Validator(root).errors(merged)]
        result.add(version, [(w, m) for w, m in extra if (w, m.rsplit(" (merged onto ", 1)[0]) not in seen])


def validate_select(entries, schemas, name, versions, path, result):
    if not isinstance(entries, list) or not entries:
        result.problems.append((S.pointer(path), "$select is a non-empty list of selector entries"))
        return
    for i, entry in enumerate(entries):
        at = path + [i]
        if not isinstance(entry, dict):
            result.problems.append((S.pointer(at), "a selector entry is an object"))
            continue
        unknown = set(entry) - SELECT_KEYS
        if unknown:
            result.problems.append((S.pointer(at), f"unknown selector fields {sorted(unknown)}"))
        if not set(entry) & {"ids", "where", "tag"}:
            result.problems.append((S.pointer(at), "a selector entry has ids, where or tag"))
        if not isinstance(entry.get("patch"), dict):
            result.problems.append((S.pointer(at + ["patch"]), "a selector entry has a patch object"))
            continue
        validate_record(entry["patch"], schemas, name, versions, at + ["patch"], result, patch=True)


def validate_data(doc, schemas, layers, pin=None, only=None, skip_unknown=False, corpus=None):
    result = Result()
    if not isinstance(doc, dict):
        result.problems.append(("/", "a data file is an object keyed by registry name"))
        return result
    for top, records in doc.items():
        m = S.DATA_KEY.match(top)
        if not m:
            result.problems.append((S.pointer([top]), f"{top!r} is not a registry name (<name> or <name>@<major>)"))
            continue
        name, major = m.group("name"), (int(m.group("major")) if m.group("major") else None)
        if only is not None and name != only:
            result.problems.append((S.pointer([top]), f"an example of {only} names registry {name}"))
            continue
        if skip_unknown and name not in schemas:
            continue
        versions = pick_versions(schemas, name, major, pin, S.pointer([top]), result.missing)
        if not versions:
            continue
        if not isinstance(records, dict):
            result.problems.append((S.pointer([top]), "holds an object of records by key"))
            continue
        for key, value in records.items():
            at = [top, key]
            if layers:
                if key == "$select":
                    validate_select(value, schemas, name, versions, at, result)
                    continue
                if not S.RECORD_ID.match(key):
                    result.problems.append((S.pointer(at), f"{key!r} is not a full record id (<namespace>/<key>)"))
                    continue
                if not isinstance(value, dict):
                    result.problems.append((S.pointer(at), "a layer is an object (a JSON Merge Patch)"))
                    continue
                validate_record(value, schemas, name, versions, at, result, patch=True)
            else:
                if not S.valid_key(key):
                    result.problems.append((S.pointer(at), f"{key!r} is not a record key (no spaces, '?', '|' or '*', "
                                            "no leading '/' or '$', no empty part)"))
                    continue
                record = strip_protect(value, at, result)
                if isinstance(record, dict) and "extends" in record:
                    validate_variant(record, schemas, name, versions, at, result, corpus)
                else:
                    validate_record(record, schemas, name, versions, at, result)
    return result


def is_data_file(doc, name):
    if not isinstance(doc, dict) or not doc:
        return False
    for key in doc:
        m = S.DATA_KEY.match(key)
        if not m or m.group("name") != name:
            return False
    return True


def check_example(path, name, schemas, expect, corpus=None, doc=None):
    entry = {"file": str(path), "registry": name, "expect": expect, "kind": None, "versions": {}, "problems": []}
    if doc is None:
        try:
            doc = S.load(path)
        except S.JsonError as e:
            entry["problems"].append(["/", f"not JSON: {e}"])
            entry["ok"] = False
            return entry
    pin = file_pin(path)
    if is_data_file(doc, name):
        entry["kind"] = "layers" if is_layers_name(path) else "records"
        corpus = (corpus or Corpus()).overlay(doc, entry["kind"] == "layers")
        result = validate_data(doc, schemas, entry["kind"] == "layers", pin=pin, only=name, corpus=corpus)
    else:
        entry["kind"] = "record"
        result = Result()
        versions = pick_versions(schemas, name, None, pin, "/", result.missing)
        record = strip_protect(doc, [], result)
        if isinstance(record, dict) and "extends" in record:
            validate_variant(record, schemas, name, versions, [], result, corpus)
        else:
            validate_record(record, schemas, name, versions, [], result)
    entry["versions"] = {v: [list(e) for e in errs] for v, errs in result.by_version.items()}
    entry["problems"] += [list(p) for p in result.missing]
    if expect == "valid":
        entry["problems"] += [list(p) for p in result.problems]
        entry["ok"] = bool(entry["versions"]) and not entry["problems"] and not any(entry["versions"].values())
    else:
        accepted = [v for v, errs in entry["versions"].items() if not errs]
        entry["accepted_by"] = [] if result.problems else accepted
        entry["rejections"] = [list(p) for p in result.problems]
        rejected = bool(result.problems) or (bool(entry["versions"]) and not accepted)
        entry["ok"] = not result.missing and rejected
    return entry


def discover(root):
    root = Path(root)
    found = []
    if not root.is_dir():
        return found
    for folder in sorted(p for p in root.iterdir() if p.is_dir()):
        examples = folder / "examples"
        for f in sorted(examples.glob("*.json")):
            found.append((folder.name, f, "valid"))
        for f in sorted((examples / "must-fail").glob("*.json")):
            found.append((folder.name, f, "invalid"))
    return found


def load_schemas(root):
    out = {}
    for name, versions in S.registry_schemas(root).items():
        loaded = {}
        for version, f in versions.items():
            try:
                loaded[version] = S.load(f)
            except S.JsonError:
                continue
        out[name] = loaded
    return out


def run_examples(root, display):
    schemas = load_schemas(root)
    found = discover(root)
    corpus = Corpus()
    docs = {}
    for name, f, expect in found:
        try:
            docs[f] = S.load(f)
        except S.JsonError:
            continue
        if expect == "valid":
            corpus.add_data(docs[f], is_layers_name(f))
    entries = []
    for name, f, expect in found:
        entry = check_example(f, name, schemas, expect, corpus, docs.get(f))
        entry["file"] = str(Path(display) / f.relative_to(root))
        entries.append(entry)
    return entries


def describe(entry):
    lines = []
    versions = ", ".join(sorted(entry["versions"])) or ("structure" if entry.get("rejections") else "no schema")
    if entry["expect"] == "valid":
        if entry["ok"]:
            return [f"ok    {entry['file']} ({entry['kind']}; {versions})"]
        lines.append(f"FAIL  {entry['file']} ({entry['kind']}; {versions}): must validate")
        for where, message in entry["problems"]:
            lines.append(f"        {where}: {message}")
        for version, errors in sorted(entry["versions"].items()):
            for where, message in errors:
                lines.append(f"        {version} {where}: {message}")
        return lines
    if entry["ok"]:
        first = (entry["rejections"] or [e for _, errs in sorted(entry["versions"].items()) for e in errs])[0]
        return [f"ok    {entry['file']} (must fail; {versions}): rejected, {first[0]}: {first[1]}"]
    lines.append(f"FAIL  {entry['file']} (must fail; {versions}): must be rejected")
    for where, message in entry["problems"]:
        lines.append(f"        {where}: {message}")
    if entry.get("accepted_by"):
        lines.append(f"        accepted by {', '.join(entry['accepted_by'])}")
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description="Validate the registry examples, or data files, against the SDK's schemas.")
    ap.add_argument("--repo", default=".", help="the repository root (default: the current directory)")
    ap.add_argument("--registries", default=REGISTRIES, help=f"the registries folder, relative to --repo (default: {REGISTRIES})")
    ap.add_argument("--json", action="store_true", help="print one JSON report instead of lines")
    ap.add_argument("--schema", help="validate FILES as records against this one schema file")
    ap.add_argument("--patch", action="store_true", help="with --schema: the files are layers (JSON Merge Patches)")
    ap.add_argument("--data", action="store_true", help="validate FILES as an addon's registry/*.json data files")
    ap.add_argument("--addon", help="with --data: the addon id, so a variant's base among FILES is merged and checked")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args(argv)
    repo = Path(args.repo)
    root = repo / args.registries

    if args.schema:
        schema = S.load(args.schema)
        failed = 0
        for f in args.files:
            try:
                errors = S.Validator(schema).errors(S.load(f), patch=args.patch)
            except S.JsonError as e:
                errors = [("/", f"not JSON: {e}")]
            failed += bool(errors)
            print(f"{'ok  ' if not errors else 'FAIL'}  {f}")
            for where, message in errors:
                print(f"        {where}: {message}")
        return 1 if failed else 0

    if args.data:
        schemas = load_schemas(root)
        corpus = Corpus(args.addon) if args.addon else None
        for f in args.files if corpus else ():
            try:
                corpus.add_data(S.load(f), is_layers_name(f))
            except S.JsonError:
                pass
        failed = 0
        for f in args.files:
            report = []
            try:
                doc = S.load(f)
                result = validate_data(doc, schemas, is_layers_name(f), skip_unknown=True, corpus=corpus)
                report += [f"{w}: {m}" for w, m in result.problems + result.missing]
                report += [f"{v} {w}: {m}" for v, errs in sorted(result.by_version.items()) for w, m in errs]
                checked = ", ".join(sorted(result.by_version)) or "no SDK registry"
            except S.JsonError as e:
                report.append(f"not JSON: {e}")
                checked = "-"
            failed += bool(report)
            print(f"{'ok  ' if not report else 'FAIL'}  {f} ({checked})")
            for line in report:
                print(f"        {line}")
        return 1 if failed else 0

    entries = run_examples(root, args.registries)
    failed = [e for e in entries if not e["ok"]]
    if args.json:
        json.dump({"examples": entries, "failed": len(failed)}, sys.stdout, indent=2)
        print()
    else:
        for e in entries:
            for line in describe(e):
                print(line)
        valid = sum(e["expect"] == "valid" for e in entries)
        print(f"examples: {valid} valid, {len(entries) - valid} must-fail, {len(failed)} wrong")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
