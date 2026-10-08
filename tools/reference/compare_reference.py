"""Run the port-vs-original comparison and summarise it by Thing kind.

    python compare_reference.py [--build build_E] [--last-tick 99999] [--ref extracted/reference/movie0]

Runs <build>/Debug/reference_test.exe (src/tests/reference_test.cpp: plays movie 0 in the port from the
snapshot and compares every dumped tick slot by slot with the original's GameState), keeps its full
text report in <build>/Debug/reference_report.txt and its per-slot CSV in <build>/Debug/reference_slots.csv,
and prints:
  * the per-tick summary lines of the first 20 compared ticks and of every 1000th tick,
  * per (reference class, type, state) of the first divergence: number of slots, earliest tick, the
    most common first differing field.
Build the exe first: cd src && cmake --preset msvc-x64 -B ../build_E &&
cmake --build ../build_E --config Debug --target reference_test
"""
from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLASSES = ["-", "c1", "scenery", "player", "c4", "creature", "c6", "c7", "c8", "projectile", "effect",
           "switch", "spell", "c13"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=str(ROOT / "build_E"))
    ap.add_argument("--game", default=str(ROOT / "MagicCarpet" / "magic"))
    ap.add_argument("--ref", default=str(ROOT / "extracted" / "reference" / "movie0"))
    ap.add_argument("--last-tick", type=int, default=99999)
    a = ap.parse_args(argv)
    out_dir = Path(a.build) / "Debug"
    exe = out_dir / "reference_test.exe"
    if not exe.exists():
        raise SystemExit(f"{exe} missing; build the reference_test target first")
    csv_path = out_dir / "reference_slots.csv"
    rep_path = out_dir / "reference_report.txt"
    r = subprocess.run([str(exe), a.game, a.ref, str(a.last_tick), str(csv_path)],
                       capture_output=True, text=True, cwd=str(out_dir))
    rep_path.write_text(r.stdout + r.stderr)
    if r.returncode != 0:
        print(r.stdout[-3000:], r.stderr[-3000:])
        raise SystemExit(f"reference_test exited with {r.returncode}")
    lines = r.stdout.splitlines()
    n = 0
    for ln in lines:
        s = ln.strip()
        if s[:1].isdigit() and "/" in s and "things" in s:
            n += 1
            tick = int(s.split()[0])
            if n <= 20 or tick % 1000 == 0:
                print(ln)
        elif s.startswith(("compared", "player ", "first diverging", "handlers")):
            print(ln)
    groups: dict[tuple, list] = defaultdict(list)
    with open(csv_path, newline="") as f:
        for row in csv.DictReader(f):
            t = int(row["tick"])
            if t:
                groups[(int(row["ref_cls"]), int(row["ref_type"]), int(row["ref_state"]))].append((t, row["field"]))
    print("\nfirst divergence by reference (class, type, state): slots, earliest tick, commonest first field")
    for key, v in sorted(groups.items(), key=lambda kv: min(t for t, _ in kv[1])):
        cls, typ, st = key
        name = CLASSES[cls] if cls < len(CLASSES) else str(cls)
        fld, cnt = Counter(f for _, f in v).most_common(1)[0]
        print(f"  {name:<10} type {typ:3d} state {st:3d}: {len(v):4d} slots, first tick {min(t for t, _ in v):5d}, "
              f"{fld} ({cnt})")
    print(f"\nfull report: {rep_path}\nper-slot CSV: {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
