import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from . import report

PIN = ["taskset", "-c", "2-7,10,11,14-31", "nice", "-n", "10"]
DEFAULT_LOCK = "/mnt/work/.f1-game.lock.d"
DEFAULT_QUEUE = "/mnt/work/.f1-game.queue"
WHO = "std16-gate2"
DEFAULT_CAP = 1 << 20
PROFILE = "compat"
CATEGORIES = ("dll", "drop", "chain", "conflict", "unresolved", "register", "start", "run")
CORE_DLL = "swofty_core.dll"
REGISTRY_NOTE = "[registry] "
CORE_DLLS = (CORE_DLL, "swofty_gamelog.dll")

STAMP = re.compile(r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)? \[\s*\d+\] ?")
POINTER = re.compile(r"\b0x[0-9a-fA-F]{9,}\b|\b[0-9A-F]{16}\b")
DURATION = re.compile(r"\b\d+(?:\.\d+)? ?m?s\b")
CRASH_NAME = re.compile(r"crash-\d{8}-\d{6}")
LOADER_FRAME = re.compile(r"(?:VERSION|swofty_core|swofty_gamelog)\.dll\+0x[0-9a-f]+(?: \.text fn \+0x[0-9a-f]+\+0x[0-9a-f]+)?")
LOADED = re.compile(r"^addon (\S+): loaded at \S+, (.*)$")
ALSO = re.compile(r"^addon (\S+): (also in|settings\.json lists|listed twice|loaded at \S+ but exports no|too many|exports SwoftyCoreAttach but did not)(.*)$")
DROPPED = re.compile(r"^\[core\] addon (\S+) dropped: (.*)$")
INSTALLED = re.compile(r"^\[core\] (\w+) (\S+) at (?:0x)?\S+ \(base\+0x[0-9a-f]+\) installed: (.*)$")
OWNER = re.compile(r"^(.+?)\((-?\d+)(, overwrite)?\)$")
DLL_NAME = re.compile(r"[\w.-]+\.dll")
CONFLICT = re.compile(r"^\[core\] (conflict|warning): ")
NEEDS = re.compile(r"^\[core\] addon (\S+) (needs .*|waits: .*)$")
REGISTERED = re.compile(r"^\[core\] addon (\S+): registered (\d+) mixin\(s\)")
REFUSED = re.compile(r"^\[core\] addon (\S+): (.*(?:refused|malformed).*)$")
NOISE = re.compile(
    r"^(\[core\] (bind |binding |scan|caches|hook pool|build |bindings|session\.json|binding resolution|core mixin set)|"
    r"\[bind\] |scan |swofty loader|exe: |\[core\]   )"
)
SECTION_END = re.compile(r"^(addons:|stage:|early stage:|\[core\] (commit|.* pass:|bindings:|hook pool|conflict|warning)|"
                         r"trigger|fallback)")


def clean(line, roots=()):
    line = STAMP.sub("", line.rstrip("\r\n"))
    for root in roots:
        for v in (root, "Z:" + root.replace("/", "\\"), "Z:" + root.replace("/", "\\\\")):
            line = line.replace(v, "<game>")
    line = POINTER.sub("<ptr>", line)
    line = CRASH_NAME.sub("crash-<t>", line)
    line = LOADER_FRAME.sub(lambda m: m.group(0).split("+")[0] + "+<off>", line)
    return DURATION.sub("<ms>", line)


