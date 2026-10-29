import json

PASS, DIFF, ERROR, SKIP = "pass", "diff", "error", "skip"
DETAIL_LIMIT = 20


def _short(value, limit=300):
    text = value if isinstance(value, str) else json.dumps(value, sort_keys=True)
    return text if len(text) <= limit else text[: limit - 3] + "..."


def status_of(result):
    if result.get("skipped"):
        return SKIP
    if result.get("error"):
        return ERROR
    return PASS if result["comparison"]["same"] else DIFF


def first_difference(result):
    if result.get("skipped"):
        return result["skipped"]
    if result.get("error"):
        return result["error"]
    diffs = result["comparison"]["differences"]
    if not diffs:
        return ""
    d = diffs[0]
    return f"{d['category']}: {d['what']}"


def exit_code(results):
    if any(status_of(r) == ERROR for r in results):
        return 2
    if any(status_of(r) == DIFF for r in results):
        return 1
    return 0


def build(meta, results):
    counts = {PASS: 0, DIFF: 0, ERROR: 0}
    for r in results:
        r["status"] = status_of(r)
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    return {"meta": meta, "counts": counts, "exit": exit_code(results), "results": results}


def summary(report):
    meta = report["meta"]
    a, b = meta.get("a", {}), meta.get("b", {})
    out = [
        f"compat gate: a = swofty {a.get('version', '?')} ({a.get('cli', '?')}), "
        f"b = swofty {b.get('version', '?')} ({b.get('cli', '?')})",
    ]
    if meta.get("allow"):
        out.append(f"allowed difference categories: {', '.join(meta['allow'])}")
    for note in meta.get("notes", []):
        out.append(f"note: {note}")
    out.append("")
    width = max([len(r["set"]) for r in report["results"]] + [10])
    for r in report["results"]:
        line = f"{r['status'].upper():5} {r['set']:{width}}"
        extra = first_difference(r)
        if extra:
            n = len(r.get("comparison", {}).get("differences", []))
            more = f" (+{n - 1} more)" if n > 1 else ""
            line += f"  {extra}{more}"
        allowed = len(r.get("comparison", {}).get("allowed", []))
        if allowed:
            line += f"  [{allowed} allowed]"
        out.append(line.rstrip())
    details = [r for r in report["results"] if r["status"] == DIFF]
    for r in details:
        out.append("")
        out.append(f"{r['set']}:")
        diffs = r["comparison"]["differences"]
        for d in diffs[:DETAIL_LIMIT]:
            out.append(f"  {d['category']}: {d['what']}")
            out.append(f"    a: {_short(d['a'])}")
            out.append(f"    b: {_short(d['b'])}")
        if len(diffs) > DETAIL_LIMIT:
            out.append(f"  and {len(diffs) - DETAIL_LIMIT} more in report.json")
    c = report["counts"]
    out.append("")
    skipped = f", {c[SKIP]} skipped" if c.get(SKIP) else ""
    out.append(f"{c[PASS]} pass, {c[DIFF]} with differences, {c[ERROR]} tool errors{skipped}; exit {report['exit']}")
    return "\n".join(out) + "\n"
