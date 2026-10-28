#!/usr/bin/env python3
"""Shared KB access for re/db. The only sanctioned writer of db/*.tsv (see PROTOCOL.md §4, §6).

  kb.py --agent A add <table> col=value ...           add a row
  kb.py --agent A set <table> <key> col=value col+=;token ...
  kb.py get <table> <key|name>
  kb.py find <regex>                                  search names/notes/paths in every table
  kb.py --agent A claim|release <subsystem>
  kb.py --agent A note <subsystem> "text"             append to subsystems/<id>.md ## Inbox
  kb.py --agent A verify <kind> <item> agree|disagree|partial "<evidence>" ["notes"]
  kb.py --agent A test-new "<question>"               allocate re/tests/T###.md
  kb.py --agent A import <rows.tsv> <table>           add rows not yet present (ghidra_export output)
Tables: functions globals db_keys strings_anchors subsystems verify
"""
import argparse, datetime, json, os, re, sys, contextlib
try:
    import fcntl
except ImportError:  # Windows
    fcntl = None
    import msvcrt

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(HERE)
ROOT = os.path.dirname(RE)
DB = os.path.join(RE, 'db')
LOCK = os.path.join(DB, '.kb.lock')
JOURNAL = os.path.join(DB, 'changes.log')

TABLES = {
    'functions': (['addr', 'name', 'sig', 'level', 'subsystem', 'proto', 'evidence', 'notes'], ('addr',)),
    'globals': (['addr', 'name', 'type', 'level', 'subsystem', 'sig', 'evidence', 'notes'], ('addr',)),
    'db_keys': (['key', 'path', 'reader', 'sites', 'level', 'subsystem', 'evidence', 'notes'], ('key', 'path', 'reader')),
    'strings_anchors': (['id', 'addr', 'enc', 'text', 'refs', 'funcs', 'level', 'notes'], ('id',)),
    'subsystems': (['id', 'prefix', 'owner', 'status', 'description'], ('id',)),
    'verify': (['date', 'verifier', 'kind', 'item', 'claimed', 'verdict', 'evidence', 'notes'], None),
}
LEVELS = ('L0', 'L1', 'L2', 'L3')
TOKEN_KINDS = ('decomp', 'xref', 'str', 'imm', 'pat', 'doc', 'kb', 'trace', 'sig')
ADDR_RE = re.compile(r'^0x1[45][0-9a-f]{7}$')
NAME_RE = re.compile(r'^[A-Za-z][A-Za-z0-9_]{1,63}$')
FUNC_NAME_RE = re.compile(r'^([A-Z][A-Za-z0-9]*)_[A-Za-z0-9]+(_[A-Za-z0-9]+)*$')
GLOBAL_NAME_RE = re.compile(r'^(g|k|vt|site)_([A-Z][A-Za-z0-9]*)(_[A-Za-z0-9]+)*$')
SIG_ID_RE = re.compile(r'^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$')
TEST_RE = re.compile(r'^T\d{3}$')


def path(table):
    return os.path.join(DB, f'{table}.tsv')


def read_table(table):
    cols, _ = TABLES[table]
    p = path(table)
    rows = []
    if not os.path.exists(p):
        return rows, [f'{table}.tsv missing']
    errs = []
    with open(p) as f:
        lines = f.read().split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    if not lines or lines[0].split('\t') != cols:
        errs.append(f'{table}.tsv: header must be {"<TAB>".join(cols)}')
    for n, line in enumerate(lines[1:], 2):
        c = line.split('\t')
        if len(c) != len(cols):
            errs.append(f'{table}.tsv:{n}: {len(c)} columns, expected {len(cols)}')
            continue
        r = dict(zip(cols, c))
        r['_line'] = n
        rows.append(r)
    return rows, errs


def write_table(table, rows):
    cols, key = TABLES[table]
    if key and key[0] in ('addr',):
        rows = sorted(rows, key=lambda r: int(r['addr'], 16))
    elif key:
        rows = sorted(rows, key=lambda r: tuple(r[k] for k in key))
    p = path(table)
    tmp = f'{p}.{os.getpid()}.tmp'
    with open(tmp, 'w') as f:
        f.write('\t'.join(cols) + '\n')
        for r in rows:
            f.write('\t'.join(r[c] for c in cols) + '\n')
    os.replace(tmp, p)


def row_key(table, r):
    _, key = TABLES[table]
    return tuple(r[k] for k in key) if key else None


def tokens(ev):
    out = []
    if ev in ('', '-'):
        return out
    for t in ev.split(';'):
        t = t.strip()
        if not t:
            continue
        k, _, v = t.partition(':')
        out.append((k, v))
    return out


