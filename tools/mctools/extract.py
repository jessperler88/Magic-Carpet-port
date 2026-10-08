"""Unpack the whole game data tree.

    python -m mctools.extract <game_dir> <out_dir>

* Every RNC-compressed file is decompressed to the same relative path under
  <out_dir>/raw (files that are not compressed are copied as-is).
* levels/levels.dat is split into per-level files and each level is parsed to
  JSON + GAM-style text under <out_dir>/levels.
* Palettes are rendered to PNG swatches under <out_dir>/palettes.
* A manifest.json records original/unpacked sizes and SHA-1s for every file.
"""
import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
from __future__ import annotations

import hashlib
import json
import shutil
import sys
from pathlib import Path

from . import rnc, dattab, level, palette


def sha1(b: bytes) -> str:
    return hashlib.sha1(b).hexdigest()


def extract(game_dir: Path, out_dir: Path) -> dict:
    raw_dir = out_dir / "raw"
    manifest = {}
    for p in sorted(game_dir.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(game_dir)
        data = p.read_bytes()
        entry = {"size": len(data), "sha1": sha1(data)}
        blob = data[8:] if data[:8] == b"BULLFROG" else data
        if rnc.is_rnc(blob):
            hdr = rnc.RNCHeader.parse(blob)
            out = rnc.unpack(blob)
            entry.update({"rnc": True, "rnc_method": hdr.method, "unpacked_size": len(out),
                          "unpacked_sha1": sha1(out), "bullfrog_tag": data[:8] == b"BULLFROG"})
        else:
            out = data
            entry["rnc"] = False
        dst = raw_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(out)
        manifest[str(rel).replace("\\", "/")] = entry

    # --- levels ---------------------------------------------------------------
    lv_dir = out_dir / "levels"
    lv_dir.mkdir(parents=True, exist_ok=True)
    ldat = (game_dir / "levels/levels.dat").read_bytes()
    ltab = (game_dir / "levels/levels.tab").read_bytes()
    blobs = dattab.split_levels_dat(ldat, ltab)
    summaries = {}
    for i, b in enumerate(blobs):
        lv = level.Level.parse(b)
        (lv_dir / f"lev{i:05d}.bin").write_bytes(lv.pack())
        (lv_dir / f"lev{i:05d}.txt").write_text(level.to_gam_text(lv))
        summaries[i] = lv.summary()
        # cross-check against the loose LEVxxxxx.DAT copy if present
        loose = game_dir / f"levels/lev{i:05d}.dat"
        if loose.exists():
            summaries[i]["matches_loose_file"] = rnc.unpack(loose.read_bytes()) == lv.pack()
    (lv_dir / "levels_summary.json").write_text(json.dumps(summaries, indent=1))

    # --- palettes -------------------------------------------------------------
    pal_dir = out_dir / "palettes"
    pal_dir.mkdir(parents=True, exist_ok=True)
    for p in sorted(game_dir.rglob("*.pal")) + [game_dir / "data/palette.dat"]:
        try:
            pal = palette.load_palette(p.read_bytes())
        except Exception as e:  # noqa: BLE001
            print(f"palette {p.name}: {e}")
            continue
        palette.palette_png(pal, pal_dir / (p.stem + ".png"))

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1))
    return manifest


def main():
    game_dir = Path(sys.argv[1] if len(sys.argv) > 1 else (_REPO + "/MagicCarpet/magic"))
    out_dir = Path(sys.argv[2] if len(sys.argv) > 2 else r"C:\Magic Carpet\extracted")
    m = extract(game_dir, out_dir)
    n_rnc = sum(1 for e in m.values() if e["rnc"])
    print(f"{len(m)} files, {n_rnc} RNC-compressed, written to {out_dir}")


if __name__ == "__main__":
    main()
