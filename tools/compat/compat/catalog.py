import json
import urllib.request
from dataclasses import dataclass, field

from . import semver

DEFAULT_URL = "https://api.f1.swofty.net"
AGENT = "swofty-compat-gate (read-only)"


@dataclass(frozen=True)
class Entry:
    id: str
    name: str
    version: str
    launcher: str
    size: int
    sha256: str
    url: str
    native: bool = False
    yanked: bool = False
    requires: tuple = field(default_factory=tuple)

    @property
    def label(self):
        return f"{self.id}@{self.version}"

    def accepts(self, launcher_version):
        if not self.launcher:
            return True
        try:
            return semver.Requirement(self.launcher).matches(launcher_version)
        except semver.Invalid:
            return False


def fetch(base_url=DEFAULT_URL, timeout=60):
    url = base_url.rstrip("/") + "/v1/addons"
    req = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def entries(catalog):
    out = []
    for addon in catalog.get("addons", []):
        for v in addon.get("versions", []):
            requires = tuple(
                (r.get("id", ""), r.get("version") or "*")
                for r in (v.get("requires") or [])
                if isinstance(r, dict) and r.get("id")
            )
            out.append(
                Entry(
                    id=addon["id"],
                    name=addon.get("name", addon["id"]),
                    version=v["version"],
                    launcher=v.get("launcher") or "",
                    size=int(v.get("size") or 0),
                    sha256=(v.get("sha256") or "").lower(),
                    url=v.get("url") or "",
                    native=bool(v.get("native")),
                    yanked=bool(v.get("yanked")),
                    requires=requires,
                )
            )
    return out


def _version_key(e):
    try:
        return semver.key(e.version)
    except semver.Invalid:
        return (-1, -1, -1, (0,))


def newest_first(items):
    ordered = sorted(items, key=_version_key, reverse=True)
    ordered.sort(key=lambda e: e.id)
    return ordered


def parse_only(specs):
    out = []
    for s in specs or []:
        for part in s.split(","):
            part = part.strip()
            if not part:
                continue
            addon, _, version = part.partition("@")
            out.append((addon, version or None))
    return out


def wanted(entry, only):
    if not only:
        return True
    return any(entry.id == a and (v is None or entry.version == v) for a, v in only)


def select(all_entries, launcher_version=None, latest=False, only=None):
    picked = []
    seen = set()
    for e in newest_first(all_entries):
        if e.yanked or not wanted(e, only):
            continue
        if launcher_version is not None and not e.accepts(launcher_version):
            continue
        if latest and e.id in seen:
            continue
        seen.add(e.id)
        picked.append(e)
    return picked


def newest_matching(all_entries, addon_id, requirement, launcher_version=None):
    try:
        req = semver.Requirement(requirement)
    except semver.Invalid:
        req = semver.Requirement("*")
    best = None
    for e in all_entries:
        if e.id != addon_id or e.yanked:
            continue
        if launcher_version is not None and not e.accepts(launcher_version):
            continue
        try:
            if not req.matches(e.version):
                continue
        except semver.Invalid:
            continue
        if best is None or _version_key(e) > _version_key(best):
            best = e
    return best


def requirements(entry, all_entries, launcher_version=None):
    known = {e.id for e in all_entries}
    resolved, unresolved, outside = [], [], []
    stack = list(entry.requires)
    taken = {entry.id}
    while stack:
        addon_id, requirement = stack.pop(0)
        if addon_id in taken:
            continue
        if addon_id not in known:
            outside.append((addon_id, requirement))
            taken.add(addon_id)
            continue
        dep = newest_matching(all_entries, addon_id, requirement, launcher_version)
        taken.add(addon_id)
        if dep is None:
            unresolved.append((addon_id, requirement))
            continue
        resolved.append(dep)
        stack.extend(dep.requires)
    return resolved, unresolved, outside
