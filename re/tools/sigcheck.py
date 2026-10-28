#!/usr/bin/env python3
"""Verify every signature in re/db/signatures.json against an F1_25.exe copy.

  sigcheck.py                      check all signatures against the KB build (work/exe/F1_25.exe)
  sigcheck.py --exe NEW.exe        port check: same rules on another build, reports new match/target
  sigcheck.py --only ID[,ID]       check a subset
  sigcheck.py --sigs FILE          check candidate signatures in FILE (same schema)
  sigcheck.py --make VA [--kind mid|func_start|call|data] [--id ID]
                                   print a candidate signature object for VA (unique in .xtls)
Exit status 0 only when every checked signature passes.
"""
import argparse, hashlib, json, os, re, sys, bisect

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DB = os.path.join(ROOT, 're', 'db')
DEFAULT_EXE = os.path.join(ROOT, 'work', 'exe', 'F1_25.exe')
BASE = 0x140000000
MIN_LEN = 12


def sha256_file(path):
    st = os.stat(path)
    cache = os.path.join(DB, '.exe_hash_cache.json')
    key = f'{os.path.realpath(path)}:{st.st_size}:{int(st.st_mtime)}'
    try:
        c = json.load(open(cache))
        if key in c:
            return c[key]
    except Exception:
        c = {}
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 24), b''):
            h.update(b)
    c[key] = h.hexdigest()
    try:
        tmp = cache + f'.{os.getpid()}'
        json.dump(c, open(tmp, 'w'))
        os.replace(tmp, cache)
    except Exception:
        pass
    return c[key]


