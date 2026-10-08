"""Linear Executable (LE) parser for DOS/4GW-bound Watcom programs.

Parses the MZ stub + LE header, object table, object page map, fixup page
table and fixup records, and can produce:

  * a JSON memory map (object bases, sizes, flags, entry point) for syncing
    addresses between Ghidra, DOSBox debugger and our own tools;
  * a flat, relocated memory image (every object placed at its default base)
    for byte-level searches, table extraction and emulation experiments.

Reference: Open Watcom exeflat.h, moddingwiki "Linear Executable (LX/LE) Format".
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field, asdict
from pathlib import Path

LE_SIG = b"LE\x00\x00"

OBJ_READ = 0x0001
OBJ_WRITE = 0x0002
OBJ_EXEC = 0x0004
OBJ_RESOURCE = 0x0008
OBJ_DISCARD = 0x0010
OBJ_SHARED = 0x0020
OBJ_PRELOAD = 0x0040
OBJ_INVALID = 0x0080
OBJ_BIG = 0x2000  # 32-bit segment


@dataclass
class LEObject:
    index: int  # 1-based
    virtual_size: int
    base: int
    flags: int
    page_map_index: int
    page_count: int

    @property
    def is_code(self) -> bool:
        return bool(self.flags & OBJ_EXEC)

    def flag_names(self) -> list[str]:
        names = []
        for bit, n in [(OBJ_READ, "R"), (OBJ_WRITE, "W"), (OBJ_EXEC, "X"), (OBJ_RESOURCE, "res"),
                       (OBJ_DISCARD, "discard"), (OBJ_SHARED, "shared"), (OBJ_PRELOAD, "preload"),
                       (OBJ_BIG, "32bit")]:
            if self.flags & bit:
                names.append(n)
        return names


@dataclass
class Fixup:
    page: int  # 0-based page index in which the fixup source lives
    src_type: int
    flags: int
    src_off: int  # offset within page (signed 16)
    target_obj: int  # 1-based
    target_off: int
    addr: int  # absolute linear address of the fixup source

    @property
    def is_32bit_offset(self) -> bool:
        return (self.src_type & 0x0F) == 0x07


@dataclass
class LEFile:
    path: str
    le_offset: int
    page_size: int
    page_count: int
    last_page_size: int
    entry_obj: int
    entry_eip: int
    stack_obj: int
    stack_esp: int
    data_pages_offset: int
    inner_mz_offset: int = 0
    objects: list[LEObject] = field(default_factory=list)
    fixups: list[Fixup] = field(default_factory=list)

    @property
    def entry_address(self) -> int:
        return self.objects[self.entry_obj - 1].base + self.entry_eip

    @property
    def initial_esp(self) -> int:
        return self.objects[self.stack_obj - 1].base + self.stack_esp

    # ------------------------------------------------------------------
    @classmethod
    def parse(cls, path: str | Path) -> "LEFile":
        data = Path(path).read_bytes()
        le = data.find(LE_SIG)
        if le < 0:
            raise ValueError("no LE header found")
        h = memoryview(data)[le:]
        u32 = lambda off: struct.unpack_from("<I", h, off)[0]
        page_count = u32(0x14)
        obj = cls(
            path=str(path), le_offset=le,
            page_size=u32(0x28), page_count=page_count, last_page_size=u32(0x2C),
            entry_obj=u32(0x18), entry_eip=u32(0x1C), stack_obj=u32(0x20), stack_esp=u32(0x24),
            data_pages_offset=u32(0x80),  # relative to file start
        )
        # Bound DOS/4GW executables are <stub MZ exe><original LE file>; the original
        # file keeps its own small MZ header whose e_lfanew points at the LE header.
        # File-relative fields in the LE header (data pages offset) are relative to
        # that inner MZ header, not to the start of the bound file.
        inner = 0
        pos = le
        while True:
            pos = data.rfind(b"MZ", 0, pos)
            if pos < 0:
                break
            if pos + 0x40 <= len(data) and struct.unpack_from("<I", data, pos + 0x3C)[0] == le - pos:
                inner = pos
                break
        obj.inner_mz_offset = inner
        obj.data_pages_offset += inner
        obj_tab = u32(0x40)
        obj_cnt = u32(0x44)
        obj_pagemap = u32(0x48)
        fixup_page_tab = u32(0x68)
        fixup_rec_tab = u32(0x6C)
        for i in range(obj_cnt):
            vs, base, flags, pmi, pc, _ = struct.unpack_from("<IIIIII", h, obj_tab + i * 24)
            obj.objects.append(LEObject(i + 1, vs, base, flags, pmi, pc))
        obj._data = data  # type: ignore[attr-defined]
        obj._obj_pagemap = obj_pagemap  # type: ignore[attr-defined]
        obj._parse_fixups(h, fixup_page_tab, fixup_rec_tab)
        return obj

    # ------------------------------------------------------------------
    def _page_file_offset(self, page_index0: int) -> int:
        """LE page map: each entry is 4 bytes (high24 = page number BE-ish, flags).
        For DOS/4GW LE files pages are stored sequentially; page n (0-based) lives at
        data_pages_offset + n*page_size.  We honour the map's page number anyway."""
        h = memoryview(self._data)[self.le_offset:]  # type: ignore[attr-defined]
        e = self._obj_pagemap + page_index0 * 4  # type: ignore[attr-defined]
        b0, b1, b2, flags = h[e], h[e + 1], h[e + 2], h[e + 3]
        page_no = (b0 << 16) | (b1 << 8) | b2  # big-endian 24-bit, 1-based
        return self.data_pages_offset + (page_no - 1) * self.page_size

    def _page_linear_address(self, page_index0: int) -> int:
        for o in self.objects:
            first = o.page_map_index - 1
            if first <= page_index0 < first + o.page_count:
                return o.base + (page_index0 - first) * self.page_size
        raise ValueError(f"page {page_index0} not owned by any object")

    def _parse_fixups(self, h: memoryview, fixup_page_tab: int, fixup_rec_tab: int):
        # fixup page table: page_count+1 u32 offsets into fixup record table
        starts = [struct.unpack_from("<I", h, fixup_page_tab + i * 4)[0] for i in range(self.page_count + 1)]
        for page in range(self.page_count):
            p = fixup_rec_tab + starts[page]
            end = fixup_rec_tab + starts[page + 1]
            try:
                page_la = self._page_linear_address(page)
            except ValueError:
                continue
            while p < end:
                src_type = h[p]
                flags = h[p + 1]
                p += 2
                src_list = src_type & 0x20
                if src_list:
                    cnt = h[p]
                    p += 1
                    src_offs = None
                else:
                    (src_off,) = struct.unpack_from("<h", h, p)
                    p += 2
                    src_offs = [src_off]
                    cnt = 1
                target_kind = flags & 0x03
                if target_kind != 0:  # only internal refs expected in DOS/4GW files
                    raise ValueError(f"unsupported fixup target kind {target_kind} at page {page}")
                if flags & 0x40:  # 16-bit object number
                    (tobj,) = struct.unpack_from("<H", h, p)
                    p += 2
                else:
                    tobj = h[p]
                    p += 1
                stype = src_type & 0x0F
                if stype == 0x02:  # 16-bit selector fixup: no target offset
                    toff = 0
                else:
                    if flags & 0x10:  # 32-bit target offset
                        (toff,) = struct.unpack_from("<I", h, p)
                        p += 4
                    else:
                        (toff,) = struct.unpack_from("<H", h, p)
                        p += 2
                if src_list:
                    src_offs = [struct.unpack_from("<h", h, p + i * 2)[0] for i in range(cnt)]
                    p += cnt * 2
                for so in src_offs:  # type: ignore[union-attr]
                    self.fixups.append(Fixup(page, src_type, flags, so, tobj, toff, page_la + so))

    # ------------------------------------------------------------------
    def object_bytes(self, o: LEObject) -> bytearray:
        buf = bytearray(o.virtual_size)
        for k in range(o.page_count):
            page = o.page_map_index - 1 + k
            foff = self._page_file_offset(page)
            size = self.last_page_size if page == self.page_count - 1 else self.page_size
            chunk = self._data[foff:foff + size]  # type: ignore[attr-defined]
            dst = k * self.page_size
            buf[dst:dst + len(chunk)] = chunk
        return buf

    def flat_image(self, apply_fixups: bool = True) -> tuple[int, bytearray]:
        """Return (base, image) covering from the lowest object base to the
        highest object end, with objects placed at their default bases."""
        lo = min(o.base for o in self.objects)
        hi = max(o.base + o.virtual_size for o in self.objects)
        img = bytearray(hi - lo)
        for o in self.objects:
            b = self.object_bytes(o)
            img[o.base - lo:o.base - lo + len(b)] = b
        if apply_fixups:
            for f in self.fixups:
                # target offsets can be 'negative' (pointer to before an object,
                # e.g. a biased array base) so always wrap to 32 bits
                tgt = (self.objects[f.target_obj - 1].base + f.target_off) & 0xFFFFFFFF
                i = f.addr - lo
                st = f.src_type & 0x0F
                if st == 0x07:  # 32-bit offset
                    if f.src_type & 0x10:  # additive: add to existing value
                        cur = struct.unpack_from("<I", img, i)[0]
                        struct.pack_into("<I", img, i, (cur + tgt) & 0xFFFFFFFF)
                    else:
                        struct.pack_into("<I", img, i, tgt)
                elif st == 0x05:  # 16-bit offset
                    struct.pack_into("<H", img, i, tgt & 0xFFFF)
                elif st == 0x02:  # 16-bit selector: leave (flat model)
                    pass
                elif st == 0x06:  # 16:32 pointer: offset part only
                    struct.pack_into("<I", img, i, tgt)
                elif st == 0x08:  # 32-bit self-relative
                    struct.pack_into("<I", img, i, (tgt - (f.addr + 4)) & 0xFFFFFFFF)
        return lo, img

    def memory_map(self) -> dict:
        return {
            "file": self.path,
            "le_header_offset": self.le_offset,
            "inner_mz_offset": self.inner_mz_offset,
            "data_pages_offset": self.data_pages_offset,
            "page_size": self.page_size,
            "page_count": self.page_count,
            "entry": f"{self.entry_address:#010x}",
            "initial_esp": f"{self.initial_esp:#010x}",
            "fixup_count": len(self.fixups),
            "objects": [
                {"index": o.index, "base": f"{o.base:#010x}", "end": f"{o.base + o.virtual_size:#010x}",
                 "size": o.virtual_size, "flags": o.flag_names(), "pages": o.page_count}
                for o in self.objects
            ],
        }


