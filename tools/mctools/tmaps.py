"""data/tmaps.dat + tmaps.tab: the big texture/sprite-frame container.

tmaps.dat = 8-byte tag "BULLFROG" followed by 528 back-to-back RNC method-1
chunks.  tmaps.tab is 530 entries x 10 bytes:

    u32 unpacked_size
    u32 offset            (absolute offset of the RNC header in tmaps.dat)
    u16 group             (observed: 0 .. 528, mostly non-decreasing)

Each decompressed chunk starts with a small header:

    u8  kind              (2 = 8-bit image, 3 = seen on 128x128 chunks)
    u8  unknown
    u16 width
    u16 height
    ... pixel data (width*height bytes for kind 2)
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from . import rnc

TAB_ENTRY = struct.Struct("<IIH")


@dataclass
class TMapEntry:
    index: int
    unpacked_size: int
    offset: int
    group: int


@dataclass
class TMap:
    index: int
    kind: int
    unk: int
    width: int
    height: int
    pixels: bytes
    raw: bytes


def read_tab(tab: bytes) -> list[TMapEntry]:
    n = len(tab) // TAB_ENTRY.size
    return [TMapEntry(i, *TAB_ENTRY.unpack_from(tab, i * TAB_ENTRY.size)) for i in range(n)]


def read_all(dat_path: str | Path, tab_path: str | Path | None = None) -> list[TMap]:
    dat_path = Path(dat_path)
    tab_path = Path(tab_path) if tab_path else dat_path.with_suffix(".tab")
    dat = dat_path.read_bytes()
    out = []
    for e in read_tab(tab_path.read_bytes()):
        if e.unpacked_size == 0:
            continue
        chunk = dat[e.offset:]
        # a few chunks are stored uncompressed when RNC would not help
        blob = rnc.unpack(chunk) if rnc.is_rnc(chunk) else chunk[:e.unpacked_size]
        kind, unk, w, h = struct.unpack_from("<BBHH", blob, 0)
        out.append(TMap(e.index, kind, unk, w, h, blob[6:6 + w * h], blob))
    return out


def main():
    import sys
    from . import palette

    game = Path(sys.argv[1] if len(sys.argv) > 1 else r"C:\Magic Carpet\MagicCarpet\magic")
    outdir = Path(sys.argv[2] if len(sys.argv) > 2 else r"C:\Magic Carpet\extracted\tmaps")
    outdir.mkdir(parents=True, exist_ok=True)
    pal = palette.load_palette((game / "data/palette.dat").read_bytes())
    maps = read_all(game / "data/tmaps.dat")
    kinds = {}
    for m in maps:
        kinds.setdefault(m.kind, 0)
        kinds[m.kind] += 1
        extra = len(m.raw) - 6 - m.width * m.height
        if m.width and m.height:
            palette.indexed_png(m.pixels, m.width, m.height, pal, outdir / f"tmap{m.index:03d}_k{m.kind}_{m.width}x{m.height}.png")
        if extra:
            print(f"tmap {m.index}: kind={m.kind} unk={m.unk} {m.width}x{m.height} extra bytes={extra}")
    print(f"{len(maps)} texture maps, kinds={kinds} -> {outdir}")


if __name__ == "__main__":
    main()
