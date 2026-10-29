import fcntl
import hashlib
import json
import os
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from . import snapshot

PROFILE = "compat"
PIN = ["taskset", "-c", "2-7,10,11,14-31", "nice", "-n", "10"]
DEAD_URL = "http://127.0.0.1:9"
FIXED_MTIME_NS = 1_767_225_600 * 1_000_000_000
GENERATED_DIRS = ("cache/addons", "addon-data")


class ToolError(Exception):
    pass


@dataclass
class Side:
    name: str
    cli: Path
    loader: Path = None
    version: str = ""
    dxc: Path = None
    source: str = ""

    def env(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith("SWOFTY_")}
        env["SWOFTY_USAGE"] = "0"
        env["SWOFTY_REGISTRY_URL"] = DEAD_URL
        env["SWOFTY_LOGS_URL"] = DEAD_URL
        env["SWOFTY_USAGE_URL"] = DEAD_URL
        if self.loader:
            env["SWOFTY_LOADER_DIR"] = str(self.loader)
        if self.dxc:
            env["SWOFTY_DXC"] = str(self.dxc)
        return env

    def bundled_dxc(self):
        d = Path(self.cli).resolve().parent.parent / "dxc" / "bin" / "dxc"
        return d if d.is_file() else None

    def freeze(self, dest):
        dest = Path(dest)
        if dest.exists():
            shutil.rmtree(dest)
        (dest / "bin").mkdir(parents=True)
        cli = dest / "bin" / Path(self.cli).name
        shutil.copy2(self.cli, cli)
        loader = dest / "loader"
        shutil.copytree(self.loader_dir(), loader, symlinks=True)
        return Side(self.name, cli, loader, self.version, self.dxc,
                    f"{self.cli} (sha256 {sha256_path(self.cli)[:16]}), loader {self.loader_dir()}")

    def loader_dir(self):
        if self.loader:
            return Path(self.loader)
        bin_dir = Path(self.cli).resolve().parent
        for d in (bin_dir.parent / "loader", bin_dir / "loader"):
            if (d / "version.dll").is_file():
                return d
        return None


