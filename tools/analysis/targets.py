import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
import sys, struct
sys.path.insert(0, (_REPO + "/tools"))
from mctools.lefile import LEFile
le = LEFile.parse((_REPO + "/MagicCarpet/magic/carpet.exe"))
base, img = le.flat_image(True)
out = open((_REPO + "/ghidra/names/code_ptr_targets.txt"), "w")
out.write("# target,source : code addresses referenced by relocated dwords in the data object (handler tables)\n")
seen = set()
for f in sorted(le.fixups, key=lambda f: f.addr):
    if f.is_32bit_offset and f.addr >= 0x90000:
        v = struct.unpack_from('<I', img, f.addr - base)[0]
        if 0x10000 <= v < 0x7c000 and v not in seen:
            seen.add(v); out.write(f"{v:08x},{f.addr:08x}\n")
out.close(); print("targets written:", len(seen))
