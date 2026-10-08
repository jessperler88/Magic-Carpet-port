"""Run the ORIGINAL OPL2 driver code of carpet.exe (snd_opl_*, HMI device 0xa002) in a CPU emulator
(unicorn) and compare its register writes with the port's HmiOplDriver (round 6, task A).

    python opl_orig.py <dir>          (dir = the output directory of opl_test, argv[2])

opl_test writes for every FM song <dir>/music<set>-0-<n>.events (what the sequencer sent: init, banks,
MIDI events, resets) and <dir>/music<set>-0-<n>.regs (the port driver's register writes, 2 bytes each).
This script loads the relocated image (tools/analysis/img.py), sets up a flat GDT, and calls the
original near functions with the original stack arguments:
    snd_opl_driver_init_697e1(-, -, far &port)          fn1
    snd_opl_set_timbre_bank_69401(-, -, far bank)       fn4 (twice: inst.bnk, drum.bnk)
    snd_opl_midi_event_6911c(-, -, far event)           fn0
    snd_opl_reset_state_69319()                         fn3
snd_opl_write_69f9c(reg, val) is intercepted at its first instruction (it would do the port I/O) and
returns at once. The two streams must be identical.
"""
import struct, sys, pathlib
_REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO / "tools" / "analysis"))
import img  # noqa: E402
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_PROT_ALL
from unicorn.x86_const import *

BASE = img.BASE
IMG_SIZE = (len(img.IMG) + 0xfff) & ~0xfff
GDT = 0x00001000
STACK = 0x00800000
DATA = 0x00900000          # bank memory, event buffer, port value
RET = 0x00a00000           # return address (a page with nothing on it)

def gdt_entry(base, limit, access, flags):
    e = limit & 0xffff
    e |= (base & 0xffffff) << 16
    e |= (access & 0xff) << 40
    e |= ((limit >> 16) & 0xf) << 48
    e |= (flags & 0xf) << 52
    e |= ((base >> 24) & 0xff) << 56
    return struct.pack("<Q", e)

class Orig:
    def __init__(self):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0, 0x10000)                                   # GDT page (+ null page)
        uc.mem_map(BASE, IMG_SIZE)
        uc.mem_write(BASE, bytes(img.IMG))
        uc.mem_map(STACK - 0x10000, 0x20000)
        uc.mem_map(DATA, 0x20000)
        uc.mem_map(RET, 0x1000)
        gdt = gdt_entry(0, 0, 0, 0)
        gdt += gdt_entry(0, 0xfffff, 0x80 | 0x60 | 0x10 | 0x8 | 0x2, 0xc)   # 1: code, ring 3
        gdt += gdt_entry(0, 0xfffff, 0x80 | 0x60 | 0x10 | 0x2, 0xc)         # 2: data, ring 3
        gdt += gdt_entry(0, 0xfffff, 0x80 | 0x00 | 0x10 | 0x2, 0xc)         # 3: stack, ring 0
        uc.mem_write(GDT, gdt)
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, len(gdt) - 1, 0))
        self.dsel = (2 << 3) | 3
        uc.reg_write(UC_X86_REG_SS, (3 << 3) | 0)
        uc.reg_write(UC_X86_REG_DS, self.dsel)
        uc.reg_write(UC_X86_REG_ES, self.dsel)
        uc.reg_write(UC_X86_REG_GS, self.dsel)
        uc.reg_write(UC_X86_REG_FS, self.dsel)
        self.uc = uc
        self.writes = []
        uc.hook_add(UC_HOOK_CODE, self._write_hook, begin=0x69f9c, end=0x69f9c)

    def _write_hook(self, uc, addr, size, user):
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret, reg, val = struct.unpack("<III", uc.mem_read(esp, 12))
        self.writes.append((reg & 0xff, val & 0xff))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def call(self, fn, args):
        uc = self.uc
        sp = STACK
        for a in reversed(args):
            sp -= 4
            uc.mem_write(sp, struct.pack("<I", a & 0xffffffff))
        sp -= 4
        uc.mem_write(sp, struct.pack("<I", RET))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.emu_start(fn, RET)

    def init(self, port=0x388):
        self.uc.mem_write(DATA, struct.pack("<I", port))
        self.call(0x697e1, [0, 0, DATA, self.dsel])

    def set_bank(self, data, slot):
        addr = DATA + 0x100 + slot * 0x2000                       # banks stay where they are (the driver keeps pointers)
        self.uc.mem_write(addr, data)
        self.call(0x69401, [0, 0, addr, self.dsel])

    def event(self, st, d1, d2):
        self.uc.mem_write(DATA + 0x8000, bytes([st, d1, d2]))
        self.call(0x6911c, [0, 0, DATA + 0x8000, self.dsel])

    def reset(self):
        self.call(0x69319, [])

def main():
    out = pathlib.Path(sys.argv[1])
    sys.path.insert(0, str(_REPO / "tools"))
    from mctools import rnc
    game = _REPO / "MagicCarpet" / "magic" / "data"
    inst = rnc.unpack_if_rnc((game / "inst.bnk").read_bytes())
    drum = rnc.unpack_if_rnc((game / "drum.bnk").read_bytes())
    total_bad = 0
    for evf in sorted(out.glob("*.events")):
        o = Orig()
        n_ev = 0
        for line in evf.read_text().splitlines():
            p = line.split()
            if not p: continue
            if p[0] == "I": o.init(int(p[1], 16))
            elif p[0] == "B": o.set_bank(inst if p[1] == "inst" else drum, 0 if p[1] == "inst" else 1)
            elif p[0] == "E": o.event(int(p[1], 16), int(p[2], 16), int(p[3], 16)); n_ev += 1
            elif p[0] == "R": o.reset()
        regs = evf.with_suffix(".regs").read_bytes()
        port = [(regs[i], regs[i + 1]) for i in range(0, len(regs), 2)]
        same = port == o.writes
        first = next((i for i in range(min(len(port), len(o.writes))) if port[i] != o.writes[i]), None)
        print(f"{evf.stem}: {n_ev} events, original {len(o.writes)} writes, port {len(port)} writes: "
              f"{'IDENTICAL' if same else 'DIFFER at write %s' % first}")
        if not same:
            total_bad += 1
            i = first if first is not None else min(len(port), len(o.writes))
            for k in range(max(0, i - 5), i + 5):
                a = o.writes[k] if k < len(o.writes) else None
                b = port[k] if k < len(port) else None
                print(f"   {k}: orig {a} port {b}{'  <--' if a != b else ''}")
    print("all identical" if total_bad == 0 else f"{total_bad} song(s) differ")
    return 1 if total_bad else 0

if __name__ == "__main__":
    sys.exit(main())
