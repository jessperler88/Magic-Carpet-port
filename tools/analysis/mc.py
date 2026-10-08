import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
import csv, re, sys, collections
E = (_REPO + "/ghidra/export/")
funcs = {r['address'].lstrip('0').lower(): r for r in csv.DictReader(open(E+'carpet_functions.csv'))}
names = {}
for r in csv.reader(open((_REPO + "/ghidra/names/carpet_names.csv"))):
    if r and r[0].startswith('0x'): names[r[0][2:].lstrip('0').lower()] = r[1]
calls = collections.defaultdict(list); callers = collections.defaultdict(list)
for r in csv.DictReader(open(E+'carpet_calls.csv')):
    a, b = r['caller'].lstrip('0').lower(), r['callee'].lstrip('0').lower()
    calls[a].append(b); callers[b].append(a)
strings = list(csv.DictReader(open(E+'carpet_strings.csv')))
def norm(a): return a.lower().replace('0x','').replace('fun_','').lstrip('0')
def nm(a):
    a = norm(a); f = funcs.get(a)
    return f"{names.get(a, 'FUN_'+a.zfill(8))}[{f['size'] if f else '?'}]"
src = open(E+'carpet_all.c', encoding='utf-8', errors='replace').read()
def body(a):
    a = norm(a).zfill(8)
    m = re.search(r'// ==== %s .*?(?=\n// ==== |\Z)' % a, src, re.S)
    return m.group(0) if m else None
def tree(a, depth=2, seen=None, ind=0):
    seen = seen if seen is not None else set(); a = norm(a)
    print('  '*ind + nm(a) + (' *' if a in seen else '') + f"  <-{len(set(callers[a]))}")
    if a in seen or depth == 0: return
    seen.add(a)
    for c in sorted(set(calls[a]), key=lambda c: -int(funcs[c]['size']) if c in funcs else 0):
        tree(c, depth-1, seen, ind+1)
def strs(a):
    a = norm(a).zfill(8)
    for s in strings:
        if a in s['xrefs'].split(): print(repr(s['string']))
def reach(a):
    a = norm(a); seen=set(); st=[a]
    while st:
        x=st.pop()
        if x in seen: continue
        seen.add(x); st.extend(calls[x])
    return seen
if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'tree': tree(sys.argv[2], int(sys.argv[3]) if len(sys.argv)>3 else 2)
    elif cmd == 'body': print(body(sys.argv[2]))
    elif cmd == 'strs': strs(sys.argv[2])
    elif cmd == 'callers':
        for c in sorted(set(callers[norm(sys.argv[2])])): print(nm(c))
    elif cmd == 'reach':
        r = reach(sys.argv[2]); print(len(r), sum(int(funcs[x]['size']) for x in r if x in funcs))
