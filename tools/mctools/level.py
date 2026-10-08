"""LEVxxxxx.DAT level file parser for Magic Carpet 1.

Decompressed level files are exactly 38,812 (0x979C) bytes:

    0x0000  48     GEN_MAP header: 12 x int32 LE
                   [0]=unknown/id, Seed, Off, Raise, Gnarl, River, Sourc,
                   SnLin, SnFlt, BhLin, BhFlt, RkSte
    0x0030  1042   reserved (zeros in retail levels)
    0x0442  37710  THING_INIT table: 2095 slots x 18 bytes
                   u16 Class, Model, Xpos, Ypos, DisId, SwiSz, SwiId, Parent, Child
    0x9790  12     footer: 6 x u16

Field names come from the GAM00088.DAT text save (GEN_MAP / THING_INIT
column headers) that ships with the game; see docs/reference.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from . import rnc

LEVEL_SIZE = 38812
HEADER_FIELDS = ["unk0", "seed", "off", "raise", "gnarl", "river", "sourc",
                 "snlin", "snflt", "bhlin", "bhflt", "rkste"]
THINGS_OFFSET = 0x442
THING_SIZE = 18
THING_COUNT = 2095
FOOTER_OFFSET = 0x9790

CLASS_NAMES = {2: "Scenery", 3: "Player", 5: "Creature", 7: "Weather", 10: "Effect",
               11: "Switch", 12: "Spell"}

MODEL_NAMES = {
    2: {0: "Tree", 1: "Standing stone", 2: "Dolmen", 3: "Bad stone", 4: "2D dome", 5: "2D dome"},
    3: {4: "Flyer1", 5: "Flyer2", 6: "Flyer3", 7: "Flyer4", 8: "Flyer5", 9: "Flyer6/7",
        10: "Flyer8", 11: "Flyer6/7"},
    5: {0: "Dragon", 1: "Vulture", 2: "Bee", 3: "Worm", 4: "Archer", 5: "Crab", 6: "Kraken",
        7: "Troll", 8: "Griffon", 9: "Skeleton", 10: "Emu", 11: "Genie", 12: "Builder",
        13: "Townie", 14: "Trader", 16: "Wyvern"},
    7: {4: "Wind"},
    10: {0: "Explosion", 1: "Big explosion", 5: "Splash", 6: "Fire", 8: "Mini volcano",
         9: "Volcano", 11: "Crater", 13: "White smoke", 14: "Black smoke", 15: "Earthquake",
         17: "Meteor", 23: "Lightning", 24: "Rain of fire", 25: "Steal mana", 28: "Wall",
         29: "Path", 31: "Canyon", 34: "Teleport", 39: "Mana ball", 45: "Wizard",
         50: "Ridge node", 52: "Crab egg"},
    11: {0: "Hidden inside", 1: "Hidden outside", 2: "Hidden inside re", 4: "On victory",
         5: "Death inside", 6: "Death outside", 7: "Death inside re", 9: "Obvious inside",
         10: "Obvious outside", 13: "Dragon trigger", 16: "None trigger", 18: "Crab trigger",
         19: "Kraken trigger", 20: "Troll trigger", 24: "Genie trigger", 30: "Creature all"},
    12: {0: "Fireball", 1: "Heal", 2: "Alliance", 3: "Possession", 4: "Shield",
         5: "Beyond sight", 6: "Earthquake", 7: "Meteor", 8: "Volcano", 9: "Crater",
         10: "Teleport", 11: "Rubber band", 12: "Invisible", 13: "Steal mana", 14: "Rebound",
         15: "Lightning", 16: "Castle", 17: "Skeleton", 18: "Thunderbolt", 19: "Mana magnet",
         20: "Fire wall", 21: "Reverse speed", 22: "Smart bomb", 23: "Mini fireball"},
}


@dataclass
class Thing:
    slot: int
    cls: int
    model: int
    x: int
    y: int
    dis_id: int
    swi_sz: int
    swi_id: int
    parent: int
    child: int

    @property
    def class_name(self) -> str:
        return CLASS_NAMES.get(self.cls, f"class{self.cls}")

    @property
    def model_name(self) -> str:
        return MODEL_NAMES.get(self.cls, {}).get(self.model, f"model{self.model}")

    def pack(self) -> bytes:
        return struct.pack("<9H", self.cls, self.model, self.x, self.y, self.dis_id,
                           self.swi_sz, self.swi_id, self.parent, self.child)


@dataclass
class Level:
    header: dict = field(default_factory=dict)
    reserved: bytes = b""
    things: list[Thing] = field(default_factory=list)
    footer: list[int] = field(default_factory=list)

    @classmethod
    def parse(cls, data: bytes) -> "Level":
        data = rnc.unpack_if_rnc(data)
        if len(data) != LEVEL_SIZE:
            raise ValueError(f"unexpected level size {len(data)} (want {LEVEL_SIZE})")
        vals = struct.unpack_from("<12i", data, 0)
        lv = cls(header=dict(zip(HEADER_FIELDS, vals)))
        lv.reserved = data[0x30:THINGS_OFFSET]
        for i in range(THING_COUNT):
            o = THINGS_OFFSET + i * THING_SIZE
            f = struct.unpack_from("<9H", data, o)
            lv.things.append(Thing(i, *f))
        lv.footer = list(struct.unpack_from("<6H", data, FOOTER_OFFSET))
        return lv

    def pack(self) -> bytes:
        out = bytearray(LEVEL_SIZE)
        struct.pack_into("<12i", out, 0, *[self.header[k] for k in HEADER_FIELDS])
        out[0x30:THINGS_OFFSET] = self.reserved.ljust(THINGS_OFFSET - 0x30, b"\0")
        for t in self.things:
            o = THINGS_OFFSET + t.slot * THING_SIZE
            out[o:o + THING_SIZE] = t.pack()
        struct.pack_into("<6H", out, FOOTER_OFFSET, *self.footer)
        return bytes(out)

    @property
    def active_things(self) -> list[Thing]:
        return [t for t in self.things if t.cls != 0]

    def summary(self) -> dict:
        from collections import Counter
        c = Counter((t.class_name, t.model_name) for t in self.active_things)
        return {"header": self.header, "footer": self.footer,
                "thing_count": len(self.active_things),
                "counts": {f"{k[0]}/{k[1]}": v for k, v in sorted(c.items())}}


def to_gam_text(level: Level) -> str:
    """Render in the same text format as GAM00088.DAT (handy for diffing)."""
    h = level.header
    lines = ["//map generation parameters",
             "//          Seed  Off   Raise Gnarl River Sourc SnLin SnFlt BhLin BhFlt RkSte",
             "//          -----------------------------------------------------------------",
             "GEN_MAP     " + " ".join(f"{h[k]:05d}" for k in HEADER_FIELDS[1:]),
             "", "//list of inits to be generated at run time",
             "//          Class Model Xpos  Ypos  DisId SwiSz SwiId Paren Child",
             "//          -----------------------------------------------------"]
    for t in level.active_things:
        lines.append(f"THING_INIT  {t.slot + 1:04d}  {t.cls:05d} {t.model:05d} {t.x:05d} {t.y:05d} "
                     f"{t.dis_id:05d} {t.swi_sz:05d} {t.swi_id:05d} {t.parent:05d} {t.child:05d}")
    return "\n".join(lines) + "\n"
