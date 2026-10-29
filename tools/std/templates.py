#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import validate as V

TEMPLATES = "templates/std"
REGISTRIES = "packages/sdk/std/registries"
NOT_RECORDS = {"tags.json"}


def addons(root):
    if not root.is_dir():
        return []
    return sorted(p for p in root.iterdir() if p.is_dir() and not p.name.startswith("."))


def data_files(addon):
    folder = addon / "registry"
    if not folder.is_dir():
        return []
    return sorted(f for f in folder.glob("*.json") if f.name not in NOT_RECORDS)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Build the templates/std addons and validate their registry data files.")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--templates", default=TEMPLATES)
    ap.add_argument("--registries", default=REGISTRIES)
    ap.add_argument("--no-build", action="store_true", help="only validate the data files")
    args = ap.parse_args(argv)
    repo = Path(args.repo)

    found = addons(repo / args.templates)
    if not found:
        print(f"templates: no addons under {args.templates} yet; nothing to build")
        return 0

    failed = []
    for addon in found:
        rel = addon.relative_to(repo)
        if not (addon / "swofty.json").is_file():
            print(f"FAIL  {rel}: no swofty.json")
            failed.append(rel)
            continue
        build = addon / "native" / "build.sh"
        if not args.no_build and build.is_file():
            print(f"build {rel}/native/build.sh", flush=True)
            if subprocess.run(["bash", str(build)]).returncode != 0:
                print(f"FAIL  {rel}: native/build.sh failed")
                failed.append(rel)
        files = [str(f) for f in data_files(addon)]
        try:
            addon_id = V.S.load(addon / "swofty.json").get("id")
        except (V.S.JsonError, AttributeError):
            addon_id = None
        extra = ["--addon", addon_id] if isinstance(addon_id, str) else []
        if files and V.main(["--repo", str(repo), "--registries", args.registries, "--data", *extra, *files]) != 0:
            failed.append(rel)
    print(f"templates: {len(found)} addons, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
