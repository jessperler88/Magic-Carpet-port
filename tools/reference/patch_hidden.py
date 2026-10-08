"""State-dumping copies of the 1995 CD executables (HIDDEN.EXE = Hidden Worlds, CARPET.EXE = 1995 base game).

    python patch_hidden.py [--exe hidden|cd95] [--src ...] [--dst ...] [--mode play|record] [patch_carpet options]

The four patches of patch_carpet.py (dump cave in the dead castle-site search, `-roll N` = play/record
movie N-1, no input check during playback, dos_is_cdrom_drive always "CD-ROM") are applied at the
addresses of the 1995 code layout below. The code of every patched function is the same as in the
1996 exe (only absolute addresses / call displacements differ), so patch_carpet's cave builders are
reused unchanged: a private copy of the patch_carpet module is loaded and its address constants are
replaced by the LAYOUTS entry (patch_carpet.py itself and its default output are not affected).

Before patching, verify_layout() checks the instruction bytes at every address (see
docs/analysis/port_reference_hw.md); a mismatch aborts.

GameState difference: the 1995 demo_save_state writes 0x38d09 bytes (1996: 0x38d03).
The players array (0x340b, 0x801 per player, tick at +0x12) is at the same place, which is all the
caves use.

Only writes under extracted/.
"""
from __future__ import annotations

import argparse
import importlib.util
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REFGAME_HW = ROOT / "extracted" / "refgame_hw"
sys.path.insert(0, str(ROOT / "tools" / "mctools"))
from lefile import LEFile  # noqa: E402

STATE_SIZE_1995 = 0x38D09

# 1996 name -> address in the 1995 exe. HIDDEN.EXE and the CD CARPET.EXE share the low part of the
# layout (cave, dos_is_cdrom) and differ above ~0x34000.
LAYOUTS = {
    "hidden": dict(
        file="HIDDEN.EXE",
        CAVE=0x1307F,               # dead_castle_site_search body (1996 0x11cef)
        CAVE_END=0x13107,           # fixup source (jump-table operand), as 0x11d77 in 1996
        CALL_SITE=0x34981,          # game_tick_update (0x348f0): call sound_update
        SOUND_UPDATE=0x55630,
        DEMO_SAVE_STATE=0x3E750,
        DEMO_SAVE_TERRAIN=0x3E8C0,
        DEMO_CLOSE=0x3EF90,
        CFG_PTR_OPERAND=0x34905,    # mov eax, [0xae3f8] (g_cfg) in game_tick_update
        STATE_PTR_OPERAND=0x348F2,  # mov eax, [0xae3f0] (g_state)
        ROLL_PATCH=0x35A19,         # config_parse `-roll N` branch
        INPUT_PATCH=0x3EE1A,        # demo_record_playback_step (0x3ed10): call input_changed (0x35ad0)
        CD_PATCH=0x1105F,           # dos_is_cdrom_drive (0x11050): jne 0x110b0
        INPUT_CHANGED=0x35AD0,
        CD_FUNC=0x11050,
    ),
    "cd95": dict(
        file="CARPET.EXE",
        CAVE=0x1307F,
        CAVE_END=0x13107,
        CALL_SITE=0x345C1,          # game_tick_update (0x34530)
        SOUND_UPDATE=0x55100,
        DEMO_SAVE_STATE=0x3E410,
        DEMO_SAVE_TERRAIN=0x3E580,
        DEMO_CLOSE=0x3EC50,
        CFG_PTR_OPERAND=0x34545,    # mov eax, [0xae408]
        STATE_PTR_OPERAND=0x34532,  # mov eax, [0xae400]
        ROLL_PATCH=0x35659,
        INPUT_PATCH=0x3EADA,        # demo_record_playback_step (0x3e9d0): call input_changed (0x35710)
        CD_PATCH=0x1105F,
        INPUT_CHANGED=0x35710,
        CD_FUNC=0x11050,
    ),
}
PATCH_KEYS = ("CAVE", "CAVE_END", "CALL_SITE", "SOUND_UPDATE", "DEMO_SAVE_STATE", "DEMO_SAVE_TERRAIN",
              "DEMO_CLOSE", "CFG_PTR_OPERAND", "STATE_PTR_OPERAND", "ROLL_PATCH", "INPUT_PATCH", "CD_PATCH")


