"""Raw access to the relocated carpet.exe image (no Ghidra needed).

    python img.py dis <addr> [end_or_count]   disassemble (capstone x86-32) until end address (>0x1000) or N instrs
    python img.py bytes <addr> <len>          hex dump
    python img.py dwords <addr> <n>           dword table
As a module: IMG (bytearray), BASE, u8/u16/u32(addr), dis(addr, count)
"""
import os, sys, struct, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from mctools.lefile import LEFile
# The retail carpet.exe (MC_CARPET_EXE, default <repo>/MagicCarpet/magic/carpet.exe) and the cache of its
# relocated flat image (MC_EXE_CACHE, default <repo>/extracted/exe/carpet_flat.bin; "" = no cache).
EXE = pathlib.Path(os.environ.get("MC_CARPET_EXE") or ROOT / "MagicCarpet" / "magic" / "carpet.exe")
_cache = os.environ.get("MC_EXE_CACHE")
CACHE = pathlib.Path(_cache) if _cache else (None if _cache == "" else ROOT / "extracted" / "exe" / "carpet_flat.bin")
if CACHE is not None and CACHE.exists() and CACHE.stat().st_mtime >= EXE.stat().st_mtime:
    IMG = bytearray(CACHE.read_bytes()); BASE = 0x10000
else:
    BASE, IMG = LEFile.parse(str(EXE)).flat_image()
    assert BASE == 0x10000, hex(BASE)
    if CACHE is not None:
        CACHE.parent.mkdir(parents=True, exist_ok=True); CACHE.write_bytes(IMG)
END = BASE + len(IMG)
def u8(a): return IMG[a-BASE]
def u16(a): return struct.unpack_from('<H', IMG, a-BASE)[0]
def u32(a): return struct.unpack_from('<I', IMG, a-BASE)[0]
def raw(a, n): return bytes(IMG[a-BASE:a-BASE+n])
_cs = None
def dis(a, count=None, end=None):
    global _cs
    if _cs is None:                      # capstone is only needed for disassembly (not for table generation)
        import capstone
        _cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); _cs.detail = False
    n = (end - a) if end else count * 16
    out = []
    for i in _cs.disasm(raw(a, n), a):
        if end and i.address >= end: break
        out.append(i)
        if count and len(out) >= count: break
    return out
def dis_text(a, count=None, end=None):
    return "\n".join(f"{i.address:06x}  {i.bytes.hex():<16} {i.mnemonic} {i.op_str}" for i in dis(a, count, end))
if __name__ == '__main__':
    c = sys.argv[1]; a = int(sys.argv[2], 16)
    if c == 'dis':
        x = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x20
        print(dis_text(a, end=x) if x > 0x1000 else dis_text(a, count=x))
    elif c == 'bytes': print(raw(a, int(sys.argv[3], 16)).hex(' '))
    elif c == 'dwords':
        for k in range(int(sys.argv[3])): print(f"{a+4*k:06x}: {u32(a+4*k):08x}")