def subsystems():
    rows, _ = read_table('subsystems')
    return {r['id']: r for r in rows}


def check_level_evidence(level, ev, kind='function'):
    errs = []
    toks = tokens(ev)
    kinds = {k for k, _ in toks}
    for k, v in toks:
        if k not in TOKEN_KINDS:
            errs.append(f'unknown evidence token kind {k!r}')
        elif not v:
            errs.append(f'empty evidence token {k}:')
    if level not in LEVELS:
        errs.append(f'level {level!r} not in {LEVELS}')
        return errs
    if level == 'L1' and not toks:
        errs.append('L1 needs at least one evidence token')
    if level in ('L2', 'L3'):
        need = {'decomp', 'xref'} if kind not in ('db_key', 'code_site') else {'decomp'}
        miss = need - kinds
        if miss:
            errs.append(f'{level} needs ' + ' and '.join(f'{m}:' for m in sorted(miss)))
    if level == 'L3' and 'trace' not in kinds:
        errs.append('L3 needs a trace: token')
    return errs


def validate_row(table, r, subs=None):
    subs = subs if subs is not None else subsystems()
    errs = []
    cols, _ = TABLES[table]
    for c in cols:
        v = r.get(c, '')
        if v == '':
            errs.append(f'{c} is empty (use -)')
        if '\t' in v or '\n' in v:
            errs.append(f'{c} contains a tab or newline')
    if errs:
        return errs
    if table in ('functions', 'globals'):
        if not ADDR_RE.match(r['addr']):
            errs.append(f'addr {r["addr"]!r} is not 0x14xxxxxxx/0x15xxxxxxx lowercase')
        if not NAME_RE.match(r['name']):
            errs.append(f'name {r["name"]!r} has invalid characters or length')
        if r['name'].startswith(('FUN_', 'DAT_', 'LAB_', 'sub_', 'PTR_', 'UNK_')):
            errs.append(f'name {r["name"]} is a tool default name')
        sub = subs.get(r['subsystem'])
        if not sub:
            errs.append(f'subsystem {r["subsystem"]!r} not in subsystems.tsv')
        if table == 'functions':
            m = FUNC_NAME_RE.match(r['name'])
            if not m:
                errs.append(f'name {r["name"]} does not follow <Prefix>_<Verb>_<Noun>')
            elif sub and m.group(1) != sub['prefix']:
                errs.append(f'name prefix {m.group(1)} != subsystem {r["subsystem"]} prefix {sub["prefix"]}')
            unk = f'{sub["prefix"] if sub else "?"}_Unk_{r["addr"][2:]}'
            if r['level'] == 'L0' and r['name'] != unk:
                errs.append(f'L0 function must be named {unk}')
            if '_Unk_' in r['name'] and r['name'] != unk:
                errs.append(f'unknown-name must be exactly {unk}')
        else:
            m = GLOBAL_NAME_RE.match(r['name'])
            if not m:
                errs.append(f'global name {r["name"]} must be g_/k_/vt_/site_<Prefix>_...')
            elif sub and m.group(2) != sub['prefix']:
                errs.append(f'global prefix {m.group(2)} != subsystem prefix {sub["prefix"]}')
        if r['sig'] != '-' and not SIG_ID_RE.match(r['sig']):
            errs.append(f'sig id {r["sig"]!r} malformed')
        errs += check_level_evidence(r['level'], r['evidence'], 'code_site' if r.get('type') == 'code' else 'function')
    elif table == 'db_keys':
        if r['key'] != '-' and not re.match(r'^0x[0-9a-f]{16}$', r['key']):
            errs.append(f'key {r["key"]!r} must be 0x + 16 hex')
        if r['key'] != '-' and r['path'] != '?' and '{' not in r['path'] and f'0x{fnv1a64(r["path"]):016x}' != r['key']:
            errs.append(f'fnv1a64({r["path"]}) != {r["key"]}')
        if not ADDR_RE.match(r['reader']):
            errs.append(f'reader {r["reader"]!r} is not an address')
        if r['subsystem'] not in subs:
            errs.append(f'subsystem {r["subsystem"]!r} not in subsystems.tsv')
        errs += check_level_evidence(r['level'], r['evidence'], 'db_key')
    elif table == 'strings_anchors':
        if not re.match(r'^[shw]_[A-Za-z0-9_]+$', r['id']):
            errs.append(f'anchor id {r["id"]!r} must be s_/h_/w_<name>')
        if r['addr'] != '-' and not ADDR_RE.match(r['addr']):
            errs.append(f'addr {r["addr"]!r} malformed')
        if r['enc'].startswith('djb2:'):
            if f'djb2:0x{djb2(r["text"]):08x}' != r['enc']:
                errs.append(f'djb2({r["text"]}) != {r["enc"]}')
        elif r['enc'] not in ('ascii', 'utf16'):
            errs.append(f'enc {r["enc"]!r} must be ascii, utf16 or djb2:0x........')
        if r['level'] not in LEVELS:
            errs.append(f'level {r["level"]!r}')
        if r['level'] in ('L2', 'L3') and r['refs'] == '-':
            errs.append('L2 anchor needs refs')
    elif table == 'subsystems':
        if not re.match(r'^[a-z][a-z0-9_]*$', r['id']):
            errs.append('subsystem id must be snake_case')
        if not re.match(r'^[A-Z][A-Za-z0-9]*$', r['prefix']):
            errs.append('prefix must be PascalCase')
    elif table == 'verify':
        if r['verdict'] not in ('agree', 'disagree', 'partial'):
            errs.append(f'verdict {r["verdict"]!r}')
        if r['kind'] not in ('func', 'global', 'struct', 'field', 'sig', 'db_key', 'anchor'):
            errs.append(f'kind {r["kind"]!r}')
        errs += [e for e in check_level_evidence('L1', r['evidence'])]
    return errs


