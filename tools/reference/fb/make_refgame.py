"""Create extracted/refgame_fb/: a private copy of the game and of the bundled DOSBox for the render reference.

(Copy of tools/reference/make_refgame.py for task F of round 5; patches with fb/patch_carpet.py.)

    python make_refgame.py [--force]

Copies MagicCarpet/magic -> extracted/refgame_fb/magic and MagicCarpet/DOSBOX/{DOSBox.exe,SDL.dll,SDL_net.dll}
-> extracted/refgame_fb/, then runs patch_carpet.py on the copy. MagicCarpet/ is only read.
Next step: run_reference.py.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "MagicCarpet"
DST = ROOT / "extracted" / "refgame_fb"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="delete and recreate extracted/refgame_fb/magic")
    a = ap.parse_args(argv)
    DST.mkdir(parents=True, exist_ok=True)
    if (DST / "magic").exists() and a.force:
        shutil.rmtree(DST / "magic")
    if not (DST / "magic").exists():
        shutil.copytree(SRC / "magic", DST / "magic")
        print(f"copied {SRC / 'magic'} -> {DST / 'magic'}")
    for f in ("DOSBox.exe", "SDL.dll", "SDL_net.dll"):
        shutil.copyfile(SRC / "DOSBOX" / f, DST / f)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import patch_carpet
    patch_carpet.main([])


if __name__ == "__main__":
    main()
