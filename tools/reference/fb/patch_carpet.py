"""Build the state- AND frame-dumping copy of carpet.exe for the render reference (task F, round 5).

    python patch_carpet.py [--src MagicCarpet/magic/carpet.exe] [--dst extracted/refgame_fb/magic/carpet.exe]
                           [--every-until 1013] [--stride 10] [--stride-until 8963] [--terrain-stride 100]
                           [--fb-every-until 1013] [--fb-stride 10] [--fb-stride-until 8963] [--verify]

Copy of tools/reference/patch_carpet.py (task E of round 4, which edits the original concurrently). The
four patches of that script are applied unchanged (state-dump cave A at the sound_update call, `-roll N`
plays movie N-1, input_changed off, CD check forced; see its docstring and
docs/analysis/port_reference.md). This copy adds:

Cave B - frame dump. game_tick_update_32e80 ends with `call vga_present_frame_2f480` at 0x32f84 (after
render_frame_1fab0, ui_draw_debug_overlay_4ad80 and the screenshot hook): the call is redirected to cave B,
which (while Config.flags & 4 and the demo handle Config+9 is open, on the fb dump policy) writes
movie/fb%05d.dat (tick = local PlayerRec.tick, the same number as the state dump of that tick) with the
game's own sprintf_603bc / file_open_619a0(name, 0x222) / file_write_61e20 / file_close_61a10, then jumps
to vga_present_frame_2f480. File layout (all little endian):

    0x00000  64000   back buffer DAT_0012ed74 (320 x 200, pitch 320: movie 0 runs in mode 1)
    0x0fa00    768   DAC palette, read through ports 0x3c7 / 0x3c9 (6-bit values)
    0x0fd00    256   Config (DAT_000adf74) bytes 0..0xff (flags, tick, tick_bits, credits_state, ...)
    0x0fe00      6   sprite animation table header [DAT_000adf50] {u16 count, u32 records}
    0x0fe06  n*0x1c  its records (n = count; 0x211 in practice): FLIC animation state per animated sprite
    ...       0x60   data 0x93f40..0x93f9f (render option globals: sprite modes, slope low-pass 0x93f7c/80)
    ...       0xc0   data 0xb5800..0xb58bf (renderer camera / fog globals DAT_000b58xx)
    ...       0x0c   data 0x12ed70..0x12ed7b (screen width, back buffer pointer, height)

So the frame of tick N is the picture rendered from the state dump of tick N (gam%05d.dat, taken before
sound_update / render_frame of the same game_tick_update call), and the renderer globals in it are the
values AFTER render N (= the carried-over state that render N + 1 starts from).

Cave B does not fit one dead function, so the assembler below spreads it over several fixup-free holes of
dead code (terrain_max_corner_level_10c30 / terrain_minmax_along_path_10cb0, no callers,
creature_attack_fire_homing_desc_19a90, no reference at all, and terrain_ring_find_height_ne8_24d70, no
callers; its jump table 0x24d54 is only used by itself) and links them with jmps; no fixup source
lies inside a written range (checked) and no fixup target points into them. Like cave A it is position
independent: the code delta comes from `call $+5 / pop`, the data delta from the relocated g_cfg operand
at 0x32e95 (runtime &g_cfg - 0xadf74).

Round 6 (task D, docs/analysis/port_render_reference2.md):
  * cave B writes width * height bytes of the back buffer (64000 in 320x200, 307200 in 640x480) and, with
    --schedule FILE.json, applies a poke schedule after the render of each playback tick (render options,
    help screen, credits roll, Config.pentium; sidecar <exe>.schedule.txt for the port test);
  * --hires: config_parse stores video mode 8 (the game, and with -roll the movie, runs in 640x480);
  * --fe FILE.json: cave C (fe_cave.py) - scripted input and frame dumps of the front end at every present
    (vga_copy_320x200_610f0), sidecar <exe>.fescript.txt; run with run_reference.py --fe.
More dead-code holes were added (each checked: no caller, no fixup source inside, no fixup target or
rel call / jmp into it); without --schedule / --fe the patched exe reproduces the round-5 frame dumps
byte for byte (1396 / 1396 files of movie0_fb).

The script only writes under extracted/ (never into MagicCarpet/).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "mctools"))
from lefile import LEFile  # noqa: E402

# ---- cave A (unchanged from tools/reference/patch_carpet.py) ----
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

ROLL_PATCH = 0x33FD9
ROLL_ORIG = bytes.fromhex("66c7400d0000" "8b942484000000" "668b18" "6689500f" "81cb20010000" "668918")
ROLL_NEW = bytes.fromhex("8b942484000000"   # mov edx, [esp+0x84]   N
                         "4a"               # dec edx               movie = N - 1
                         "6689500d"         # mov [eax+0xd], dx     Config.movie
                         "668b18"           # mov bx, [eax]         flags
                         "81cb24010000"     # or ebx, 0x124         movie-mode | no-record | playback
                         "668918"           # mov [eax], bx
                         "9090909090")      # 5 x nop
assert len(ROLL_NEW) == len(ROLL_ORIG) == 29

INPUT_PATCH = 0x3C64A
INPUT_ORIG = bytes.fromhex("e8417affff")
INPUT_NEW = bytes.fromhex("31c0909090")

CD_PATCH = 0x1001F
CD_ORIG = bytes.fromhex("754f")
CD_NEW = bytes.fromhex("eb4f")

# Optional (--hud): render_frame_1fab0 skips the flight HUD (radar, hand labels, status panels, messages)
# while a movie plays (`test byte [cfg], 4; jne` at 0x1fc82). --hud nops the jne so the original draws
# the flight HUD during movie 0 too - a HUD reference. render_frame's HUD state writes then happen as in
# a normal game, so the state dumps of such a run differ from movie0 (the port test applies the same
# override: render_reference_test with MC_RFB_HUD=1).
HUD_PATCH = 0x1FC85
HUD_ORIG = bytes.fromhex("0f85e8130000")
HUD_NEW = bytes.fromhex("909090909090")

# ---- cave B (frame dump) ----
PRESENT_SITE = 0x32F84      # game_tick_update_32e80: call vga_present_frame_2f480
PRESENT = 0x2F480
SPRINTF = 0x603BC           # sprintf(buf, fmt, ...) (stack args)
FILE_OPEN = 0x619A0         # file_open_619a0(name, 0x222 = create) -> handle or -1
FILE_WRITE = 0x61E20        # file_write_61e20(handle, buf, size)
FILE_CLOSE = 0x61A10        # file_close_61a10(handle)
CFG_VAR = 0xADF74
STATE_VAR = 0xADF6C
FB_PTR_VAR = 0x12ED74       # back buffer pointer
SCREEN_VARS = 0x12ED70      # width, buffer, height (3 dwords)
ANIM_VAR = 0xADF50          # sprite animation table header pointer
FB_BYTES = 320 * 200
DATA_REGIONS = [(0x93F40, 0x60), (0xB5800, 0xC0)]
# Fixup-free holes of dead code: [start, end)
CAVE_B_HOLES = [(0x10C8F, 0x10D13), (0x19A90, 0x19AD0), (0x19AD4, 0x19B1E), (0x19B22, 0x19B3C),
                (0x19B4D, 0x19B69), (0x24D70, 0x24DB9), (0x24DBD, 0x24DF8), (0x24DFC, 0x24E1F),
                (0x10C30, 0x10C44)]
# Round 6 (task D): more fixup-free holes of dead code for the size-by-mode frame write and the poke
# schedule (each checked: no fixup source inside, no fixup target and no rel call / jmp into the function):
# ai_goal_creature_near_rival_13600 (no callers), crab_target_nearest_mana_ball_1aef0 (no callers),
# projectile_create_type13_long_383d0 and effect_create_type37_397c0 (standalone constructors, no
# reference), players_clear_records_3bf60 (no callers).
CAVE_B_HOLES += [(0x13621, 0x136AB), (0x136D4, 0x13713), (0x13733, 0x13762), (0x136AF, 0x136D0),
                 (0x1AF06, 0x1AF4B), (0x1AF4F, 0x1AF94), (0x383D0, 0x3843C), (0x397C0, 0x39835),
                 (0x3BF6A, 0x3BFB3)]

# Round 6 (task D) --hires: config_parse_33750 stores its video-mode local into DAT_0012edae at 0x34032
# (`mov eax, [esp+0xa4]; mov [0x12edae], ax`); the local is always 1 (the 640x480 option path at 0x33f51
# is dead). Load 8 instead: the game starts in 640x480 (VESA 0x101) - with -roll the front end is skipped,
# so the movie plays in 640x480 from its first tick (game_main allocates no g_frame2 in mode 8, as when a
# 640x480 game starts a level).
HIRES_PATCH = 0x3402B
HIRES_ORIG = bytes.fromhex("8b8424a4000000")
HIRES_NEW = bytes.fromhex("b808000000" "9090")      # mov eax, 8; nop; nop


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


class HoleAsm:
    """Emitter that places whole instructions into a list of holes, linking them with `jmp rel32`.

    Instructions are given as (bytes, relocs) where relocs = [(offset in instruction, kind, target)]:
    kind 'rel32' (target = absolute address or label name), 'abs32-label' (label address, code delta
    added at run time by the caller - only used as an esi-relative displacement here). Labels name the
    address of the next instruction."""

    def __init__(self, holes):
        self.holes = list(holes)
        self.hi = 0
        self.pos = self.holes[0][0]
        self.chunks: list[tuple[int, bytearray]] = [(self.pos, bytearray())]
        self.labels: dict[str, int] = {}
        self.rel: list[tuple[int, object]] = []    # (absolute address of the rel32 field, target)
        self.disp: list[tuple[int, str]] = []      # (absolute address of a 32-bit displacement = label address)

    def _cur(self) -> bytearray:
        return self.chunks[-1][1]

    def _need(self, n: int, data: bool = False):
        end = self.holes[self.hi][1]
        reserve = 0 if data else 5
        if self.pos + n + reserve <= end:
            return
        # link to the next hole
        if not data:
            self._cur().extend(b"\xe9\0\0\0\0")
            self.rel.append((self.pos + 1, ("hole", self.hi + 1)))
        self.hi += 1
        if self.hi >= len(self.holes):
            raise ValueError("cave B does not fit the holes")
        self.pos = self.holes[self.hi][0]
        self.chunks.append((self.pos, bytearray()))
        if self.pos + n + reserve > self.holes[self.hi][1]:
            self._need(n, data)

    def label(self, name: str):
        self.labels[name] = self.pos

    def ins(self, code: bytes, rel_at: int | None = None, target=None, disp_at: int | None = None,
            disp_label: str | None = None):
        self._need(len(code))
        if rel_at is not None:
            self.rel.append((self.pos + rel_at, target))
        if disp_at is not None:
            self.disp.append((self.pos + disp_at, disp_label))
        self._cur().extend(code)
        self.pos += len(code)

    def data(self, blob: bytes, name: str):
        self._need(len(blob), data=True)
        self.labels[name] = self.pos
        self._cur().extend(blob)
        self.pos += len(blob)

    def finish(self) -> list[tuple[int, bytes]]:
        out = []
        flat: dict[int, int] = {}
        for base, buf in self.chunks:
            for i, b in enumerate(buf):
                flat[base + i] = b
        def put32(addr, v):
            for i, b in enumerate(struct.pack("<i", v)):
                flat[addr + i] = b
        for addr, target in self.rel:
            if isinstance(target, tuple):
                dest = self.holes[target[1]][0]
            elif isinstance(target, str):
                dest = self.labels[target]
            else:
                dest = target
            put32(addr, dest - (addr + 4))
        for addr, name in self.disp:
            put32(addr, self.labels[name])
        for base, buf in self.chunks:
            out.append((base, bytes(flat[base + i] for i in range(len(buf)))))
        return out


def build_cave(every_until: int, stride: int, stride_until: int, terrain_stride: int) -> bytes:
    a = Asm(CAVE)
    a.emit(0x60)                                   # pushad
    a.emit(0xE8, 0, 0, 0, 0)                       # call $+5
    a.label("next")
    a.emit(0x5E)                                   # pop esi            esi = run-time address of `next`
    a.emit(0x81, 0xEE); a.imm32(a.labels["next"])  # sub esi, next      esi = load delta
    a.emit(0x8B, 0x86); a.imm32(CFG_PTR_OPERAND)   # mov eax, [esi + 0x32e95]  = &g_cfg (relocated)
    a.emit(0x8B, 0x00)                             # mov eax, [eax]     g_cfg
    a.emit(0xF6, 0x00, 0x04)                       # test byte [eax], 4 playback?
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


def i32(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFFFFFF)


# Poke schedule entry kinds (round 6): the byte goes to GameState + off, Config + off, or the local
# player's PlayerRec + off.
KIND_STATE, KIND_CFG, KIND_PLAYER = 0, 1, 2
KIND_NAMES = {"state": KIND_STATE, "cfg": KIND_CFG, "player": KIND_PLAYER}


def build_cave_b(every_until: int, stride: int, stride_until: int,
                 schedule: list[tuple[int, int, int, int]] | None = None) -> list[tuple[int, bytes]]:
    """Frame dump at the vga_present_frame call. Registers: esi = code delta, edi = data delta,
    ebp = g_cfg, ebx = file handle. Every jump is rel32 (holes may lie far apart).

    Round 6: the back buffer is written with width * height bytes (DAT_0012ed70 * DAT_0012ed78: 64000 in
    320x200, 307200 in 640x480), and an optional poke schedule [(raw tick, kind, offset, byte)] is applied
    on every playback tick right after the render of that tick (before the dump policy): an entry with raw
    tick N changes the state the simulation and the render of tick N + 1 start from, and shows in the state
    dump of tick N + 1. Table: 8-byte entries {u32 tick, u16 off, u8 kind, u8 value}; tick 0xfffffffe =
    continue at the (link-time) address in the next dword, 0xffffffff = end."""
    a = HoleAsm(CAVE_B_HOLES)
    JZ, JNZ, JBE, JA = 0x84, 0x85, 0x86, 0x87
    a.ins(b"\x60")                                            # pushad
    a.ins(b"\xe8\0\0\0\0")                                    # call $+5
    a.label("next")
    a.ins(b"\x5e")                                            # pop esi
    a.ins(b"\x81\xee\0\0\0\0", disp_at=2, disp_label="next")  # sub esi, next      (code delta)
    a.ins(b"\x8b\x86" + i32(CFG_PTR_OPERAND))                 # mov eax, [esi+0x32e95]  (= run-time &g_cfg)
    a.ins(b"\x89\xc7")                                        # mov edi, eax
    a.ins(b"\x81\xef" + i32(CFG_VAR))                         # sub edi, 0xadf74   (data delta)
    a.ins(b"\x8b\x28")                                        # mov ebp, [eax]     g_cfg
    a.ins(b"\xf6\x45\x00\x04")                                # test byte [ebp], 4  playback?
    a.ins(b"\x0f" + bytes([JZ]) + b"\0\0\0\0", 2, "done")
    a.ins(b"\x83\x7d\x09\x00")                                # cmp dword [ebp+9], 0  demo file open?
    a.ins(b"\x0f" + bytes([JZ]) + b"\0\0\0\0", 2, "done")
    a.ins(b"\x8b\x87" + i32(STATE_VAR))                       # mov eax, [edi+0xadf6c]  g_state
    a.ins(bytes([0x0F, 0xBF, 0x50, LOCAL_PLAYER_OFF]))        # movsx edx, word [eax+8]
    a.ins(b"\x69\xd2" + i32(PLAYER_REC_SIZE))                 # imul edx, edx, 0x801
    a.ins(b"\x8b\x84\x10" + i32(PLAYERS_OFF + TICK_OFF))      # mov eax, [eax+edx+0x341d]  tick
    if schedule:
        a.ins(b"\x50")                                        # push eax          tick
        a.ins(b"\x8d\x9e\0\0\0\0", disp_at=2, disp_label="tab0")  # lea ebx, [esi+tab0]
        a.label("sl")
        a.ins(b"\x8b\x0b")                                    # mov ecx, [ebx]
        a.ins(b"\x83\xf9\xff")                                # cmp ecx, -1      end
        a.ins(b"\x0f" + bytes([JZ]) + b"\0\0\0\0", 2, "send")
        a.ins(b"\x83\xf9\xfe")                                # cmp ecx, -2      link
        a.ins(b"\x0f" + bytes([JNZ]) + b"\0\0\0\0", 2, "snolink")
        a.ins(b"\x8b\x5b\x04")                                # mov ebx, [ebx+4]
        a.ins(b"\x01\xf3")                                    # add ebx, esi
        a.ins(b"\xe9\0\0\0\0", 1, "sl")
        a.label("snolink")
        a.ins(b"\x3b\x0c\x24")                                # cmp ecx, [esp]
        a.ins(b"\x0f" + bytes([JNZ]) + b"\0\0\0\0", 2, "snext")
        a.ins(b"\x0f\xb7\x4b\x04")                            # movzx ecx, word [ebx+4]   offset
        a.ins(b"\x8a\x53\x06")                                # mov dl, [ebx+6]   kind
        a.ins(b"\x89\xe8")                                    # mov eax, ebp      Config
        a.ins(b"\x80\xfa\x01")                                # cmp dl, 1
        a.ins(b"\x0f" + bytes([JZ]) + b"\0\0\0\0", 2, "swrite")
        a.ins(b"\x8b\x87" + i32(STATE_VAR))                   # mov eax, [edi+0xadf6c]  g_state
        a.ins(b"\x84\xd2")                                    # test dl, dl
        a.ins(b"\x0f" + bytes([JZ]) + b"\0\0\0\0", 2, "swrite")
        a.ins(bytes([0x0F, 0xBF, 0x50, LOCAL_PLAYER_OFF]))    # movsx edx, word [eax+8]
        a.ins(b"\x69\xd2" + i32(PLAYER_REC_SIZE))             # imul edx, edx, 0x801
        a.ins(b"\x8d\x84\x10" + i32(PLAYERS_OFF))              # lea eax, [eax+edx+0x340b]
        a.label("swrite")
        a.ins(b"\x8a\x53\x07")                                # mov dl, [ebx+7]   value
        a.ins(b"\x88\x14\x08")                                # mov [eax+ecx], dl
        a.label("snext")
        a.ins(b"\x83\xc3\x08")                                # add ebx, 8
        a.ins(b"\xe9\0\0\0\0", 1, "sl")
        a.label("send")
        a.ins(b"\x58")                                        # pop eax
    a.ins(b"\x3d" + i32(every_until))                         # cmp eax, EVERY_UNTIL
    a.ins(b"\x0f" + bytes([JBE]) + b"\0\0\0\0", 2, "save")
    a.ins(b"\x3d" + i32(stride_until))                        # cmp eax, STRIDE_UNTIL
    a.ins(b"\x0f" + bytes([JA]) + b"\0\0\0\0", 2, "done")
    a.ins(b"\x89\xc1")                                        # mov ecx, eax
    a.ins(b"\x31\xd2")                                        # xor edx, edx
    a.ins(b"\xbb" + i32(stride))                              # mov ebx, STRIDE
    a.ins(b"\xf7\xf3")                                        # div ebx
    a.ins(b"\x85\xd2")                                        # test edx, edx
    a.ins(b"\x0f" + bytes([JNZ]) + b"\0\0\0\0", 2, "done")
    a.ins(b"\x89\xc8")                                        # mov eax, ecx
    a.label("save")
    a.ins(b"\x81\xec" + i32(0x340))                           # sub esp, 0x340  (name 0x40 + palette 0x300)
    a.ins(b"\x50")                                            # push eax        tick
    a.ins(b"\x8d\x8e\0\0\0\0", disp_at=2, disp_label="fmt")   # lea ecx, [esi+fmt]
    a.ins(b"\x51")                                            # push ecx
    a.ins(b"\x8d\x4c\x24\x08")                                # lea ecx, [esp+8]   name buffer
    a.ins(b"\x51")                                            # push ecx
    a.ins(b"\xe8\0\0\0\0", 1, SPRINTF)                        # call sprintf_603bc
    a.ins(b"\x83\xc4\x0c")                                    # add esp, 0xc
    a.ins(b"\x66\xba\xc7\x03")                                # mov dx, 0x3c7
    a.ins(b"\x31\xc0")                                        # xor eax, eax
    a.ins(b"\xee")                                            # out dx, al     DAC read index 0
    a.ins(b"\xb2\xc9")                                        # mov dl, 0xc9   dx = 0x3c9
    a.ins(b"\x57")                                            # push edi
    a.ins(b"\x8d\x7c\x24\x44")                                # lea edi, [esp+0x44]  palette buffer
    a.ins(b"\xb9" + i32(0x300))                               # mov ecx, 0x300
    a.ins(b"\xfc")                                            # cld
    a.ins(b"\xf3\x6c")                                        # rep insb
    a.ins(b"\x5f")                                            # pop edi
    a.ins(b"\x68" + i32(0x222))                               # push 0x222
    a.ins(b"\x8d\x44\x24\x04")                                # lea eax, [esp+4]
    a.ins(b"\x50")                                            # push eax
    a.ins(b"\xe8\0\0\0\0", 1, FILE_OPEN)                      # call file_open_619a0
    a.ins(b"\x83\xc4\x08")                                    # add esp, 8
    a.ins(b"\x89\xc3")                                        # mov ebx, eax   (a -1 handle makes the writes fail harmlessly)
    a.ins(b"\x8b\x97" + i32(FB_PTR_VAR))                      # mov edx, [edi+0x12ed74]
    a.ins(b"\x8b\x8f" + i32(SCREEN_VARS))                     # mov ecx, [edi+0x12ed70]   width
    a.ins(b"\x0f\xaf\x8f" + i32(SCREEN_VARS + 8))              # imul ecx, [edi+0x12ed78]  * height
    a.ins(b"\xe8\0\0\0\0", 1, "wr")
    a.ins(b"\x8d\x54\x24\x40")                                # lea edx, [esp+0x40]  palette
    a.ins(b"\xb9" + i32(0x300))
    a.ins(b"\xe8\0\0\0\0", 1, "wr")
    a.ins(b"\x89\xea")                                        # mov edx, ebp   Config
    a.ins(b"\xb9" + i32(0x100))
    a.ins(b"\xe8\0\0\0\0", 1, "wr")
    a.ins(b"\x8b\x97" + i32(ANIM_VAR))                        # mov edx, [edi+0xadf50]  anim header
    a.ins(b"\xb9" + i32(6))
    a.ins(b"\x52")                                            # push edx
    a.ins(b"\xe8\0\0\0\0", 1, "wr")
    a.ins(b"\x5a")                                            # pop edx
    a.ins(b"\x0f\xb7\x0a")                                    # movzx ecx, word [edx]
    a.ins(b"\x6b\xc9\x1c")                                    # imul ecx, ecx, 0x1c
    a.ins(b"\x8b\x52\x02")                                    # mov edx, [edx+2]   records
    a.ins(b"\xe8\0\0\0\0", 1, "wr")
    for addr, size in DATA_REGIONS + [(SCREEN_VARS, 12)]:
        a.ins(b"\x8d\x97" + i32(addr))                        # lea edx, [edi+addr]
        a.ins(b"\xb9" + i32(size))
        a.ins(b"\xe8\0\0\0\0", 1, "wr")
    a.ins(b"\x53")                                            # push ebx
    a.ins(b"\xe8\0\0\0\0", 1, FILE_CLOSE)                     # call file_close_61a10
    a.ins(b"\x58")                                            # pop eax
    a.ins(b"\x81\xc4" + i32(0x340))                           # add esp, 0x340
    a.label("done")
    a.ins(b"\x61")                                            # popad
    a.ins(b"\xe9\0\0\0\0", 1, PRESENT)                        # jmp vga_present_frame_2f480
    a.label("wr")                                             # wr(edx = buffer, ecx = size) on handle ebx
    a.ins(b"\x51")                                            # push ecx
    a.ins(b"\x52")                                            # push edx
    a.ins(b"\x53")                                            # push ebx
    a.ins(b"\xe8\0\0\0\0", 1, FILE_WRITE)                     # call file_write_61e20
    a.ins(b"\x83\xc4\x0c")                                    # add esp, 0xc
    a.ins(b"\xc3")                                            # ret
    a.data(b"movie/fb%05d.dat\0", "fmt")
    if schedule:
        entries = [struct.pack("<IHBB", t & 0xFFFFFFFF, off, kind, val) for t, kind, off, val in schedule]
        k, chunk_no = 0, 0
        while True:
            # as many entries as the rest of the current hole holds, plus the link / end entry
            room = (a.holes[a.hi][1] - a.pos) // 8 - 1
            if room < 1:
                a.hi += 1
                if a.hi >= len(a.holes):
                    raise ValueError("poke schedule does not fit the holes")
                a.pos = a.holes[a.hi][0]
                a.chunks.append((a.pos, bytearray()))
                continue
            take = entries[k:k + room]
            k += len(take)
            name = f"tab{chunk_no}"
            last = k >= len(entries)
            blob = b"".join(take) + struct.pack("<II", 0xFFFFFFFF if last else 0xFFFFFFFE, 0)
            a.data(blob, name)
            if last:
                break
            a.disp.append((a.labels[name] + len(blob) - 4, f"tab{chunk_no + 1}"))
            chunk_no += 1
    return a.finish()


def linear_to_file(le: LEFile, addr: int) -> int:
    for o in le.objects:
        if o.base <= addr < o.base + o.virtual_size:
            rel = addr - o.base
            page = o.page_map_index - 1 + rel // le.page_size
            return le._page_file_offset(page) + rel % le.page_size  # noqa: SLF001
    raise ValueError(f"{addr:#x} not in any object")


def patch(src: Path, dst: Path, every_until: int, stride: int, stride_until: int, terrain_stride: int,
          fb_every_until: int, fb_stride: int, fb_stride_until: int, verify: bool, hud: bool = False,
          hires: bool = False, schedule: list[tuple[int, int, int, int]] | None = None, fe_script=None) -> None:
    le = LEFile.parse(src)
    import fe_cave
    fe_writes = fe_cave.build(fe_script) if fe_script is not None else []
    cave = build_cave(every_until, stride, stride_until, terrain_stride)
    cave_b = build_cave_b(fb_every_until, fb_stride, fb_stride_until, schedule)
    ranges = [(CAVE, CAVE + len(cave) + 3), (CALL_SITE, CALL_SITE + 5), (ROLL_PATCH, ROLL_PATCH + len(ROLL_NEW)),
              (PRESENT_SITE, PRESENT_SITE + 5), (HIRES_PATCH, HIRES_PATCH + len(HIRES_NEW))] +         [(b, b + len(c)) for b, c in cave_b] + [(b, b + len(c)) for b, c in fe_writes]
    # No fixup may point into the holes either (a pointer to dead code would make it live).
    objs = {i + 1: o for i, o in enumerate(le.objects)}
    # (exception: the jump table 0x24d54..0x24d6f of the dead terrain_ring_find_height_ne8_24d70 itself)
    tgt = [f for f in le.fixups if f.target_obj in objs and not 0x24D54 <= f.addr < 0x24D70
           for b, c in cave_b + [w for w in fe_writes if w[0] != fe_cave.SITE]
           if b <= objs[f.target_obj].base + f.target_off < b + len(c)]
    if tgt:
        raise SystemExit(f"fixup targets inside cave B: {[hex(f.addr) for f in tgt]}")
    # A fixup's 4 bytes must not overlap a written range.
    bad = [f for f in le.fixups for lo, hi in ranges if f.addr < hi and f.addr + 4 > lo]
    if bad:
        raise SystemExit(f"fixup sources inside the patched ranges: {[hex(f.addr) for f in bad]}")
    for b, c in cave_b:
        for lo, hi in CAVE_B_HOLES:
            if lo <= b < hi and b + len(c) > hi:
                raise SystemExit(f"cave B chunk {b:#x} overruns its hole")
    data = bytearray(src.read_bytes())

    def write(addr: int, blob: bytes):
        for i, b in enumerate(blob):
            data[linear_to_file(le, addr + i)] = b

    site = bytes(data[linear_to_file(le, CALL_SITE):linear_to_file(le, CALL_SITE) + 5])
    expect = b"\xe8" + struct.pack("<i", SOUND_UPDATE - (CALL_SITE + 5))
    if site != expect and site[:1] != b"\xe8":
        raise SystemExit(f"unexpected bytes at call site {CALL_SITE:#x}: {site.hex()}")
    psite = bytes(data[linear_to_file(le, PRESENT_SITE):linear_to_file(le, PRESENT_SITE) + 5])
    pexpect = b"\xe8" + struct.pack("<i", PRESENT - (PRESENT_SITE + 5))
    if psite != pexpect:
        raise SystemExit(f"unexpected bytes at present call {PRESENT_SITE:#x}: {psite.hex()}")
    write(CAVE, cave)
    write(CALL_SITE, b"\xe8" + struct.pack("<i", CAVE - (CALL_SITE + 5)))
    for b, c in cave_b:
        write(b, c)
    entry_b = cave_b[0][0]
    write(PRESENT_SITE, b"\xe8" + struct.pack("<i", entry_b - (PRESENT_SITE + 5)))
    roll_old = bytes(data[linear_to_file(le, ROLL_PATCH):linear_to_file(le, ROLL_PATCH) + len(ROLL_ORIG)])
    if roll_old != ROLL_ORIG and roll_old != ROLL_NEW:
        raise SystemExit(f"unexpected bytes at {ROLL_PATCH:#x}: {roll_old.hex()}")
    write(ROLL_PATCH, ROLL_NEW)
    inp_old = bytes(data[linear_to_file(le, INPUT_PATCH):linear_to_file(le, INPUT_PATCH) + 5])
    if inp_old not in (INPUT_ORIG, INPUT_NEW):
        raise SystemExit(f"unexpected bytes at {INPUT_PATCH:#x}: {inp_old.hex()}")
    write(INPUT_PATCH, INPUT_NEW)
    cd_old = bytes(data[linear_to_file(le, CD_PATCH):linear_to_file(le, CD_PATCH) + 2])
    if cd_old not in (CD_ORIG, CD_NEW):
        raise SystemExit(f"unexpected bytes at {CD_PATCH:#x}: {cd_old.hex()}")
    write(CD_PATCH, CD_NEW)
    hud_old = bytes(data[linear_to_file(le, HUD_PATCH):linear_to_file(le, HUD_PATCH) + 6])
    if hud_old != HUD_ORIG:
        raise SystemExit(f"unexpected bytes at {HUD_PATCH:#x}: {hud_old.hex()}")
    if hud:
        write(HUD_PATCH, HUD_NEW)
        print(f"flight HUD forced on during playback ({HUD_PATCH:#x} jne -> nops)")
    hires_old = bytes(data[linear_to_file(le, HIRES_PATCH):linear_to_file(le, HIRES_PATCH) + len(HIRES_ORIG)])
    if hires_old != HIRES_ORIG:
        raise SystemExit(f"unexpected bytes at {HIRES_PATCH:#x}: {hires_old.hex()}")
    if fe_writes:
        fsite = bytes(data[linear_to_file(le, fe_cave.SITE):linear_to_file(le, fe_cave.SITE) + 5])
        if fsite != fe_cave.SITE_ORIG:
            raise SystemExit(f"unexpected bytes at {fe_cave.SITE:#x}: {fsite.hex()}")
        for b, c in fe_writes:
            if b != fe_cave.SITE and not any(lo <= b and b + len(c) <= hi for lo, hi in fe_cave.ALL_HOLES):
                raise SystemExit(f"cave C write {b:#x}+{len(c)} outside its holes")
            write(b, c)
        print(f"front-end cave C: {len(fe_script)} script entries, " +
              ", ".join(f"{b:#x}+{len(c)}" for b, c in fe_writes))
    if hires:
        write(HIRES_PATCH, HIRES_NEW)
        print(f"640x480: config_parse stores video mode 8 ({HIRES_PATCH:#x})")
    # The schedule for the port side (render_reference2_test applies the same pokes at the same point):
    # one line per byte, "raw_tick kind offset value" (kind 0 GameState, 1 Config, 2 local PlayerRec).
    dst.parent.mkdir(parents=True, exist_ok=True)
    side = dst.with_name(dst.stem + ".schedule.txt")
    side.write_text("".join(f"{t} {k} {o} {v}\n" for t, k, o, v in (schedule or [])) +
                    f"# hires {int(hires)} hud {int(hud)}\n")
    # The front-end script for the port side (render_reference_fe): "state op wait a b" per entry.
    fe_side = dst.with_name(dst.stem + ".fescript.txt")
    if fe_script is not None:
        fe_side.write_text("".join(f"{s} {o} {w} {x} {y}\n" for s, o, w, x, y in fe_script))
    elif fe_side.exists():
        fe_side.unlink()
    dst.write_bytes(data)
    print(f"wrote {dst} ({len(data)} bytes); cave A {CAVE:#x}..{CAVE + len(cave):#x} ({len(cave)} bytes), "
          f"call site {CALL_SITE:#x} -> {CAVE:#x}")
    print("cave B chunks: " + ", ".join(f"{b:#x}+{len(c)}" for b, c in cave_b) +
          f"; present call {PRESENT_SITE:#x} -> {entry_b:#x}")
    print(f"state dumps: every tick <= {every_until}, then every {stride} ticks <= {stride_until}, "
          f"terrain every {terrain_stride} ticks")
    print(f"frame dumps: every tick <= {fb_every_until}, then every {fb_stride} ticks <= {fb_stride_until}")
    if verify:
        le2 = LEFile.parse(dst)
        lo, img = le2.flat_image()
        try:
            from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        except ImportError:
            print("capstone not installed; skipping listing")
            return
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        for b, c in cave_b:
            got = bytes(img[b - lo:b - lo + len(c)])
            assert got == c, f"cave B bytes differ after reparse at {b:#x}"
            print(f"  -- chunk {b:#x}")
            for ins in md.disasm(got, b):
                print(f"  {ins.address:06x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")
        for ins in md.disasm(bytes(img[PRESENT_SITE - lo:PRESENT_SITE - lo + 5]), PRESENT_SITE):
            print(f"  {ins.address:06x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")


def load_schedule(path: Path) -> list[tuple[int, int, int, int]]:
    """JSON list of pokes -> byte entries (raw tick = tick - 1: applied after the render of tick - 1)."""
    import json
    out = []
    for e in json.loads(path.read_text()):
        if "comment" in e and len(e) == 1:
            continue
        kinds = [k for k in KIND_NAMES if k in e]
        assert len(kinds) == 1, e
        off = e[kinds[0]]
        off = int(off, 0) if isinstance(off, str) else off
        size = e.get("size", 1)
        val = e["value"]
        val = int(val, 0) if isinstance(val, str) else val
        for i in range(size):
            out.append((e["tick"] - 1, KIND_NAMES[kinds[0]], off + i, (val >> (8 * i)) & 0xFF))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=str(ROOT / "MagicCarpet" / "magic" / "carpet.exe"))
    ap.add_argument("--dst", default=str(ROOT / "extracted" / "refgame_fb" / "magic" / "carpet.exe"))
    ap.add_argument("--every-until", type=int, default=1013, help="state dump every tick up to this tick")
    ap.add_argument("--stride", type=int, default=10, help="after that, state dump when tick %% stride == 0")
    ap.add_argument("--stride-until", type=int, default=8963)
    ap.add_argument("--terrain-stride", type=int, default=100)
    ap.add_argument("--fb-every-until", type=int, default=1013, help="frame dump every tick up to this tick")
    ap.add_argument("--fb-stride", type=int, default=10)
    ap.add_argument("--fb-stride-until", type=int, default=8963)
    ap.add_argument("--verify", action="store_true", help="re-parse and disassemble cave B")
    ap.add_argument("--hud", action="store_true", help="also draw the flight HUD during playback (HUD reference)")
    ap.add_argument("--hires", action="store_true", help="start the game in 640x480 (video mode 8)")
    ap.add_argument("--schedule", default=None,
                    help="JSON poke schedule: [{\"tick\": T, \"state\"|\"cfg\"|\"player\": off, \"value\": v, "
                         "\"size\": 1}] - the value is in place for the frame of tick T and later")
    ap.add_argument("--fe", default=None, help="front-end input script (JSON, see fe_cave.py): dump front-end frames")
    a = ap.parse_args(argv)
    schedule = load_schedule(Path(a.schedule)) if a.schedule else None
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import fe_cave
    fe_script = fe_cave.load_script(Path(a.fe)) if a.fe else None
    dst = Path(a.dst).resolve()
    if str(dst).lower().startswith(str((ROOT / "MagicCarpet").resolve()).lower()):
        raise SystemExit("refusing to write into MagicCarpet/")
    patch(Path(a.src), dst, a.every_until, a.stride, a.stride_until, a.terrain_stride,
          a.fb_every_until, a.fb_stride, a.fb_stride_until, a.verify, a.hud, a.hires, schedule, fe_script)


if __name__ == "__main__":
    main()