def main(argv: list[str] | None = None):
    import argparse

    ap = argparse.ArgumentParser(description="Dump a DOS/4GW LE executable")
    ap.add_argument("exe")
    ap.add_argument("--flat", help="write flat relocated image to this file")
    ap.add_argument("--json", help="write memory map JSON to this file")
    ap.add_argument("--fixups", help="write fixup list (csv) to this file")
    a = ap.parse_args(argv)
    le = LEFile.parse(a.exe)
    mm = le.memory_map()
    print(json.dumps(mm, indent=2))
    if a.json:
        Path(a.json).write_text(json.dumps(mm, indent=2))
    if a.flat:
        base, img = le.flat_image()
        Path(a.flat).write_bytes(img)
        print(f"flat image: base={base:#x} size={len(img)} -> {a.flat}")
    if a.fixups:
        with open(a.fixups, "w") as f:
            f.write("addr,src_type,flags,target_obj,target_off,target_addr\n")
            for fx in le.fixups:
                tgt = (le.objects[fx.target_obj - 1].base + fx.target_off) & 0xFFFFFFFF
                f.write(f"{fx.addr:#010x},{fx.src_type:#04x},{fx.flags:#04x},{fx.target_obj},{fx.target_off:#x},{tgt:#010x}\n")


if __name__ == "__main__":
    main()