def fnv1a64(s):
    h = 0xcbf29ce484222325
    for c in s.encode():
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def djb2(s):
    h = 5381
    for c in s.encode():
        h = (h * 33 + c) & 0xffffffff
    return h


@contextlib.contextmanager
def locked():
    os.makedirs(DB, exist_ok=True)
    with open(LOCK, 'a') as lf:
        if fcntl is None:
            # Windows: lock the first byte, blocking until it is free.
            lf.seek(0)
            msvcrt.locking(lf.fileno(), msvcrt.LK_LOCK, 1)
            try:
                yield
            finally:
                lf.seek(0)
                msvcrt.locking(lf.fileno(), msvcrt.LK_UNLCK, 1)
            return
        fcntl.flock(lf, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lf, fcntl.LOCK_UN)


def journal(agent, op, table, key, before, after):
    with open(JOURNAL, 'a') as f:
        f.write(json.dumps({'ts': datetime.datetime.now().isoformat(timespec='seconds'), 'agent': agent, 'op': op,
                            'table': table, 'key': key, 'before': before, 'after': after}) + '\n')


def raiser(table, key):
    who = None
    if not os.path.exists(JOURNAL):
        return None
    for line in open(JOURNAL):
        try:
            j = json.loads(line)
        except Exception:
            continue
        if j.get('table') == table and j.get('key') == key:
            b, a = j.get('before') or {}, j.get('after') or {}
            if a.get('level') and a.get('level') != b.get('level'):
                who = j['agent']
    return who


def owner_gate(agent, sub, new_level, force):
    s = subsystems().get(sub)
    if s and s['owner'] not in ('-', agent) and new_level in ('L2', 'L3') and not force:
        raise SystemExit(f'refused: subsystem {sub} is owned by {s["owner"]}; only the owner raises rows to {new_level} (use kb.py note {sub} "...")')


def parse_assign(items, cols):
    sets, appends = {}, {}
    for it in items:
        m = re.match(r'^([a-z_]+)(\+?)=(.*)$', it, re.S)
        if not m or m.group(1) not in cols:
            raise SystemExit(f'bad assignment {it!r}; columns: {cols}')
        (appends if m.group(2) else sets)[m.group(1)] = m.group(3)
    return sets, appends


def find_row(table, rows, key):
    _, kcols = TABLES[table]
    for r in rows:
        if kcols and len(kcols) == 1 and r[kcols[0]] == key:
            return r
        if r.get('name') == key or r.get('id') == key:
            return r
        if kcols and len(kcols) > 1 and '|'.join(r[k] for k in kcols) == key:
            return r
    return None


def uniqueness(table, rows, r):
    errs = []
    k = row_key(table, r)
    if k and any(row_key(table, x) == k for x in rows if x is not r):
        errs.append(f'duplicate key {k}')
    if table in ('functions', 'globals'):
        names = set()
        for t in ('functions', 'globals'):
            other = rows if t == table else read_table(t)[0]
            names |= {x['name'] for x in other if x is not r}
        if r['name'] in names:
            errs.append(f'name {r["name"]} already used')
    return errs


