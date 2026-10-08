"""Bullfrog .dat/.tab containers.

Two distinct uses of the .tab suffix exist in Magic Carpet:

1. Sprite / font / texture tables: an array of 6-byte entries
       u32 offset   absolute offset of the sprite data inside the .dat
       u8  width
       u8  height
   (hspr0-0, mspr0-0, font0..2, pointers, building, screens/*spr, sfont*).
   Pixel data for most sprites is run-length / span encoded; see `sprites.py`.

2. Concatenation archives: levels/levels.tab is an array of u32 offsets into
   levels/levels.dat, one per level (70 used; unused slots repeat the last).

Both .dat and .tab files may themselves be RNC compressed; callers should
pass decompressed bytes (use `rnc.unpack_if_rnc`).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from . import rnc


@dataclass
class TabEntry:
    index: int
    offset: int
    width: int
    height: int

    @property
    def empty(self) -> bool:
        return self.width == 0 or self.height == 0


def read_tab(tab: bytes) -> list[TabEntry]:
    tab = rnc.unpack_if_rnc(tab)
    n = len(tab) // 6
    out = []
    for i in range(n):
        off, w, h = struct.unpack_from("<IBB", tab, i * 6)
        out.append(TabEntry(i, off, w, h))
    return out


def tab_slices(tab_entries: list[TabEntry], dat_len: int) -> list[tuple[int, int]]:
    """(start, end) byte ranges in the .dat for each entry, using the next
    non-zero offset as the end bound."""
    offs = sorted({e.offset for e in tab_entries} | {dat_len})
    nxt = {o: offs[i + 1] for i, o in enumerate(offs[:-1])}
    return [(e.offset, nxt.get(e.offset, dat_len)) for e in tab_entries]


def load_dat_tab(dat_path: str | Path, tab_path: str | Path | None = None):
    dat_path = Path(dat_path)
    if tab_path is None:
        tab_path = dat_path.with_suffix(".tab")
    dat = rnc.unpack_embedded(dat_path.read_bytes())
    entries = read_tab(Path(tab_path).read_bytes())
    return dat, entries


# --- levels.dat / levels.tab ---------------------------------------------------

def read_levels_tab(tab: bytes) -> list[int]:
    n = len(tab) // 4
    return list(struct.unpack_from(f"<{n}I", tab, 0))


def split_levels_dat(dat: bytes, tab: bytes) -> list[bytes]:
    """Return the per-level (still RNC-compressed) blobs from levels.dat."""
    offs = read_levels_tab(tab)
    out = []
    for i, o in enumerate(offs):
        nxt = offs[i + 1] if i + 1 < len(offs) else len(dat)
        if nxt <= o:  # unused trailing slots repeat the last offset
            break
        out.append(dat[o:nxt])
    return out


def build_levels_dat(level_blobs: list[bytes], slots: int = 1000) -> tuple[bytes, bytes]:
    """Inverse of split_levels_dat: concatenate compressed level blobs and build
    a .tab with `slots` u32 entries (the retail file has 1000 = 4000 bytes)."""
    dat = bytearray()
    offs = []
    for b in level_blobs:
        offs.append(len(dat))
        dat += b
    last = len(dat)
    while len(offs) < slots:
        offs.append(last)
    return bytes(dat), struct.pack(f"<{slots}I", *offs)
