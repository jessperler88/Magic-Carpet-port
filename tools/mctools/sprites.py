"""Bullfrog span-encoded sprites (hspr0-0, mspr0-0, fonts, pointers, building,
screens/*spr).

Each .tab entry (see dattab.py) gives offset/width/height.  Sprite data is a
sequence of rows; each row is a list of runs terminated by a 0 byte:

    b = int8 at cursor
    b > 0  : copy b literal pixels
    b < 0  : skip -b pixels (transparent)
    b == 0 : end of row

This is the same encoding used by Syndicate Wars and Dungeon Keeper.  The
decoder is tolerant: it stops at height rows or when the slice is exhausted.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from . import dattab, palette


@dataclass
class Sprite:
    index: int
    width: int
    height: int
    pixels: bytes  # width*height, 0 = transparent
    consumed: int  # bytes of source consumed


def decode_span_sprite(data: bytes, start: int, width: int, height: int) -> Sprite | None:
    px = bytearray(width * height)
    p = start
    n = len(data)
    for y in range(height):
        x = 0
        while True:
            if p >= n:
                return Sprite(-1, width, height, bytes(px), p - start)
            b = data[p]
            p += 1
            if b == 0:
                break
            if b < 128:
                cnt = b
                row = data[p:p + cnt]
                p += cnt
                for i, v in enumerate(row):
                    if x + i < width:
                        px[y * width + x + i] = v
                x += cnt
            else:
                x += 256 - b
    return Sprite(-1, width, height, bytes(px), p - start)


def decode_all(dat: bytes, entries: list[dattab.TabEntry]) -> list[Sprite]:
    out = []
    for e in entries:
        if e.empty:
            continue
        s = decode_span_sprite(dat, e.offset, e.width, e.height)
        if s:
            s.index = e.index
            out.append(s)
    return out


def sheet(sprites: list[Sprite], pal, path: str | Path, cols: int = 16, pad: int = 1, bg: int = 255):
    """Pack sprites into a grid PNG (indexed colour, bg index as background)."""
    from PIL import Image
    if not sprites:
        return
    cw = max(s.width for s in sprites) + pad
    ch = max(s.height for s in sprites) + pad
    rows = (len(sprites) + cols - 1) // cols
    img = Image.new("P", (cols * cw, rows * ch), bg)
    flat = []
    for r, g, b in pal:
        flat += [r, g, b]
    img.putpalette(flat)
    for i, s in enumerate(sprites):
        tile = Image.frombytes("P", (s.width, s.height), s.pixels)
        mask = Image.frombytes("L", (s.width, s.height), bytes(255 if v else 0 for v in s.pixels))
        img.paste(tile, ((i % cols) * cw, (i // cols) * ch), mask)
    img.save(path)


def main():
    import sys
    game = Path(sys.argv[1] if len(sys.argv) > 1 else r"C:\Magic Carpet\MagicCarpet\magic")
    outdir = Path(sys.argv[2] if len(sys.argv) > 2 else r"C:\Magic Carpet\extracted\sprites")
    outdir.mkdir(parents=True, exist_ok=True)
    pal = palette.load_palette((game / "data/palette.dat").read_bytes())
    sets = ["data/hspr0-0", "data/mspr0-0", "data/font0", "data/font1", "data/font2", "data/pointers",
            "data/building", "data/screens/mmspr", "data/screens/sfont0", "data/screens/sfont1",
            "data/screens/sfont2", "data/screens/confspr", "data/screens/sptrs", "data/screens/gcspr",
            "data/screens/pmultspr", "data/screens/langspr"]
    for name in sets:
        dat, ents = dattab.load_dat_tab(game / (name + ".dat"))
        sprs = decode_all(dat, ents)
        slices = dattab.tab_slices(ents, len(dat))
        exact = 0
        for s, e in zip(sprs, [e for e in ents if not e.empty]):
            a, b = slices[e.index]
            if s.consumed == b - a:
                exact += 1
        sheet(sprs, pal, outdir / (name.replace("/", "_") + ".png"))
        print(f"{name:24s} {len(sprs):4d} sprites, {exact:4d} consumed exactly their slice")


if __name__ == "__main__":
    main()