def cmd_add(a):
    cols, _ = TABLES[a.table]
    sets, appends = parse_assign(a.assign, cols)
    r = {c: sets.get(c, '-') for c in cols}
    if a.table in ('functions', 'globals') and r['addr'] != '-':
        r['addr'] = '0x' + r['addr'].lower().removeprefix('0x')
    with locked():
        rows, errs = read_table(a.table)
        if errs:
            raise SystemExit('\n'.join(errs))
        if 'subsystem' in r:
            owner_gate(a.agent, r['subsystem'], r.get('level'), a.force)
        e = validate_row(a.table, r) + uniqueness(a.table, rows, r)
        if e:
            raise SystemExit('refused:\n  ' + '\n  '.join(e))
        rows.append(r)
        write_table(a.table, rows)
        journal(a.agent, 'add', a.table, '|'.join(row_key(a.table, r) or ()), None, {c: r[c] for c in cols})
    print(f'added {a.table} {row_key(a.table, r)}')


def cmd_set(a):
    cols, _ = TABLES[a.table]
    sets, appends = parse_assign(a.assign, cols)
    with locked():
        rows, errs = read_table(a.table)
        r = find_row(a.table, rows, a.key)
        if not r:
            raise SystemExit(f'no {a.table} row {a.key}')
        before = {c: r[c] for c in cols}
        for c, v in sets.items():
            r[c] = v
        for c, v in appends.items():
            r[c] = v if r[c] in ('', '-') else r[c] + (';' if c == 'evidence' and not v.startswith(';') else ' ') + v.lstrip(';')
        if 'subsystem' in r:
            owner_gate(a.agent, r['subsystem'], r.get('level'), a.force)
        e = validate_row(a.table, r) + uniqueness(a.table, rows, r)
        if e:
            raise SystemExit('refused:\n  ' + '\n  '.join(e))
        write_table(a.table, rows)
        journal(a.agent, 'set', a.table, '|'.join(row_key(a.table, r) or ()), before, {c: r[c] for c in cols})
    print(f'updated {a.table} {row_key(a.table, r)}')


def cmd_get(a):
    rows, _ = read_table(a.table)
    r = find_row(a.table, rows, a.key)
    if not r:
        raise SystemExit(f'no {a.table} row {a.key}')
    for c in TABLES[a.table][0]:
        print(f'{c:10s} {r[c]}')


def cmd_find(a):
    rx = re.compile(a.regex, re.I)
    for t in TABLES:
        rows, _ = read_table(t)
        for r in rows:
            line = '\t'.join(r[c] for c in TABLES[t][0])
            if rx.search(line):
                print(f'{t}:{r["_line"]}: {line[:220]}')


def cmd_claim(a, release=False):
    with locked():
        rows, _ = read_table('subsystems')
        r = find_row('subsystems', rows, a.subsystem)
        if not r:
            raise SystemExit(f'no subsystem {a.subsystem}')
        before = dict(r)
        if release:
            if r['owner'] != a.agent and not a.force:
                raise SystemExit(f'{a.subsystem} is owned by {r["owner"]}, not {a.agent}')
            r['owner'] = '-'
        else:
            if r['owner'] not in ('-', a.agent) and not a.force:
                raise SystemExit(f'{a.subsystem} is already owned by {r["owner"]}')
            r['owner'] = a.agent
        write_table('subsystems', rows)
        journal(a.agent, 'release' if release else 'claim', 'subsystems', a.subsystem, {'owner': before['owner']}, {'owner': r['owner']})
    print(f'{a.subsystem}: owner {r["owner"]}')


def cmd_note(a):
    p = os.path.join(DB, 'subsystems', f'{a.subsystem}.md')
    if not os.path.exists(p):
        raise SystemExit(f'no {p}')
    with locked():
        txt = open(p).read()
        if '\n## Inbox' not in txt:
            txt = txt.rstrip('\n') + '\n\n## Inbox\n'
        stamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
        txt = txt.rstrip('\n') + f'\n- {stamp} {a.agent}: {a.text}\n'
        tmp = p + f'.{os.getpid()}.tmp'
        open(tmp, 'w').write(txt)
        os.replace(tmp, p)
        journal(a.agent, 'note', 'subsystems', a.subsystem, None, {'text': a.text})
    print(f'noted in {p}')


