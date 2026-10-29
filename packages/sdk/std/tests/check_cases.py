#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Checks the shape of every case in packages/sdk/std/tests/cases (README.md describes the format)."""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
APPLIES = {"boot", "content", "session", "read"}
SCOPES = {"global", "profile", "career"}
STAGES = {"register", "on_load"}
STATUSES = {"ok", "no_record", "no_registry", "invalid", "late"}
CONFLICT_KINDS = {"field", "protected", "provider"}
REASONS = {"invalid", "boot_filtered", "late", "cycle", "too_deep"}
EDIT_OPS = {"set", "add", "mul"}
IN_KEYS = {"mode", "content", "content_exact", "data_group", "setup_mode", "owner", "any", "not"}
CONTEXT_KEYS = {"profile", "career", "mode", "content", "data_group", "setup_mode", "options", "settings", "disabled"}
ADDON_ID = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
RECORD_ID = re.compile(r"^[a-z0-9][a-z0-9_.-]*(/[A-Za-z0-9_.-]+)+$")
POINTER = re.compile(r"^(/([^/=,]+(=[^/,]+(,[^/=,]+=[^/,]+)*)?|[0-9]+))*$")
OPS = {
    "add": ({"registry", "key", "value"}, {"stage"}),
    "variant": ({"registry", "key", "base"}, {"patch", "edit", "stage"}),
    "provide": ({"registry", "records"}, {"stage"}),
    "layer": ({"registry"}, {"target", "select", "patch", "edit", "scope", "profile", "career", "in", "when", "stage"}),
    "clear_layer": ({"registry", "target"}, {"scope", "profile", "career"}),
    "tag_add": ({"registry", "tag", "id"}, set()),
    "protect": ({"registry", "id", "fields"}, set()),
}


class Bad(Exception):
    pass


def need(cond, msg):
    if not cond:
        raise Bad(msg)


def keys_ok(obj, required, optional, where):
    need(isinstance(obj, dict), f"{where}: not an object")
    missing = required - obj.keys()
    need(not missing, f"{where}: missing {sorted(missing)}")
    extra = obj.keys() - required - optional
    need(not extra, f"{where}: unknown keys {sorted(extra)}")


def pattern_matches(pattern, value):
    parts = [p for p in pattern.split("/") if p]
    out = [value]
    for p in parts:
        nxt = []
        for v in out:
            if p == "*" and isinstance(v, list):
                nxt.extend(v)
            elif isinstance(v, dict) and p in v:
                nxt.append(v[p])
        out = nxt
    return out


def check_keyed(registry, decl, value, where):
    for pattern, fields in decl.get("key_fields", {}).items():
        for arr in pattern_matches(pattern, value):
            if arr is None:
                continue
            need(isinstance(arr, list), f"{where}: {registry} {pattern} is not a list")
            seen = set()
            for el in arr:
                need(isinstance(el, dict), f"{where}: {registry} {pattern} element is not an object")
                missing = [f for f in fields if f not in el]
                need(not missing, f"{where}: {registry} {pattern} element lacks key fields {missing}")
                k = tuple(json.dumps(el[f]) for f in fields)
                need(k not in seen, f"{where}: {registry} {pattern} has key {k} twice")
                seen.add(k)


def check_in(sel, where):
    need(isinstance(sel, dict) and len(sel) == 1, f"{where}: an in selector is an object with one key")
    (k, v), = sel.items()
    need(k in IN_KEYS, f"{where}: unknown in selector {k}")
    if k == "any":
        need(isinstance(v, list) and v, f"{where}: any takes a list")
        for s in v:
            check_in(s, where)
    elif k == "not":
        check_in(v, where)
    else:
        need(isinstance(v, str), f"{where}: {k} takes a string")


def check_edit(edit, where):
    need(isinstance(edit, list) and edit, f"{where}: edit is a non-empty list")
    for e in edit:
        keys_ok(e, {"op", "path", "value"}, set(), where + " edit")
        need(e["op"] in EDIT_OPS, f"{where}: unknown edit op {e['op']}")
        need(POINTER.match(e["path"]) and e["path"], f"{where}: bad edit path {e['path']}")
        if e["op"] != "set":
            need(isinstance(e["value"], (int, float)), f"{where}: {e['op']} takes a number")


