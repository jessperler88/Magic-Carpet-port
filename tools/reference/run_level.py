"""Per-tick reference of a campaign level without a human (port round 5, task E).

    python run_level.py LEVEL [LEVEL ...] [--stop 5000] [--every-until 1000] [--stride 10] [--out-root extracted/reference]

For each level L (0-based levels.dat index, as `-level` and the port's sim_load_level use it):

 1. record run: extracted/refgame/magic/carprec.exe (patch_carpet.py --mode record) is started with
    `carprec -roll M+1 -level L`, M = 20000 + L. 0x100 skips the front end; the record cave dumps
    tick 1 (gam00001.dat: the generated level after its first tick), sets the record bit as soon as
    player 0's Thing exists (tick 1 in practice), so the next tick's first packet opens movie/mviM.dat
    and saves the snapshot pair gamM / mapM (tick 2, before player 0's packet), dumps every 500th tick
    while recording and closes the recording at tick STOP. The local player is a human without input
    (idle flyer); the AI wizards, creatures, castles and spells of the level run normally.
 2. play run: extracted/refgame/magic/carplay.exe (--mode play, own dump policy) plays movie M the way
    movie 0 is played: dumps every tick <= EVERY_UNTIL and every STRIDE-th tick after, terrain every
    250 ticks.
 3. <out>/movie/{mvi,gam,map}M.dat (the recording, for the port's demo_open), <out>/tick%05d.gam +
    .map (play run) and tick00001.gam (record run), index.json. The record run's 500-tick dumps are
    compared byte for byte with the play run's (recording and playback are the same simulation) and
    then deleted.

The port side is src/tests/reference_test.cpp (`reference_test <game> <out>`: level mode when
<out>/index.json names a level). MagicCarpet/ is never touched.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REFGAME = ROOT / "extracted" / "refgame"
MOVIE_DIR = REFGAME / "magic" / "movie"
sys.path.insert(0, str(HERE))
import patch_carpet  # noqa: E402
import run_reference  # noqa: E402


def movie_files(m: int) -> list[Path]:
    want = {f"{k}{m:05d}.dat" for k in ("mvi", "gam", "map")}
    return [p for p in MOVIE_DIR.iterdir() if p.name.lower() in want]


def run_level(level: int, a) -> dict:
    m = 20000 + level
    out = Path(a.out_root) / ("gen" if a.gen_only else "") / f"level{level:02d}"
    rec = out / "rec"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for p in movie_files(m):
        p.unlink()
    t0 = time.time()
    r = run_reference.main(["--exe", "carprec", "--roll", str(m + 1), "--level", str(level), "--stop-tick", str(a.stop),
                            "--idle", "60", "--no-movie-check", "--out", str(rec)])
    files = {p.name.lower(): p for p in movie_files(m)}
    if len(files) != 3:
        # e.g. level 17: the original itself dies at tick 1-2 under `-level 17` without the front end
        # (divide by zero in ui_draw_status_bars_219f0, a Thing with max_health 0) - no reference.
        print(f"level {level}: recording incomplete: {sorted(files)} (original crashed?); skipped")
        for p in files.values():
            p.unlink()
        (out / "FAILED.txt").write_text("recording incomplete: the original stopped before the recording started\n")
        shutil.rmtree(rec, ignore_errors=True)
        return {}
    (out / "movie").mkdir()
    for name, p in files.items():
        shutil.move(str(p), str(out / "movie" / name))
    t1 = time.time()
    # the play run removes stale dumps itself; the recording lives in <out>/movie, so copy it back for DOSBox
    for name in files:
        shutil.copyfile(out / "movie" / name, MOVIE_DIR / name)
    r2 = None
    if not a.gen_only:
        r2 = run_reference.main(["--exe", "carplay", "--roll", str(m + 1), "--level", str(level), "--idle", "20",
                                 "--no-movie-check", "--movie", str(m), "--out", str(out)])
    for p in movie_files(m):
        p.unlink()
    t2 = time.time()
    # cross-check record vs play dumps, keep the record run's tick-1 dump
    same, differ = [], []
    for p in sorted(rec.glob("tick*.gam")):
        t = int(p.stem[4:])
        if t == 1:
            continue
        q = out / p.name
        if q.exists():
            if q.read_bytes() == p.read_bytes():
                same.append(t)
            else:
                differ.append(t)
                shutil.copyfile(p, out / f"rec_tick{t:05d}.gam")    # kept for dump_info --diff
    if (rec / "tick00001.gam").exists():
        shutil.move(str(rec / "tick00001.gam"), str(out / "tick00001.gam"))
    shutil.rmtree(rec)
    idx_path = out / "index.json"
    idx = json.loads(idx_path.read_text()) if idx_path.exists() else {}
    mvi = (out / "movie" / f"mvi{m:05d}.dat").stat().st_size
    idx.update({
        "level": level,
        "movie": m,
        "record_stop_tick": a.stop,
        "recording_bytes": mvi,
        "record_vs_play_same_ticks": same,
        "record_vs_play_differ_ticks": differ,
        "record_seconds": round(t1 - t0, 1),
        "play_seconds": round(t2 - t1, 1),
        "has_tick1": (out / "tick00001.gam").exists(),
        "how": "run_level.py: record run (carprec, idle local player) + play run (carplay)",
        "gen_only": 1 if a.gen_only else 0,
    })
    idx_path.write_text(json.dumps(idx, indent=1))
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"level {level}: record exit {r}, play exit {r2}, recording {mvi} bytes, "
          f"{idx.get('tick_count')} play dumps ({idx.get('first_tick')}..{idx.get('last_tick')}), "
          f"record==play at {len(same)} ticks, differ at {differ}, {size / 1e6:.0f} MB")
    return idx


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("levels", type=int, nargs="+")
    ap.add_argument("--stop", type=int, default=5000, help="last recorded tick")
    ap.add_argument("--every-until", type=int, default=1000)
    ap.add_argument("--stride", type=int, default=10)
    ap.add_argument("--out-root", default=str(ROOT / "extracted" / "reference"))
    ap.add_argument("--gen-only", action="store_true",
                    help="only the level-generation reference: record ticks 1..10 (tick00001.gam + the tick-2 "
                         "snapshot pair) into <out-root>/gen/levelNN, no play run")
    ap.add_argument("--play-stride-until", type=int, default=19999)
    a = ap.parse_args(argv)
    if a.gen_only:
        a.stop = 10
    patch_carpet.main(["--mode", "record", "--dst", str(REFGAME / "magic" / "carprec.exe"),
                       "--stride", str(a.stop if a.gen_only else 500), "--stop-tick", str(a.stop)])
    patch_carpet.main(["--mode", "play", "--dst", str(REFGAME / "magic" / "carplay.exe"),
                       "--every-until", str(a.every_until), "--stride", str(a.stride),
                       "--stride-until", str(a.play_stride_until), "--terrain-stride", "250"])
    for lv in a.levels:
        run_level(lv, a)


if __name__ == "__main__":
    main()
