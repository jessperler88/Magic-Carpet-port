"""Install the Hidden Worlds data set for mcport (world_set.h) from your own Magic Carpet Plus CD.

The GOG release keeps the CD as a plain ISO 9660 image (CARPET.CD/game.gog). This copies the files
HIDDEN.EXE loads instead of the base game's (plus HIDDEN.EXE itself: mcport reads the level names
from it) into <game dir>/hidden/ with lower-case names. Nothing else is changed.

usage:
  python tools/port/install_hidden_worlds.py <game dir> [<source>]

<source> is the CD image (game.gog / .iso) or an extracted CARPET folder of the CD; default: the GOG
install at C:/Program Files/GOG Galaxy/Games/Magic Carpet Plus/CARPET.CD/game.gog.
"""
import os
import struct
import sys

DEFAULT_SOURCE = r"C:\Program Files\GOG Galaxy\Games\Magic Carpet Plus\CARPET.CD\game.gog"

# path on the CD (under CARPET/) -> path under <game>/hidden
FILES = [
    "HIDDEN.EXE",
    "DATA/BLK1-0.DAT", "DATA/BLK1-1.DAT", "DATA/PAL1-0.DAT",
    "DATA/BUILD1-0.DAT", "DATA/BUILD1-0.TAB", "DATA/SKY1-0.DAT",
    "DATA/MSPR1-0.DAT", "DATA/MSPR1-0.TAB", "DATA/HSPR1-0.DAT", "DATA/HSPR1-0.TAB",
    "DATA/TMAPS1-0.DAT", "DATA/TMAPS1-0.TAB", "DATA/DTABLES.DAT",
    "LEVELS/DDLEVELS.DAT", "LEVELS/DDLEVELS.TAB",
]


class Iso9660:
    """Just enough ISO 9660 to read files by path (no Joliet / Rock Ridge needed for this CD)."""

    SECTOR = 2048

    def __init__(self, path):
        self.f = open(path, "rb")
        pvd = self._read(16 * self.SECTOR, self.SECTOR)
        if pvd[1:6] != b"CD001":
            raise ValueError(f"{path}: not an ISO 9660 image")
        self.root = self._parse_record(pvd[156:156 + 34])

    def _read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    @staticmethod
    def _parse_record(r):
        length = r[0]
        extent = struct.unpack_from("<I", r, 2)[0]
        size = struct.unpack_from("<I", r, 10)[0]
        flags = r[25]
        name_len = r[32]
        name = r[33:33 + name_len].decode("ascii", "replace")
        return {"len": length, "extent": extent, "size": size, "dir": bool(flags & 2), "name": name}

    def _list(self, rec):
        data = self._read(rec["extent"] * self.SECTOR, rec["size"])
        out, pos = [], 0
        while pos < len(data):
            n = data[pos]
            if n == 0:                       # records do not cross sectors: skip to the next one
                pos = (pos // self.SECTOR + 1) * self.SECTOR
                continue
            e = self._parse_record(data[pos:pos + n])
            if e["name"] not in ("\x00", "\x01"):
                e["name"] = e["name"].split(";")[0].rstrip(".").upper()
                out.append(e)
            pos += n
        return out

    def read(self, path):
        rec = self.root
        for part in path.upper().split("/"):
            match = [e for e in self._list(rec) if e["name"] == part]
            if not match:
                raise FileNotFoundError(path)
            rec = match[0]
        return self._read(rec["extent"] * self.SECTOR, rec["size"])


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 2
    game = argv[1]
    src = argv[2] if len(argv) > 2 else DEFAULT_SOURCE
    if not os.path.isfile(os.path.join(game, "carpet.exe")) and not os.path.isfile(os.path.join(game, "CARPET.EXE")):
        print(f"warning: {game} has no carpet.exe - is it the game directory?")
    if os.path.isdir(src):
        def read(p):
            for cand in (os.path.join(src, p), os.path.join(src, "CARPET", p)):
                if os.path.isfile(cand):
                    with open(cand, "rb") as f:
                        return f.read()
            raise FileNotFoundError(p)
    else:
        iso = Iso9660(src)
        read = lambda p: iso.read("CARPET/" + p)
    dest = os.path.join(game, "hidden")
    for p in FILES:
        data = read(p)
        out = os.path.join(dest, p.lower())
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(data)
        print(f"{p:24s} {len(data):8d} bytes -> {out}")
    print(f"Hidden Worlds installed in {dest}: mcport continues the campaign after level 50 "
          f"(or `mcport <game> play 100` for Hidden Worlds level 1).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
