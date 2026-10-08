import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
import sys, struct, csv
sys.path.insert(0, (_REPO + "/tools"))
from mctools.lefile import LEFile
from mctools.level import CLASS_NAMES, MODEL_NAMES
le = LEFile.parse((_REPO + "/MagicCarpet/magic/carpet.exe"))
base, img = le.flat_image(True)
u32 = lambda a: struct.unpack_from('<I', img, a-base)[0]
u16 = lambda a: struct.unpack_from('<H', img, a-base)[0]
funcs = {int(r['address'],16): r for r in csv.DictReader(open((_REPO + "/ghidra/export/carpet_functions.csv")))}
CT = 0x943da
for c in range(0, 14):
    r = CT + c*0x12
    ta, tb, par, idx = u32(r), u32(r+4), u32(r+8), u16(r+12)
    print(f"\nclass {c} ({CLASS_NAMES.get(c,'?')}): rec@{r:06x} tableA={ta:06x} tableB={tb:06x} parent={par:06x} idx={idx} extra={u32(r+14):08x}")
    for name, t in (('A', ta), ('B', tb)):
        if not t: continue
        # walk records while parent==0x943cc
        m = 0; rows=[]
        while True:
            a = t + m*14
            if u32(a) != 0x943cc or m > 200: break
            rows.append((m, u16(a+4), u32(a+6), u32(a+10)))
            m += 1
        print(f"  table {name} @{t:06x}: {len(rows)} records")
        for m, midx, h, en in rows:
            fn = funcs.get(h)
            tag = f"size={fn['size']}" if fn else ("null" if h==0 else "NOFUNC")
            print(f"    m{m:3d} idx={midx:3d} handler={h:06x} en={en} {tag:10s} {MODEL_NAMES.get(c,{}).get(m,'')}")
