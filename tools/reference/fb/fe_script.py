"""Writes the front-end tour script (fe_script.json) for patch_carpet.py --fe (round 6, task D).

    python fe_script.py [out.json]        (default: fe_script.json next to this file)

Steps are gated on the front-end state DAT_0012ed2e and counted in presents (vga_copy_320x200_610f0
calls), see fe_cave.py. Dump numbers name the screens; render_reference_fe (the port side) runs the same
script against fe_frame and compares each dump. Coordinates are 320x200 pixels here, doubled to the
640x400 virtual mouse space in the file.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

# main-menu items: the mmmask.dat pixel of each item closest to its centroid (320x200)
ITEM = {1: (27, 66), 2: (56, 162), 3: (133, 128), 4: (130, 176), 5: (245, 18), 6: (240, 53), 7: (255, 88),
        8: (245, 118), 9: (252, 148), 10: (245, 179), 11: (120, 48)}


def build() -> list[dict]:
    es: list[dict] = []

    def move(x, y, state="any", wait=2):
        es.append({"state": state, "wait": wait, "op": "move", "a": x * 2, "b": y * 2})

    def click(x, y, state="any", wait=2):
        move(x, y, state, wait)
        es.append({"state": "any", "wait": 2, "op": "ldown"})
        es.append({"state": "any", "wait": 2, "op": "lup"})

    def rclick(state="any", wait=2):
        es.append({"state": state, "wait": wait, "op": "rdown"})
        es.append({"state": "any", "wait": 2, "op": "rup"})

    def key(sc, state="any", wait=2):
        es.append({"state": state, "wait": wait, "op": "kdown", "a": sc})
        es.append({"state": "any", "wait": 2, "op": "kup", "a": sc})

    def dump(n, state="any", wait=40):
        es.append({"state": state, "wait": wait, "op": "dump", "a": n})

    # 6: language (pointer at the centre, where input_mouse_init_5bc14 puts it)
    dump(1, state=6, wait=40)
    click(0xB3 + 10, 0x2B + 10, wait=5)        # German flag: the selection frame moves
    dump(2, wait=30)
    click(0x40 + 10, 0x2B + 10, wait=5)        # English
    dump(3, wait=30)
    click(0x119 + 5, 0x73 + 5, wait=5)         # OK
    # 1: config (sound summary of sndsetup.dat, input device page)
    dump(4, state=1, wait=40)
    click(0xAB + 20, 0x2A + 30, state=1, wait=5)   # next input device page
    dump(5, wait=30)
    click(0xAB + 20, 0x2A + 30, wait=5)
    click(0xAB + 20, 0x2A + 30, wait=5)
    click(0xAB + 20, 0x2A + 30, wait=5)
    click(0xAB + 20, 0x2A + 30, wait=5)        # back to page 0 (mouse)
    dump(6, wait=30)
    click(0x118 + 5, 0x79 + 5, wait=5)         # OK -> logos (no Intel logo: Config.pentium 0 in DOSBox)
    # 9, 0, 8: Bullfrog logo, intro, title run by themselves (FLIC frames: compared with a phase search)
    dump(7, state=9, wait=20)
    dump(8, state=8, wait=10)
    # 2: main menu
    dump(10, state=2, wait=80)
    move(*ITEM[4], wait=2)                     # hover: Quit
    dump(11, wait=30)
    move(*ITEM[1], wait=2)                     # hover: new / resume game
    dump(12, wait=30)
    move(*ITEM[11], wait=2)                    # hover: start level (disabled at program start)
    dump(13, wait=30)
    click(*ITEM[5], wait=2)                    # load: the six slots
    dump(14, wait=40)
    move(*ITEM[7], wait=2)                     # hover a slot
    dump(15, wait=30)
    rclick(wait=2)                             # back to the menu buttons
    dump(16, wait=40)
    click(*ITEM[6], wait=2)                    # save: the six slots
    dump(17, wait=40)
    rclick(wait=2)
    dump(18, wait=40)
    click(*ITEM[2], wait=2)                    # name / call-name dialog
    dump(19, wait=150)
    key(0x1C, wait=4)                          # Enter, Enter
    key(0x1C, wait=10)
    dump(20, wait=60)
    click(*ITEM[4], wait=2)                    # quit: Yes / No
    dump(21, wait=150)
    click(0xF0 + 4, 0x69 + 4, wait=2)          # No
    dump(22, wait=60)
    # save into slot 1: the slot dialog (scroll), Enter ends the text entry, Enter accepts
    click(*ITEM[6], wait=2)
    dump(23, wait=40)
    click(*ITEM[5], wait=2)
    dump(24, wait=150)
    key(0x1C, wait=4)
    dump(25, wait=40)
    key(0x1C, wait=4)
    dump(26, wait=80)
    # load: the slot list with the new name, the "load?" question, No
    click(*ITEM[5], wait=2)
    dump(27, wait=40)
    click(*ITEM[5], wait=2)
    dump(28, wait=150)
    click(0xF0 + 4, 0x69 + 4, wait=2)          # No
    dump(29, wait=60)
    rclick(wait=2)
    # item 1 at program start (DAT_0009e500 = 1): starts Config.level at once (state 5 is set before the
    # level). In the level (the front-end state stays 5): I opens the chat line, "RATTY" + Enter opens the
    # cheat gate (Config.flags |= 0x8000), Shift+C wins the level (status bit 2), Space leaves it (status
    # 10) -> the result screen (state 5: levelw1/w2 FLIC, then pperf with the statistics).
    click(*ITEM[1], wait=20)
    # every in-game entry is gated on state 5 (render_reference_fe_test consumes them without a level)
    def k5(sc, wait, op=None):
        if op is None:
            es.append({"state": 5, "wait": wait, "op": "kdown", "a": sc})
            es.append({"state": 5, "wait": 2, "op": "kup", "a": sc})
        else:
            es.append({"state": 5, "wait": wait, "op": op, "a": sc})
    k5(0x17, 300)                              # I: chat
    for sc in (0x13, 0x1E, 0x14, 0x14, 0x15):  # R A T T Y
        k5(sc, 20)
    k5(0x1C, 20)                               # Enter: send
    k5(0x2A, 40, "kdown")                      # Shift+C
    k5(0x2E, 4)
    k5(0x2A, 4, "kup")
    k5(0x39, 60)                               # Space: leave the level
    # the result screen: the statistics lines appear one by one over 0x168 timer ticks while the screen
    # presents some 7000 frames a second in DOSBox (the state is already 2 during the statistics part)
    dump(40, wait=30000)
    key(0x39, wait=4)                          # Space: back to the main menu
    dump(30, state=2, wait=80)                 # main menu again: start level (item 11) enabled now
    move(*ITEM[11], wait=2)
    dump(31, wait=30)
    click(*ITEM[1], wait=2)                    # "New game?" Yes / No
    dump(32, wait=150)
    click(0xF0 + 4, 0x69 + 4, wait=2)          # No
    dump(33, wait=60)
    return es


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    out = Path(argv[0]) if argv else Path(__file__).resolve().parent / "fe_script.json"
    es = build()
    out.write_text(json.dumps(es, indent=0))
    print(f"{len(es)} entries -> {out}")


if __name__ == "__main__":
    main()
