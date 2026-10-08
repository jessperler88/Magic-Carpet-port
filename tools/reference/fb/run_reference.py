"""Play movie N in the bundled DOSBox with the state- and frame-dumping carpet.exe and collect the dumps.

(Copy of tools/reference/run_reference.py for task F of round 5: extracted/refgame_fb/, frame dumps
fb%05d.dat -> <out>/tick%05d.fb, default out extracted/reference/movie0_fb.)

    python run_reference.py [--movie 0] [--out extracted/reference/movie0_fb] [--idle 60] [--timeout 3600]
                            [--visible] [--keep]

Steps:
 1. expects extracted/refgame_fb/magic (a copy of MagicCarpet/magic, see make_refgame.py) with the
    patched carpet.exe (patch_carpet.py) and extracted/refgame_fb/DOSBox.exe + SDL dlls (copied from
    MagicCarpet/DOSBOX);
 2. writes extracted/refgame_fb/dosbox_ref.conf: the bundled dosbox.conf with sound off, windowed
    surface output, max cycles and an autoexec that mounts the copy, runs `carpet -roll N+1 -level 38` (the patched option that starts movie N; options need the - or / prefix) and exits;
 3. starts DOSBox headless (SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy) unless --visible, and
    watches extracted/refgame_fb/magic/movie/ for gam%05d.dat (tick-numbered by the patch) until DOSBox
    exits or no new dump appears for --idle seconds (the game sits in the front end after the movie);
 4. moves every dump to <out>/tick%05d.gam (and map%05d.dat -> tick%05d.map), checks the size
    (0x38d03) and that PlayerRec.tick of the local player equals the file number, and writes
    <out>/index.json {movie, ticks, terrain_ticks, run_seconds, dosbox_exit, ...}.

The original snapshot pair gam00000 / map00000 stays in the movie directory (playback needs it).
Nothing under MagicCarpet/ is touched.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REFGAME = ROOT / "extracted" / "refgame_fb"
STATE_SIZE = 0x38D03
MAP_SIZE = 0x612C2
FB_MIN_SIZE = 64000 + 768 + 256 + 6   # see fb/patch_carpet.py for the layout
PLAYERS_OFF = 0x340B
PLAYER_REC_SIZE = 0x801
TICK_OFF = 0x12

CONF_TEMPLATE = """[sdl]
fullscreen=false
fulldouble=false
fullresolution=original
windowresolution=original
output=surface
autolock=false
sensitivity=100
waitonerror=false
priority=higher,higher
usescancodes=true

[dosbox]
machine=svga_s3
captures=captures
memsize=16

[render]
frameskip=0
aspect=false
scaler=none

[cpu]
core=auto
cputype=auto
cycles=max
cycleup=1000
cycledown=1000

[mixer]
nosound=true
rate=22050
blocksize=1024
prebuffer=20

[midi]
mpu401=intelligent
mididevice=none

[sblaster]
sbtype=sb16
sbbase=220
irq=7
dma=1
hdma=5
sbmixer=true
oplmode=auto
oplemu=default
oplrate=22050

[gus]
gus=false

[speaker]
pcspeaker=false
tandy=off
disney=false

[joystick]
joysticktype=none

[serial]
serial1=disabled
serial2=disabled
serial3=disabled
serial4=disabled

[dos]
xms=true
ems=false
umb=true

[ipx]
ipx=false