def parse_log(text, roots=()):
    out = {"dlls": [], "drops": [], "chains": {}, "conflicts": [], "unresolved": {}, "registered": {}, "start": {}}
    stage = "window"
    current = None
    for raw in text.splitlines():
        if not raw.strip():
            continue
        line = clean(raw, roots)
        if "(early stage)" in line:
            stage = "early"
        elif "loading the addons without" in line:
            stage = "window"
        m = LOADED.match(line)
        if m:
            kind = "v1" if "API v1 plugin" in m.group(2) else "v2+"
            out["dlls"].append({"dll": m.group(1), "stage": stage, "api": kind})
            current = m.group(1)
            out["start"].setdefault(current, [])
            continue
        m = ALSO.match(line)
        if m:
            out["drops"].append(f"{m.group(1)}: {m.group(2)}{m.group(3)}".strip())
            continue
        m = DROPPED.match(line)
        if m:
            out["drops"].append(f"{m.group(1)}: {m.group(2)}")
            continue
        m = INSTALLED.match(line)
        if m:
            owners = []
            for part in m.group(3).split(" > "):
                o = OWNER.match(part.strip())
                name = o.group(1) if o else part.strip()
                if name.startswith(CORE_DLLS):
                    name = CORE_DLL
                owners.append({"addon": name, "priority": int(o.group(2)), "overwrite": bool(o.group(3))}
                              if o else {"addon": name, "priority": None, "overwrite": False})
            out["chains"][f"{m.group(1)} {m.group(2)}"] = owners
            current = None
            continue
        if CONFLICT.match(line):
            out["conflicts"].append(line)
            current = None
            continue
        m = NEEDS.match(line)
        if m:
            out["unresolved"].setdefault(m.group(1), []).append(m.group(2))
            continue
        m = REGISTERED.match(line)
        if m:
            out["registered"][m.group(1)] = int(m.group(2))
            continue
        m = REFUSED.match(line)
        if m:
            out["drops"].append(f"{m.group(1)}: {m.group(2)}")
            continue
        if NOISE.match(line) or SECTION_END.match(line):
            current = None
            continue
        if current:
            out["start"][current].append(line)
            continue
        known = [d for d in DLL_NAME.findall(line) if d in out["start"]]
        if known:
            out["start"][known[0]].append(line)
    return out


def _diff(category, what, a, b, addon=None):
    d = {"category": category, "what": what, "a": a, "b": b}
    if addon:
        d["addon"] = addon
    return d


def _only_core(owners):
    return all(o["addon"] == CORE_DLL for o in owners)


def _without_core(owners):
    return [o for o in owners if o["addon"] != CORE_DLL]


def _fmt(owners):
    if owners is None:
        return None
    return " > ".join(f"{o['addon']}({o['priority']}{', overwrite' if o['overwrite'] else ''})" for o in owners)


