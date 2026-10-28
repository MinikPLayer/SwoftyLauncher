"""The season cars' power unit.

The season cars race under the 2025 rules: DRS in the DRS zones and the 2025 ERS, whose deploy modes include
overtake, over a 4 MJ store with a 126 kW MGU-K. The stock 2025 teams share one power unit: Engine, Turbo, FuelTank,
FuelECU, EnergyRecoverySystem (no OverrideModeAvailable and no per-track TrackSettings, so no 2026 overtake mode and
none of its HUD) and the R&D upgrades of those parts. `apply` replaces PARTS and UPGRADES in every handling file of a
season archive with SOURCE's, so a car built with any 2026 parts or TrackSettings goes back to the 2025 ones.
`stock` reads them from the game folder, which has to hold the stock 2025 car archives. build_cars.py applies it on
every build; run on its own, the tool applies it to built cars.

    python3 tools/powerunit.py [--years 2016-2024] [--game <F1 25 folder>]
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import erpw
import vtf

SOURCE = Path("2025_asset_groups/f1_2025_vehicle_package/teams/ferrari/wep/ferrari.erp")
PARTS = (b"Engine", b"Turbo", b"FuelTank", b"FuelECU", b"EnergyRecoverySystem")
UPGRADES = (b"radUpgradeEngine", b"radUpgradeFuelConsumption", b"radUpgradeERS", b"radUpgradeWearMGUH")


def _tops(root):
    return [root] if root.name == b"VTF" else root.find(b"VTF")


def _key(node):
    if node.kind != 0:
        return None
    if node.name in PARTS:
        return node.name
    if node.name == b"Upgrade" and node.attr(b"name") in UPGRADES:
        return node.attr(b"name")
    return None


def stock(game):
    e = erpw.Erp(str(Path(game) / SOURCE))
    r = next(r for r in e.res if r.name.endswith("/handling/ferrari.vtf"))
    top = _tops(vtf.parse(e.frag_data(r, 0)))[0]
    parts = {_key(c): c for c in top.children if _key(c)}
    missing = [k.decode() for k in PARTS + UPGRADES if k not in parts]
    if missing:
        raise SystemExit(f"{Path(game) / SOURCE}: the 2025 handling file has no {', '.join(missing)}")
    return parts


def apply(e, parts):
    done = 0
    for r in e.res:
        if not r.name.endswith(".vtf"):
            continue
        old = e.frag_data(r, 0)
        root = vtf.parse(old)
        for top in _tops(root):
            top.children = [parts[_key(c)].clone() if _key(c) else c for c in top.children]
        new = vtf.write(root)
        if new == old:
            continue
        e.set_frag(r, 0, new)
        if len(r.frags) > 1:
            meta = e.frag_data(r, 1)
            version = struct.unpack_from("<I", meta, 4)[0] if len(meta) >= 8 else 1
            e.set_frag(r, 1, vtf.tsmd(root, version))
        done += 1
    return done


def main():
    import pace
    from layout import cars_dir

    ap = argparse.ArgumentParser()
    ap.add_argument("--years", default=f"{min(pace.SEASONS)}-{max(pace.SEASONS)}")
    ap.add_argument("--game", default="/mnt/gaming/SteamLibrary/steamapps/common/F1 25")
    args = ap.parse_args()
    parts = stock(args.game)
    for year in pace.parse_years(args.years):
        main_teams, extra = pace.teams(year)
        paths = [cars_dir(year) / "teams" / f / "wep" / f"{f}.erp" for f in main_teams]
        if extra:
            paths.append(cars_dir(year) / "extra" / "teams" / extra["folder"] / "wep" / f"{extra['folder']}.erp")
        for path in paths:
            e = erpw.Erp(str(path))
            done = apply(e, parts)
            if done:
                e.to_file(str(path))
            print(f"{year} {path.parent.parent.name}: {done} handling file(s) given the 2025 power unit", flush=True)


if __name__ == "__main__":
    main()
