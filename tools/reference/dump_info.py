"""Inspect the per-tick reference dumps (tick%05d.gam) without the port.

    python dump_info.py [--dir extracted/reference/movie0] [--ticks 413,500,587] [--diff A B] [--census]

Default: one line per dump with the local player's tick, status, position of every player's Thing,
live-Thing count per class and the RNG word. --diff A B lists the byte ranges that differ between two
dumps (header, player records, per Thing slot). Thing pointers are left as the original's pointers
(the C++ reference_test converts them); this script only reads fields that are indices or plain values.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE_SIZE = 0x38D03
PLAYERS = 0x340B
REC = 0x801
THINGS = 0x7463
THING = 0xA4
CLASSES = ["-", "c1", "scenery", "player", "c4", "creature", "c6", "weather", "c8", "projectile",
           "effect", "switch", "spell", "c13"]


def thing(blob: bytes, i: int) -> bytes:
    return blob[THINGS + i * THING:THINGS + (i + 1) * THING]


def summary(blob: bytes) -> dict:
    local = struct.unpack_from("<h", blob, 8)[0]
    nplayers = struct.unpack_from("<h", blob, 0xA)[0]
    rec = PLAYERS + local * REC
    tick = struct.unpack_from("<I", blob, rec + 0x12)[0]
    status = struct.unpack_from("<H", blob, rec + 2)[0]
    rng = struct.unpack_from("<I", blob, 4)[0]
    census = {}
    for i in range(1, 1000):
        cls = thing(blob, i)[0x40]
        if cls:
            name = CLASSES[cls] if cls < len(CLASSES) else str(cls)
            census[name] = census.get(name, 0) + 1
    players = []
    for p in range(min(nplayers, 8)):
        r = PLAYERS + p * REC
        ti = struct.unpack_from("<H", blob, r + 0xA)[0]
        t = thing(blob, ti) if 0 < ti < 1000 else None
        if t:
            x, y, z = struct.unpack_from("<HHh", t, 0x48)
            health = struct.unpack_from("<i", t, 0x2c)[0]
            players.append((p, ti, x >> 8, y >> 8, z, t[0x46], health))
    return {"tick": tick, "status": status, "rng": rng, "census": census, "players": players,
            "free_top": struct.unpack_from("<i", blob, 0x28)[0]}


def diff(a: bytes, b: bytes):
    print(f"header 0..{PLAYERS:#x}:")
    for off in range(0, PLAYERS):
        if a[off] != b[off]:
            print(f"  {off:#07x}: {a[off]:02x} -> {b[off]:02x}")
    for p in range(8):
        r = PLAYERS + p * REC
        d = [o for o in range(REC) if a[r + o] != b[r + o]]
        if d:
            print(f"player {p}: {len(d)} bytes differ, first at rec+{d[0]:#x}, last at rec+{d[-1]:#x}")
    n = 0
    for i in range(1, 1000):
        ta, tb = thing(a, i), thing(b, i)
        if ta != tb:
            d = [o for o in range(THING) if ta[o] != tb[o]]
            n += 1
            if n <= 60:
                print(f"thing {i:3d} cls {ta[0x40]}/{tb[0x40]} type {ta[0x41]}/{tb[0x41]} state {ta[0x46]}/{tb[0x46]}: "
                      f"{len(d)} bytes at " + ",".join(f"{o:#x}" for o in d[:12]) + (" ..." if len(d) > 12 else ""))
    print(f"{n} thing slots differ")
    tail = [o for o in range(THINGS + 1000 * THING, STATE_SIZE) if a[o] != b[o]]
    print(f"level block / tail: {len(tail)} bytes differ" + (f", first {tail[0]:#x}" if tail else ""))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=str(ROOT / "extracted" / "reference" / "movie0"))
    ap.add_argument("--ticks", default=None, help="comma-separated tick numbers (default: all)")
    ap.add_argument("--diff", nargs=2, type=int, metavar=("A", "B"))
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    d = Path(a.dir)
    if a.diff:
        diff((d / f"tick{a.diff[0]:05d}.gam").read_bytes(), (d / f"tick{a.diff[1]:05d}.gam").read_bytes())
        return
    files = sorted(d.glob("tick*.gam"))
    if a.ticks:
        want = {int(t) for t in a.ticks.split(",")}
        files = [f for f in files if int(f.stem[4:]) in want]
    for f in files:
        blob = f.read_bytes()
        if len(blob) != STATE_SIZE:
            print(f"{f.name}: bad size {len(blob)}")
            continue
        s = summary(blob)
        if a.json:
            print(json.dumps({"file": f.name, **s}))
        else:
            pl = " ".join(f"p{p}:t{ti}@{x},{y},{z}/s{st}/h{h}" for p, ti, x, y, z, st, h in s["players"])
            cen = " ".join(f"{k}={v}" for k, v in sorted(s["census"].items()))
            print(f"{f.name} tick {s['tick']} status {s['status']:#x} rng {s['rng']:08x} free {s['free_top']} | {pl} | {cen}")


if __name__ == "__main__":
    main()
