"""Per-tick level references of the 1995 CD executables (Hidden Worlds and the 1995 base game).

    python run_level_hw.py LEVEL [LEVEL ...] [--exe hidden|cd95] [--gen-only] [--stop 5000]
                           [--every-until 1000] [--stride 10] [--out-root extracted/reference]

Exactly run_level.py (record run with an idle local player, then a play run with dumps; --gen-only:
tick00001.gam + the tick-2 snapshot pair), but

* the game copy is extracted/refgame_hw/magic (make_refgame_hw.py), the exes are patched by
  patch_hidden.py (1995 code layout) into carprec.exe / carplay.exe there;
* the GameState is 0x38d09 bytes (1995) instead of 0x38d03, the size check uses that;
* LEVEL is the `-level` argument: for HIDDEN.EXE an index into levels/ddlevels.dat (the Hidden
  Worlds campaign is 0..24; port campaign index 100 + LEVEL), for the CD CARPET.EXE (cd95) an index
  into levels/levels.dat as for the 1996 exe;
* outputs: hidden -> <out-root>/hw_gen/levelNN (gen) or <out-root>/hw_levelNN;
  cd95 -> <out-root>/cd95_gen/levelNN or <out-root>/cd95_levelNN.
  index.json gets "exe", "state_size" and, for hidden, "port_level" = 100 + LEVEL.

run_level.py / run_reference.py / patch_carpet.py are used through private module copies whose
constants are replaced, so their own behaviour is unchanged. Never writes outside extracted/.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REFGAME_HW = ROOT / "extracted" / "refgame_hw"
sys.path.insert(0, str(HERE))
import patch_hidden  # noqa: E402


def private(name: str, file: str):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class PatchShim:
    """Stands in for the patch_carpet module inside run_level: same main(argv), 1995 layout."""

    def __init__(self, exe: str):
        self.exe = exe

    def main(self, argv):
        patch_hidden.main(["--exe", self.exe] + list(argv))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("levels", type=int, nargs="+")
    ap.add_argument("--exe", choices=sorted(patch_hidden.LAYOUTS), default="hidden")
    ap.add_argument("--stop", type=int, default=5000)
    ap.add_argument("--every-until", type=int, default=1000)
    ap.add_argument("--stride", type=int, default=10)
    ap.add_argument("--out-root", default=str(ROOT / "extracted" / "reference"))
    ap.add_argument("--gen-only", action="store_true")
    ap.add_argument("--play-stride-until", type=int, default=19999)
    a = ap.parse_args(argv)
    if not (REFGAME_HW / "magic").exists():
        raise SystemExit("run make_refgame_hw.py first")

    rr = private("run_reference_hw", "run_reference.py")
    rr.REFGAME = REFGAME_HW
    rr.STATE_SIZE = patch_hidden.STATE_SIZE_1995
    rl = private("run_level_hw_impl", "run_level.py")
    rl.REFGAME = REFGAME_HW
    rl.MOVIE_DIR = REFGAME_HW / "magic" / "movie"
    rl.run_reference = rr
    rl.patch_carpet = PatchShim(a.exe)

    tmp_root = Path(a.out_root) / f"_tmp_{a.exe}"
    prefix = "hw" if a.exe == "hidden" else "cd95"
    inner = ["--out-root", str(tmp_root), "--stop", str(a.stop), "--every-until", str(a.every_until),
             "--stride", str(a.stride), "--play-stride-until", str(a.play_stride_until)]
    if a.gen_only:
        inner.append("--gen-only")
    for lv in a.levels:
        rl.main([str(lv)] + inner)
        src = tmp_root / ("gen" if a.gen_only else "") / f"level{lv:02d}"
        dst = (Path(a.out_root) / f"{prefix}_gen" / f"level{lv:02d}") if a.gen_only \
            else Path(a.out_root) / f"{prefix}_level{lv:02d}"
        if dst.exists():
            shutil.rmtree(dst)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        idx_path = dst / "index.json"
        idx = json.loads(idx_path.read_text()) if idx_path.exists() else {}
        idx.update({"exe": a.exe, "exe_file": patch_hidden.LAYOUTS[a.exe]["file"],
                    "state_size": patch_hidden.STATE_SIZE_1995,
                    "how": f"run_level_hw.py --exe {a.exe}: record run (carprec) + play run (carplay) in extracted/refgame_hw"})
        if a.exe == "hidden":
            idx["port_level"] = 100 + lv
            idx["ddlevels_index"] = lv
        idx_path.write_text(json.dumps(idx, indent=1))
        print(f"-> {dst}")
    shutil.rmtree(tmp_root, ignore_errors=True)


if __name__ == "__main__":
    main()