def cmd_verify(a):
    table = {'func': 'functions', 'global': 'globals', 'db_key': 'db_keys', 'anchor': 'strings_anchors'}.get(a.kind)
    claimed = '-'
    if table:
        rows, _ = read_table(table)
        r = find_row(table, rows, a.item)
        if not r:
            raise SystemExit(f'no {table} row {a.item}')
        claimed = r.get('level', '-')
        who = raiser(table, '|'.join(row_key(table, r) or ()))
        if who == a.agent:
            raise SystemExit(f'refused: {a.agent} raised this item to {claimed}; verification must come from another agent')
    r = {'date': datetime.date.today().isoformat(), 'verifier': a.agent, 'kind': a.kind, 'item': a.item, 'claimed': claimed,
         'verdict': a.verdict, 'evidence': a.evidence, 'notes': a.notes or '-'}
    e = validate_row('verify', r)
    if e:
        raise SystemExit('refused:\n  ' + '\n  '.join(e))
    with locked():
        rows, _ = read_table('verify')
        n = len(rows) + 1
        r['notes'] = f'V{n:04d} ' + r['notes']
        rows.append(r)
        cols = TABLES['verify'][0]
        p = path('verify')
        tmp = f'{p}.{os.getpid()}.tmp'
        with open(tmp, 'w') as f:
            f.write('\t'.join(cols) + '\n')
            for x in rows:
                f.write('\t'.join(x[c] for c in cols) + '\n')
        os.replace(tmp, p)
        journal(a.agent, 'verify', a.kind, a.item, None, {c: r[c] for c in cols})
    print(f'V{n:04d} recorded ({a.verdict}); on disagree, demote the item and add DISPUTE(V{n:04d}) to its notes')


def cmd_test_new(a):
    tdir = os.path.join(RE, 'tests')
    with locked():
        nums = [int(f[1:4]) for f in os.listdir(tdir) if re.match(r'^T\d{3}\.md$', f)]
        n = max(nums, default=-1) + 1
        tid = f'T{n:03d}'
        tpl = open(os.path.join(tdir, 'TEMPLATE.md')).read()
        txt = tpl.replace('T###', tid).replace('<question>', a.question).replace('<agent>', a.agent).replace('<date>', datetime.date.today().isoformat())
        p = os.path.join(tdir, f'{tid}.md')
        open(p, 'x').write(txt)
        journal(a.agent, 'test-new', 'tests', tid, None, {'question': a.question})
    print(p)


def cmd_import(a):
    cols, _ = TABLES[a.table]
    added = skipped = 0
    with locked():
        rows, _ = read_table(a.table)
        for line in open(a.file):
            c = line.rstrip('\n').split('\t')
            if c == cols or len(c) != len(cols):
                continue
            r = dict(zip(cols, c))
            if validate_row(a.table, r) or uniqueness(a.table, rows, r):
                skipped += 1
                continue
            rows.append(r)
            added += 1
            journal(a.agent, 'import', a.table, '|'.join(row_key(a.table, r) or ()), None, {k: r[k] for k in cols})
        write_table(a.table, rows)
    print(f'imported {added}, skipped {skipped} (invalid or already present; fix and add them by hand)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--agent', default=os.environ.get('RE_AGENT'))
    ap.add_argument('--force', action='store_true', help='lead only: bypass ownership checks')
    sp = ap.add_subparsers(dest='cmd', required=True)
    p = sp.add_parser('add'); p.add_argument('table', choices=[t for t in TABLES if t != 'verify']); p.add_argument('assign', nargs='+')
    p = sp.add_parser('set'); p.add_argument('table', choices=[t for t in TABLES if t != 'verify']); p.add_argument('key'); p.add_argument('assign', nargs='+')
    p = sp.add_parser('get'); p.add_argument('table', choices=list(TABLES)); p.add_argument('key')
    p = sp.add_parser('find'); p.add_argument('regex')
    p = sp.add_parser('claim'); p.add_argument('subsystem')
    p = sp.add_parser('release'); p.add_argument('subsystem')
    p = sp.add_parser('note'); p.add_argument('subsystem'); p.add_argument('text')
    p = sp.add_parser('verify'); p.add_argument('kind'); p.add_argument('item'); p.add_argument('verdict'); p.add_argument('evidence'); p.add_argument('notes', nargs='?')
    p = sp.add_parser('test-new'); p.add_argument('question')
    p = sp.add_parser('import'); p.add_argument('file'); p.add_argument('table', choices=['functions', 'globals'])
    a = ap.parse_args()
    if a.cmd not in ('get', 'find') and not a.agent:
        raise SystemExit('--agent NAME (or RE_AGENT) is required for writes')
    {'add': cmd_add, 'set': cmd_set, 'get': cmd_get, 'find': cmd_find, 'claim': cmd_claim,
     'release': lambda x: cmd_claim(x, True), 'note': cmd_note, 'verify': cmd_verify, 'test-new': cmd_test_new,
     'import': cmd_import}[a.cmd](a)


if __name__ == '__main__':
    main()
