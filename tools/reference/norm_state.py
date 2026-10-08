"""Compare GameState dumps of different executables (1996 carpet.exe, 1995 CARPET.EXE / HIDDEN.EXE).

    python norm_state.py A.gam B.gam [--map-a A.map] [--map-b B.map] [diff_state options]

The raw dumps hold the original's pointers (Thing.next / desc / player, the free and recyclable
stacks), whose values depend on where the exe's data and heap lie, so dumps of two different
executables never compare byte for byte. Both files are converted to the port's index form exactly
as thing_relink_snapshot (src/mcengine/thing.cpp) does it, cut to the 1996 size 0x38d03 (a 1995 dump
is 0x38d09 bytes: 6 more bytes after spells_present, printed separately), optionally followed by the
first 0x60000 bytes of a terrain dump (type, height, light, flags, u16 cell heads), and handed to
diff_state.py, which names every differing field.
"""
from __future__ import annotations

import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

STATE_96 = 0x38D03
FREE_TOP, FREE_LIST, ACTIVE_TOP, ACTIVE_LIST = 0x28, 0x251, 0x11F1, 0x11F5
PLAYERS, PLAYER_SIZE, P_BLOCK, COMMANDS = 0x340B, 0x801, 0x44F, 0x7413
THINGS, THING_SIZE, SLOTS = 0x7463, 0xA4, 1000
T_NEXT, T_DESC, T_PLAYER, T_CLS = 0x00, 0x9C, 0xA0, 0x40
MOVE_DESC_SIZE = None   # taken from the anchor rule below


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def put(b, o, v):
    struct.pack_into("<I", b, o, v & 0xFFFFFFFF)


def normalise(blob: bytes, move_desc_size: int = 0x20) -> tuple[bytearray, bytes]:
    """-> (0x38d03 bytes in index form, extra tail bytes). Mirrors thing_relink_snapshot."""
    s = bytearray(blob[:STATE_96])
    tail = bytes(blob[STATE_96:])
    top = struct.unpack_from("<i", s, FREE_TOP)[0]
    p0_thing = struct.unpack_from("<H", s, PLAYERS + 0xA)[0] % SLOTS
    if top >= 0:
        max_ptr = max(u32(s, FREE_LIST + 4 * i) for i in range(top + 1))
        max_idx = max(i for i in range(1, SLOTS) if s[THINGS + i * THING_SIZE + T_CLS] == 0)
        things_base = max_ptr - max_idx * THING_SIZE
    else:
        pp = u32(s, THINGS + p0_thing * THING_SIZE + T_PLAYER)
        things_base = pp - (PLAYERS + P_BLOCK) + THINGS
    state_base = things_base - THINGS

    def to_index(p):
        if p < things_base:
            return 0
        d = p - things_base
        return d // THING_SIZE if d % THING_SIZE == 0 and d // THING_SIZE < SLOTS else 0

    for i in range(top + 1):
        put(s, FREE_LIST + 4 * i, to_index(u32(s, FREE_LIST + 4 * i)))
    atop = struct.unpack_from("<i", s, ACTIVE_TOP)[0]
    for i in range(min(atop + 1, SLOTS)):
        put(s, ACTIVE_LIST + 4 * i, to_index(u32(s, ACTIVE_LIST + 4 * i)))
    anchor = u32(s, THINGS + p0_thing * THING_SIZE + T_DESC)
    lo, hi = state_base + PLAYERS, state_base + COMMANDS
    for i in range(SLOTS):
        b = THINGS + i * THING_SIZE
        if i and s[b + T_CLS] == 0:
            for o in (T_NEXT, T_DESC, T_PLAYER):
                put(s, b + o, 0)
            continue
        put(s, b + T_NEXT, to_index(u32(s, b + T_NEXT)))
        d = (u32(s, b + T_DESC) - anchor + 7 * move_desc_size) & 0xFFFFFFFF
        put(s, b + T_DESC, d // move_desc_size if d % move_desc_size == 0 and d // move_desc_size < 30 else 0)
        p = u32(s, b + T_PLAYER)
        put(s, b + T_PLAYER, p - state_base if lo <= p < hi else 0)
    return s, tail


def move_desc_size() -> int:
    """sizeof(MoveDesc) from mc_types.h (static_assert), default 0x1f."""
    import re
    src = (HERE.parents[1] / "src" / "mcengine" / "mc_types.h").read_text(errors="replace")
    m = re.search(r"static_assert\(\s*sizeof\(MoveDesc\)\s*==\s*(0x[0-9a-fA-F]+|\d+)", src)
    return int(m.group(1), 0) if m else 0x20


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--map-a")
    ap.add_argument("--map-b")
    a, rest = ap.parse_known_args(argv)
    mds = move_desc_size()
    files = []
    tmp = Path(tempfile.mkdtemp(prefix="norm_state_"))
    for name, path, mp in (("a", a.a, a.map_a), ("b", a.b, a.map_b)):
        s, tail = normalise(Path(path).read_bytes(), mds)
        print(f"{name}: {path}: {len(s) + len(tail):#x} bytes" + (f", tail {tail.hex()}" if tail else ""))
        if mp:
            s += Path(mp).read_bytes()[:0x60000]
        out = tmp / f"{name}.bin"
        out.write_bytes(s)
        files.append(str(out))
    import diff_state
    return diff_state.main(files + rest)


if __name__ == "__main__":
    sys.exit(main())
