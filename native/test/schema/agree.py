#!/usr/bin/env python3
# Compares the core's schema check (schema_agree's report) with tools/std/validate.py --json on the same
# registries folder: every file's verdict must match, and so must the error pointers of each version,
# the structure problems and the rejections. Messages may differ in wording and are not compared.
#
#   agree.py <validate.py report> <schema_agree report>

import json
import sys

# Files allowed to differ (by the end of their path), each with the reason. A listed file that no longer
# differs is reported, so the entry can go.
INTENDED = {}


def intended(f):
    for suffix, reason in INTENDED.items():
        if f.endswith("/" + suffix):
            return suffix, reason
    return None


def pointers(pairs):
    return [p[0] for p in pairs]


def main():
    py = json.load(open(sys.argv[1]))
    cc = json.load(open(sys.argv[2]))
    by_file = {e["file"]: e for e in cc["examples"]}
    wrong = []
    notes = []
    for e in py["examples"]:
        f = e["file"]
        c = by_file.pop(f, None)
        if c is None:
            wrong.append(f"{f}: missing from the core's report")
            continue
        found = []
        if e["ok"] != c["ok"]:
            found.append(f"{f}: verdict differs (python ok={e['ok']}, core ok={c['ok']})")
        if e["kind"] != c["kind"]:
            found.append(f"{f}: kind differs (python {e['kind']}, core {c['kind']})")
        if sorted(e["versions"]) != sorted(c["versions"]):
            found.append(f"{f}: checked versions differ (python {sorted(e['versions'])}, core {sorted(c['versions'])})")
        checks = [("problems", pointers(e["problems"]), pointers(c["problems"]))]
        for v in sorted(set(e["versions"]) & set(c["versions"])):
            checks.append((f"{v} errors", pointers(e["versions"][v]), pointers(c["versions"][v])))
        if e["expect"] == "invalid":
            checks.append(("rejections", pointers(e.get("rejections", [])), pointers(c.get("rejections", []))))
            checks.append(("accepted_by", sorted(e.get("accepted_by", [])), sorted(c.get("accepted_by", []))))
        for what, a, b in checks:
            if a != b:
                found.append(f"{f}: {what} differ: python {a}, core {b}")
        known = intended(f)
        if known and found:
            notes += [f"{line} (intended: {known[1]})" for line in found]
        elif known:
            notes.append(f"{f}: listed as intended but agrees now; remove {known[0]!r} from INTENDED")
        else:
            wrong += found
    for f in by_file:
        wrong.append(f"{f}: only in the core's report")
    for line in notes:
        print(f"note  {line}")
    for line in wrong:
        print(f"FAIL  {line}")
    total = len(py["examples"])
    print(f"schema agreement: {total} files, {len(wrong)} differences, {len(notes)} intended")
    return 1 if wrong or total == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
