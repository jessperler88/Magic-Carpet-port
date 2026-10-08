"""VGA palettes: 256 x (r,g,b) with 6-bit components (0..63)."""
from __future__ import annotations

from pathlib import Path

from . import rnc


def load_palette(data: bytes) -> list[tuple[int, int, int]]:
    data = rnc.unpack_if_rnc(data)
    if len(data) < 768:
        raise ValueError(f"palette too short: {len(data)}")
    return [(data[i] << 2 | data[i] >> 4, data[i + 1] << 2 | data[i + 1] >> 4,
             data[i + 2] << 2 | data[i + 2] >> 4) for i in range(0, 768, 3)]


def palette_png(pal: list[tuple[int, int, int]], path: str | Path, cell: int = 16):
    from PIL import Image
    img = Image.new("RGB", (16 * cell, 16 * cell))
    px = img.load()
    for i, (r, g, b) in enumerate(pal):
        cx, cy = (i % 16) * cell, (i // 16) * cell
        for y in range(cell):
            for x in range(cell):
                px[cx + x, cy + y] = (r, g, b)
    img.save(path)


def indexed_png(pixels: bytes, width: int, height: int, pal, path: str | Path, transparent0: bool = True):
    from PIL import Image
    img = Image.frombytes("P", (width, height), bytes(pixels[: width * height]).ljust(width * height, b"\0"))
    flat = []
    for r, g, b in pal:
        flat += [r, g, b]
    img.putpalette(flat)
    if transparent0:
        img.info["transparency"] = 0
        img.save(path, transparency=0)
    else:
        img.save(path)
