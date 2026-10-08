"""Create extracted/refgame_hw/: a private, writable copy of the 1995 CD game (Hidden Worlds + base) and
of the bundled DOSBox for the Hidden Worlds reference runs.

    python make_refgame_hw.py [--force]

* extracted/gog_cd/CARPET (the CD's CARPET folder: CARPET.EXE, HIDDEN.EXE, DATA, LEVELS, MOVIE, ...)
  -> extracted/refgame_hw/magic. The unmodified HIDDEN.EXE / CARPET.EXE stay there; the patched
  copies are hidden.exe -> hidrec.exe / hidplay.exe etc. (written by run_level_hw.py), so nothing
  of the CD copy is overwritten except by --force.
* extracted/refgame_hw/magic/CARPET.CD: what the CD installer leaves on the hard disk (the 1995 exes
  read C:\\CARPET.CD\\{SNDSETUP.INF, INTRO.PLD, LANGUAGE.INF, DATA\\TMAPS*, LEVELS\\*, SAVE}); built
  from the CD's own DATA/TMAPS*, LEVELS/* and the sound / language setup files of
  extracted/refgame/magic/CARPET.CD (sound is off in the reference runs anyway).
  The game directory is mounted as C:, so C:\\CARPET.CD is this folder.
* MagicCarpet/DOSBOX/{DOSBox.exe,SDL.dll,SDL_net.dll} -> extracted/refgame_hw/.

MagicCarpet/, extracted/gog_cd/ and the GOG install are only read. Next step: run_level_hw.py.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CD = ROOT / "extracted" / "gog_cd" / "CARPET"
DST = ROOT / "extracted" / "refgame_hw"
SETUP_FROM = ROOT / "extracted" / "refgame" / "magic" / "CARPET.CD"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="delete and recreate extracted/refgame_hw/magic")
    a = ap.parse_args(argv)
    DST.mkdir(parents=True, exist_ok=True)
    game = DST / "magic"
    if game.exists() and a.force:
        shutil.rmtree(game)
    if not game.exists():
        shutil.copytree(CD, game)
        print(f"copied {CD} -> {game}")
    hd = game / "CARPET.CD"
    for sub in ("DATA", "LEVELS", "SAVE"):
        (hd / sub).mkdir(parents=True, exist_ok=True)
    for p in (CD / "DATA").iterdir():
        if p.name.upper().startswith("TMAPS"):
            shutil.copyfile(p, hd / "DATA" / p.name)
    for p in (CD / "LEVELS").iterdir():
        shutil.copyfile(p, hd / "LEVELS" / p.name)
    for f in ("SNDSETUP.INF", "SNDSETUP.DAT", "INTRO.PLD", "LANGUAGE.INF"):
        if (SETUP_FROM / f).exists():
            shutil.copyfile(SETUP_FROM / f, hd / f)
    (game / "MOVIE").mkdir(exist_ok=True)
    for f in ("DOSBox.exe", "SDL.dll", "SDL_net.dll"):
        shutil.copyfile(ROOT / "MagicCarpet" / "DOSBOX" / f, DST / f)
    print(f"refgame_hw ready: {DST}")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import patch_hidden
    patch_hidden.main(["--exe", "hidden", "--verify-only"])
    patch_hidden.main(["--exe", "cd95", "--verify-only"])


if __name__ == "__main__":
    main()
