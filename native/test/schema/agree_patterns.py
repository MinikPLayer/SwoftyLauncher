#!/usr/bin/env python3
# Random patterns and texts through the core's matcher (schema_agree --patterns) and Python's re.search,
# the reference. Every pattern the core compiles must compile in Python and match the same texts; the
# core may refuse a pattern Python takes (lookaround, backreferences, flags), never the other way round
# for the syntax it supports.
#
#   agree_patterns.py <schema_agree binary> [rounds]

import json
import random
import re
import subprocess
import sys
import warnings

ATOMS = ["a", "b", "ab", ".", "\\d", "\\w", "\\s", "\\D", "\\W", "\\S", "\\.", "\\-", "[ab]", "[^a]", "[a-c]", "[\\d_]",
         "[]a]", "[a-]", "\\b", "\\B", "^", "$", "\\A", "\\Z", "\\x41", "\\u00e9", "\\n", "{", "}", "-", ",", " ", "é"]
QUANTS = ["", "", "", "*", "+", "?", "*?", "+?", "{2}", "{1,2}", "{,2}", "{2,}", "{0}"]


def pattern(rng, depth=0):
    parts = []
    for _ in range(rng.randint(1, 4)):
        r = rng.random()
        if r < 0.15 and depth < 3:
            body = pattern(rng, depth + 1)
            parts.append(rng.choice(["(", "(?:"]) + body + ")" + rng.choice(QUANTS))
        elif r < 0.25 and depth < 3:
            parts.append(pattern(rng, depth + 1) + "|" + pattern(rng, depth + 1))
        else:
            parts.append(rng.choice(ATOMS) + rng.choice(QUANTS))
    return "".join(parts)


def text(rng):
    return "".join(rng.choice("aabbc-_ \n{}A1é9.") for _ in range(rng.randint(0, 10)))


def main():
    binary = sys.argv[1]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    rng = random.Random(1606)
    cases = [[pattern(rng), [text(rng) for _ in range(6)]] for _ in range(rounds)]
    cases += [["^[a-z0-9_]+$", ["abc", "abc\n", "a b", ""]], ["^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$", ["#a0b1c2", "#a0b1c2ff1"]]]
    out = subprocess.run([binary, "--patterns"], input=json.dumps(cases), capture_output=True, text=True, check=True)
    results = json.loads(out.stdout)
    wrong = []
    compiled = 0
    for (pat, texts), (ok, matched) in zip(cases, results):
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                rx = re.compile(pat)
        except re.error:
            rx = None
        if not ok:
            continue
        compiled += 1
        if rx is None:
            wrong.append(f"{pat!r}: the core compiles it, Python refuses it")
            continue
        for t, m in zip(texts, matched):
            if bool(rx.search(t)) != m:
                wrong.append(f"{pat!r} on {t!r}: python {bool(rx.search(t))}, core {m}")
    for line in wrong[:40]:
        print(f"FAIL  {line}")
    print(f"pattern agreement: {len(cases)} patterns, {compiled} compiled by the core, {len(wrong)} differences")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
