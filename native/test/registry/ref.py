#!/usr/bin/env python3
"""A reference resolver for the registry corpus, written from packages/sdk/std/tests/README.md.

python3 ref.py <cases dir>          run every case and compare with its expectations
python3 ref.py --dump <case.json>   print every evaluation's records, conflicts and refusals as JSON
"""
import copy
import json
import math
import os
import re
import sys

MAX_DEPTH = 8
KEY_RE = re.compile(r'^[A-Za-z0-9_.\-/]+$')


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def same(a, b):
    if is_num(a) and is_num(b):
        return a == b
    if a is None or b is None:
        return a is None and b is None
    if type(a) is not type(b) and not (isinstance(a, bool) and isinstance(b, bool)):
        return False
    if isinstance(a, dict):
        for k in set(a) | set(b):
            if not same(a.get(k), b.get(k)):
                return False
        return True
    if isinstance(a, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b


def seg_escape(s):
    return s.replace('~', '~0').replace('/', '~1')


def key_text(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if v is None:
        return 'null'
    if is_num(v):
        return json.dumps(v)
    return v


def key_seg(e, fields):
    return seg_escape(','.join(f + '=' + key_text(e[f]) for f in fields))


def holds(e, fields):
    return isinstance(e, dict) and all(f in e and not isinstance(e[f], (dict, list)) for f in fields)


def key_eq(a, b, fields):
    return all(same(a.get(f), b.get(f)) for f in fields)


def segs(pointer):
    if not pointer:
        return []
    return [s.replace('~1', '/').replace('~0', '~') for s in pointer[1:].split('/')]


def key_value(text):
    if text == 'true':
        return True
    if text == 'false':
        return False
    if text == 'null':
        return None
    try:
        return int(text)
    except ValueError:
        pass
    try:
        return float(text)
    except ValueError:
        return text


def child(v, s):
    if isinstance(v, dict):
        return (v, s) if s in v else None
    if isinstance(v, list):
        if s.isdigit():
            i = int(s)
            return (v, i) if i < len(v) else None
        if '=' in s:
            parts = [p.split('=', 1) for p in s.split(',')]
            for i, e in enumerate(v):
                if isinstance(e, dict) and all(f in e and same(e[f], key_value(t)) for f, t in parts):
                    return (v, i)
    return None


def get(v, pointer):
    cur = v
    for s in segs(pointer):
        c = child(cur, s)
        if c is None:
            return None, False
        cur = c[0][c[1]]
    return cur, True


def setp(v, pointer, value):
    ss = segs(pointer)
    cur = v
    for s in ss[:-1]:
        c = child(cur, s)
        if c is None:
            return False
        cur = c[0][c[1]]
    c = child(cur, ss[-1])
    if c is None:
        if isinstance(cur, dict):
            cur[ss[-1]] = value
            return True
        return False
    c[0][c[1]] = value
    return True


def delp(v, pointer):
    ss = segs(pointer)
    cur = v
    for s in ss[:-1]:
        c = child(cur, s)
        if c is None:
            return
        cur = c[0][c[1]]
    c = child(cur, ss[-1])
    if c is not None:
        del c[0][c[1]]


def present(v, pointer):
    x, ok = get(v, pointer)
    return ok and x is not None


class Reg:
    def __init__(self, name, spec):
        self.name = name
        self.applies = spec['applies']
        self.keys = {p: f for p, f in spec.get('key_fields', {}).items()}
        self.refs = dict(spec.get('refs', {}))
        self.schema = spec.get('schema')
        self.shared = spec.get('shared')
        self.prefix = name.rsplit('.', 1)[-1]
        self.provs = {}      # id -> list of dict(addon, variant, value, base, change, seq, op)
        self.dyns = []       # dict(addon, records, seq)
        self.layers = []
        self.tags = {}       # tag -> list of ids
        self.protect = {}


def merge(target, patch, keys, path):
    if isinstance(patch, dict):
        if not isinstance(target, dict):
            target = {}
        for k, m in patch.items():
            if m is None:
                target.pop(k, None)
                continue
            target[k] = merge(target.get(k), m, keys, path + '/' + seg_escape(k))
        return target
    if isinstance(patch, list) and path in keys:
        fields = keys[path]
        if not patch:
            return target
        if not all(holds(e, fields) for e in patch):
            raise ValueError(path + ': a keyed list element without its key fields')
        lst = target if isinstance(target, list) else []
        for e in patch:
            if e.get('$delete') is True:
                lst = [t for t in lst if not (holds(t, fields) and key_eq(t, e, fields))]
                continue
            idx = next((i for i, t in enumerate(lst) if holds(t, fields) and key_eq(t, e, fields)), None)
            if idx is None:
                lst.append({})
                idx = len(lst) - 1
            lst[idx] = merge(lst[idx], e, keys, path + '/*')
            if isinstance(lst[idx], dict):
                lst[idx].pop('$delete', None)
        return lst
    return copy.deepcopy(patch)


def run_steps(value, steps):
    v = copy.deepcopy(value)
    for st in steps:
        cur, ok = get(v, st['path'])
        if not ok:
            return None
        if st['op'] == 'set':
            setp(v, st['path'], copy.deepcopy(st['value']))
        else:
            if not is_num(cur):
                return None
            x = st['value']
            nv = cur + x if st['op'] == 'add' else cur * x
            if isinstance(cur, int) and isinstance(x, int):
                nv = int(nv)
            else:
                nv = float(nv)
            setp(v, st['path'], nv)
    return v


def type_ok(t, v):
    return {'object': isinstance(v, dict), 'array': isinstance(v, list), 'string': isinstance(v, str),
            'boolean': isinstance(v, bool), 'null': v is None,
            'number': is_num(v), 'integer': is_num(v) and float(v).is_integer()}.get(t, True)


def schema_ok(s, v):
    if not isinstance(s, dict):
        return True
    if 'type' in s:
        ts = s['type'] if isinstance(s['type'], list) else [s['type']]
        if not any(type_ok(t, v) for t in ts):
            return False
    if 'enum' in s and not any(same(e, v) and type(e) is type(v) for e in s['enum']):
        return False
    if is_num(v):
        if 'minimum' in s and v < s['minimum']:
            return False
        if 'maximum' in s and v > s['maximum']:
            return False
    if 'pattern' in s and isinstance(v, str) and not re.search(s['pattern'], v):
        return False
    if isinstance(v, dict):
        for k in s.get('required', []):
            if v.get(k) is None:
                return False
        for k, sub in s.get('properties', {}).items():
            if v.get(k) is not None and not schema_ok(sub, v[k]):
                return False
    if isinstance(v, list) and 'items' in s:
        return all(schema_ok(s['items'], x) for x in v)
    return True


def walk(v, pattern, ptr='', element=''):
    ss = segs(pattern)

    def go(cur, i, ptr, element, container, slot):
        if i == len(ss):
            yield ptr, element, container, slot
            return
        if ss[i] == '*':
            if isinstance(cur, list):
                for k in range(len(cur)):
                    p = ptr + '/' + str(k)
                    yield from go(cur[k], i + 1, p, p, cur, k)
            return
        if isinstance(cur, dict) and ss[i] in cur:
            yield from go(cur[ss[i]], i + 1, ptr + '/' + seg_escape(ss[i]), element, cur, ss[i])

    yield from go(v, 0, '', '', None, None)


def split_alts(text):
    opt = text.endswith('?')
    if opt:
        text = text[:-1]
    alts = []
    for a in text.split('|'):
        if a.endswith('?'):
            a = a[:-1]
            opt = True
        alts.append(a)
    return alts, opt


class World:
    def __init__(self, case, ctx):
        self.case = case
        self.ctx = ctx
        self.order = ['game'] + [a['id'] for a in case['addons']]
        self.regs = {n: Reg(n, s) for n, s in case['registries'].items()}
        for n in ('vehicle', 'vehicle.handling'):
            if n in self.regs:
                self.regs[n].tags.setdefault('c/f1_cars', [])
        if 'track' in self.regs:
            self.regs['track'].tags.setdefault('c/street_circuits', [])
        self.seq = 0
        self.refusals = []
        self.disabled = set(ctx.get('disabled') or [])
        for b in case['bases']:
            r = self.regs[b['registry']]
            rid = (r.prefix if r.shared else 'game') + '/' + b['key']
            r.provs.setdefault(rid, []).append(dict(addon='game', variant=False, value=b['value'], seq=self.next()))
        for reg, tags in (case.get('tags') or {}).items():
            for tag, ids in tags.items():
                self.regs[reg].tags.setdefault(tag, []).extend(ids)
        self.register()

    def next(self):
        self.seq += 1
        return self.seq

    def idx(self, addon):
        return self.order.index(addon) if addon in self.order else len(self.order)

    def refuse(self, addon, reg, rid, op, reason):
        self.refusals.append(dict(addon=addon, registry=reg, id=rid, op=op, reason=reason))

    def register(self):
        enabled = [a for a in self.case['addons'] if a.get('enabled', True) and a['id'] not in self.disabled]
        for stage in ('register', 'on_load'):
            for a in enabled:
                for i, op in enumerate(a['ops']):
                    if op.get('stage', 'register') == stage:
                        self.run_op(a['id'], i, op, stage == 'on_load')

    def run_op(self, addon, i, op, late):
        r = self.regs.get(op['registry'])
        kind = op['op']
        if kind in ('add', 'variant'):
            rid = addon + '/' + op['key']
            if r is None:
                return self.refuse(addon, op['registry'], rid, i, 'unavailable')
            if late and r.applies == 'boot':
                return self.refuse(addon, r.name, rid, i, 'late')
            ent = dict(addon=addon, seq=self.next(), op=i)
            if kind == 'add':
                value = {k: v for k, v in op['value'].items() if k not in ('$select', '$protect')}
                ent.update(variant=False, value=value)
            else:
                ent.update(variant=True, base=op['base'],
                           change=('patch', op['patch']) if 'patch' in op else ('edit', op['edit']))
            r.provs.setdefault(rid, []).append(ent)
        elif kind == 'provide':
            if late and r.applies == 'boot':
                return self.refuse(addon, r.name, '', i, 'late')
            r.dyns.append(dict(addon=addon, records=op['records'], seq=self.next()))
        elif kind == 'layer':
            target = op.get('target', '') if 'select' not in op else ''
            if op.get('in') and r.applies == 'boot':
                return self.refuse(addon, r.name, target, i, 'boot_filtered')
            if late and r.applies == 'boot':
                return self.refuse(addon, r.name, target, i, 'late')
            scope = op.get('scope', 'global')
            owner = op.get('profile') if scope == 'profile' else op.get('career') if scope == 'career' else ''
            layer = dict(addon=addon, scope=scope, owner=owner, seq=self.next(), op=i, handle=self.seq,
                         select=op.get('select'), target=op.get('target'), inn=op.get('in'), when=op.get('when'),
                         change=('patch', op['patch']) if 'patch' in op else ('edit', op['edit']))
            if layer['select'] is None:
                r.layers = [L for L in r.layers if not (L['select'] is None and L['addon'] == addon and
                                                        L['target'] == layer['target'] and L['scope'] == scope and
                                                        L['owner'] == owner)]
            r.layers.append(layer)
        elif kind == 'clear_layer':
            scope = op.get('scope', 'global')
            owner = op.get('profile') if scope == 'profile' else op.get('career') if scope == 'career' else ''
            r.layers = [L for L in r.layers if not (L['select'] is None and L['addon'] == addon and
                                                    L['target'] == op['target'] and L['scope'] == scope and
                                                    L['owner'] == owner)]
        elif kind == 'tag_add':
            if op['tag'] not in r.tags and not op['tag'].startswith(addon + '/'):
                return self.refuse(addon, r.name, op['id'], i, 'invalid')
            r.tags.setdefault(op['tag'], []).append(op['id'])
        elif kind == 'protect':
            if op['id'].split('/', 1)[0] != addon:
                return self.refuse(addon, r.name, op['id'], i, 'invalid')
            lst = r.protect.setdefault(op['id'], [])
            for f in op['fields']:
                if f not in lst:
                    lst.append(f)


class Resolver:
    def __init__(self, world, reg, dropped):
        self.w = world
        self.r = reg
        self.dropped = dropped
        self.failures = {}
        self.variant_refusals = []
        self.memo = {}
        self.ctx = world.ctx

    def dyn(self, r, rid):
        owner = rid.split('/', 1)[0]
        for d in r.dyns:
            if d['addon'] != owner or not rid.startswith(owner + '/'):
                continue
            v = d['records'].get(rid[len(owner) + 1:])
            if v is not None:
                return v
        return None

    def prov(self, r, rid):
        lst = r.provs.get(rid)
        return lst[-1] if lst else None

    def has(self, r, rid):
        if r.shared:
            return rid in r.provs and self.winner(r, rid)[0] is not None
        if self.dyn(r, rid) is not None:
            return 'value'
        p = self.prov(r, rid)
        if p is None:
            return None
        return 'variant' if p['variant'] else 'value'

    def chain(self, r, rid):
        path, seen, cur = [], set(), rid
        while True:
            h = self.has(r, cur)
            if not h:
                return 'missing', path[::-1]
            path.append(cur)
            if h != 'variant':
                return 'ok', path[::-1]
            if cur in seen:
                return 'cycle', path[::-1]
            seen.add(cur)
            if len(path) > MAX_DEPTH + 1:
                return 'too_deep', path[::-1]
            cur = self.prov(r, cur)['base']

    def exists(self, r, rid):
        return self.chain(r, rid)[0] == 'ok'

    def resolves(self, regname, rid):
        r = self.w.regs.get(regname)
        if r is None:
            return True
        if rid.startswith('person/') and regname == 'driver.person':
            key = rid[len('person/'):]
            sub = Resolver(self.w, r, set())
            return any(i.rsplit('/', 1)[-1] == key for i in sub.listed_ids(r))
        if rid.startswith('person/') and regname == 'driver.identity':
            sub = Resolver(self.w, r, set())
            for i in sub.listed_ids(r):
                v = sub.basev(i)
                if isinstance(v, dict) and v.get('person') == rid:
                    return True
            return False
        return self.exists(r, rid)

    def pick(self, regname, text):
        alts, opt = split_alts(text)
        for a in alts:
            if self.resolves(regname, a):
                return a, opt, True
        return (alts[0] if alts else ''), opt, False

    def listed_ids(self, r):
        ids = set(k for k, v in r.provs.items() if v)
        for d in r.dyns:
            for k, v in d['records'].items():
                if v is not None:
                    ids.add(d['addon'] + '/' + k)
        return sorted(i for i in ids if self.has(r, i))

    def winner(self, r, rid):
        pr = self.w.regs.get(r.shared['provider_registry'])
        found = []
        if pr is not None:
            sub = Resolver(self.w, pr, set())
            ids = set(k for k, v in pr.provs.items() if v)
            for d in pr.dyns:
                ids |= {d['addon'] + '/' + k for k, v in d['records'].items() if v is not None}
            for pid in sorted(ids):
                if not sub.exists(pr, pid):
                    continue
                b = sub.basev(pid)
                lst, ok = get(b, r.shared['provides'])
                if ok and isinstance(lst, list) and rid in lst:
                    p = sub.prov(pr, pid)
                    found.append((self.w.idx(pid.split('/', 1)[0]), p['seq'] if p else 0, pid))
        found.sort()
        addons = []
        for _, _, pid in found:
            a = pid.split('/', 1)[0]
            if a not in addons:
                addons.append(a)
        return (found[-1][2] if found else None), addons

    def change(self, ch, value):
        kind, body = ch
        if kind == 'patch':
            try:
                return merge(copy.deepcopy(value), body, self.r.keys, '')
            except ValueError:
                return None
        return run_steps(value, body)

    def basev(self, rid):
        st, path = self.chain(self.r, rid)
        if st != 'ok':
            return None
        d = self.dyn(self.r, path[0])
        v = copy.deepcopy(d if d is not None else self.prov(self.r, path[0])['value'])
        for vid in path[1:]:
            nv = self.change(self.prov(self.r, vid)['change'], v)
            if nv is not None:
                v = nv
        return v

    def members(self, r, tag):
        sub = Resolver(self.w, r, set())
        base = {m for m in r.tags.get(tag, []) if sub.exists(r, m)}
        out = set(base)
        for rid in sub.listed_ids(r):
            st, path = sub.chain(r, rid)
            if st == 'ok' and base & set(path):
                out.add(rid)
        return sorted(out)

    def where_ok(self, where, v):
        for k, want in where.items():
            got = v.get(k) if isinstance(v, dict) else None
            if isinstance(want, list):
                if not any(same(got, x) for x in want):
                    return False
            elif isinstance(want, dict) and ('min' in want or 'max' in want):
                if not is_num(got):
                    return False
                if 'min' in want and got < want['min']:
                    return False
                if 'max' in want and got > want['max']:
                    return False
            elif not same(got, want):
                return False
        return True

    def matches(self, L, rid):
        s = L['select']
        if 'ids' in s and not rid.startswith(s['ids'][:-1]):
            return False
        if rid in s.get('except', []):
            return False
        if 'where' in s and not self.where_ok(s['where'], self.basev(rid)):
            return False
        if 'tag' in s:
            t = s['tag']
            if isinstance(t, str):
                return rid in self.members(self.r, t)
            (field, tag), = t.items()
            ref = (self.basev(rid) or {}).get(field)
            target = self.r.refs.get('/' + field)
            if not isinstance(ref, str) or target is None or target not in self.w.regs:
                return False
            rid2, _, _ = self.pick(target, ref)
            return rid2 in self.members(self.w.regs[target], tag)
        return True

    def in_ok(self, sel):
        if isinstance(sel, list):
            return all(self.in_ok(x) for x in sel)
        c = self.ctx
        content = c.get('content') or []
        for k, v in sel.items():
            if k == 'mode':
                ok = c.get('mode') == v
            elif k == 'content':
                ok = v in content
            elif k == 'content_exact':
                ok = bool(content) and content[0] == v
            elif k in ('data_group', 'setup_mode'):
                ok = c.get(k) == v
            elif k == 'owner':
                ok = bool(content) and content[0].split('/', 1)[0] == v
            elif k == 'any':
                ok = any(self.in_ok(x) for x in v)
            elif k == 'not':
                ok = not self.in_ok(v)
            else:
                ok = False
            if not ok:
                return False
        return True

    def when_ok(self, addon, when):
        if not when:
            return True
        opts = (self.ctx.get('options') or {}).get(addon, {})
        if when['option'] not in opts:
            return False
        got = opts[when['option']]
        if 'is' in when:
            return same(got, when['is']) and type(got) is type(when['is'])
        if 'in' in when:
            return any(same(got, x) and type(got) is type(x) for x in when['in'])
        truthy = bool(got) if not isinstance(got, str) else got not in ('', 'false', 'off', '0')
        if 'on' in when:
            return truthy
        if 'off' in when:
            return not truthy
        return True

    def active(self, L):
        if L['handle'] in self.dropped:
            return False
        if L['scope'] == 'profile' and L['owner'] != self.ctx.get('profile', 'default'):
            return False
        if L['scope'] == 'career' and (not self.ctx.get('career') or L['owner'] != self.ctx.get('career')):
            return False
        if L['inn'] and not self.in_ok(L['inn']):
            return False
        return self.when_ok(L['addon'], L['when'])

    def valid(self, v):
        if not isinstance(v, dict):
            return False
        if self.r.schema is not None and not schema_ok(self.r.schema, v):
            return False
        for pat, target in self.r.refs.items():
            for ptr, el, cont, slot in walk(v, pat):
                x = cont[slot]
                if isinstance(x, str):
                    _, opt, ok = self.pick(target, x)
                    if not ok and not opt:
                        return False
        return True

    def diff(self, a, b, ptr, pat, out):
        if same(a, b):
            return
        if isinstance(a, dict) and isinstance(b, dict):
            for k in list(b) + [k for k in a if k not in b]:
                if same(a.get(k), b.get(k)):
                    continue
                s = '/' + seg_escape(k)
                self.diff(a.get(k), b.get(k), ptr + s, pat + s, out)
            return
        if isinstance(a, list) and isinstance(b, list) and pat in self.r.keys:
            f = self.r.keys[pat]
            if all(holds(e, f) for e in a + b):
                for e in b:
                    old = next((o for o in a if key_eq(o, e, f)), None)
                    seg = '/' + key_seg(e, f)
                    if old is None:
                        out.append(ptr + seg)
                    else:
                        self.diff(old, e, ptr + seg, pat + '/*', out)
                return
        out.append(ptr)

    def apply(self, R, ch, addon, rid, layer):
        after = self.change(ch, R['v'])
        if after is None:
            return False
        blocked = []
        if layer is not None and layer['select'] is not None and addon != rid.split('/', 1)[0] and not \
                ((self.ctx.get('settings') or {}).get(addon, {}).get('also_change_year_packs', False)):
            for f in self.r.protect.get(rid, []):
                p = f if f.startswith('/') else '/' + f
                b, bok = get(R['v'], p)
                a, aok = get(after, p)
                if same(b if bok else None, a if aok else None):
                    continue
                if not bok or b is None:
                    delp(after, p)
                else:
                    setp(after, p, copy.deepcopy(b))
                blocked.append(p)
        if not self.valid(after):
            return False
        out = []
        self.diff(R['v'], after, '', '', out)
        for p in out:
            R['origins'] = {q: a for q, a in R['origins'].items() if not q.startswith(p + '/')}
            R['origins'][p] = addon
            R['changers'].setdefault(p, []).append(addon)
        for p in blocked:
            R['blocked'].setdefault(p, []).append(addon)
        R['v'] = after
        R['origins'] = {q: a for q, a in R['origins'].items() if present(after, q)}
        return True

    def compute(self, rid, excluded):
        key = (rid, tuple(sorted(excluded)))
        if key in self.memo:
            return copy.deepcopy(self.memo[key])
        st, path = self.chain(self.r, rid)
        R = dict(st='no_record', v=None, owner=None, origins={}, changers={}, blocked={})
        if st == 'missing':
            self.memo[key] = R
            return copy.deepcopy(R)
        if st != 'ok':
            R['st'] = 'invalid'
            self.memo[key] = R
            return copy.deepcopy(R)
        R['owner'] = path[0].split('/', 1)[0]
        if len(path) == 1:
            if self.r.shared:
                p = self.prov(self.r, rid)
                v = copy.deepcopy(p['value'])
                w, _ = self.winner(self.r, rid)
                setp(v, self.r.shared['resolved'], w)
                R['owner'] = 'game'
            else:
                d = self.dyn(self.r, rid)
                v = copy.deepcopy(d if d is not None else self.prov(self.r, rid)['value'])
            if not self.valid(v):
                R['st'] = 'invalid'
                self.memo[key] = R
                return copy.deepcopy(R)
            R['st'], R['v'] = 'ok', v
        else:
            ex = set(excluded)
            for L in self.r.layers:
                if L['select'] is not None and self.active(L) and self.matches(L, rid):
                    ex.add(L['handle'])
            B = self.compute(path[-2], ex)
            if B['st'] != 'ok':
                R['st'] = 'no_record' if B['st'] == 'no_record' else 'invalid'
                self.memo[key] = R
                return copy.deepcopy(R)
            owner = R['owner']
            R = B
            R['owner'] = owner
            p = self.prov(self.r, rid)
            if not self.apply(R, p['change'], p['addon'], rid, None):
                if not excluded:
                    self.variant_refusals.append(dict(addon=p['addon'], registry=self.r.name, id=rid, op=p['op'],
                                                      reason='invalid'))
                R = dict(st='invalid', v=None, owner=None, origins={}, changers={}, blocked={})
                self.memo[key] = R
                return copy.deepcopy(R)
        ls = []
        for L in self.r.layers:
            if not self.active(L):
                continue
            if L['select'] is not None:
                if L['handle'] not in excluded and self.matches(L, rid):
                    ls.append(L)
            elif L['target'] == rid:
                ls.append(L)
        scope_rank = {'global': 0, 'profile': 1, 'career': 2}
        ls.sort(key=lambda L: (self.w.idx(L['addon']), scope_rank[L['scope']], 0 if L['select'] is not None else 1,
                               L['seq']))
        for L in ls:
            if not self.apply(R, L['change'], L['addon'], rid, L):
                self.failures.setdefault(L['handle'], set()).add(rid)
        self.memo[key] = R
        return copy.deepcopy(R)

    def finalize(self, R):
        if R['st'] != 'ok':
            return R
        v = R['v']
        removals, nulls = set(), []
        for pat, target in self.r.refs.items():
            for ptr, el, cont, slot in list(walk(v, pat)):
                x = cont[slot]
                if not isinstance(x, str):
                    continue
                rid, opt, ok = self.pick(target, x)
                if ok:
                    cont[slot] = rid
                elif not opt:
                    R['st'] = 'invalid'
                    return R
                elif el:
                    arr, i = el.rsplit('/', 1)
                    removals.add((arr, int(i)))
                else:
                    nulls.append(ptr)
        for p in nulls:
            setp(v, p, None)
        for arr, i in sorted(removals, key=lambda t: (t[0], t[1]), reverse=True):
            lst, ok = get(v, arr)
            if ok and isinstance(lst, list) and i < len(lst):
                del lst[i]
        R['origins'] = {q: a for q, a in R['origins'].items() if present(v, q)}
        return R


def origin_of(R, p):
    if p in R['origins']:
        return R['origins'][p]
    best = None
    for q in R['origins']:
        if p.startswith(q + '/') and (best is None or len(q) > len(best)):
            best = q
    return R['origins'][best] if best else R['owner']


def evaluate(case, ctx):
    w = World(case, ctx)
    out = {'records': {}, 'conflicts': [], 'refusals': list(w.refusals)}
    for name, r in w.regs.items():
        dropped = set()
        for _ in range(64):
            res = Resolver(w, r, dropped)
            ids = res.listed_ids(r)
            top = {i: res.finalize(res.compute(i, set())) for i in ids}
            fresh = [h for h in res.failures if h not in dropped]
            if not fresh:
                break
            for h in fresh:
                dropped.add(h)
                L = next(L for L in r.layers if L['handle'] == h)
                out['refusals'].append(dict(addon=L['addon'], registry=name, id=min(res.failures[h]), op=L['op'],
                                            reason='invalid'))
        out['refusals'] += res.variant_refusals
        recs = {}
        for i, R in top.items():
            recs[i] = R
            if R['st'] != 'ok':
                continue
            for p, who in R['changers'].items():
                u = sorted(dict.fromkeys(who), key=w.idx)
                if len(u) >= 2:
                    out['conflicts'].append(dict(kind='field', registry=name, id=i, pointer=p, addons=u,
                                                 winner=origin_of(R, p)))
            for p, who in R['blocked'].items():
                u = sorted(dict.fromkeys(who), key=w.idx)
                out['conflicts'].append(dict(kind='protected', registry=name, id=i, pointer=p, addons=u,
                                             winner=origin_of(R, p)))
        for i, lst in r.provs.items():
            if not r.shared and len(lst) >= 2 and lst[-1]['addon'] != 'game':
                out['conflicts'].append(dict(kind='provider', registry=name, id=i, pointer='',
                                             addons=[lst[-1]['addon']], winner=lst[-1]['addon']))
        if r.shared:
            for i in ids:
                win, addons = res.winner(r, i)
                if len(addons) > 1:
                    out['conflicts'].append(dict(kind='provider', registry=name, id=i, pointer=r.shared['resolved'],
                                                 addons=addons, winner=win.split('/', 1)[0]))
        graph = {i: lst[-1]['base'] for i, lst in r.provs.items() if lst and lst[-1]['variant']}
        reported = set()
        for start in graph:
            path, seen, cur = [], set(), start
            while cur in graph and cur not in seen and res.dyn(r, cur) is None:
                seen.add(cur)
                path.append(cur)
                cur = graph[cur]
            if cur not in seen:
                continue
            loop = path[path.index(cur):]
            worst = max(loop, key=lambda i: (w.idx(r.provs[i][-1]['addon']), r.provs[i][-1]['seq']))
            if worst not in reported:
                reported.add(worst)
                p = r.provs[worst][-1]
                out['refusals'].append(dict(addon=p['addon'], registry=name, id=worst, op=p['op'], reason='cycle'))
        for i in graph:
            if res.chain(r, i)[0] == 'too_deep' and res.chain(r, graph[i])[0] == 'ok':
                p = r.provs[i][-1]
                out['refusals'].append(dict(addon=p['addon'], registry=name, id=i, op=p['op'], reason='too_deep'))
        out['records'][name] = (recs, res, r)
    return out, w


def num_eq(a, b):
    return abs(a - b) <= 1e-6 * max(1.0, abs(a), abs(b))


def equal(a, b):
    if a is None or b is None:
        return a is None and b is None
    if is_num(a) and is_num(b):
        return num_eq(a, b)
    if isinstance(a, dict) and isinstance(b, dict):
        return all(equal(a.get(k), b.get(k)) for k in set(a) | set(b))
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equal(x, y) for x, y in zip(a, b))
    return a == b and type(a) is type(b)


def canon(e):
    return json.dumps(e, sort_keys=True)


def unique(lst):
    return sorted(set(canon(x) for x in lst))


def record_of(world_out, reg, rid):
    recs, res, r = world_out['records'][reg]
    R = recs.get(rid)
    if R is None:
        R = res.finalize(res.compute(rid, set()))
    out = {'status': R['st']}
    if R['st'] == 'ok':
        out['value'] = R['v']
        out['origins'] = R['origins']
    return out


def run_case(path):
    case = json.load(open(path))
    fails = []
    for ei, ev in enumerate(case['evaluations']):
        out, w = evaluate(case, ev.get('context', {}))
        for reg, ids in ev.get('expect', {}).items():
            for rid, want in ids.items():
                got = record_of(out, reg, rid)
                if got['status'] != want['status']:
                    fails.append(f'eval {ei} {reg} {rid}: status {got["status"]}, want {want["status"]}')
                    continue
                if want['status'] != 'ok':
                    continue
                if not equal(got['value'], want['value']):
                    fails.append(f'eval {ei} {reg} {rid}: value {json.dumps(got["value"])}')
                if got['origins'] != want['origins']:
                    fails.append(f'eval {ei} {reg} {rid}: origins {got["origins"]}, want {want["origins"]}')
        if unique(out['conflicts']) != unique(ev.get('conflicts', [])) or \
                len(out['conflicts']) != len(ev.get('conflicts', [])):
            fails.append(f'eval {ei} conflicts {unique(out["conflicts"])}')
        if unique(out['refusals']) != unique(ev.get('refusals', [])):
            fails.append(f'eval {ei} refusals {unique(out["refusals"])}')
        for l in ev.get('lists', []):
            recs, res, r = out['records'][l['registry']]
            got = sorted(i for i, R in recs.items() if R['st'] != 'no_record' and i.startswith(l['prefix']))
            if got != l['ids']:
                fails.append(f'eval {ei} list {got}')
        for t in ev.get('tag_members', []):
            recs, res, r = out['records'][t['registry']]
            got = res.members(r, t['tag'])
            if got != t['ids']:
                fails.append(f'eval {ei} tag {got}')
    return fails


def dump_case(path):
    case = json.load(open(path))
    evs = []
    for ev in case['evaluations']:
        out, w = evaluate(case, ev.get('context', {}))
        recs = {}
        for reg, (rs, res, r) in out['records'].items():
            ids = set(i for i, R in rs.items() if R['st'] != 'no_record')
            ids |= set(ev.get('expect', {}).get(reg, {}))
            recs[reg] = {i: record_of(out, reg, i) for i in sorted(ids)}
        evs.append({'records': recs, 'conflicts': [json.loads(x) for x in unique(out['conflicts'])],
                    'refusals': [json.loads(x) for x in unique(out['refusals'])]})
    return evs


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == '--dump':
        print(json.dumps(dump_case(sys.argv[2])))
        return 0
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '../../../packages/sdk/std/tests/cases')
    files = sorted(f for f in os.listdir(d) if f.endswith('.json'))
    bad = 0
    for f in files:
        fails = run_case(os.path.join(d, f))
        for x in fails:
            print(f'FAIL {f}: {x}')
        bad += bool(fails)
    print(f'ref.py: {len(files)} case(s), {bad} failing')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
