"""First differing tick of two per-tick checksum logs.

    python tools/reference/tick_log_diff.py A B [--context N] [--all-parts]

Reads files of lines "<tick> <checksum hex>[ name=hex ...]":
  * mcport MC_TICK_LOG=<file> ("<tick> <net_state_checksum>"; with MC_TICK_LOG_PARTS=1 every line also
    carries the checksum parts " rng=... things.creature=... player0=... map.height=... mode=... ai_seed=..."),
  * net_test's per-peer files (net_peer*.txt), anything else of that shape.
Other lines ("frames ...", "end ...", "# ...") are ignored. The ticks are matched by number (a log that
starts later or stops earlier is compared where both have the tick).

Prints the number of ticks compared, the first tick whose checksum differs (with --context N the N lines
before it from both files), and when both logs carry parts: the parts that differ at that tick (in the
checksum's order, the first one named) and the first tick at which each part ever differs. A difference in
ai_seed alone (not part of the total) is reported too. Exit code: 0 identical, 1 different, 2 error.
"""
from __future__ import annotations

import argparse
import re
import sys

LINE = re.compile(r"^\s*(-?\d+)\s+([0-9a-fA-F]{1,8})\b(.*)$")
PART = re.compile(r"([A-Za-z_][\w.+]*)=([0-9a-fA-F]{1,8})")


def read_log(path):
    ticks = {}
    order = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            m = LINE.match(raw)
            if not m:
                continue
            tick = int(m.group(1))
            parts = {k: int(v, 16) for k, v in PART.findall(m.group(3))}
            if tick not in ticks:
                order.append(tick)
            ticks[tick] = (int(m.group(2), 16), parts, raw.rstrip("\n"))
    return ticks, order


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--context", type=int, default=0, help="show this many lines before the first difference")
    ap.add_argument("--all-parts", action="store_true", help="print every part's value at the first difference")
    args = ap.parse_args(argv)
    try:
        A, order_a = read_log(args.a)
        B, order_b = read_log(args.b)
    except OSError as e:
        print(f"tick_log_diff: {e}", file=sys.stderr)
        return 2
    common = [t for t in order_a if t in B]
    if not common:
        print(f"tick_log_diff: no common ticks ({len(A)} in A, {len(B)} in B)")
        return 2
    have_parts = any(A[t][1] for t in common[:1]) and any(B[t][1] for t in common[:1])
    first = None
    first_part_tick = {}
    for t in common:
        ca, pa, _ = A[t]
        cb, pb, _ = B[t]
        if have_parts:
            for k in pa:
                if k in pb and pa[k] != pb[k] and k not in first_part_tick:
                    first_part_tick[k] = t
        if first is None and (ca != cb or (have_parts and any(k in pb and pa[k] != pb[k] for k in pa))):
            first = t
            if not have_parts:
                break
    only_a = len(A) - len(common)
    only_b = len([t for t in order_b if t not in A])
    print(f"A: {args.a}: {len(A)} ticks ({order_a[0]}..{order_a[-1]})" if order_a else f"A: {args.a}: empty")
    print(f"B: {args.b}: {len(B)} ticks ({order_b[0]}..{order_b[-1]})" if order_b else f"B: {args.b}: empty")
    print(f"{len(common)} common ticks compared" + (f" ({only_a} only in A, {only_b} only in B)" if only_a or only_b else "")
          + (", with checksum parts" if have_parts else ""))
    if first is None:
        print("identical")
        return 0
    ca, pa, la = A[first]
    cb, pb, lb = B[first]
    print(f"first differing tick: {first} (checksum {ca:08x} / {cb:08x})")
    if args.context:
        i = common.index(first)
        for t in common[max(0, i - args.context):i + 1]:
            print(f"  A {A[t][2][:120]}")
            print(f"  B {B[t][2][:120]}")
    if have_parts:
        diff = [k for k in pa if k in pb and pa[k] != pb[k]]
        if diff:
            print(f"  first differing part: {diff[0]}" + ("  (not in the total)" if diff[0] == "ai_seed" else ""))
            print(f"  parts differing at tick {first}: {', '.join(diff)}")
        else:
            print("  the parts agree at that tick (the total differs: a part list from another build?)")
        if args.all_parts:
            for k in pa:
                print(f"    {k:18s} {pa[k]:08x} {pb.get(k, 0):08x}{'  *' if pa[k] != pb.get(k) else ''}")
        later = sorted(((t, k) for k, t in first_part_tick.items() if k not in diff))
        if later:
            print("  parts that diverge later: " + ", ".join(f"{k} @{t}" for t, k in later[:20]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
