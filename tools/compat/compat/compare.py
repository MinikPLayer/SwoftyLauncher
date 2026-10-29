import json
from collections import Counter

CATEGORIES = (
    "step",
    "build",
    "buildhash",
    "order",
    "file",
    "loader",
    "plan",
    "lines",
    "warnings",
    "db",
    "parts",
    "conflicts",
    "waiting",
    "generated",
)


def _diff(category, what, a, b, addon=None):
    d = {"category": category, "what": what, "a": a, "b": b}
    if addon:
        d["addon"] = addon
    return d


def _list_diff(category, what, a, b):
    a, b = list(a or []), list(b or [])
    if a == b:
        return []
    ca, cb = Counter(a), Counter(b)
    only_a = sorted((ca - cb).elements())
    only_b = sorted((cb - ca).elements())
    if not only_a and not only_b:
        return [_diff(category, f"{what}: same entries in a different order", a, b)]
    return [_diff(category, what, only_a, only_b)]


def _canon(v):
    return json.dumps(v, sort_keys=True)


def compare_files(a, b):
    out = []
    for rel in sorted(set(a) | set(b)):
        fa, fb = a.get(rel), b.get(rel)
        cat = "loader" if "loader" in ((fa or {}).get("class"), (fb or {}).get("class")) else "file"
        addon = (fa or fb).get("addon")
        if fa is None:
            out.append(_diff(cat, f"{rel}: only in b", None, fb.get("sha256"), addon))
        elif fb is None:
            out.append(_diff(cat, f"{rel}: only in a", fa.get("sha256"), None, addon))
        elif fa.get("missing") or fb.get("missing"):
            if fa.get("missing") != fb.get("missing"):
                out.append(_diff(cat, f"{rel}: source missing", fa.get("missing"), fb.get("missing"), addon))
        elif fa.get("sha256") != fb.get("sha256"):
            out.append(
                _diff(cat, f"{rel}: contents differ", f"{fa.get('size')} {fa.get('sha256', '')[:16]}",
                      f"{fb.get('size')} {fb.get('sha256', '')[:16]}", addon)
            )
    return out


def compare_plan(a, b):
    if a is None or b is None:
        if (a is None) != (b is None):
            return [_diff("plan", "plan.json", "present" if a else "absent", "present" if b else "absent")]
        return []
    out = []
    if _canon(a.get("tracks")) != _canon(b.get("tracks")):
        out.append(_diff("plan", "tracks", a.get("tracks"), b.get("tracks")))
    fa, fb = a.get("files") or {}, b.get("files") or {}
    for rel in sorted(set(fa) & set(fb)):
        if fa[rel] != fb[rel]:
            out.append(_diff("plan", f"{rel}: placed differently", fa[rel], fb[rel]))
    return out


def compare_parts(a, b):
    out = []
    for addon in sorted(set(a or {}) | set(b or {})):
        pa, pb = (a or {}).get(addon), (b or {}).get(addon)
        if pa == pb:
            continue
        if pa is None or pb is None:
            out.append(_diff("parts", f"{addon}: outputs recorded on one side only", pa, pb, addon))
            continue
        for part in sorted(set(pa) | set(pb)):
            if pa.get(part) != pb.get(part):
                out.append(_diff("parts", f"{addon}: {part} output differs", pa.get(part), pb.get(part), addon))
    return out


def compare(a, b, allow=()):
    diffs = []
    sa, sb = a.get("steps") or [], b.get("steps") or []
    for i in range(max(len(sa), len(sb))):
        x = sa[i] if i < len(sa) else None
        y = sb[i] if i < len(sb) else None
        if x is None or y is None or x.get("step") != y.get("step"):
            diffs.append(_diff("step", f"step {i + 1}", x and x.get("step"), y and y.get("step")))
            continue
        if x.get("rc") != y.get("rc"):
            diffs.append(_diff("step", f"{x['step']}: exit code", x.get("rc"), y.get("rc")))
        diffs += _list_diff("step", f"{x['step']}: output", x.get("out"), y.get("out"))
    ba, bb = a.get("build") or {}, b.get("build") or {}
    if ba.get("rc") != bb.get("rc"):
        diffs.append(_diff("build", "build exit code", ba.get("rc"), bb.get("rc")))
    diffs += _list_diff("build", "build errors", ba.get("stderr"), bb.get("stderr"))
    if a.get("buildhash") != b.get("buildhash"):
        diffs.append(_diff("buildhash", "build hash", a.get("buildhash"), b.get("buildhash")))
    if a.get("order") != b.get("order"):
        diffs.append(_diff("order", "load order", a.get("order"), b.get("order")))
    diffs += compare_files(a.get("files") or {}, b.get("files") or {})
    diffs += compare_plan(a.get("plan"), b.get("plan"))
    if a.get("lines") is None and b.get("lines") is None:
        diffs += _list_diff("lines", "build output", _no_hash(ba.get("stdout")), _no_hash(bb.get("stdout")))
    else:
        diffs += _list_diff("lines", "build lines", a.get("lines"), b.get("lines"))
    diffs += _list_diff("warnings", "warnings", a.get("warnings"), b.get("warnings"))
    da, db = a.get("db") or {}, b.get("db") or {}
    for k in sorted(set(da) | set(db)):
        if _canon(da.get(k)) != _canon(db.get(k)):
            diffs.append(_diff("db", f"database {k}", da.get(k), db.get(k)))
    diffs += compare_parts(a.get("parts"), b.get("parts"))
    for k in ("conflicts", "waiting", "registry_override"):
        if _canon(a.get(k)) != _canon(b.get(k)):
            diffs.append(_diff("conflicts" if k == "conflicts" else "waiting", k, a.get(k), b.get(k)))
    ga, gb = a.get("generated") or {}, b.get("generated") or {}
    for rel in sorted(set(ga) | set(gb)):
        if ga.get(rel) != gb.get(rel):
            diffs.append(_diff("generated", f"{rel}: written during the build", ga.get(rel), gb.get(rel)))
    explained = []
    if [d["category"] for d in diffs] == ["buildhash"] and ga and ga == gb:
        d = diffs.pop()
        d["explained"] = (
            "every output is the same; the build hash covers the modification times of the "
            f"{len(ga)} file(s) the addon's build program wrote, and their contents are the same"
        )
        explained.append(d)
    elif (ga or gb) and any(d["category"] == "generated" for d in diffs) and all(
        d["category"] in {"buildhash", "generated"} or d["category"] in allow for d in diffs
    ):
        changed = sum(d["category"] == "generated" for d in diffs)
        for d in diffs:
            if d["category"] in {"buildhash", "generated"}:
                d["explained"] = (
                    f"every output is the same; the addon's build program wrote {changed} file(s) differently "
                    "on the two sides, and no game file, plan entry, line, warning or count changed"
                )
                explained.append(d)
        diffs = [d for d in diffs if "explained" not in d]
    allowed = explained + [d for d in diffs if d["category"] in allow]
    failing = [d for d in diffs if d["category"] not in allow]
    return {"same": not failing, "differences": failing, "allowed": allowed}


def _no_hash(lines):
    return [l for l in (lines or []) if not l.startswith("overlay ")]
