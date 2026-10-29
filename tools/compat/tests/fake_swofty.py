import hashlib
import json
import os
import sys
import time
import zipfile
from pathlib import Path


def main(argv):
    if argv == ["--version"]:
        print(f"swofty {os.environ.get('FAKE_VERSION', '1.5.23')}")
        return 0
    data = Path(argv[argv.index("--data-dir") + 1])
    profile = argv[argv.index("--profile") + 1] if "--profile" in argv else "default"
    rest = [a for i, a in enumerate(argv) if a not in ("--data-dir", "--profile") and argv[i - 1] not in ("--data-dir", "--profile")]
    loader = Path(os.environ.get("SWOFTY_LOADER_DIR") or Path(__file__).parent / "loader")
    if rest[:2] == ["addons", "install"]:
        with zipfile.ZipFile(rest[2]) as z:
            manifest = json.loads(z.read("swofty.json"))
            z.extractall(data / "addons" / manifest["id"])
        print(f"installed {manifest['id']} {manifest['version']}")
        return 0
    if rest[:2] == ["addons", "enable"]:
        p = data / "profiles" / f"{profile}.json"
        p.parent.mkdir(parents=True, exist_ok=True)
        enabled = json.loads(p.read_text())["enabled"] if p.exists() else []
        p.write_text(json.dumps({"enabled": enabled + [rest[2]]}))
        print(f"enabled {rest[2]} in profile {profile}")
        return 0
    if rest[0] == "build":
        enabled = json.loads((data / "profiles" / f"{profile}.json").read_text())["enabled"]
        h = hashlib.sha256()
        h.update(str(data).encode())
        for addon in enabled:
            for f in sorted((data / "addons" / addon).rglob("*")):
                st = f.stat()
                h.update(f"{f.name}|{st.st_size}|{st.st_mtime_ns}".encode())
        generated = data / "cache" / "addons" / enabled[-1] / "made.json"
        if os.environ.get("FAKE_GENERATES"):
            generated.parent.mkdir(parents=True, exist_ok=True)
            generated.write_text("{}")
            h.update(str(time.time_ns()).encode())
        buildhash = h.hexdigest()[:16]
        out = data / "cache" / "overlay" / profile / buildhash
        (out / "swofty").mkdir(parents=True, exist_ok=True)
        requests = "Z:" + str(data / "state" / "requests" / f"{profile}.json").replace("/", "\\")
        (out / "swofty" / "settings.json").write_text(json.dumps({"requests": requests, "addons": enabled}))
        extra = os.environ.get("FAKE_EXTRA_EDIT")
        edits = [{"hash": "1"}] + ([{"hash": extra}] if extra else [])
        (out / "swofty" / "assets.json").write_text(json.dumps({"db_edits": edits}))
        loader_map = {
            "version.dll": str(loader / "version.dll"),
            "swofty/settings.json": str(out / "swofty" / "settings.json"),
            "swofty/assets.json": str(out / "swofty" / "assets.json"),
        }
        for addon in enabled:
            for dll in sorted((data / "addons" / addon / "native").glob("*.dll")):
                loader_map[f"swofty/addons/{addon}/native/{dll.name}"] = str(dll)
        plan = {"profile": profile, "buildhash": buildhash, "tracks": [], "replace": {}, "add": {}, "loader": loader_map}
        (out / "plan.json").write_text(json.dumps(plan))
        lines = [f"loader: 1 file(s) from {loader}", "database: unchanged"]
        warnings = [os.environ["FAKE_WARNING"]] if os.environ.get("FAKE_WARNING") else []
        game = Path(rest[rest.index("--game") + 1]) if "--game" in rest else None
        if game is not None and (game / "marker").exists():
            warnings.append("the game folder has a marker")
        report = {"profile": profile, "built_at": time.time(), "pairs": [], "order": ["swofty.core"] + enabled}
        built = {"version": 2, "buildhash": buildhash, "lines": lines, "warnings": warnings, "conflicts": [],
                 "waiting": [], "report": report, "registry_override": True}
        (out / "built.json").write_text(json.dumps(built))
        print(f"profile {profile}: {' -> '.join(['swofty.core'] + enabled)}")
        for l in lines:
            print(l)
        for w in warnings:
            print(f"warning: {w}")
        print(f"runtime: swofty/assets.json with 0 asset group(s), 0 resource(s), {len(edits)} database edit(s), 0 language table(s), 0 overlay file(s)")
        print(f"overlay {buildhash}: {out}")
        once = os.environ.get("FAKE_TOUCH_GAME_ONCE")
        if once and game is not None and not Path(once).exists():
            Path(once).write_text("")
            (game / "marker").write_text("")
        return 0
    print(f"fake swofty: unknown command {rest}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
