"""Build the state-dumping copy of carpet.exe used for the per-tick reference.

    python patch_carpet.py [--src MagicCarpet/magic/carpet.exe] [--dst extracted/refgame/magic/carpet.exe]
                           [--every-until 1013] [--stride 10] [--stride-until 8963] [--terrain-stride 100]
                           [--verify]

What the patch does (option 1 of the round-4 briefing, task E):

* game_tick_update_32e80 calls sound_update_494b0 at 0x32f11 right after the thing update passes;
  that `call` is redirected to a code cave written into the dead function
  dead_castle_site_search_11cef (0x11cef..0x11d76: no fixup source lands there; the only fixup of
  the dead block is at 0x11d77, which the cave stays clear of).
* The cave is position independent (a `call $+5 / pop` gives the load delta, and the run-time
  addresses of the g_cfg / g_state pointer variables are read out of the already relocated operands
  of game_tick_update itself at 0x32e95 / 0x32e82), so no fixup records are added. All calls and the
  final `jmp sound_update_494b0` are relative and stay inside the code object.
* Per tick, while a movie is playing (Config.flags & 4 and the demo file handle Config+9 is open)
  it takes the local player's tick counter (GameState+0x340b + local*0x801 + 0x12) and calls the
  game's own demo_save_state_3c2c0(tick) -> movie/gam%05d.dat (raw 0x38d03-byte GameState) when
  tick <= EVERY_UNTIL, or when tick <= STRIDE_UNTIL and tick % STRIDE == 0. When it dumps and
  tick % TERRAIN_STRIDE == 0 it also calls demo_save_terrain_3c430(tick) -> movie/map%05d.dat.
  The dump therefore holds the state after thing_update_all and before sound_update / render_frame,
  which is where the port's game_tick_sim() ends. The cave saves and restores every register.

Second patch: the retail game only plays a movie from the main menu's attract mode (three idle
periods of 0x12c0 timer ticks, fe_screen_main_menu: Config.movie = 0, flags |= 0x24). `-movie N` only
stores N (and ignores 0) and `-roll N` sets flags |= 0x120 without the playback bit. The 29-byte
`-roll` branch of config_parse_33750 (0x33fd9..0x33ff3) is rewritten to `Config.movie = N - 1;
flags |= 0x124` (0x100 skips the front end, 0x20 blocks recording like the attract mode, 4 = play
back), so `carpet -roll 1 -level 38` starts movie 0 directly: game_main goes straight to
level_load_and_init (level 38 = the movie's level) and the first tick of the level loads the snapshot
pair. Config.roll (+0xf) is not written any more (nothing reads it).

Third patch: the `call input_changed_34090` in demo_record_playback_step_3c540 (0x3c64a) becomes
`xor eax, eax`, so that a mouse event DOSBox delivers cannot end the playback.

Fourth patch: dos_is_cdrom_drive_10010 always answers "CD-ROM" (0x1001f jne -> jmp). Without it the
copy-protection probe player_cd_check_quit_3bbd0 quits the level at tick 588 of movie 0 (the first
spell-book packet on a tick multiple of 8), because the copy is mounted as a hard disk (it must be
writable for the dumps).

The first dump of a movie-0 run is tick 413 (the snapshot gam00000.dat is tick 413 before player
0's packet of that tick was executed; the first playback tick loads it and finishes the tick), the
last one is tick 8963 (the quit packet closes the demo file and the condition fails).

The script only writes under extracted/ (never into MagicCarpet/).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "mctools"))
from lefile import LEFile  # noqa: E402

CAVE = 0x11CEF              # dead_castle_site_search_11cef
CAVE_END = 0x11D77          # fixup source at 0x11d77 (jump-table operand of the dead code)
CALL_SITE = 0x32F11         # game_tick_update_32e80: call sound_update_494b0
SOUND_UPDATE = 0x494B0
DEMO_SAVE_STATE = 0x3C2C0
DEMO_SAVE_TERRAIN = 0x3C430
CFG_PTR_OPERAND = 0x32E95   # operand bytes of `mov eax, [DAT_000adf74]` (g_cfg) in game_tick_update
STATE_PTR_OPERAND = 0x32E82 # operand bytes of `mov eax, [DAT_000adf6c]` (g_state)
PLAYERS_OFF = 0x340B        # GameState.players
PLAYER_REC_SIZE = 0x801
TICK_OFF = 0x12             # PlayerRec.tick (u32)
LOCAL_PLAYER_OFF = 8        # GameState.local_player (i16)

# config_parse_33750, `-roll N` branch (eax = g_cfg):
#   33fd9 mov word [eax+0xd], 0 ; 33fdf mov edx, [esp+0x84] ; 33fe6 mov bx, [eax]
#   33fe9 mov word [eax+0xf], dx ; 33fed or ebx, 0x120 ; 33ff3 mov [eax], bx
ROLL_PATCH = 0x33FD9
ROLL_ORIG = bytes.fromhex("66c7400d0000" "8b942484000000" "668b18" "6689500f" "81cb20010000" "668918")


def roll_new(flags: int) -> bytes:
    return bytes.fromhex("8b942484000000"   # mov edx, [esp+0x84]   N
                         "4a"               # dec edx               movie = N - 1
                         "6689500d"         # mov [eax+0xd], dx     Config.movie
                         "668b18"           # mov bx, [eax]         flags
                         "81cb") + struct.pack("<I", flags) + bytes.fromhex(  # or ebx, FLAGS
                         "668918"           # mov [eax], bx
                         "9090909090")      # 5 x nop


# play: 0x124 = skip front end | no-record | playback.  record (round 5): 0x100 = skip front end only;
# the record cave (build_record_cave) sets the record bit 2 once player 0's Thing exists, and the next
# packet opens movie/mvi%05d.dat for writing and saves the snapshot pair gam%05d / map%05d
# (demo_record_playback_step_3c540): a recording of `-level L` from (almost) its first tick.
ROLL_FLAGS = {"play": 0x124, "record": 0x100}
ROLL_NEW = roll_new(ROLL_FLAGS["play"])
ROLL_VARIANTS = {roll_new(f) for f in ROLL_FLAGS.values()}
assert len(ROLL_NEW) == len(ROLL_ORIG) == 29

# demo_record_playback_step_3c540: `call input_changed_34090` (any mouse / key change ends playback).
# Under DOSBox the mouse driver reports a position change ~175 ticks into the movie and the
# recording stopped at tick 587; the call is replaced by `xor eax, eax` (= no input).
INPUT_PATCH = 0x3C64A
INPUT_ORIG = bytes.fromhex("e8417affff")
INPUT_NEW = bytes.fromhex("31c0909090")

# dos_is_cdrom_drive_10010: `jne 0x10070` (cached result) -> `jmp 0x10070` = always "is a CD-ROM".
# player_cd_check_quit_3bbd0 (run from player_set_input_mode_3bb50, i.e. on the movie's spell-book
# packets, every 8th tick) otherwise sets PlayerRec.quit = 2 on a hard-disk mount: movie 0 ended
# at tick 587/588 (packet 0x15 at tick 588). The check has no other side effect on the GameState.
CD_PATCH = 0x1001F
CD_ORIG = bytes.fromhex("754f")
CD_NEW = bytes.fromhex("eb4f")


class Asm:
    """Minimal x86-32 emitter with labels (only the handful of forms the cave needs)."""

    def __init__(self, base: int):
        self.base = base
        self.code = bytearray()
        self.labels: dict[str, int] = {}
        self.fix8: list[tuple[int, str]] = []      # (offset of rel8 byte, label)
        self.fix32: list[tuple[int, int]] = []     # (offset of rel32, absolute target)

    def here(self) -> int:
        return self.base + len(self.code)

    def label(self, name: str):
        self.labels[name] = self.here()

    def emit(self, *bs: int):
        self.code += bytes(bs)

    def imm32(self, v: int):
        self.code += struct.pack("<I", v & 0xFFFFFFFF)

    def jcc8(self, opcode: int, label: str):
        self.emit(opcode, 0)
        self.fix8.append((len(self.code) - 1, label))

    def call32(self, target: int):
        self.emit(0xE8)
        self.fix32.append((len(self.code), target))
        self.imm32(0)

    def jmp32(self, target: int):
        self.emit(0xE9)
        self.fix32.append((len(self.code), target))
        self.imm32(0)

    def finish(self) -> bytes:
        for off, label in self.fix8:
            rel = self.labels[label] - (self.base + off + 1)
            if not -128 <= rel <= 127:
                raise ValueError(f"rel8 out of range for {label}")
            self.code[off] = rel & 0xFF
        for off, target in self.fix32:
            rel = target - (self.base + off + 4)
            struct.pack_into("<i", self.code, off, rel)
        return bytes(self.code)


def build_cave(every_until: int, stride: int, stride_until: int, terrain_stride: int, mask: int = 4) -> bytes:
    a = Asm(CAVE)
    a.emit(0x60)                                   # pushad
    a.emit(0xE8, 0, 0, 0, 0)                       # call $+5
    a.label("next")
    a.emit(0x5E)                                   # pop esi            esi = run-time address of `next`
    a.emit(0x81, 0xEE); a.imm32(a.labels["next"])  # sub esi, next      esi = load delta
    a.emit(0x8B, 0x86); a.imm32(CFG_PTR_OPERAND)   # mov eax, [esi + 0x32e95]  = &g_cfg (relocated)
    a.emit(0x8B, 0x00)                             # mov eax, [eax]     g_cfg
    a.emit(0xF6, 0x00, mask)                       # test byte [eax], MASK  (4 playback, 6 also recording)
    a.jcc8(0x74, "done")                           # jz done
    a.emit(0x83, 0x78, 0x09, 0x00)                 # cmp dword [eax+9], 0  demo file open?
    a.jcc8(0x74, "done")                           # jz done
    a.emit(0x8B, 0x86); a.imm32(STATE_PTR_OPERAND) # mov eax, [esi + 0x32e82] = &g_state (relocated)
    a.emit(0x8B, 0x00)                             # mov eax, [eax]     g_state
    a.emit(0x0F, 0xBF, 0x50, LOCAL_PLAYER_OFF)     # movsx edx, word [eax+8]
    a.emit(0x69, 0xD2); a.imm32(PLAYER_REC_SIZE)   # imul edx, edx, 0x801
    a.emit(0x8B, 0x84, 0x10); a.imm32(PLAYERS_OFF + TICK_OFF)  # mov eax, [eax+edx+0x341d]  tick
    a.emit(0x3D); a.imm32(every_until)             # cmp eax, EVERY_UNTIL
    a.jcc8(0x76, "save")                           # jbe save
    a.emit(0x3D); a.imm32(stride_until)            # cmp eax, STRIDE_UNTIL
    a.jcc8(0x77, "done")                           # ja done
    a.emit(0x89, 0xC1)                             # mov ecx, eax
    a.emit(0x31, 0xD2)                             # xor edx, edx
    a.emit(0xBB); a.imm32(stride)                  # mov ebx, STRIDE
    a.emit(0xF7, 0xF3)                             # div ebx
    a.emit(0x85, 0xD2)                             # test edx, edx
    a.jcc8(0x75, "done")                           # jnz done
    a.emit(0x89, 0xC8)                             # mov eax, ecx
    a.label("save")
    a.emit(0x50)                                   # push eax           (tick)
    a.call32(DEMO_SAVE_STATE)                      # call demo_save_state_3c2c0
    a.emit(0x8B, 0x04, 0x24)                       # mov eax, [esp]
    a.emit(0x31, 0xD2)                             # xor edx, edx
    a.emit(0xBB); a.imm32(terrain_stride)          # mov ebx, TERRAIN_STRIDE
    a.emit(0xF7, 0xF3)                             # div ebx
    a.emit(0x85, 0xD2)                             # test edx, edx
    a.jcc8(0x75, "skip_terrain")                   # jnz skip_terrain
    a.call32(DEMO_SAVE_TERRAIN)                    # call demo_save_terrain_3c430 (arg still pushed)
    a.label("skip_terrain")
    a.emit(0x83, 0xC4, 0x04)                       # add esp, 4
    a.label("done")
    a.emit(0x61)                                   # popad
    a.jmp32(SOUND_UPDATE)                          # jmp sound_update_494b0
    code = a.finish()
    if CAVE + len(code) > CAVE_END:
        raise ValueError(f"cave too big: {len(code)} bytes, {CAVE_END - CAVE} available")
    return code


DEMO_CLOSE = 0x3C7C0        # demo_close_3c7c0: closes Config+9, flags &= ~6 (no arguments)


def build_record_cave(stride: int, stop_tick: int) -> bytes:
    """Record-mode cave (round 5).

    * Tick 1 (not recording yet): demo_save_state(1) -> gam00001.dat, the state after the first tick of
      the freshly generated level (the port's sim_load_level + one game_tick_sim is compared with it).
    * Not recording yet, tick < STOP and players[0].thing != 0: Config.flags |= 2. The next
      tick's first packet then opens movie/mvi%05d.dat (Config.movie) for writing and saves the snapshot
      pair (demo_record_playback_step_3c540), exactly like the in-game record command 0xc. The snapshot
      must not be taken before player 0's Thing exists: demo_relink_state_pointers_3dc10 rebases every
      Thing.desc relative to things[players[0].thing].desc, so a snapshot without the player crashes the
      playback (divide by zero in a townie update).
    * Recording (flags & 2, Config+9 open): dump the GameState every STRIDE ticks (progress / record-vs-
      playback cross-check) and at tick STOP close the recording with demo_close_3c7c0, so the DOS file
      is flushed and complete (DOSBox loses the buffered tail of a file that is still open when it is
      killed). The recording then ends without a quit packet: its playback stops on the short read
      (status 8), in the original and in the port."""
    a = Asm(CAVE)
    a.emit(0x60)                                   # pushad
    a.emit(0xE8, 0, 0, 0, 0)                       # call $+5
    a.label("next")
    a.emit(0x5E)                                   # pop esi
    a.emit(0x81, 0xEE); a.imm32(a.labels["next"])  # sub esi, next
    a.emit(0x8B, 0xBE); a.imm32(CFG_PTR_OPERAND)   # mov edi, [esi + 0x32e95]
    a.emit(0x8B, 0x3F)                             # mov edi, [edi]     g_cfg
    a.emit(0x8B, 0x86); a.imm32(STATE_PTR_OPERAND) # mov eax, [esi + 0x32e82]
    a.emit(0x8B, 0x00)                             # mov eax, [eax]     g_state
    a.emit(0x89, 0xC5)                             # mov ebp, eax
    a.emit(0x0F, 0xBF, 0x50, LOCAL_PLAYER_OFF)     # movsx edx, word [eax+8]
    a.emit(0x69, 0xD2); a.imm32(PLAYER_REC_SIZE)   # imul edx, edx, 0x801
    a.emit(0x8B, 0x8C, 0x10); a.imm32(PLAYERS_OFF + TICK_OFF)  # mov ecx, [eax+edx+0x341d]  tick
    a.emit(0xF6, 0x07, 0x02)                       # test byte [edi], 2
    a.jcc8(0x74, "notrec")
    a.emit(0x83, 0x7F, 0x09, 0x00)                 # cmp dword [edi+9], 0
    a.jcc8(0x74, "notrec")
    a.emit(0x89, 0xC8)                             # mov eax, ecx
    a.emit(0x31, 0xD2)                             # xor edx, edx
    a.emit(0xBB); a.imm32(stride)                  # mov ebx, STRIDE
    a.emit(0xF7, 0xF3)                             # div ebx
    a.emit(0x85, 0xD2)                             # test edx, edx
    a.jcc8(0x75, "nosave")
    a.emit(0x51)                                   # push ecx           (tick)
    a.call32(DEMO_SAVE_STATE)
    a.emit(0x59)                                   # pop ecx
    a.label("nosave")
    a.emit(0x81, 0xF9); a.imm32(stop_tick)         # cmp ecx, STOP
    a.jcc8(0x72, "done")                           # jb done
    a.call32(DEMO_CLOSE)                           # call demo_close_3c7c0
    a.jcc8(0xEB, "done")                           # jmp done
    a.label("notrec")
    a.emit(0x83, 0xF9, 0x01)                       # cmp ecx, 1
    a.jcc8(0x75, "not1")                           # jne not1
    a.emit(0x51)                                   # push ecx           tick 1: level-start dump
    a.call32(DEMO_SAVE_STATE)
    a.emit(0x59)                                   # pop ecx
    a.label("not1")
    a.emit(0x81, 0xF9); a.imm32(stop_tick)         # cmp ecx, STOP
    a.jcc8(0x73, "done")                           # jae done
    a.emit(0x66, 0x83, 0xBD); a.imm32(PLAYERS_OFF + 0xA); a.emit(0x00)  # cmp word [ebp+0x3415], 0
    a.jcc8(0x74, "done")                           # je done
    a.emit(0x80, 0x0F, 0x02)                       # or byte [edi], 2
    a.label("done")
    a.emit(0x61)                                   # popad
    a.jmp32(SOUND_UPDATE)
    code = a.finish()
    if CAVE + len(code) > CAVE_END:
        raise ValueError(f"cave too big: {len(code)} bytes, {CAVE_END - CAVE} available")
    return code


def linear_to_file(le: LEFile, addr: int) -> int:
    for o in le.objects:
        if o.base <= addr < o.base + o.virtual_size:
            rel = addr - o.base
            page = o.page_map_index - 1 + rel // le.page_size
            return le._page_file_offset(page) + rel % le.page_size  # noqa: SLF001
    raise ValueError(f"{addr:#x} not in any object")


def patch(src: Path, dst: Path, every_until: int, stride: int, stride_until: int, terrain_stride: int,
          verify: bool, mode: str = "play", stop_tick: int = 5000) -> None:
    le = LEFile.parse(src)
    if mode == "record":
        cave = build_record_cave(stride, stop_tick)
    else:
        cave = build_cave(every_until, stride, stride_until, terrain_stride)
    roll = roll_new(ROLL_FLAGS[mode])
    bad = [f for f in le.fixups if CAVE - 3 <= f.addr < CAVE + len(cave) or CALL_SITE <= f.addr < CALL_SITE + 5
           or ROLL_PATCH <= f.addr < ROLL_PATCH + len(ROLL_NEW)]
    if bad:
        raise SystemExit(f"fixup sources inside the patched ranges: {[hex(f.addr) for f in bad]}")
    data = bytearray(src.read_bytes())

    def write(addr: int, blob: bytes):
        for i, b in enumerate(blob):
            data[linear_to_file(le, addr + i)] = b

    # sanity: the call site must be the original `call sound_update_494b0`
    site = bytes(data[linear_to_file(le, CALL_SITE):linear_to_file(le, CALL_SITE) + 5])
    expect = b"\xe8" + struct.pack("<i", SOUND_UPDATE - (CALL_SITE + 5))
    if site != expect and site[:1] != b"\xe8":
        raise SystemExit(f"unexpected bytes at call site {CALL_SITE:#x}: {site.hex()}")
    write(CAVE, cave)
    write(CALL_SITE, b"\xe8" + struct.pack("<i", CAVE - (CALL_SITE + 5)))
    # `-roll N` branch of config_parse: eax = g_cfg here (loaded at 0x33fd4).
    roll_old = bytes(data[linear_to_file(le, ROLL_PATCH):linear_to_file(le, ROLL_PATCH) + len(ROLL_ORIG)])
    if roll_old != ROLL_ORIG and roll_old not in ROLL_VARIANTS:
        raise SystemExit(f"unexpected bytes at {ROLL_PATCH:#x}: {roll_old.hex()}")
    write(ROLL_PATCH, roll)
    inp_old = bytes(data[linear_to_file(le, INPUT_PATCH):linear_to_file(le, INPUT_PATCH) + 5])
    if inp_old not in (INPUT_ORIG, INPUT_NEW):
        raise SystemExit(f"unexpected bytes at {INPUT_PATCH:#x}: {inp_old.hex()}")
    write(INPUT_PATCH, INPUT_NEW)
    cd_old = bytes(data[linear_to_file(le, CD_PATCH):linear_to_file(le, CD_PATCH) + 2])
    if cd_old not in (CD_ORIG, CD_NEW):
        raise SystemExit(f"unexpected bytes at {CD_PATCH:#x}: {cd_old.hex()}")
    write(CD_PATCH, CD_NEW)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    print(f"wrote {dst} ({len(data)} bytes); cave {CAVE:#x}..{CAVE + len(cave):#x} ({len(cave)} bytes), "
          f"call site {CALL_SITE:#x} -> {CAVE:#x}")
    print(f"mode {mode}: -roll N sets Config.movie = N - 1, flags |= {ROLL_FLAGS[mode]:#x}")
    if mode == "record":
        print(f"record cave: dump tick 1, start recording once players[0].thing != 0, dump every "
              f"{stride} ticks while recording, demo_close at tick {stop_tick}")
    else:
        print(f"dump policy: every tick <= {every_until}, then every {stride} ticks <= {stride_until}, "
              f"terrain every {terrain_stride} ticks")
    if verify:
        le2 = LEFile.parse(dst)
        lo, img = le2.flat_image()
        got = bytes(img[CAVE - lo:CAVE - lo + len(cave)])
        assert got == cave, "cave bytes differ after reparse"
        try:
            from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        except ImportError:
            print("capstone not installed; skipping listing")
            return
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        for ins in md.disasm(got, CAVE):
            print(f"  {ins.address:06x}  {ins.bytes.hex():<16} {ins.mnemonic} {ins.op_str}")
        for ins in md.disasm(bytes(img[CALL_SITE - lo:CALL_SITE - lo + 5]), CALL_SITE):
            print(f"  {ins.address:06x}  {ins.bytes.hex():<16} {ins.mnemonic} {ins.op_str}")
        for ins in md.disasm(bytes(img[ROLL_PATCH - lo:ROLL_PATCH - lo + len(ROLL_NEW)]), ROLL_PATCH):
            print(f"  {ins.address:06x}  {ins.bytes.hex():<16} {ins.mnemonic} {ins.op_str}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=str(ROOT / "MagicCarpet" / "magic" / "carpet.exe"))
    ap.add_argument("--dst", default=str(ROOT / "extracted" / "refgame" / "magic" / "carpet.exe"))
    ap.add_argument("--every-until", type=int, default=1013, help="dump every tick up to this tick (snapshot is 413)")
    ap.add_argument("--stride", type=int, default=10, help="after that, dump when tick %% stride == 0")
    ap.add_argument("--stride-until", type=int, default=8963, help="last tick considered for strided dumps")
    ap.add_argument("--terrain-stride", type=int, default=100, help="also dump the terrain when tick %% this == 0")
    ap.add_argument("--verify", action="store_true", help="re-parse and disassemble the patched bytes")
    ap.add_argument("--stop-tick", type=int, default=5000, help="record mode: close the recording at this tick")
    ap.add_argument("--mode", choices=sorted(ROLL_FLAGS), default="play",
                    help="play: -roll N plays movie N-1 (dumps during playback); record: -roll N records "
                         "movie N-1 from the first tick of -level L (dumps also while recording)")
    a = ap.parse_args(argv)
    dst = Path(a.dst).resolve()
    if str(dst).lower().startswith(str((ROOT / "MagicCarpet").resolve()).lower()):
        raise SystemExit("refusing to write into MagicCarpet/")
    patch(Path(a.src), dst, a.every_until, a.stride, a.stride_until, a.terrain_stride, a.verify, a.mode, a.stop_tick)


if __name__ == "__main__":
    main()