def check_case(path):
    c = json.loads(path.read_text())
    keys_ok(c, {"name", "rule", "registries", "bases", "addons", "evaluations"}, {"tags"}, path.name)
    regs = c["registries"]
    need(isinstance(regs, dict) and regs, "registries: empty")
    for name, decl in regs.items():
        need(re.match(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$", name), f"registry name {name}")
        keys_ok(decl, {"applies"}, {"key_fields", "refs", "schema", "shared"}, f"registry {name}")
        need(decl["applies"] in APPLIES, f"registry {name}: applies {decl['applies']}")
        for p, fields in decl.get("key_fields", {}).items():
            need(p.startswith("/") and isinstance(fields, list) and fields, f"registry {name}: key_fields {p}")
        for p, target in decl.get("refs", {}).items():
            need(p.startswith("/") and target in regs, f"registry {name}: ref {p} -> {target} (declare the target)")
        if "shared" in decl:
            keys_ok(decl["shared"], {"provider_registry", "provides", "resolved"}, set(), f"registry {name} shared")
            need(decl["shared"]["provider_registry"] in regs, f"registry {name}: shared provider registry undeclared")

    ids_in_case = set()
    for i, b in enumerate(c["bases"]):
        keys_ok(b, {"addon", "registry", "key", "value"}, set(), f"bases[{i}]")
        need(b["addon"] == "game", f"bases[{i}]: addon must be game")
        need(b["registry"] in regs, f"bases[{i}]: undeclared registry {b['registry']}")
        check_keyed(b["registry"], regs[b["registry"]], b["value"], f"bases[{i}]")
        ids_in_case.add(f"game/{b['key']}")

    for reg, tags in c.get("tags", {}).items():
        need(reg in regs, f"tags: undeclared registry {reg}")
        for tag, members in tags.items():
            need(tag.startswith("c/"), f"tags: core tags are c/ ({tag})")
            need(all(RECORD_ID.match(m) for m in members), f"tags: bad member in {tag}")

    seen_addons = set()
    op_counts = {}
    for ai, a in enumerate(c["addons"]):
        keys_ok(a, {"id", "ops"}, {"enabled"}, f"addons[{ai}]")
        need(ADDON_ID.match(a["id"]) and a["id"] != "game", f"addons[{ai}]: bad id {a['id']}")
        need(a["id"] not in seen_addons, f"addons: {a['id']} twice in load order")
        seen_addons.add(a["id"])
        op_counts[a["id"]] = len(a["ops"])
        for oi, op in enumerate(a["ops"]):
            where = f"{a['id']} op {oi}"
            need(isinstance(op, dict) and op.get("op") in OPS, f"{where}: unknown op")
            req, opt = OPS[op["op"]]
            keys_ok(op, req | {"op"}, opt, where)
            need(op["registry"] in regs, f"{where}: undeclared registry {op['registry']}")
            decl = regs[op["registry"]]
            if op.get("stage") is not None:
                need(op["stage"] in STAGES, f"{where}: stage")
            if op["op"] == "add":
                check_keyed(op["registry"], decl, op["value"], where)
                ids_in_case.add(f"{a['id']}/{op['key']}")
            elif op["op"] == "variant":
                need(("patch" in op) != ("edit" in op), f"{where}: variant takes patch or edit")
                need(RECORD_ID.match(op["base"]), f"{where}: base id")
                if "edit" in op:
                    check_edit(op["edit"], where)
                ids_in_case.add(f"{a['id']}/{op['key']}")
            elif op["op"] == "provide":
                need(isinstance(op["records"], dict) and op["records"], f"{where}: records")
                for k, v in op["records"].items():
                    if v is not None:
                        check_keyed(op["registry"], decl, v, where)
                        ids_in_case.add(f"{a['id']}/{k}")
            elif op["op"] == "layer":
                need(("target" in op) != ("select" in op), f"{where}: layer takes target or select")
                need(("patch" in op) != ("edit" in op), f"{where}: layer takes patch or edit")
                if "target" in op:
                    need(RECORD_ID.match(op["target"]), f"{where}: target id")
                else:
                    sel = op["select"]
                    keys_ok(sel, set(), {"ids", "except", "where", "tag"}, where + " select")
                    need(sel, f"{where}: empty select")
                    if "ids" in sel:
                        need(sel["ids"] == "*" or sel["ids"].endswith("/*"), f"{where}: ids is \"*\" or \"<prefix>/*\"")
                    if "tag" in sel:
                        t = sel["tag"]
                        if isinstance(t, dict):
                            need(len(t) == 1, f"{where}: a field tag names one field")
                            (field, _), = t.items()
                            need(f"/{field}" in decl.get("refs", {}), f"{where}: tag field {field} is not a declared ref")
                if "edit" in op:
                    check_edit(op["edit"], where)
                scope = op.get("scope", "global")
                need(scope in SCOPES, f"{where}: scope")
                need((scope == "profile") == ("profile" in op), f"{where}: a profile layer names its profile")
                need((scope == "career") == ("career" in op), f"{where}: a career layer names its career")
                for s in op.get("in", []):
                    check_in(s, where)
                if "when" in op:
                    w = op["when"]
                    need(isinstance(w, dict) and "option" in w and len(w) == 2
                         and (set(w) - {"option"}) <= {"is", "in", "on", "off"}, f"{where}: when")
            elif op["op"] == "clear_layer":
                need(op.get("scope", "global") in SCOPES, f"{where}: scope")
            elif op["op"] == "tag_add":
                need(op["tag"].startswith("c/") or op["tag"].startswith(a["id"] + "/"), f"{where}: tag id")
                need(RECORD_ID.match(op["id"]), f"{where}: member id")
            elif op["op"] == "protect":
                need(op["id"].startswith(a["id"] + "/"), f"{where}: protect only your own records")
                need(isinstance(op["fields"], list) and op["fields"], f"{where}: fields")

    need(c["evaluations"], "no evaluations")
    for ei, e in enumerate(c["evaluations"]):
        where = f"evaluations[{ei}]"
        keys_ok(e, {"context", "expect", "conflicts", "refusals"}, {"lists", "tag_members"}, where)
        keys_ok(e["context"], set(), CONTEXT_KEYS, where + " context")
        cx = e["context"]
        for d in cx.get("disabled", []):
            need(d in seen_addons, f"{where}: disabled {d} is not an addon of the case")
        if cx.get("content") is not None:
            need(isinstance(cx["content"], list) and cx["content"], f"{where}: content is the active content's variant chain")
        for addon_id in list(cx.get("options", {})) + list(cx.get("settings", {})):
            need(addon_id in seen_addons, f"{where}: options/settings for unknown addon {addon_id}")
        need(e["expect"], f"{where}: expect is empty")
        for reg, recs in e["expect"].items():
            need(reg in regs, f"{where}: expect names undeclared registry {reg}")
            for rid, r in recs.items():
                need(isinstance(r, dict) and r.get("status") in STATUSES, f"{where}: {rid} status")
                if r["status"] == "ok":
                    keys_ok(r, {"status", "value", "origins"}, set(), f"{where} {rid}")
                    check_keyed(reg, regs[reg], r["value"], f"{where} {rid}")
                    for p, who in r["origins"].items():
                        need(POINTER.match(p) and p, f"{where}: {rid} origin pointer {p}")
                        need(who in seen_addons, f"{where}: {rid} origin {who} is not an addon of the case")
                else:
                    keys_ok(r, {"status"}, set(), f"{where} {rid}")
        for k in e["conflicts"]:
            keys_ok(k, {"kind", "registry", "id", "pointer", "addons", "winner"}, set(), where + " conflict")
            need(k["kind"] in CONFLICT_KINDS and k["registry"] in regs, f"{where}: conflict kind/registry")
            need(k["pointer"] == "" or POINTER.match(k["pointer"]), f"{where}: conflict pointer")
            need(all(x in seen_addons for x in k["addons"]) and k["addons"], f"{where}: conflict addons")
            order = [a["id"] for a in c["addons"]]
            need([order.index(x) for x in k["addons"]] == sorted(order.index(x) for x in k["addons"]),
                 f"{where}: conflict addons are listed in load order")
        for r in e["refusals"]:
            keys_ok(r, {"addon", "registry", "id", "op", "reason"}, set(), where + " refusal")
            need(r["addon"] in seen_addons and r["registry"] in regs, f"{where}: refusal addon/registry")
            need(r["reason"] in REASONS, f"{where}: refusal reason {r['reason']}")
            need(0 <= r["op"] < op_counts[r["addon"]], f"{where}: refusal op index")
        for l in e.get("lists", []):
            keys_ok(l, {"registry", "prefix", "ids"}, set(), where + " list")
            need(l["registry"] in regs and l["ids"] == sorted(l["ids"]), f"{where}: list ids are sorted")
        for t in e.get("tag_members", []):
            keys_ok(t, {"registry", "tag", "ids"}, set(), where + " tag_members")
            need(t["registry"] in regs and t["ids"] == sorted(t["ids"]), f"{where}: tag member ids are sorted")


def main():
    files = sorted((HERE / "cases").glob("*.json"))
    if not files:
        print("no cases")
        return 1
    failed = 0
    names = set()
    for f in files:
        try:
            need(re.match(r"^[0-9]{3}-[a-z0-9-]+\.json$", f.name), "file name is NNN-<slug>.json")
            need(f.name[:3] not in names, "case number used twice")
            names.add(f.name[:3])
            check_case(f)
            print(f"ok   {f.name}")
        except (Bad, json.JSONDecodeError, KeyError, TypeError) as e:
            failed += 1
            print(f"FAIL {f.name}: {e}")
    print(f"{len(files) - failed}/{len(files)} cases well formed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