def load_patch_module(layout: dict):
    """A private instance of patch_carpet with the 1995 addresses (sys.modules is not touched)."""
    spec = importlib.util.spec_from_file_location("patch_carpet_1995", HERE / "patch_carpet.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for k in PATCH_KEYS:
        setattr(mod, k, layout[k])
    # the original `call input_changed` bytes (rel32 differs from the 1996 exe)
    mod.INPUT_ORIG = b"\xe8" + struct.pack("<i", layout["INPUT_CHANGED"] - (layout["INPUT_PATCH"] + 5))
    return mod


def verify_layout(src: Path, L: dict) -> list[str]:
    """Check the original instruction bytes at every patched / called address. Returns a report."""
    le = LEFile.parse(src)
    lo, img = le.flat_image()

    def b(addr, n):
        return bytes(img[addr - lo:addr - lo + n])

    def u32(addr):
        return struct.unpack_from("<I", img, addr - lo)[0]

    def rel_target(addr):           # E8/E9 rel32 at addr
        return addr + 5 + struct.unpack_from("<i", img, addr + 1 - lo)[0]

    rep, errs = [], []

    def check(cond, what):
        (rep if cond else errs).append(("ok   " if cond else "FAIL ") + what)

    cave, end = L["CAVE"], L["CAVE_END"]
    # cave: preceded by the stubbed-out search (push ebx/esi/edi, sub esp 8, xor eax, eax, ..., ret)
    check(b(cave - 0xF, 0xF) == bytes.fromhex("53565783ec0831c083c4085f5e5bc3"),
          f"{cave:#x}: dead body directly after the 'return 0' stub at {cave - 0xF:#x}")
    srcs = [f.addr for f in le.fixups if cave - 4 <= f.addr < end + 4]
    check(srcs == [end], f"only fixup source near the cave is {end:#x} (got {[hex(s) for s in srcs]})")
    tg = sorted(le.objects[f.target_obj - 1].base + f.target_off for f in le.fixups
                if f.target_obj and cave <= le.objects[f.target_obj - 1].base + f.target_off < end)
    tg_src = sorted(f.addr for f in le.fixups
                    if f.target_obj and cave <= le.objects[f.target_obj - 1].base + f.target_off < end)
    check(len(tg) == 4 and all(cave - 0x2B <= s < cave - 0xF for s in tg_src),
          f"fixups into the cave only from its own jump table at {cave - 0x2B:#x} ({[hex(t) for t in tg]})")
    code = le.objects[0]
    refs = []
    for i in range(code.base - lo, code.base + code.virtual_size - lo - 5):
        if img[i] in (0xE8, 0xE9) and cave <= lo + i + 5 + struct.unpack_from("<i", img, i + 1)[0] < end:
            refs.append(lo + i)
    check(not refs, f"no rel32 call/jmp into the cave ({[hex(r) for r in refs]})")
    # call site
    site = b(L["CALL_SITE"], 5)
    check(site[0] == 0xE8 and rel_target(L["CALL_SITE"]) in (L["SOUND_UPDATE"], cave),
          f"{L['CALL_SITE']:#x}: call sound_update {L['SOUND_UPDATE']:#x}")
    check(b(L["SOUND_UPDATE"], 4) == bytes.fromhex("53565780"),
          f"{L['SOUND_UPDATE']:#x}: sound_update prologue push ebx/esi/edi; cmp byte")
    # pointer operands: A1 imm32, the same variables demo_save_state / demo_close use
    st_var, cfg_var = u32(L["STATE_PTR_OPERAND"]), u32(L["CFG_PTR_OPERAND"])
    check(img[L["STATE_PTR_OPERAND"] - 1 - lo] == 0xA1 and img[L["CFG_PTR_OPERAND"] - 1 - lo] == 0xA1,
          f"operands {L['STATE_PTR_OPERAND']:#x} / {L['CFG_PTR_OPERAND']:#x} are mov eax, [g_state={st_var:#x}] / [g_cfg={cfg_var:#x}]")
    ss = L["DEMO_SAVE_STATE"]
    check(b(ss, 8) == bytes.fromhex("83ec400fbf442444") and b(ss + 0x20, 5) == b"\x68" + struct.pack("<I", STATE_SIZE_1995)
          and b(ss + 0x25, 2) == bytes.fromhex("8b15") and u32(ss + 0x27) == st_var,
          f"{ss:#x}: demo_save_state(tick): push {STATE_SIZE_1995:#x}; mov edx, [g_state]")
    stt = L["DEMO_SAVE_TERRAIN"]
    check(b(stt, 9) == bytes.fromhex("5383ec400fbf5c2448") and b(stt + 0x21, 5) == bytes.fromhex("6822020000"),
          f"{stt:#x}: demo_save_terrain(tick) prologue, open mode 0x222")
    dc = L["DEMO_CLOSE"]
    check(b(dc, 1) == b"\xa1" and u32(dc + 1) == cfg_var and b(dc + 5, 7) == bytes.fromhex("8b500985d2741c"),
          f"{dc:#x}: demo_close: mov eax, [g_cfg]; mov edx, [eax+9]; test; je")
    rp = L["ROLL_PATCH"]
    pc = load_patch_module(L)
    old = b(rp, len(pc.ROLL_ORIG))
    check((old == pc.ROLL_ORIG or old in pc.ROLL_VARIANTS) and b(rp - 5, 1) == b"\xa1" and u32(rp - 4) == cfg_var,
          f"{rp:#x}: -roll branch (unique match of the 29 original bytes), preceded by mov eax, [g_cfg]")
    ip = L["INPUT_PATCH"]
    check((b(ip, 1) == b"\xe8" and rel_target(ip) == L["INPUT_CHANGED"]) or b(ip, 5) == pc.INPUT_NEW,
          f"{ip:#x}: call input_changed {L['INPUT_CHANGED']:#x}")
    cp = L["CD_PATCH"]
    check(b(L["CD_FUNC"], 2) == bytes.fromhex("5356") and b(cp - 4, 6) in (bytes.fromhex("31db84e4754f"), bytes.fromhex("31db84e4eb4f")),
          f"{cp:#x}: dos_is_cdrom_drive ({L['CD_FUNC']:#x}) cached-result jne")
    if errs:
        raise SystemExit("layout verification failed:\n  " + "\n  ".join(errs + rep))
    return rep


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", choices=sorted(LAYOUTS), default="hidden")
    ap.add_argument("--src", default=None, help="default extracted/gog_cd/CARPET/<HIDDEN|CARPET>.EXE")
    ap.add_argument("--dst", default=None, help="default extracted/refgame_hw/magic/hidden.exe (cd95: carpet.exe)")
    ap.add_argument("--verify-only", action="store_true")
    a, rest = ap.parse_known_args(argv)
    L = LAYOUTS[a.exe]
    src = Path(a.src) if a.src else ROOT / "extracted" / "gog_cd" / "CARPET" / L["file"]
    dst = Path(a.dst) if a.dst else REFGAME_HW / "magic" / ("hidden.exe" if a.exe == "hidden" else "carpet.exe")
    for p in (ROOT / "MagicCarpet", Path(r"C:\Program Files"), ROOT / "extracted" / "gog_cd"):
        if str(dst.resolve()).lower().startswith(str(p.resolve()).lower()):
            raise SystemExit(f"refusing to write into {p}")
    rep = verify_layout(src, L)
    print(f"{a.exe}: {src.name} layout verified ({len(rep)} checks)")
    for r in rep:
        print("  " + r)
    if a.verify_only:
        return
    mod = load_patch_module(L)
    mod.main(["--src", str(src), "--dst", str(dst)] + rest)


if __name__ == "__main__":
    main()