def compare_logs(a, b, allow=()):
    diffs, allowed = [], []

    def explain(d, why):
        d["explained"] = why
        allowed.append(d)

    la = [(x["dll"], x["stage"]) for x in a["dlls"]]
    lb = [(x["dll"], x["stage"]) for x in b["dlls"]]
    core_b = [x for x in lb if x[0] == CORE_DLL and x not in la]
    lb_cmp = [x for x in lb if x not in core_b]
    la_cmp = [x for x in la if x[0] != CORE_DLL or x in lb]
    if la_cmp != lb_cmp:
        only_a = [x for x in la_cmp if x not in lb_cmp]
        only_b = [x for x in lb_cmp if x not in la_cmp]
        what = "DLLs loaded" if only_a or only_b else "DLLs loaded in a different order or stage"
        diffs.append(_diff("dll", what, [f"{n} ({s})" for n, s in (only_a or la_cmp)],
                           [f"{n} ({s})" for n, s in (only_b or lb_cmp)]))
    apis_a = {x["dll"]: x["api"] for x in a["dlls"]}
    for x in b["dlls"]:
        if x["dll"] in apis_a and apis_a[x["dll"]] != x["api"]:
            diffs.append(_diff("dll", f"{x['dll']}: API level", apis_a[x["dll"]], x["api"], x["dll"]))

    new_drops = [d for d in b["drops"] if d not in a["drops"]]
    gone = [d for d in a["drops"] if d not in b["drops"]]
    if new_drops:
        diffs.append(_diff("drop", "DLLs dropped on b and not on a", gone, new_drops))
    elif gone:
        diffs.append(_diff("drop", "DLLs dropped on a and not on b", gone, []))

    for key in sorted(set(a["chains"]) | set(b["chains"])):
        ca, cb = a["chains"].get(key), b["chains"].get(key)
        if ca == cb:
            continue
        d = _diff("chain", f"mixin chain {key}", _fmt(ca), _fmt(cb))
        if ca is None and cb is not None and _only_core(cb):
            explain(d, "a mixin chain only the 1.6 core installs")
        elif ca is not None and cb is not None and _without_core(ca) == _without_core(cb):
            explain(d, "the addons' order is the same; the 1.6 core's own handler differs (a hook point subscriber)")
        elif "chain" in allow:
            allowed.append(d)
        else:
            diffs.append(d)
    ka, kb = [c for c in a["conflicts"]], [c for c in b["conflicts"]]
    if ka != kb:
        diffs.append(_diff("conflict", "conflict pass output", sorted(set(ka) - set(kb)) or ka, sorted(set(kb) - set(ka)) or kb))
    for addon in sorted(set(a["unresolved"]) | set(b["unresolved"])):
        ua, ub = a["unresolved"].get(addon, []), b["unresolved"].get(addon, [])
        if ua != ub:
            diffs.append(_diff("unresolved", f"{addon}: bindings it needs that do not resolve", ua, ub, addon))
    for addon in sorted(set(a["registered"]) | set(b["registered"])):
        if addon in CORE_DLLS:
            continue
        if a["registered"].get(addon) != b["registered"].get(addon):
            diffs.append(_diff("register", f"{addon}: mixins registered", a["registered"].get(addon),
                               b["registered"].get(addon), addon))
    for dll in sorted(set(a["start"]) | set(b["start"])):
        if dll in CORE_DLLS:
            continue
        sa, sb = a["start"].get(dll, []), b["start"].get(dll, [])
        notes = [l for l in sb if l.startswith(REGISTRY_NOTE) and l not in sa]
        if notes:
            sb = [l for l in sb if l not in notes]
            allowed.append(dict(_diff("start", f"{dll}: start lines", [], notes, dll),
                                explained="registry notes the 1.6 core adds when a 1.5 call lands in a registry"))
        if sa != sb:
            diffs.append(_diff("start", f"{dll}: start lines", [l for l in sa if l not in sb] or sa,
                               [l for l in sb if l not in sa] or sb, dll))
    allowed_cats = set(allow)
    kept = []
    for d in diffs:
        (allowed if d["category"] in allowed_cats else kept).append(d)
    return {"same": not kept, "differences": kept, "allowed": allowed}


class LockWait(Exception):
    pass


class Lock:
    def __init__(self, path, queue, who, wait, hold, sleep=time.sleep, now=time.time):
        self.path, self.queue, self.who = Path(path), Path(queue), who
        self.wait, self.hold, self.sleep, self.now = wait, hold, sleep, now
        self.entry = None
        self.since = None

    def _queued(self):
        try:
            return [l.strip() for l in self.queue.read_text().splitlines() if l.strip()]
        except OSError:
            return []

    def _enqueue(self):
        self.entry = f"{self.who} {time.strftime('%H:%M')}"
        with open(self.queue, "a") as f:
            f.write(self.entry + "\n")

    def _dequeue(self):
        if self.entry is None:
            return
        keep = [l for l in self._queued() if l != self.entry]
        self.queue.write_text("".join(l + "\n" for l in keep))
        self.entry = None

    def take(self):
        try:
            self._take()
        except BaseException:
            self._dequeue()
            raise

    def _take(self):
        if not self.path.parent.is_dir():
            self.since = self.now()
            return
        started = self.now()
        while True:
            line_up = [l for l in self._queued() if l != self.entry]
            first = (self._queued() or [None])[0]
            my_turn = self.entry is not None and first == self.entry
            if (not line_up or my_turn) and self._mkdir():
                self._dequeue()
                (self.path / "owner").write_text(f"gate2 wine {time.strftime('%H:%M')}\n")
                self.since = self.now()
                return
            if self.entry is None and (line_up or self.path.exists()):
                self._enqueue()
            if self.now() - started >= self.wait:
                self._dequeue()
                raise LockWait(f"the game lock is held by {self.owner()}; queue: {self._queued() or 'empty'}")
            self.sleep(5)

    def _mkdir(self):
        try:
            self.path.mkdir()
            return True
        except FileExistsError:
            return False

    def owner(self):
        try:
            return (self.path / "owner").read_text().strip() or "unknown"
        except OSError:
            return "unknown"

    def expired(self):
        return self.since is not None and self.now() - self.since >= self.hold

    def release(self):
        if self.since is not None and self.path.parent.is_dir():
            shutil.rmtree(self.path, ignore_errors=True)
        self.since = None


