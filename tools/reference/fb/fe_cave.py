"""Cave C (round 6, task D): scripted input and frame dumps for the FRONT END of the original.

The front end always runs in 320x200 (port_render_reference2.md) and several of its screens loop inside
one frontend_menu_loop_52070 call (language screen, dialogs, fades), so the hook is at the one place every
displayed frame passes: vga_copy_320x200_610f0, right after mouse_cursor_hide_for_blit_5b7f8 drew the
pointer into the back buffer and before the `rep movsd` to 0xa0000 (`mov edi, 0xa0000` at 0x61104 becomes
`call cave C`, which ends with that instruction). A "frame" below is one such present. Per present:

  * if a dump was requested by the previous present's script step, writes movie/fe%05d.dat (the dump
    number) with the game's own sprintf_603bc / file_open_619a0 / file_write_61e20 / file_close_61a10;
  * runs the input script: entries fire in order; an entry fires when the front-end state DAT_0012ed2e
    equals its state (0xfe = any) and at least `wait` presents passed since the previous entry fired;
    several entries can fire at one present (wait 0). The next fe_input_poll_57cc0 sees the result.
    Operations write what the int 33h / int 9 handlers write (input.h of the port has the same globals):
      1 move    a, b -> g_mouse_x / g_mouse_y (DAT_0009e5dc / de, 640x400 virtual)
      2 left down      g_mouse_click_x / y (9e5d8 / da) = position, held_left (12ee14) = 1, click_left (12ee0e) = 1
      3 left up        held_left = 0
      4 right down     click position, held_right (12ee12) = 1, click_right (12ee0c) = 1
      5 right up       held_right = 0
      6 key down  a    g_key_down[a] (12ee20 + a) = 1, g_key_last (12eea0) = a
      7 key up    a    g_key_down[a] = 0, g_key_last = a | 0x80
      8 dump      a    dump number a at the NEXT present
    Table: 8-byte entries {u8 state, u8 op, u16 wait, i16 a, i16 b}; op 0 = end, op 0xfe = continue at the
    link-time address in the dword at +4 (the table is spread over several dead functions).

Dump layout (fe%05d.dat):
    0x00000  64000  back buffer [DAT_0012ed74] = the frame going to the screen, pointer included
    0x0fa00    768  DAC palette (ports 0x3c7 / 0x3c9, 6-bit)
    0x0fd00  64000  front-end background buffer [DAT_000adf68]
    0x1f700  0x500  data 0x12ea00..0x12eeff (front-end globals 0x12ec00.., mouse / key state, g_timer_ticks 0x12eab4)
    0x1fc00  0x300  data 0x9e300..0x9e5ff (sound / music flags, DAT_0009e500 / 504, mouse position 0x9e5dc)
    0x1ff00  0x100  Config bytes 0..0xff
    0x20000     12  data 0x12ed70..0x12ed7b (width, back buffer pointer, height)
    0x2000c  0x801  the local player's PlayerRec (GameState + 0x340b + local * 0x801: status, name, P block
                    with the level statistics of the result screen)

The cave's three variables live in the dead code too (DOS/4GW's flat data selector writes code pages).
Holes (each: no caller in the call graph, no fixup source inside, no fixup target or rel call / jmp into it):
projectile_line_of_fire_clear_466af (549 bytes, the code, variables and the format string),
crt_heap_realloc_dpmi_block_70bf6 (the dump subroutine), tmap_cache_add_4c460, input_mouse_set_range_4a170,
thing_try_damage_116e0, terrain_set_cell_height_3d7d0, terrain_max_drop_around_2376f, ui_draw_map_43910,
castle_near_thing_11820, ai_goal_repair_castle_12d70, thing_find_in_sight_of_class_3e9a0
(the script table).
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

SITE = 0x61104              # vga_copy_320x200_610f0: `mov edi, 0xa0000` after the pointer went into the back buffer
SITE_ORIG = bytes.fromhex("bf00000a00")
CODE = 0x466B0
CODE_END = 0x468A0
V_N, V_PTR, V_DUMP = 0x468A0, 0x468A4, 0x468A8
FMT = 0x468AC               # "movie/fe%05d.dat\0" (17 bytes, ends 0x468bd < 0x468d4)
DUMP_CODE, DUMP_END = 0x70C0C, 0x70D10       # the dump subroutine (crt_heap_realloc_dpmi_block_70bf6)
TABLE_HOLES = [(0x4C460, 0x4C545), (0x4A170, 0x4A1AE), (0x116E0, 0x117B6), (0x3D7D0, 0x3D82B), (0x3D845, 0x3D908),
               (0x4A1B2, 0x4A28F), (0x2376F, 0x237A8), (0x43910, 0x4394C), (0x43A00, 0x43A34),
               (0x1185E, 0x118B4), (0x12D9E, 0x12DEE), (0x3E9B1, 0x3EA2A)]
ALL_HOLES = [(0x466AF, 0x468D4), (DUMP_CODE, DUMP_END)] + TABLE_HOLES

SPRINTF, FILE_OPEN, FILE_WRITE, FILE_CLOSE = 0x603BC, 0x619A0, 0x61E20, 0x61A10
CFG_PTR_OPERAND, CFG_VAR, STATE_VAR = 0x32E95, 0xADF74, 0xADF6C
FE_STATE = 0x12ED2E
MOUSE_X, MOUSE_Y, CLICK_X, CLICK_Y = 0x9E5DC, 0x9E5DE, 0x9E5D8, 0x9E5DA
HELD_L, HELD_R, CLICK_L, CLICK_R = 0x12EE14, 0x12EE12, 0x12EE0E, 0x12EE0C
KEY_DOWN, KEY_LAST = 0x12EE20, 0x12EEA0
FB_PTR = 0x12ED74
BG_PTR = 0xADF68

OPS = {"move": 1, "ldown": 2, "lup": 3, "rdown": 4, "rup": 5, "kdown": 6, "kup": 7, "dump": 8}


def i32(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFFFFFF)


def i16(v: int) -> bytes:
    return struct.pack("<H", v & 0xFFFF)


class Emit:
    def __init__(self, base: int):
        self.base, self.code, self.labels, self.rel = base, bytearray(), {}, []

    def here(self):
        return self.base + len(self.code)

    def label(self, n):
        self.labels[n] = self.here()

    def b(self, *parts):
        for p in parts:
            self.code += p if isinstance(p, (bytes, bytearray)) else bytes([p])

    def j(self, op: bytes, target):            # op = e8 / e9 / 0f 8x; target = label or absolute
        self.b(op)
        self.rel.append((len(self.code), target))
        self.b(b"\0\0\0\0")

    def finish(self) -> bytes:
        for off, t in self.rel:
            dest = self.labels[t] if isinstance(t, str) else t
            struct.pack_into("<i", self.code, off, dest - (self.base + off + 4))
        return bytes(self.code)


JE, JNE, JB = b"\x0f\x84", b"\x0f\x85", b"\x0f\x82"
CALL, JMP = b"\xe8", b"\xe9"


def build_code() -> bytes:
    a = Emit(CODE)
    a.b(0x60)                                                # pushad
    a.j(CALL, "next"); a.label("next")                       # call $+5
    a.b(0x5E)                                                # pop esi
    a.b(b"\x81\xee", i32(a.labels["next"]))                  # sub esi, next          code delta
    a.b(b"\x8b\x86", i32(CFG_PTR_OPERAND))                   # mov eax, [esi+0x32e95] (= run-time &g_cfg)
    a.b(b"\x89\xc7")                                         # mov edi, eax
    a.b(b"\x81\xef", i32(CFG_VAR))                           # sub edi, 0xadf74       data delta
    a.b(b"\x8b\x28")                                         # mov ebp, [eax]         g_cfg
    a.b(b"\x83\xbe", i32(V_PTR), 0)                          # cmp dword [esi+V_PTR], 0
    a.j(JNE, "inited")
    a.b(b"\x8d\x86", i32(TABLE_HOLES[0][0]))                 # lea eax, [esi+tab0]
    a.b(b"\x89\x86", i32(V_PTR))                             # mov [esi+V_PTR], eax
    a.b(b"\xc7\x86", i32(V_DUMP), i32(-1))                   # mov dword [esi+V_DUMP], -1
    a.label("inited")
    a.b(b"\x8b\x86", i32(V_DUMP))                            # mov eax, [esi+V_DUMP]
    a.b(b"\x83\xf8\xff")                                     # cmp eax, -1
    a.j(JE, "nodump")
    a.j(CALL, DUMP_CODE)
    a.b(b"\xc7\x86", i32(V_DUMP), i32(-1))                   # mov dword [esi+V_DUMP], -1
    a.label("nodump")
    a.b(b"\xff\x86", i32(V_N))                               # inc dword [esi+V_N]
    a.label("L")
    a.b(b"\x8b\x9e", i32(V_PTR))                             # mov ebx, [esi+V_PTR]
    a.b(b"\x8a\x43\x01")                                     # mov al, [ebx+1]   op
    a.b(b"\x84\xc0")                                         # test al, al
    a.j(JE, "done")
    a.b(b"\x3c\xfe")                                         # cmp al, 0xfe      link
    a.j(JNE, "notlink")
    a.b(b"\x8b\x5b\x04")                                     # mov ebx, [ebx+4]
    a.b(b"\x01\xf3")                                         # add ebx, esi
    a.b(b"\x89\x9e", i32(V_PTR))                             # mov [esi+V_PTR], ebx
    a.j(JMP, "L")
    a.label("notlink")
    a.b(b"\x8a\x03")                                         # mov al, [ebx]     state
    a.b(b"\x3c\xfe")                                         # cmp al, 0xfe
    a.j(JE, "any")
    a.b(b"\x3a\x87", i32(FE_STATE))                          # cmp al, [edi+0x12ed2e]
    a.j(JNE, "done")
    a.label("any")
    a.b(b"\x0f\xb7\x4b\x02")                                 # movzx ecx, word [ebx+2]   wait
    a.b(b"\x39\x8e", i32(V_N))                               # cmp [esi+V_N], ecx
    a.j(JB, "done")
    a.b(b"\xc7\x86", i32(V_N), i32(0))                       # mov dword [esi+V_N], 0
    a.b(b"\x8d\x43\x08")                                     # lea eax, [ebx+8]
    a.b(b"\x89\x86", i32(V_PTR))                             # mov [esi+V_PTR], eax
    a.b(b"\x0f\xbf\x4b\x04")                                 # movsx ecx, word [ebx+4]   a
    a.b(b"\x0f\xbf\x53\x06")                                 # movsx edx, word [ebx+6]   b
    a.b(b"\x8a\x43\x01")                                     # mov al, [ebx+1]
    for op, name in ((1, "o1"), (2, "o2"), (3, "o3"), (4, "o4"), (5, "o5"), (6, "o6"), (7, "o7"), (8, "o8")):
        a.b(b"\x3c", op)                                     # cmp al, op
        a.j(JE, name)
    a.j(JMP, "L")
    a.label("o1")
    a.b(b"\x66\x89\x8f", i32(MOUSE_X))                       # mov [edi+x], cx
    a.b(b"\x66\x89\x97", i32(MOUSE_Y))                       # mov [edi+y], dx
    a.j(JMP, "L")
    a.label("o2")
    a.j(CALL, "clickpos")
    a.b(b"\x66\xc7\x87", i32(HELD_L), i16(1))                # mov word [edi+held_l], 1
    a.b(b"\x66\xc7\x87", i32(CLICK_L), i16(1))
    a.j(JMP, "L")
    a.label("o3")
    a.b(b"\x66\xc7\x87", i32(HELD_L), i16(0))
    a.j(JMP, "L")
    a.label("o4")
    a.j(CALL, "clickpos")
    a.b(b"\x66\xc7\x87", i32(HELD_R), i16(1))
    a.b(b"\x66\xc7\x87", i32(CLICK_R), i16(1))
    a.j(JMP, "L")
    a.label("o5")
    a.b(b"\x66\xc7\x87", i32(HELD_R), i16(0))
    a.j(JMP, "L")
    a.label("o6")
    a.b(b"\xc6\x84\x0f", i32(KEY_DOWN), 1)                   # mov byte [edi+ecx+keydown], 1
    a.b(b"\x88\x8f", i32(KEY_LAST))                          # mov [edi+keylast], cl
    a.j(JMP, "L")
    a.label("o7")
    a.b(b"\xc6\x84\x0f", i32(KEY_DOWN), 0)
    a.b(b"\x80\xc9\x80")                                     # or cl, 0x80
    a.b(b"\x88\x8f", i32(KEY_LAST))
    a.j(JMP, "L")
    a.label("o8")
    a.b(b"\x89\x8e", i32(V_DUMP))                            # mov [esi+V_DUMP], ecx
    a.j(JMP, "L")
    a.label("done")
    a.b(0x61)                                                # popad
    a.b(b"\xbf", i32(0xA0000))                               # mov edi, 0xa0000   (the replaced instruction)
    a.b(0xC3)                                                # ret
    a.label("clickpos")
    a.b(b"\x66\x8b\x87", i32(MOUSE_X))                       # mov ax, [edi+x]
    a.b(b"\x66\x89\x87", i32(CLICK_X))
    a.b(b"\x66\x8b\x87", i32(MOUSE_Y))
    a.b(b"\x66\x89\x87", i32(CLICK_Y))
    a.b(0xC3)
    code = a.finish()
    if CODE + len(code) > CODE_END:
        raise ValueError(f"cave C code too big: {len(code)} bytes")
    return code


def build_dump() -> bytes:
    # dump(eax = number); registers esi / edi / ebp as in the main code; ebx = file handle
    a = Emit(DUMP_CODE)
    a.b(b"\x81\xec", i32(0x340))                             # sub esp, 0x340   name 0x40 + DAC 0x300
    a.b(0x50)                                                # push eax
    a.b(b"\x8d\x8e", i32(FMT))                               # lea ecx, [esi+fmt]
    a.b(0x51)
    a.b(b"\x8d\x4c\x24\x08")                                 # lea ecx, [esp+8]
    a.b(0x51)
    a.j(CALL, SPRINTF)
    a.b(b"\x83\xc4\x0c")
    a.b(b"\x66\xba\xc7\x03")                                 # mov dx, 0x3c7
    a.b(b"\x31\xc0")
    a.b(0xEE)                                                # out dx, al
    a.b(b"\xb2\xc9")                                         # mov dl, 0xc9
    a.b(0x57)                                                # push edi
    a.b(b"\x8d\x7c\x24\x44")                                 # lea edi, [esp+0x44]
    a.b(b"\xb9", i32(0x300))
    a.b(0xFC)
    a.b(b"\xf3\x6c")                                         # rep insb
    a.b(0x5F)                                                # pop edi
    a.b(b"\x68", i32(0x222))
    a.b(b"\x8d\x44\x24\x04")
    a.b(0x50)
    a.j(CALL, FILE_OPEN)
    a.b(b"\x83\xc4\x08")
    a.b(b"\x89\xc3")                                         # mov ebx, eax
    a.b(b"\x8b\x97", i32(FB_PTR))                            # mov edx, [edi+0x12ed74]  the frame being presented
    a.b(b"\xb9", i32(64000))
    a.j(CALL, "wr")
    a.b(b"\x8d\x54\x24\x40")                                 # lea edx, [esp+0x40]   DAC
    a.b(b"\xb9", i32(0x300))
    a.j(CALL, "wr")
    a.b(b"\x8b\x97", i32(BG_PTR))                            # mov edx, [edi+0xadf68]   front-end background
    a.b(b"\xb9", i32(64000))
    a.j(CALL, "wr")
    for addr, size in ((0x12EA00, 0x500), (0x9E300, 0x300)):
        a.b(b"\x8d\x97", i32(addr))                          # lea edx, [edi+addr]
        a.b(b"\xb9", i32(size))
        a.j(CALL, "wr")
    a.b(b"\x89\xea")                                         # mov edx, ebp   Config
    a.b(b"\xb9", i32(0x100))
    a.j(CALL, "wr")
    a.b(b"\x8d\x97", i32(0x12ED70))
    a.b(b"\xb9", i32(12))
    a.j(CALL, "wr")
    a.b(b"\x8b\x87", i32(STATE_VAR))                         # mov eax, [edi+0xadf6c]   g_state
    a.b(b"\x0f\xbf\x50\x08")                                 # movsx edx, word [eax+8]  local player
    a.b(b"\x69\xd2", i32(0x801))                             # imul edx, edx, 0x801
    a.b(b"\x8d\x94\x10", i32(0x340B))                        # lea edx, [eax+edx+0x340b] its PlayerRec
    a.b(b"\xb9", i32(0x801))
    a.j(CALL, "wr")
    a.b(0x53)                                                # push ebx
    a.j(CALL, FILE_CLOSE)
    a.b(0x58)
    a.b(b"\x81\xc4", i32(0x340))
    a.b(0xC3)
    a.label("wr")
    a.b(0x51, 0x52, 0x53)                                    # push ecx / edx / ebx
    a.j(CALL, FILE_WRITE)
    a.b(b"\x83\xc4\x0c")
    a.b(0xC3)
    code = a.finish()
    if DUMP_CODE + len(code) > DUMP_END:
        raise ValueError(f"cave C dump code too big: {len(code)} bytes")
    return code


def load_script(path: Path) -> list[tuple[int, int, int, int, int]]:
    """JSON list of {"state": S | "any", "wait": N, "op": name, "a": .., "b": ..} -> entries."""
    out = []
    for e in json.loads(path.read_text()):
        if "op" not in e:
            continue
        st = e.get("state", "any")
        out.append((0xFE if st == "any" else int(st), OPS[e["op"]], int(e.get("wait", 0)), int(e.get("a", 0)),
                    int(e.get("b", 0))))
    return out


def build_table(entries) -> list[tuple[int, bytes]]:
    """Spread the entries over TABLE_HOLES with link entries; returns [(address, bytes)]."""
    blobs = [struct.pack("<BBHhh", s, op, w, a, b) for s, op, w, a, b in entries]
    out, k = [], 0
    for hi, (lo_, hi_) in enumerate(TABLE_HOLES):
        room = (hi_ - lo_) // 8 - 1
        take = blobs[k:k + room]
        k += len(take)
        if k >= len(blobs):
            out.append((lo_, b"".join(take) + struct.pack("<BBHI", 0, 0, 0, 0)))
            return out
        if hi + 1 >= len(TABLE_HOLES):
            break
        out.append((lo_, b"".join(take) + struct.pack("<BBHI", 0, 0xFE, 0, TABLE_HOLES[hi + 1][0])))
    raise ValueError(f"front-end script too long ({len(entries)} entries)")


def build(script) -> list[tuple[int, bytes]]:
    """All writes of cave C: code, variables (V_N 0, V_PTR 0 = not initialised, V_DUMP -1), format, table, site."""
    writes = [(CODE, build_code()), (DUMP_CODE, build_dump()), (V_N, struct.pack("<IIi", 0, 0, -1)), (FMT, b"movie/fe%05d.dat\0")]
    writes += build_table(script)
    writes.append((SITE, b"\xe8" + struct.pack("<i", CODE - (SITE + 5))))
    return writes
