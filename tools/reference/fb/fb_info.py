"""Inspect the frame dumps of the render reference (fb/patch_carpet.py layout).

    python fb_info.py [--dir extracted/reference/movie0_fb] --ppm TICK[,TICK..] [--out DIR]
    python fb_info.py --same-run extracted/reference/movie0      # gam dumps byte-identical to another set?
    python fb_info.py --summary                                  # palette / Config / anim stats per tick

--ppm writes <out>/ref_<tick>.ppm (the back buffer through the dumped DAC palette, 6-bit -> 8-bit).
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FB = 64000
PAL = 64000
CFG = PAL + 768
ANIM = CFG + 256


def parse(blob: bytes) -> dict:
    # round 6: the frame is width * height bytes (the screen variables at the end of the file)
    w, _, h = struct.unpack_from("<III", blob, len(blob) - 12)
    n = w * h if (w, h) in ((320, 200), (640, 480)) else FB
    d = {"fb": blob[:n], "pal": blob[n:n + 768], "cfg": blob[n + 768:n + 1024], "w": w, "h": h}
    count, recs = struct.unpack_from("<HI", blob, n + 1024)
    o = n + 1024 + 6
    d["anim_count"] = count
    d["anim"] = blob[o:o + count * 0x1C]
    o += count * 0x1C
    d["render_93f40"] = blob[o:o + 0x60]; o += 0x60
    d["render_b5800"] = blob[o:o + 0xC0]; o += 0xC0
    d["screen"] = struct.unpack_from("<III", blob, o) if len(blob) >= o + 12 else None
    return d


def write_ppm(path: Path, fb: bytes, pal: bytes, w=320, h=200):
    rgb = bytearray()
    for i in fb[:w * h]:
        r, g, b = pal[i * 3:i * 3 + 3]
        rgb += bytes(((r & 63) * 255 // 63, (g & 63) * 255 // 63, (b & 63) * 255 // 63))
    path.write_bytes(f"P6\n{w} {h}\n255\n".encode() + bytes(rgb))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=str(ROOT / "extracted" / "reference" / "movie0_fb"))
    ap.add_argument("--ppm", default=None)
    ap.add_argument("--out", default=".")
    ap.add_argument("--same-run", default=None)
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args(argv)
    d = Path(a.dir)
    if a.ppm:
        for t in a.ppm.split(","):
            p = parse((d / f"tick{int(t):05d}.fb").read_bytes())
            out = Path(a.out) / f"ref_{int(t):05d}.ppm"
            write_ppm(out, p["fb"], p["pal"], p["w"], p["h"])
            print("wrote", out, "screen", p["screen"])
    if a.same_run:
        other = Path(a.same_run)
        same = diff = missing = 0
        first_diff = None
        for f in sorted(d.glob("tick*.gam")):
            g = other / f.name
            if not g.exists():
                missing += 1
                continue
            if f.read_bytes() == g.read_bytes():
                same += 1
            else:
                diff += 1
                first_diff = first_diff or f.name
        print(f"gam vs {other}: {same} identical, {diff} differ (first {first_diff}), {missing} not in the other set")
    if a.summary:
        last_pal = None
        for f in sorted(d.glob("tick*.fb")):
            p = parse(f.read_bytes())
            cfg = p["cfg"]
            flags, = struct.unpack_from("<H", cfg, 0)
            tick, = struct.unpack_from("<I", cfg, 4)
            h = hashlib.md5(p["pal"]).hexdigest()[:8]
            active = sum(1 for i in range(p["anim_count"]) if struct.unpack_from("<I", p["anim"], i * 0x1C)[0])
            if h != last_pal or int(f.stem[4:]) % 500 == 0:
                print(f"{f.stem}: cfg.flags {flags:#x} cfg.tick {tick} pal {h} credits {cfg[0xa1:0xa8].hex()} "
                      f"tick_bits {cfg[0x5e:0x61].hex()} pentium {cfg[8]} anim {active}/{p['anim_count']}")
                last_pal = h


if __name__ == "__main__":
    main()
