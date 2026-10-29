#!/usr/bin/env python3
"""Randomized layer stacks: seeded cases in the corpus format, resolved by the C++ core (corpus_test --dump)
and by ref.py, which must agree on every record, origin, conflict and refusal."""
import argparse
import json
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref  # noqa: E402

ADDONS = ['mod.a', 'mod.b', 'mod.c', 'mod.d', 'swofty.cs']
KINDS = ['f1', 'f2', 'f1_26']


def thing(rng, name):
    items = [{'key': k, 'v': rng.randint(0, 9)} for k in rng.sample(['k1', 'k2', 'k3'], rng.randint(0, 2))]
    return {'name': name, 'n': rng.randint(0, 20), 'kind': rng.choice(KINDS), 'items': items}


def patch(rng):
    p = {}
    for _ in range(rng.randint(1, 3)):
        f = rng.choice(['name', 'n', 'kind', 'items', 'items'])
        if f == 'name':
            p['name'] = rng.choice(['x', 'y', 'z'])
        elif f == 'n':
            p['n'] = rng.choice([rng.randint(0, 30), 999])
        elif f == 'kind':
            p['kind'] = rng.choice(KINDS)
        else:
            el = {'key': rng.choice(['k1', 'k2', 'k3', 'k4'])}
            r = rng.random()
            if r < 0.15:
                el['$delete'] = True
            else:
                el['v'] = rng.randint(0, 9)
            p['items'] = [el]
    return p


def edit(rng):
    return [{'op': rng.choice(['add', 'mul']), 'path': '/n', 'value': rng.choice([1, 2, 3])}]


def selector(rng, ids):
    s = {}
    r = rng.random()
    if r < 0.3:
        s['ids'] = '*'
    elif r < 0.5:
        s['ids'] = rng.choice(['game/', 'swofty.cs/', 'mod.a/']) + '*'
    if rng.random() < 0.4:
        s['where'] = {'kind': rng.choice([rng.choice(KINDS), KINDS[:2]])} if rng.random() < 0.7 else \
            {'n': {'min': rng.randint(0, 10), 'max': rng.randint(10, 30)}}
    if rng.random() < 0.25:
        s['tag'] = 'c/things'
    if rng.random() < 0.2 and ids:
        s['except'] = [rng.choice(ids)]
    if not s:
        s['ids'] = '*'
    return s


