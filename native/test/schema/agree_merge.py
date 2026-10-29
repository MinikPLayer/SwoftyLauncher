#!/usr/bin/env python3
# The keyed merge rule, three ways: tools/std/stdschema.merge_patch (the CI's reference) against
# swofty::json::merge_patch with the schema's keys (schema_agree --merge), on a case for each rule and on
# random bases and patches. A merge that fails must fail in both, and one that succeeds must give the
# same value, member order included.
#
#   agree_merge.py <schema_agree binary> [rounds]

import json
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "std"))

import stdschema as S

SCHEMA = {
    "type": "object",
    "properties": {
        "teams": {"type": "array", "x-key": "slot", "items": {"$ref": "#/$defs/Team"}},
        "changes": {"type": "array", "x-key": ["round", "slot"], "items": {"type": "object"}},
        "seats": {"type": "array"},
        "rules": {"type": "object", "properties": {"order": {"type": "array", "x-key": "id"}}},
        "extra": {"type": "object", "additionalProperties": {"type": "array", "x-key": "id"}},
    },
    "$defs": {"Team": {"type": "object", "properties": {
        "slot": {"type": "integer"}, "drivers": {"type": "array", "x-key": "name"}, "seats": {"type": "array"}}}},
}

RULES = [
    ({"teams": [{"slot": 1, "a": 1}, {"slot": 2}]}, {"teams": [{"slot": 1, "b": 2}, {"slot": 3}]}, "merge by key, append new keys"),
    ({"teams": [{"slot": 1}, {"slot": 2}]}, {"teams": []}, "an empty keyed list changes nothing"),
    ({}, {"teams": []}, "an empty keyed list on a missing list"),
    ({"teams": [{"slot": 1}]}, {"teams": [{"a": 1}]}, "an element without its key is an error"),
    ({"teams": [{"slot": 1}]}, {"teams": [{"slot": None}]}, "a null key is an error"),
    ({"teams": [{"slot": 1}]}, {"teams": [3]}, "a non-object element is an error"),
    ({"changes": [{"round": 1, "slot": 2}]}, {"changes": [{"round": 1}]}, "a two-field key needs both"),
    ({"teams": [{"slot": 1}, {"slot": 2}]}, {"teams": [{"slot": 2, "$delete": True}, {"slot": 9, "$delete": True}]}, "$delete"),
    ({"teams": [{"slot": 1}]}, {"teams": [{"slot": 1, "$delete": False, "a": 1}]}, "$delete false merges and is dropped"),
    ({"teams": [{"slot": 1}]}, {"teams": [{"slot": 1, "$delete": "yes"}]}, "a $delete that isn't a bool"),
    ({"seats": [1, 2]}, {"seats": []}, "a list without x-key replaces whole"),
    ({"seats": [1, 2]}, {"seats": [{"a": 1}]}, "a list without x-key replaces whole with objects"),
    ({"teams": [{"slot": 1, "drivers": [{"name": "a"}]}]}, {"teams": [{"slot": 1, "drivers": []}]}, "nested empty keyed list"),
    ({"teams": [{"slot": 1, "drivers": [{"name": "a"}]}]}, {"teams": [{"slot": 1, "drivers": [{"x": 1}]}]}, "nested keyless"),
    ({"rules": {"order": [{"id": 1, "v": 1}]}}, {"rules": {"order": [{"id": 1, "v": 2}]}}, "keyed list inside an object"),
    ({"extra": {"m": [{"id": 1}]}}, {"extra": {"m": [{"x": 1}]}}, "additionalProperties don't make a keyed list"),
    ({"teams": [{"slot": 1}]}, {"teams": [{"slot": 1.0, "f": 1}]}, "number keys match by value"),
    ({"teams": "text"}, {"teams": [{"slot": 1}]}, "a keyed patch on a value that isn't a list"),
]


def rand_value(rng, depth):
    r = rng.random()
    if depth > 2 or r < 0.3:
        return rng.choice([None, 1, 2, 1.0, "a", True, [], {}])
    if r < 0.5:
        return [rand_value(rng, depth + 1) for _ in range(rng.randint(0, 3))]
    return {rng.choice(["a", "b", "slot", "name", "$delete"]): rand_value(rng, depth + 1) for _ in range(rng.randint(0, 3))}


def rand_element(rng, key, depth):
    e = {} if rng.random() < 0.1 else {key: rng.choice([1, 2, 3, 1.0, "1", None] if rng.random() < 0.2 else [1, 2, 3])}
    for _ in range(rng.randint(0, 2)):
        name = rng.choice(["a", "drivers", "seats", "$delete"])
        if name == "drivers":
            e[name] = [rand_element(rng, "name", depth + 1) for _ in range(rng.randint(0, 2))] if depth < 2 else []
        elif name == "$delete":
            e[name] = rng.choice([True, False, True, "x"])
        else:
            e[name] = rand_value(rng, depth + 1)
    return e


def rand_doc(rng):
    doc = {}
    for name in rng.sample(["teams", "seats", "rules", "a"], rng.randint(0, 4)):
        if name == "teams":
            doc[name] = [rand_element(rng, "slot", 0) for _ in range(rng.randint(0, 3))] if rng.random() < 0.9 else None
        elif name == "rules":
            doc[name] = {"order": [rand_element(rng, "id", 1) for _ in range(rng.randint(0, 2))]}
        else:
            doc[name] = rand_value(rng, 1)
    return doc


def python_merge(base, patch):
    try:
        return S.merge_patch(SCHEMA, SCHEMA, base, patch)
    except S.MergeError:
        return None


def main():
    binary = sys.argv[1]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    rng = random.Random(1606)
    cases = [(b, p, why) for b, p, why in RULES]
    for i in range(rounds):
        base = {k: v for k, v in rand_doc(rng).items() if v is not None}
        cases.append((base, rand_doc(rng), f"random {i}"))
    payload = json.dumps([[SCHEMA, b, p] for b, p, _ in cases])
    out = subprocess.run([binary, "--merge"], input=payload, capture_output=True, text=True, check=True)
    core = json.loads(out.stdout)
    wrong = []
    for (base, patch, why), got in zip(cases, core):
        want = python_merge(base, patch)
        if json.dumps(want) != json.dumps(got):
            wrong.append(f"{why}: {json.dumps(base)} + {json.dumps(patch)}: python {json.dumps(want)}, core {json.dumps(got)}")
    for line in wrong[:30]:
        print(f"FAIL  {line}")
    print(f"merge agreement: {len(RULES)} rule cases and {rounds} random ones, {len(wrong)} differences")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
