"""References with an active local player (port round 6, task C): a scripted recording made by the port,
played back by the original with per-tick dumps.

    python run_player.py SCRIPT [SCRIPT ...] [--out-root extracted/reference/player] [--every-until 3000]
                         [--stride 5] [--terrain-stride 250] [--no-record] [--port-exe build_C/Debug/reference_player_test.exe]

For each script (format: src/tests/reference_player_test.cpp; `level L`, `movie M`, `stop T` + the input lines):

 1. record (port): `reference_player_test <game> record SCRIPT <out>` plays level L from its first tick with the
    scripted local player and writes the recording with the port's recorder (demo.cpp, the original's movie
    format: <out>/movie/mviM.dat + the snapshot pair gamM / mapM of tick 2, pointers in the layout of the
    DOSBox run). The script is copied to <out>/script.txt. --no-record keeps an existing recording (e.g. the
    one the original already played: the port's own recording changes when its game logic changes).
 2. play (original): the recording is copied into extracted/refgame/magic/movie and played by carplay.exe
    (patch_carpet.py --mode play, own dump policy) with `carplay -roll M+1 -level L`: tick%05d.gam every tick
    <= EVERY_UNTIL, then every STRIDE-th, tick%05d.map every TERRAIN_STRIDE ticks.
 3. <out>/index.json gets "level" and "movie", so `reference_test <game> <out>` (level mode) compares the
    port's playback of the same recording with the dumps; `reference_test <game> <out-root>` runs all of them.

<out> = <out-root>/<script file name without .txt>. MagicCarpet/ is never touched.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REFGAME = ROOT / "extracted" / "refgame"
MOVIE_DIR = REFGAME / "magic" / "movie"
GAME = ROOT / "MagicCarpet" / "magic"
sys.path.insert(0, str(HERE))
import patch_carpet  # noqa: E402
import run_reference  # noqa: E402


def header(script: Path) -> dict:
    h = {"level": None, "movie": None, "stop": 3000}
    for line in script.read_text().splitlines():
        w = line.split("#", 1)[0].split()
        if len(w) >= 2 and w[0] in h:
            h[w[0]] = int(w[1], 0)
    if h["level"] is None:
        raise SystemExit(f"{script}: no `level` line")
    if h["movie"] is None:
        h["movie"] = 30000 + h["level"]
    return h


def movie_files(m: int) -> list[Path]:
    # + the quick save gam10000.dat (command 10) the original writes during a playback
    want = {f"{k}{m:05d}.dat" for k in ("mvi", "gam", "map")} | {"gam10000.dat"}
    return [p for p in MOVIE_DIR.iterdir() if p.name.lower() in want]


def run_one(script: Path, a) -> dict:
    h = header(script)
    lv, m = h["level"], h["movie"]
    out = Path(a.out_root) / script.stem
    t0 = time.time()
    if not a.no_record:
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        env = {"MC_REC_QUIET": "1"}
        import os
        env = dict(os.environ, **env)
        r = subprocess.run([str(a.port_exe), str(GAME), "record", str(script), str(out)], env=env,
                           capture_output=True, text=True)
        print(r.stdout.strip().splitlines()[-2:] if r.stdout else r.stderr)
        if not (out / "movie" / f"mvi{m:05d}.dat").exists():
            raise SystemExit(f"{script}: the port wrote no recording")
    else:
        for p in out.glob("tick*.*"):
            p.unlink()
    t1 = time.time()
    for p in movie_files(m):
        p.unlink()
    for k in ("mvi", "gam", "map"):
        shutil.copyfile(out / "movie" / f"{k}{m:05d}.dat", MOVIE_DIR / f"{k}{m:05d}.dat")
    r2 = run_reference.main(["--exe", "carplay", "--roll", str(m + 1), "--level", str(lv), "--idle", "20",
                             "--no-movie-check", "--movie", str(m), "--out", str(out), "--stop-tick", str(h["stop"])])
    # A playback dumps from tick 2 on. A tick-1 dump means the level ended early in the original (e.g. the
    # level was lost) and game_main started it again, overwriting the dumps of the first pass.
    restarted = (out / "tick00001.gam").exists()
    if restarted:
        print(f"WARNING {script.stem}: the original restarted the level during the playback (tick-1 dump); "
              "the dumps are not usable")
    # The quick save the original wrote during the playback (command 10) is kept for comparison with the
    # port's (demo_save_state): <out>/orig_gam10000.dat.
    for p in movie_files(m):
        if p.name.lower() == "gam10000.dat":
            shutil.copyfile(p, out / "orig_gam10000.dat")
        p.unlink()
    # Where the playback really ended: a quick load (command 0xb) sets the level clock back and the dumps
    # are named by it, so the order in which the dump files were written tells the passes apart. The last
    # written dump is the end; the number of times the tick went backwards before it is the number of
    # quick loads the original executed. (Round 6: the original stops a few ticks after a quick load in
    # a playback - see docs/analysis/port_reference3.md.)
    order = sorted(out.glob("tick*.gam"), key=lambda q: q.stat().st_mtime_ns)
    loads, prev = 0, None
    for q in order:
        t = int(q.stem[4:])
        if prev is not None and t < prev:
            loads += 1
        prev = t
    end_tick = prev if prev is not None else 0
    t2 = time.time()
    idx_path = out / "index.json"
    idx = json.loads(idx_path.read_text()) if idx_path.exists() else {}
    idx.update({
        "level": lv,
        "movie": m,
        "record_stop_tick": h["stop"],
        "playback_restarted": 1 if restarted else 0,
        "playback_end_tick": end_tick,
        "playback_end_loads": loads,
        "recording_bytes": (out / "movie" / f"mvi{m:05d}.dat").stat().st_size,
        "record_seconds": round(t1 - t0, 1),
        "play_seconds": round(t2 - t1, 1),
        "how": "run_player.py: port recording (reference_player_test record, scripted local player) + "
               "original play run (carplay)",
    })
    idx_path.write_text(json.dumps(idx, indent=1))
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"{script.stem}: level {lv} movie {m}: play exit {r2}, {idx.get('tick_count')} dumps "
          f"({idx.get('first_tick')}..{idx.get('last_tick')}), {size / 1e6:.0f} MB")
    return idx


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scripts", type=Path, nargs="+")
    ap.add_argument("--out-root", default=str(ROOT / "extracted" / "reference" / "player"))
    ap.add_argument("--every-until", type=int, default=3000)
    ap.add_argument("--stride", type=int, default=5)
    ap.add_argument("--stride-until", type=int, default=19999)
    ap.add_argument("--terrain-stride", type=int, default=250)
    ap.add_argument("--no-record", action="store_true", help="play the existing recording in <out>/movie again")
    ap.add_argument("--port-exe", type=Path, default=ROOT / "build_C" / "Debug" / "reference_player_test.exe")
    a = ap.parse_args(argv)
    patch_carpet.main(["--mode", "play", "--dst", str(REFGAME / "magic" / "carplay.exe"),
                       "--every-until", str(a.every_until), "--stride", str(a.stride),
                       "--stride-until", str(a.stride_until), "--terrain-stride", str(a.terrain_stride)])
    for s in a.scripts:
        run_one(s, a)


if __name__ == "__main__":
    main()