def make_case(rng):
    bases = [{'addon': 'game', 'registry': 'test.thing', 'key': k, 'value': thing(rng, k)}
             for k in rng.sample(['a', 'b', 'c', 'd'], rng.randint(1, 4))]
    ids = ['game/' + b['key'] for b in bases]
    addons = []
    order = rng.sample(ADDONS, rng.randint(2, len(ADDONS)))
    for a in order:
        ops = []
        for _ in range(rng.randint(1, 5)):
            r = rng.random()
            if r < 0.2:
                key = rng.choice(['x', 'y', 'z'])
                ops.append({'op': 'add', 'registry': 'test.thing', 'key': key, 'value': thing(rng, key)})
                ids.append(a + '/' + key)
            elif r < 0.35:
                key = rng.choice(['v1', 'v2'])
                base = rng.choice(ids + ['mod.a/v1', 'mod.b/v2'])
                op = {'op': 'variant', 'registry': 'test.thing', 'key': key, 'base': base}
                if rng.random() < 0.7:
                    op['patch'] = patch(rng)
                else:
                    op['edit'] = edit(rng)
                ops.append(op)
                ids.append(a + '/' + key)
            elif r < 0.75:
                op = {'op': 'layer', 'registry': 'test.thing'}
                if rng.random() < 0.5:
                    op['target'] = rng.choice(ids + ['mod.c/x'])
                else:
                    op['select'] = selector(rng, ids)
                if rng.random() < 0.6:
                    op['patch'] = patch(rng)
                else:
                    op['edit'] = edit(rng)
                sc = rng.random()
                if sc < 0.15:
                    op['scope'], op['profile'] = 'profile', rng.choice(['p1', 'p2'])
                elif sc < 0.25:
                    op['scope'], op['career'] = 'career', 'c1'
                if rng.random() < 0.15:
                    op['in'] = [rng.choice([{'mode': 'time_trial'}, {'content': 'swofty.cs/box'},
                                            {'not': {'mode': 'grand_prix'}}])]
                if rng.random() < 0.1 and 'patch' in op:
                    op['when'] = {'option': 'o', 'is': rng.choice(['on', 'off'])}
                ops.append(op)
            elif r < 0.82:
                ops.append({'op': 'clear_layer', 'registry': 'test.thing', 'target': rng.choice(ids)})
            elif r < 0.9:
                own = [i for i in ids if i.startswith(a + '/')]
                if own:
                    ops.append({'op': 'protect', 'registry': 'test.thing', 'id': rng.choice(own),
                                'fields': [rng.choice(['n', 'kind', 'name'])]})
            elif r < 0.95:
                ops.append({'op': 'tag_add', 'registry': 'test.thing', 'tag': 'c/things', 'id': rng.choice(ids)})
            else:
                recs = {rng.choice(['x', 'p']): (thing(rng, 'p') if rng.random() < 0.8 else None)}
                ops.append({'op': 'provide', 'registry': 'test.thing', 'records': recs})
        addons.append({'id': a, 'ops': ops})
    evals = []
    for _ in range(rng.randint(1, 3)):
        ctx = {'profile': rng.choice(['default', 'p1', 'p2'])}
        if rng.random() < 0.3:
            ctx['career'] = 'c1'
        if rng.random() < 0.4:
            ctx['mode'] = rng.choice(['time_trial', 'grand_prix'])
        if rng.random() < 0.3:
            ctx['content'] = ['swofty.cs/box']
        if rng.random() < 0.3:
            ctx['disabled'] = [rng.choice(order)]
        ctx['options'] = {a: {'o': rng.choice(['on', 'off'])} for a in order}
        if rng.random() < 0.3:
            ctx['settings'] = {rng.choice(order): {'also_change_year_packs': True}}
        evals.append({'context': ctx, 'expect': {}, 'conflicts': [], 'refusals': []})
    schema = {'type': 'object', 'properties': {'n': {'type': 'integer', 'minimum': 0, 'maximum': 500}}}
    return {'name': 'random', 'rule': 'random', 'bases': bases,
            'registries': {'test.thing': {'applies': 'read', 'key_fields': {'/items': ['key']}, 'schema': schema}},
            'tags': {'test.thing': {'c/things': [ids[0]]}}, 'addons': addons, 'evaluations': evals}


def norm(x):
    if isinstance(x, dict):
        return {k: norm(v) for k, v in x.items() if v is not None}
    if isinstance(x, list):
        return [norm(v) for v in x]
    if isinstance(x, float) and x.is_integer():
        return int(x)
    return x


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runner', required=True)
    ap.add_argument('--count', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1606)
    ap.add_argument('--keep', default='')
    args = ap.parse_args()
    rng = random.Random(args.seed)
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for n in range(args.count):
            case = make_case(rng)
            path = os.path.join(tmp, f'{n}.json')
            with open(path, 'w') as f:
                json.dump(case, f)
            got = subprocess.run([args.runner, '--dump', path], capture_output=True, text=True)
            if got.returncode != 0:
                print(f'stack {n}: the C++ runner failed: {got.stderr[-400:]}')
                bad += 1
                continue
            cpp = norm(json.loads(got.stdout))
            py = norm(ref.dump_case(path))
            if not ref.equal(cpp, py):
                bad += 1
                if args.keep:
                    os.makedirs(args.keep, exist_ok=True)
                    with open(os.path.join(args.keep, f'{n}.json'), 'w') as f:
                        json.dump(case, f, indent=1)
                if bad <= 3:
                    for e, (a, b) in enumerate(zip(cpp, py)):
                        for k in ('conflicts', 'refusals'):
                            if not ref.equal(a[k], b[k]):
                                print(f'stack {n} eval {e} {k}:\n  c++ {json.dumps(a[k])}\n  py  {json.dumps(b[k])}')
                        for reg in set(a['records']) | set(b['records']):
                            ra, rb = a['records'].get(reg, {}), b['records'].get(reg, {})
                            for i in sorted(set(ra) | set(rb)):
                                if not ref.equal(ra.get(i), rb.get(i)):
                                    print(f'stack {n} eval {e} {reg} {i}:\n  c++ {json.dumps(ra.get(i))}\n  py  {json.dumps(rb.get(i))}')
    print(f'random stacks: {args.count} seeded stack(s) (seed {args.seed}), {bad} disagreement(s)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
