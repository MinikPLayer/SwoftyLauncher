import re

_VERSION = re.compile(
    r"^\s*v?(\d+)(?:\.(\d+|[xX*]))?(?:\.(\d+|[xX*]))?(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?\s*$"
)
_COMPARATOR = re.compile(r"^\s*(>=|<=|>|<|=|\^|~)?\s*(.+?)\s*$")


class Invalid(ValueError):
    pass


def _pre_key(pre):
    if not pre:
        return (1,)
    parts = []
    for p in pre.split("."):
        parts.append((0, int(p), "") if p.isdigit() else (1, 0, p))
    return (0, tuple(parts))


def parse(text):
    m = _VERSION.match(text or "")
    if not m or m.group(2) is None or m.group(3) is None:
        raise Invalid(f"not a version: {text!r}")
    major, minor, patch, pre = m.groups()
    if not (minor.isdigit() and patch.isdigit()):
        raise Invalid(f"not a version: {text!r}")
    return (int(major), int(minor), int(patch), pre or "")


def key(version):
    if isinstance(version, str):
        version = parse(version)
    major, minor, patch, pre = version
    return (major, minor, patch, _pre_key(pre))


def _partial(text):
    m = _VERSION.match(text)
    if not m:
        raise Invalid(f"not a version requirement: {text!r}")
    major, minor, patch, pre = m.groups()
    nums = [int(major)]
    for part in (minor, patch):
        if part is None or not part.isdigit():
            break
        nums.append(int(part))
    return nums, pre or ""


def _bounds(op, nums, pre):
    major = nums[0]
    minor = nums[1] if len(nums) > 1 else None
    patch = nums[2] if len(nums) > 2 else None
    full = (major, minor or 0, patch or 0, pre)
    if op in ("", "^"):
        if patch is not None:
            if major > 0:
                return full, (major + 1, 0, 0, "")
            if minor > 0:
                return full, (0, minor + 1, 0, "")
            return full, (0, 0, patch + 1, "")
        if minor is not None:
            if major > 0 or minor > 0:
                return full, ((major + 1, 0, 0, "") if major > 0 else (0, minor + 1, 0, ""))
            return full, (0, 1, 0, "")
        return full, (major + 1, 0, 0, "")
    if op == "~":
        if minor is None:
            return full, (major + 1, 0, 0, "")
        return full, (major, minor + 1, 0, "")
    if op == "=":
        if patch is not None:
            return full, None
        if minor is not None:
            return full, (major, minor + 1, 0, "")
        return full, (major + 1, 0, 0, "")
    if op == ">=":
        return full, ...
    if op == ">":
        if patch is not None:
            return ("gt", full), ...
        if minor is not None:
            return (major, minor + 1, 0, ""), ...
        return (major + 1, 0, 0, ""), ...
    if op == "<":
        return None, ("lt", full)
    if op == "<=":
        if patch is not None:
            return None, ("le", full)
        if minor is not None:
            return None, (major, minor + 1, 0, "")
        return None, (major + 1, 0, 0, "")
    raise Invalid(f"unknown operator {op!r}")


class Comparator:
    def __init__(self, text):
        m = _COMPARATOR.match(text)
        if not m:
            raise Invalid(f"not a comparator: {text!r}")
        self.op = m.group(1) or ""
        if not self.op and any(c in m.group(2) for c in "xX*"):
            self.op = "="
        nums, self.pre = _partial(m.group(2))
        self.nums = nums
        lo, hi = _bounds(self.op, nums, self.pre)
        self.exact = hi is None
        self.lo, self.hi = lo, hi

    def same_release(self, version):
        return len(self.nums) == 3 and tuple(self.nums) == version[:3] and bool(self.pre)

    def matches(self, version):
        k = key(version)
        lo = self.lo
        if lo is not None:
            if isinstance(lo, tuple) and lo and lo[0] == "gt":
                if not k > key(lo[1]):
                    return False
            elif k < key(lo):
                return False
        if self.exact:
            return k == key(lo)
        hi = self.hi
        if hi is ...:
            return True
        if isinstance(hi, tuple) and hi and hi[0] in ("lt", "le"):
            return k < key(hi[1]) if hi[0] == "lt" else k <= key(hi[1])
        return k < key(hi)


class Requirement:
    def __init__(self, text):
        self.text = (text or "").strip()
        parts = [p for p in self.text.split(",") if p.strip()]
        self.any = not parts or all(p.strip() == "*" for p in parts)
        self.comparators = [] if self.any else [Comparator(p) for p in parts if p.strip() != "*"]

    def matches(self, version):
        if isinstance(version, str):
            version = parse(version)
        if self.any:
            return not version[3]
        if version[3] and not any(c.same_release(version) for c in self.comparators):
            return False
        return all(c.matches(version) for c in self.comparators)

    def __str__(self):
        return self.text or "*"