class Image:
    def __init__(self, path):
        import pefile, numpy as np
        self.path = path
        pe = pefile.PE(path, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.secs = [(s.Name.rstrip(b'\0').decode(), s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
        self.data = np.memmap(path, dtype=np.uint8, mode='r')
        self.ts = pe.FILE_HEADER.TimeDateStamp
        self._sec_bytes = {}
        self._pdata = None

    def sec(self, name):
        for s in self.secs:
            if s[0] == name:
                return s
        return None

    def sec_of(self, va):
        r = va - self.base
        for s in self.secs:
            if s[1] <= r < s[1] + s[2]:
                return s[0]
        return None

    def section_bytes(self, name):
        if name not in self._sec_bytes:
            n, va, vs, ro, rs = self.sec(name)
            self._sec_bytes[name] = (va, bytes(self.data[ro:ro + rs]))
        return self._sec_bytes[name]

    def read(self, va, n):
        r = va - self.base
        for name, sva, vs, ro, rs in self.secs:
            if sva <= r < sva + rs:
                o = r - sva + ro
                return bytes(self.data[o:o + n])
        return None

    def pdata(self):
        if self._pdata is None:
            import numpy as np
            n, va, vs, ro, rs = self.sec('.srdata') or self.sec('.pdata')
            raw = bytes(self.data[ro:ro + rs])
            raw = raw[:len(raw) - len(raw) % 12]
            arr = np.frombuffer(raw, dtype=np.uint32).reshape(-1, 3)
            arr = arr[(arr[:, 0] != 0) & (arr[:, 1] > arr[:, 0])]
            arr = arr[np.argsort(arr[:, 0])]
            self._pdata = (arr[:, 0].tolist(), arr[:, 1].tolist())
        return self._pdata

    def func_of(self, va):
        starts, ends = self.pdata()
        r = va - self.base
        i = bisect.bisect_right(starts, r) - 1
        if i >= 0 and starts[i] <= r < ends[i]:
            return starts[i] + self.base, ends[i] + self.base
        return None


def parse_pattern(p):
    out = []
    for t in p.split():
        if t in ('?', '??'):
            out.append(None)
        else:
            if not re.fullmatch(r'[0-9A-Fa-f]{2}', t):
                raise ValueError(f'bad pattern token {t!r}')
            out.append(int(t, 16))
    return out


def to_regex(pat):
    return re.compile(b''.join(re.escape(bytes([x])) if x is not None else b'.' for x in pat), re.S)


def find_all(img, section, pat, limit=50):
    va, b = img.section_bytes(section)
    hits = []
    for m in to_regex(pat).finditer(b):
        hits.append(img.base + va + m.start())
        if len(hits) >= limit:
            break
    return hits


def disasm_one(img, va):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    b = img.read(va, 16)
    for i in md.disasm(b, va):
        return i
    return None


def insn_boundary(img, va, func_start=None):
    f = img.func_of(va)
    if not f and func_start is not None and func_start <= va < func_start + 0x10000:
        f = (func_start, va + 16)
    if not f:
        return False, 'not inside a .pdata function'
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    start, end = f
    pos = start
    b = img.read(start, end - start)
    while pos < va:
        insns = list(md.disasm(b[pos - start:pos - start + 16], pos, count=1))
        if not insns:
            return False, f'undecodable byte at 0x{pos:x}'
        pos += insns[0].size
    return pos == va, ('ok' if pos == va else f'0x{va:x} splits the instruction ending at 0x{pos:x}')


def resolve(img, match, sig):
    anchor = int(str(sig.get('anchor', 0)), 0)
    r = sig.get('resolve', {'kind': 'addr'})
    at = match + anchor
    kind = r.get('kind', 'addr')
    if kind == 'addr':
        return at, None
    insn_off = int(str(r.get('insn', anchor - 1 if kind == 'rel32' else anchor)), 0)
    i = disasm_one(img, match + insn_off)
    if i is None:
        return None, f'cannot decode instruction at match+0x{insn_off:x}'
    if kind == 'rel32':
        if i.imm_size != 4 or match + insn_off + i.imm_offset != at:
            return None, f'match+0x{anchor:x} is not the rel32 operand of "{i.mnemonic} {i.op_str}"'
        rel = int.from_bytes(img.read(at, 4), 'little', signed=True)
        return match + insn_off + i.size + rel, None
    if kind == 'riprel':
        if i.disp_size != 4 or match + insn_off + i.disp_offset != at:
            return None, f'match+0x{anchor:x} is not the disp32 of "{i.mnemonic} {i.op_str}"'
        if 'len' in r and int(str(r['len']), 0) != i.size:
            return None, f'instruction length {i.size} != {r["len"]}'
        disp = int.from_bytes(img.read(at, 4), 'little', signed=True)
        return match + insn_off + i.size + disp, None
    return None, f'unknown resolve kind {kind}'


def load_kb_names():
    names = {}
    for t in ('functions.tsv', 'globals.tsv'):
        p = os.path.join(DB, t)
        if not os.path.exists(p):
            continue
        for ln, line in enumerate(open(p), 1):
            if ln == 1:
                continue
            c = line.rstrip('\n').split('\t')
            if len(c) > 2:
                names[c[1]] = (int(c[0], 16), c[2] if t == 'functions.tsv' else c[5])
    return names


def check(img, sig, kb_build, names, same_build):
    errs, info = [], {}
    sid = sig.get('id', '?')
    try:
        pat = parse_pattern(sig['pattern'])
    except Exception as e:
        return [f'pattern: {e}'], info
    if len(pat) < MIN_LEN:
        errs.append(f'pattern shorter than {MIN_LEN} bytes')
    if pat and (pat[0] is None or pat[-1] is None):
        errs.append('pattern starts or ends with a wildcard')
    section = sig.get('section', '.xtls')
    if not img.sec(section):
        return errs + [f'section {section} not in exe'], info
    hits = find_all(img, section, pat)
    info['hits'] = len(hits)
    if len(hits) != 1:
        errs.append(f'{"at least " if len(hits) >= 50 else ""}{len(hits)} matches in {section} (need exactly 1)' + (f', first {", ".join(hex(h) for h in hits[:4])}' if hits else ''))
        return errs, info
    match = hits[0]
    info['match'] = match
    if same_build and int(str(sig.get('match', '0')), 0) != match:
        errs.append(f'match 0x{match:x} != recorded {sig.get("match")}')
    target, e = resolve(img, match, sig)
    if e:
        errs.append(e)
        return errs, info
    info['target'] = target
    if same_build and int(str(sig.get('target', '0')), 0) != target:
        errs.append(f'target 0x{target:x} != recorded {sig.get("target")}')
    anchor = int(str(sig.get('anchor', 0)), 0)
    if sig.get('expect'):
        want = bytes(parse_pattern(sig['expect']))
        got = img.read(match + anchor, len(want))
        if got != want:
            errs.append(f'expect {sig["expect"]} at match+0x{anchor:x}, found {got.hex(" ").upper()}')
    kind = sig.get('kind')
    tsec = img.sec_of(target)
    if kind in ('mid', 'func_start', 'call'):
        data1 = tsec == '.data1' and section == '.data1' and bool(sig.get('data1_reason'))
        if tsec != '.xtls' and kind == 'mid' and not data1:
            errs.append(f'mid target in {tsec}, hooks are only allowed in .xtls (a .data1 site needs section .data1 and a data1_reason)')
        if kind == 'mid':
            fs = int(str(sig['func_start']), 0) if sig.get('func_start') else None
            ok, why = insn_boundary(img, target, fs)
            if not ok:
                errs.append(f'mid target not an instruction boundary: {why}')
        if kind in ('func_start', 'call') and tsec == '.xtls':
            f = img.func_of(target)
            if not f or f[0] != target:
                head = img.read(target, 5)
                dest = target + 5 + int.from_bytes(head[1:5], 'little', signed=True) if head[0] == 0xE9 else None
                body = img.func_of(dest) if dest and img.sec_of(dest) == '.data1' else None
                # leaf .data1 bodies have no .pdata; the signature then names the body in func_start (§7)
                leaf = bool(dest) and img.sec_of(dest) == '.data1' and bool(sig.get('func_start')) \
                    and int(str(sig['func_start']), 0) == dest
                if (not body or body[0] != dest) and not leaf:
                    errs.append(f'target 0x{target:x} is not a .pdata function start or a jmp thunk to a .data1 body')
    elif kind != 'data':
        errs.append(f'unknown kind {kind!r}')
    if not sig.get('verified'):
        errs.append('no verified[] entry')
    elif not any(v.get('build', '') and kb_build.startswith(v['build']) for v in sig['verified']):
        errs.append(f'verified[] has no entry for the KB build {kb_build[:16]}')
    if same_build:
        for n in sig.get('names', []):
            if n not in names:
                errs.append(f'name {n} not in functions.tsv/globals.tsv')
            else:
                a, s = names[n]
                if a != target:
                    errs.append(f'name {n} is at 0x{a:x} in the KB, signature resolves to 0x{target:x}')
                if s != sid:
                    errs.append(f'name {n} has sig column {s!r}, expected {sid}')
    return errs, info


def pattern_for(img, start, length):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    from capstone.x86 import X86_OP_MEM, X86_REG_RIP, X86_GRP_JUMP, X86_GRP_CALL
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    b = img.read(start, length + 16)
    pat = []
    for i in md.disasm(b, start):
        bs = list(i.bytes)
        mask = [True] * len(bs)
        if i.disp_size == 4 and any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in i.operands):
            for k in range(i.disp_offset, i.disp_offset + 4):
                mask[k] = False
        if (i.group(X86_GRP_JUMP) or i.group(X86_GRP_CALL)) and i.imm_size >= 4:
            for k in range(i.imm_offset, i.imm_offset + i.imm_size):
                mask[k] = False
        pat += [(x if m else None) for x, m in zip(bs, mask)]
        if len(pat) >= length:
            break
    while pat and pat[-1] is None:
        pat.pop()
    return pat


def make(img, va, kind, func_start=None):
    f = img.func_of(va)
    if not f and func_start is not None:
        f = (func_start, va + 16)
    section = img.sec_of(va)
    starts = [va]
    if kind == 'mid' and f:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        pos, bounds = f[0], []
        b = img.read(f[0], va - f[0] + 16)
        while pos < va:
            i = next(md.disasm(b[pos - f[0]:pos - f[0] + 16], pos, count=1))
            bounds.append(pos)
            pos += i.size
        starts = [s for s in reversed(bounds) if va - s <= 48][:8] + [va]
    best = None
    for s in starts:
        for n in range(MIN_LEN, 97, 4):
            pat = pattern_for(img, s, max(n, va - s + 6))
            if len(pat) < MIN_LEN:
                continue
            hits = find_all(img, section, pat, limit=2)
            if len(hits) == 1:
                if best is None or len(pat) < len(best[1]):
                    best = (s, pat)
                break
    if not best:
        return None
    s, pat = best
    return {'id': '<subsystem>.<name>', 'section': section, 'pattern': ' '.join('??' if x is None else f'{x:02X}' for x in pat),
            'match': hex(s), 'anchor': hex(va - s), 'resolve': {'kind': 'addr'}, 'target': hex(va), 'kind': kind,
            'expect': ' '.join(f'{x:02X}' for x in img.read(va, 4)) if kind == 'mid' else None,
            'names': [], 'verified': [], 'used_by': []}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=None)
    ap.add_argument('--only', default=None)
    ap.add_argument('--make', default=None)
    ap.add_argument('--kind', default='func_start')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--sigs', default=None, help='check this signatures file instead of re/db/signatures.json')
    a = ap.parse_args()
    doc = json.load(open(a.sigs or os.path.join(DB, 'signatures.json')))
    kb_build = doc['build']['sha256']
    exe = a.exe or DEFAULT_EXE
    img = Image(exe)
    if a.make:
        r = make(img, int(a.make, 16), a.kind)
        if not r:
            print('no unique pattern found within 96 bytes', file=sys.stderr)
            return 1
        r = {k: v for k, v in r.items() if v is not None}
        print(json.dumps(r, indent=1))
        return 0
    h = sha256_file(exe)
    same = h == kb_build
    size_ok = os.path.getsize(exe) == doc['build'].get('size')
    print(f'exe {exe}\n  sha256 {h}\n  KB build {kb_build} -> {"MATCH" if same else "DIFFERENT BUILD (port check: match/target/name comparisons skipped)"}')
    if same and not size_ok:
        print('  size mismatch against signatures.json build.size')
        return 2
    names = load_kb_names()
    only = set(a.only.split(',')) if a.only else None
    sigs = [s for s in doc['signatures'] if not only or s['id'] in only]
    ids = [s['id'] for s in doc['signatures']]
    dup = {i for i in ids if ids.count(i) > 1}
    fails = 0
    if dup:
        print(f'FAIL duplicate signature ids: {sorted(dup)}')
        fails += 1
    out = []
    for s in sigs:
        errs, info = check(img, s, kb_build, names, same)
        st = 'OK  ' if not errs else 'FAIL'
        fails += bool(errs)
        tgt = f"0x{info['target']:x}" if 'target' in info else '-'
        print(f'{st} {s["id"]:34s} hits={info.get("hits", "?")} match={hex(info["match"]) if "match" in info else "-"} target={tgt}')
        for e in errs:
            print(f'       - {e}')
        out.append({'id': s['id'], 'ok': not errs, 'errors': errs, **{k: hex(v) for k, v in info.items() if k in ('match', 'target')}})
    print(f'{len(sigs) - fails if fails <= len(sigs) else 0}/{len(sigs)} signatures pass' + ('' if not fails else f', {fails} failure(s)'))
    if a.json:
        json.dump(out, sys.stdout, indent=1)
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