def sha256_path(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def cli_version(cli):
    try:
        out = subprocess.run([str(cli), "--version"], capture_output=True, text=True, timeout=60)
    except OSError as e:
        raise ToolError(f"{cli}: {e}") from e
    if out.returncode != 0:
        raise ToolError(f"{cli} --version exited {out.returncode}: {out.stderr.strip()}")
    words = out.stdout.split()
    if not words:
        raise ToolError(f"{cli} --version printed nothing")
    return words[-1]


def link_tree(src, dst):
    src, dst = Path(src), Path(dst)
    for root, dirs, files in os.walk(src):
        rel = Path(root).relative_to(src)
        (dst / rel).mkdir(parents=True, exist_ok=True)
        for f in files:
            s, d = Path(root) / f, dst / rel / f
            if d.exists():
                continue
            link_or_copy(s, d)


def seed_baseline(source_data, seed):
    seed = Path(seed)
    seed.parent.mkdir(parents=True, exist_ok=True)
    with open(seed.parent / f".{seed.name}.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return _seed_baseline(source_data, seed)


def link_or_copy(s, d):
    try:
        os.link(s, d)
    except OSError:
        tmp = d.with_name(f".{d.name}.{os.getpid()}.part")
        shutil.copy2(s, tmp)
        os.replace(tmp, d)


def _seed_baseline(source_data, seed):
    src = Path(source_data) / "baseline"
    if not src.is_dir():
        return None
    manifests = sorted(p for p in src.glob("*/*.json") if p.parent.name != "objects")
    if (seed / ".done").is_file() and all(
        (seed / p.relative_to(src)).is_file() and (seed / p.relative_to(src)).read_bytes() == p.read_bytes()
        for p in manifests
    ):
        link_tree(src / "objects", seed / "objects")
        return seed
    (seed / ".done").unlink(missing_ok=True)
    for entry in src.iterdir():
        if entry.name == "objects":
            continue
        if entry.is_dir():
            (seed / entry.name).mkdir(parents=True, exist_ok=True)
            for f in entry.iterdir():
                if f.is_file():
                    shutil.copy2(f, seed / entry.name / f.name)
    if (src / "objects").is_dir():
        link_tree(src / "objects", seed / "objects")
    (seed / ".done").write_text("")
    return seed


def pin_mtimes(root, mtime_ns=FIXED_MTIME_NS):
    root = Path(root)
    if not root.is_dir():
        return 0
    n = 0
    for dirpath, dirs, files in os.walk(root, topdown=False):
        for name in files + dirs:
            p = os.path.join(dirpath, name)
            if not os.path.islink(p):
                os.utime(p, ns=(mtime_ns, mtime_ns))
                n += 1
    os.utime(root, ns=(mtime_ns, mtime_ns))
    return n


def game_state(game):
    state = {}
    for root, _, files in os.walk(game):
        for f in files:
            p = os.path.join(root, f)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            state[os.path.relpath(p, game)] = (st.st_size, st.st_mtime_ns)
    return state


def state_changes(before, after, limit=10):
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    return changed[:limit], len(changed)


def _run(cmd, env, timeout, pin):
    full = (PIN + cmd) if pin and shutil.which("taskset") else cmd
    try:
        r = subprocess.run(full, env=env, capture_output=True, text=True, timeout=timeout)
        return {"rc": r.returncode, "stdout": r.stdout, "stderr": r.stderr}
    except subprocess.TimeoutExpired as e:
        raise ToolError(f"{' '.join(cmd[:3])}: timed out after {timeout} s") from e
    except OSError as e:
        raise ToolError(f"{cmd[0]}: {e}") from e


def build_set(side, addons, data_dir, game, seed=None, cache=None, timeout=3600, pin=True, meta=None):
    data_dir = Path(data_dir)
    if data_dir.exists():
        shutil.rmtree(data_dir)
    data_dir.mkdir(parents=True)
    if seed:
        link_tree(seed, data_dir / "baseline")
    env = side.env()
    base = [str(side.cli), "--data-dir", str(data_dir)]
    steps = []
    for path in addons["zips"]:
        r = _run(base + ["addons", "install", str(path)], env, timeout, pin)
        steps.append({"step": f"install {Path(path).parent.parent.name}@{Path(path).parent.name}", **r})
    pin_mtimes(data_dir / "addons")
    for addon_id in addons["enable"]:
        r = _run(base + ["--profile", PROFILE, "addons", "enable", addon_id], env, timeout, pin)
        steps.append({"step": f"enable {addon_id}", **r})
    started = time.time_ns()
    build = _run(base + ["--profile", PROFILE, "build", "--game", str(game)], env, timeout, pin)
    roots = {
        "data": data_dir.resolve(),
        "game": Path(game).resolve(),
        "loader": side.loader_dir().resolve() if side.loader_dir() else None,
        "cache": Path(cache).resolve() if cache else None,
        "home": Path.home(),
    }
    norm = snapshot.Normaliser(roots)
    snap = snapshot.extract(data_dir, PROFILE, steps, build, norm, meta=meta)
    snap["generated"] = generated_inputs(data_dir, started, norm)
    return snap


def canonical_json_hash(path, norm):
    if Path(path).suffix.lower() != ".json":
        return None
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    text = json.dumps(norm.json(value), sort_keys=True, separators=(",", ":"))
    return "json:" + hashlib.sha256(text.encode()).hexdigest()


def generated_inputs(data_dir, since_ns, norm):
    out = {}
    for sub in GENERATED_DIRS:
        root = Path(data_dir) / sub
        if not root.is_dir():
            continue
        for dirpath, _, files in os.walk(root):
            for f in files:
                p = Path(dirpath) / f
                try:
                    if p.stat().st_mtime_ns < since_ns:
                        continue
                except OSError:
                    continue
                rel = p.relative_to(data_dir).as_posix()
                out[rel] = canonical_json_hash(p, norm) or snapshot.hash_file(p, norm).get("sha256", "missing")
    return dict(sorted(out.items()))
