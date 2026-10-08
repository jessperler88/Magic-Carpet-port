import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
import sys, csv, struct
sys.path.insert(0, (_REPO + "/tools"))
from mctools.lefile import LEFile
le = LEFile.parse((_REPO + "/MagicCarpet/magic/carpet.exe"))
base, img = le.flat_image(True)
funcs = {int(r['address'],16): r for r in csv.DictReader(open((_REPO + "/ghidra/export/carpet_functions.csv")))}
# fixups whose source is in data object (>=0x90000) and target is a function start
code_lo, code_hi = 0x10000, 0x7c000
hits = []
for f in le.fixups:
    if not f.is_32bit_offset: continue
    if f.addr < 0x90000: continue
    off = f.addr - base
    val = struct.unpack_from('<I', img, off)[0]
    if code_lo <= val < code_hi:
        hits.append((f.addr, val))
hits.sort()
print("data->code pointers:", len(hits))
# group into tables: consecutive 4-byte stride
tables=[]; cur=[hits[0]]
for h in hits[1:]:
    if h[0] == cur[-1][0] + 4: cur.append(h)
    else: tables.append(cur); cur=[h]
tables.append(cur)
for t in tables:
    if len(t) < 2: continue
    nf = sum(1 for a,v in t if v in funcs)
    print(f"\n== table at {t[0][0]:06x}, {len(t)} entries, {nf} are function starts")
    for a,v in t:
        r = funcs.get(v)
        print(f"  {a:06x}: {v:06x} {'FUN size='+r['size']+' callers='+r['callers'] if r else 'NOT A FUNC START'}")
singles=[t[0] for t in tables if len(t)==1]
print("\nsingle pointers:", len(singles))
for a,v in singles:
    r=funcs.get(v); print(f"  {a:06x}: {v:06x} {'size='+r['size']+' callers='+r['callers'] if r else 'mid-func'}")