CODE = (".dll", ".json", ".tsv", ".txt", ".toml", ".cfg", ".ini")


def wanted(kind, rel_or_src, size, cap):
    return size <= cap or (kind == "loader" and str(rel_or_src).lower().endswith(CODE))


def remap(path, mapping):
    for old, new in mapping:
        if path == old or path.startswith(old + "/"):
            return new + path[len(old):]
    return path


def side_mapping(out, side, index):
    work = f"{out}/work/s{index:04d}/data"
    kept = f"{out}/{side}/s{index:04d}/data"
    return [(work, kept)]


def settings_path(data, plan, buildhash):
    return Path(data) / "cache" / "overlay" / PROFILE / buildhash / "swofty" / "settings.json"


def load_plan(out, side, index, buildhash):
    data = Path(out) / side / f"s{index:04d}" / "data"
    path = data / "cache" / "overlay" / PROFILE / buildhash / "plan.json"
    return data, json.loads(path.read_text())


def prepare_game(plan, mapping, dest, exe, max_copy=DEFAULT_CAP, settings=None):
    dest = Path(dest)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    copied, skipped, missing = 0, [], []
    for kind in ("replace", "add", "loader"):
        for rel, src in sorted((plan.get(kind) or {}).items()):
            if not isinstance(src, str):
                continue
            src = remap(src, mapping)
            target = dest / rel
            try:
                size = os.stat(src).st_size
            except OSError:
                missing.append(rel)
                continue
            if not wanted(kind, rel, size, max_copy):
                skipped.append(rel)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, target)
            copied += 1
    shutil.copyfile(exe, dest / "F1_25.exe")
    if settings and Path(settings).is_file():
        (dest / "swofty").mkdir(exist_ok=True)
        shutil.copyfile(settings, dest / "swofty" / "settings.json")
    return {"copied": copied, "skipped": skipped, "missing": missing}


def wine_env(prefix):
    env = dict(os.environ)
    env.update(WINEPREFIX=str(prefix), WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d",
               WINEDEBUG="-all", SWOFTY_USAGE="0")
    return env