[autoexec]
@echo off
mount C "{game_dir}"
C:
{cmdline}
exit
"""


def local_tick(blob: bytes) -> int:
    local = struct.unpack_from("<h", blob, 8)[0]
    return struct.unpack_from("<I", blob, PLAYERS_OFF + local * PLAYER_REC_SIZE + TICK_OFF)[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--movie", type=int, default=0)
    ap.add_argument("--level", type=int, default=38, help="level generated before the snapshot replaces it (movie 0 = level 38)")
    ap.add_argument("--out", default=None, help="default extracted/reference/movie<N>")
    ap.add_argument("--idle", type=float, default=30.0, help="seconds without a new dump before DOSBox is killed")
    ap.add_argument("--timeout", type=float, default=3600.0)
    ap.add_argument("--visible", action="store_true", help="real SDL window instead of the dummy driver")
    ap.add_argument("--keep", action="store_true", help="leave the dumps in the movie directory as well")
    ap.add_argument("--poll", type=float, default=2.0)
    # round 5 (level references, run_level.py): another patched exe, explicit -roll value, early stop
    ap.add_argument("--exe", default="carpet", help="DOS command name of the patched exe in the copy")
    ap.add_argument("--roll", type=int, default=None, help="value of -roll (default movie + 1)")
    ap.add_argument("--stop-tick", type=int, default=None,
                    help="kill DOSBox as soon as a dump of this tick or later exists (record runs)")
    ap.add_argument("--no-movie-check", action="store_true", help="do not require movie/mvi<movie>.dat")
    # round 6 (task D): front-end runs (patch_carpet.py --fe): `carpet` without options, fe%05d.dat dumps
    ap.add_argument("--fe", action="store_true", help="front-end run: no -roll / -level, collect fe%%05d.dat")
    a = ap.parse_args(argv)

    game_dir = REFGAME / "magic"
    movie_dir = game_dir / "movie"
    dosbox = REFGAME / "DOSBox.exe"
    out = Path(a.out) if a.out else ROOT / "extracted" / "reference" / f"movie{a.movie}_fb"
    need = [game_dir / f"{a.exe}.exe", dosbox] + ([] if a.no_movie_check else [movie_dir / f"mvi{a.movie:05d}.dat"])
    for p in need:
        if not p.exists():
            raise SystemExit(f"missing {p} (run make_refgame.py / patch_carpet.py first)")
    out.mkdir(parents=True, exist_ok=True)
    (REFGAME / "captures").mkdir(exist_ok=True)

    pat = re.compile(r"^(gam|map|fb|fe)(\d{5})\.dat$", re.I)

    def dumps() -> dict[tuple[str, int], Path]:
        found = {}
        for p in movie_dir.iterdir():
            m = pat.match(p.name)
            # 0 and >= 20000 are recordings' snapshot pairs (movie 0, run_level.py movies 20000 + level)
            if m and (m.group(1).lower() == "fe" or 0 < int(m.group(2)) < 20000):
                found[(m.group(1).lower(), int(m.group(2)))] = p
        return found

    stale = dumps()
    for p in stale.values():
        p.unlink()
    if stale:
        print(f"removed {len(stale)} stale dump(s) from {movie_dir}")

    conf = REFGAME / "dosbox_ref.conf"
    # The patched config_parse plays movie N - 1 for `-roll N` (see patch_carpet.py); the level number
    # only decides which level is generated before the first tick replaces everything by the snapshot.
    if a.fe:
        # c:\carpet.cd\language.inf makes the original skip the language screen: remove it so every
        # front-end run starts on that screen (the run writes it again).
        lang = game_dir / "CARPET.CD" / "LANGUAGE.INF"
        if lang.exists():
            lang.unlink()
        for g in (game_dir / "CARPET.CD" / "SAVE").glob("CARPET*.GAM"):   # the tour saves into slot 1
            g.unlink()
    roll = a.roll if a.roll is not None else a.movie + 1
    cmdline = a.exe if a.fe else f"{a.exe} -roll {roll} -level {a.level}"
    conf.write_text(CONF_TEMPLATE.format(game_dir=str(game_dir), cmdline=cmdline))
    env = dict(os.environ)
    if not a.visible:
        env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    cmd = [str(dosbox), "-conf", str(conf), "-noconsole"]
    print("starting:", " ".join(cmd))
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=str(REFGAME), env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    last_count, last_new = 0, time.time()
    exit_code = None
    while True:
        time.sleep(a.poll)
        n = len(dumps())
        now = time.time()
        if n != last_count:
            last_count, last_new = n, now
            print(f"\r  {n} dump files after {now - t0:6.1f}s", end="", flush=True)
        exit_code = proc.poll()
        if exit_code is not None:
            print(f"\nDOSBox exited with {exit_code} after {now - t0:.1f}s")
            break
        if now - last_new > a.idle and n > 0:
            print(f"\nno new dump for {a.idle:.0f}s; stopping DOSBox")
            proc.kill(); proc.wait()
            break
        if a.stop_tick is not None and any(k == "gam" and t >= a.stop_tick for (k, t) in dumps()):
            time.sleep(a.poll)          # let the last file be written completely
            print(f"\nreached tick {a.stop_tick}; stopping DOSBox")
            proc.kill(); proc.wait()
            break
        if now - t0 > a.timeout:
            print("\ntimeout; stopping DOSBox")
            proc.kill(); proc.wait()
            break
    run_seconds = time.time() - t0

    found = dumps()
    ticks, terrain_ticks, fb_ticks, bad, fe_dumps = [], [], [], [], []
    for (kind, n), p in sorted(found.items()):
        blob = p.read_bytes()
        if kind == "gam":
            if len(blob) != STATE_SIZE or local_tick(blob) != n:
                bad.append((p.name, len(blob), local_tick(blob) if len(blob) >= 0x8000 else -1))
                continue
            dst = out / f"tick{n:05d}.gam"
            ticks.append(n)
        elif kind == "fe":
            dst = out / f"fe{n:05d}.fe"
            fe_dumps.append(n)
        elif kind == "fb":
            if len(blob) < FB_MIN_SIZE:
                bad.append((p.name, len(blob), -1))
                continue
            dst = out / f"tick{n:05d}.fb"
            fb_ticks.append(n)
        else:
            if len(blob) != MAP_SIZE:
                bad.append((p.name, len(blob), -1))
                continue
            dst = out / f"tick{n:05d}.map"
            terrain_ticks.append(n)
        if a.keep:
            shutil.copyfile(p, dst)
        else:
            shutil.move(str(p), str(dst))
    index = {
        "movie": a.movie,
        "game_dir": str(game_dir),
        "state_size": STATE_SIZE,
        "map_size": MAP_SIZE,
        "tick_count": len(ticks),
        "first_tick": ticks[0] if ticks else None,
        "last_tick": ticks[-1] if ticks else None,
        "ticks": ticks,
        "terrain_ticks": terrain_ticks,
        "fb_ticks": fb_ticks,
        "fe_dumps": fe_dumps,
        "fb_point": "before vga_present_frame_2f480 (game_tick_update_32e80 + 0x104): back buffer, DAC, Config[0..0xff], anim table, render globals",
        "bad_files": bad,
        "run_seconds": round(run_seconds, 1),
        "dosbox_exit": exit_code,
        "dump_point": "after thing_update_all, before sound_update_494b0 (game_tick_update_32e80 + 0x91)",
        "file_format": "tick%05d.gam = raw GameState (0x38d03 bytes, original pointers); tick%05d.map = demo_save_terrain_3c430 layout",
    }
    # Round 6: the patch's poke schedule / mode sidecar (patch_carpet.py --schedule / --hires / --hud),
    # read by render_reference2_test.
    fe_side = game_dir / f"{a.exe}.fescript.txt"
    if a.fe and fe_side.exists():
        shutil.copyfile(fe_side, out / "fe_script.txt")
    side = game_dir / f"{a.exe}.schedule.txt"
    if side.exists():
        shutil.copyfile(side, out / "schedule.txt")
        index["schedule"] = side.read_text().splitlines()[-1]
    (out / "index.json").write_text(json.dumps(index, indent=1))
    print(f"{len(ticks)} state dumps ({ticks[0] if ticks else '-'}..{ticks[-1] if ticks else '-'}), "
          f"{len(terrain_ticks)} terrain dumps, {len(fb_ticks)} frame dumps, {len(bad)} bad -> {out}")
    if bad:
        print("bad:", bad[:10])
    if a.fe:
        print(f"{len(fe_dumps)} front-end dumps -> {out}")
        return 0 if fe_dumps and not bad else 1
    return 0 if ticks and not bad else 1


if __name__ == "__main__":
    sys.exit(main())
