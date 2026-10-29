#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "tools")]
import erpw

fix = Path(sys.argv[1])
out = Path(sys.argv[2])
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def read(name):
    return erpw.Erp(str(out / name))


URI = "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.teams.driver_select_team_logos.texarray"
for name in ("flow_a.erp", "screens_b.erp"):
    e = read(name)
    img, view = e.find(URI + ".image"), e.find(URI)
    check([e.frag_data(img, 0), e.frag_data(img, 1), e.frag_data(view, 0)] ==
          [(fix / f"expect/{n}.bin").read_bytes() for n in ("head", "data", "view")], f"{name}: the array fragments equal the ones logos.py builds")
    filler = e.find("eaid://ui_package/art/filler.texture")
    check(e.frag_data(filler, 0) == bytes(range(256)) * 9, f"{name}: an unchanged resource is intact")
    import hashlib
    check(img.hash == hashlib.md5(e.frag_data(img, 0) + e.frag_data(img, 1)).digest(), f"{name}: the changed resource has its new hash")
e = read("databases.erp")
check(e.frag_data(e.find("eaid://ui_package/databases/overrides.uidb"), 0) == (fix / "expect/overrides.uidb").read_bytes(),
      "databases.erp: overrides.uidb equals the one tools/pkg.py builds with the two entries")
check(e.frag_data(e.find("eaid://ui_package/databases/other.uidb"), 0) == b"zz" * 40, "databases.erp: the other database is intact")
sys.exit(1 if fails else 0)