def run_wine(game, prefix, mode, timeout, pin=True):
    cmd = (PIN if pin else []) + ["xvfb-run", "-a", "wine", str(Path(game) / "F1_25.exe"), mode]
    try:
        p = subprocess.run(cmd, cwd=game, env=wine_env(prefix), capture_output=True, text=True, timeout=timeout)
        rc, stdout = p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        rc, stdout = "timeout", (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        subprocess.run(["wineserver", "-k"], env=wine_env(prefix), capture_output=True)
    log = Path(game) / "swofty" / "loader.log"
    text = log.read_text(errors="replace") if log.is_file() else ""
    return rc, text, stdout


SYNTH_KINDS = ("func", "site")
SITE_EXPECT = "B8 01 00 00 00"


def stub_tag(binding_id):
    return hashlib.sha1(binding_id.encode()).digest()[:8]


def stub_source(ids):
    out = ['__asm__(".text\\n");']
    table = []
    for i, bid in enumerate(sorted(ids)):
        tag = ",".join(f"0x{b:02x}" for b in stub_tag(bid))
        out.append(
            f'__asm__(".balign 16\\n.globl gate_stub_{i}\\ngate_stub_{i}:\\n.byte 0x48,0xB8,{tag}\\n'
            f'.byte 0xB8,1,0,0,0,0xC3\\n");'
        )
        table.append(f"gate_stub_{i}")
    out += [f"extern char {n}[];" for n in table]
    out.append("__attribute__((used)) char *const gate_stubs[] = {%s};" % ", ".join(table or ["0"]))
    return "\n".join(out) + "\n"


def synth_ids(bindings_files):
    ids = set()
    for path in bindings_files:
        try:
            doc = json.loads(Path(path).read_text())
        except (OSError, ValueError):
            continue
        ids |= {b["id"] for b in doc.get("bindings", []) if b.get("kind") in SYNTH_KINDS}
    return ids


def synth_bindings(game):
    path = Path(game) / "swofty" / "addons" / "core" / "bindings.json"
    try:
        doc = json.loads(path.read_text())
    except (OSError, ValueError):
        return 0
    n = 0
    for b in doc.get("bindings", []):
        if b.get("kind") not in SYNTH_KINDS:
            continue
        b["section"] = ".text"
        b["pattern"] = " ".join(["48 B8"] + [f"{x:02X}" for x in stub_tag(b["id"])])
        b["anchor"] = 0 if b["kind"] == "func" else 10
        b["resolve"] = {"kind": "addr"}
        b["expect"] = SITE_EXPECT if b["kind"] == "site" else None
        for k in ("rva", "match", "string_anchor", "unresolvable"):
            b.pop(k, None)
        n += 1
    path.write_text(json.dumps(doc, indent=1))
    return n


def build_fake_exe(repo, dest, ids=None):
    dest = Path(dest)
    if dest.is_file():
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    cache = dest.parent / "zig-cache"
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(cache), ZIG_LOCAL_CACHE_DIR=str(cache))
    src = Path(repo) / "native" / "test" / "fake_game.c"
    cmd = PIN + ["zig", "cc", "-target", "x86_64-windows-gnu", "-O2", "-o", str(dest), str(src)]
    if ids:
        stubs = dest.parent / "gate_stubs.c"
        stubs.write_text(stub_source(ids))
        cmd.append(str(stubs))
    cmd += ["-lversion", "-luser32"]
    p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(f"building the synthetic F1_25.exe failed:\n{p.stderr[-2000:]}")
    return dest


def harness_sets(rep):
    out = []
    for i, r in enumerate(rep["results"]):
        out.append((i, r))
    return out


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def slim_data(data, buildhash, mapping=(), cap=DEFAULT_CAP):
    data = Path(data)
    plan_path = data / "cache" / "overlay" / PROFILE / buildhash / "plan.json"
    plan = json.loads(plan_path.read_text())
    keep = {plan_path, settings_path(data, None, buildhash)}
    for kind in ("replace", "add", "loader"):
        for src in (plan.get(kind) or {}).values():
            if isinstance(src, str):
                src = remap(src, mapping)
                try:
                    if wanted(kind, src, os.stat(src).st_size, cap):
                        keep.add(Path(src))
                except OSError:
                    pass
    freed = 0
    for root, dirs, files in os.walk(data):
        for name in files:
            p = Path(root) / name
            if p in keep:
                continue
            try:
                freed += p.lstat().st_size if p.lstat().st_nlink == 1 else 0
                p.unlink()
            except OSError:
                pass
    for root, dirs, files in os.walk(data, topdown=False):
        if not os.listdir(root) and Path(root) != data:
            os.rmdir(root)
    return freed


def slim_run(out, cap=DEFAULT_CAP):
    out = Path(out)
    rep = json.loads((out / "report.json").read_text())
    freed = 0
    for index, r in enumerate(rep["results"]):
        for side in ("a", "b"):
            hash_ = (r.get("buildhash") or {}).get(side)
            data = out / side / f"s{index:04d}" / "data"
            if hash_ and data.is_dir():
                freed += slim_data(data, hash_, side_mapping(out, side, index), cap)
            elif data.is_dir():
                shutil.rmtree(data, ignore_errors=True)
    return freed
