"""Inspect the front-end dumps of the original (fe_cave.py layout, run_reference.py --fe).

    python fe_info.py DIR [--png OUTDIR] [--back]

Prints per dump: the front-end state DAT_0012ed2e, the mouse position, g_timer_ticks, DAT_0009e504,
and (with --png) writes fe<N>.png (VRAM through the dumped DAC; --back: the back buffer instead).
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

VRAM, DAC, BACK, G12, G9E, CFG, SCR = 0, 64000, 64768, 128768, 130048, 130816, 131072


def parse(blob: bytes) -> dict:
    g12 = blob[G12:G12 + 0x500]
    g9e = blob[G9E:G9E + 0x300]
    return {
        "vram": blob[VRAM:VRAM + 64000], "dac": blob[DAC:DAC + 768], "back": blob[BACK:BACK + 64000],
        "state": g12[0x12ed2e - 0x12ea00], "timer": struct.unpack_from("<I", g12, 0x12eab4 - 0x12ea00)[0],
        "mouse": struct.unpack_from("<hh", g9e, 0x9e5dc - 0x9e300), "leave": g9e[0x9e504 - 0x9e300],
        "cfg": blob[CFG:CFG + 256], "screen": struct.unpack_from("<III", blob, SCR),
    }


def to_png(path: Path, pix: bytes, dac: bytes):
    from PIL import Image
    im = Image.frombytes("P", (320, 200), pix)
    im.putpalette(bytes((c & 63) * 255 // 63 for c in dac))
    im.convert("RGB").save(path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--png", default=None)
    ap.add_argument("--back", action="store_true")
    a = ap.parse_args(argv)
    for p in sorted(Path(a.dir).glob("fe*.fe")):
        d = parse(p.read_bytes())
        print(f"{p.stem}: state {d['state']} mouse {d['mouse']} timer {d['timer']} leave {d['leave']} screen {d['screen'][0]}x{d['screen'][2]}")
        if a.png:
            Path(a.png).mkdir(parents=True, exist_ok=True)
            to_png(Path(a.png) / f"{p.stem}.png", d["back"] if a.back else d["vram"], d["dac"])


if __name__ == "__main__":
    main()
