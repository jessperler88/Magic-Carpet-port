# Analysis helpers (Python 3, run from anywhere; paths are absolute to C:\Magic Carpet)

| Script | Purpose |
|---|---|
| `mc.py` | Query the Ghidra export: `python mc.py body <addr>` (decompiled C), `tree <addr> [depth]` (call tree with sizes), `callers <addr>`, `strs <addr>` (strings referenced), `reach <addr>` (transitive reach). |
| `img.py` | Raw access to the relocated carpet.exe image (cached in `extracted/exe/carpet_flat.bin`): `dis <addr> <end|count>` capstone disassembly, `bytes`, `dwords`; importable (`img.u32`, `img.dis`). Used for stub classification and jump-table recovery. |
| `ptrtabs.py` | Scan LE fixups for data-segment dwords pointing into code; prints pointer tables (how the dispatch tables were found). |
| `targets.py` | Write `ghidra/names/code_ptr_targets.txt` (input of `FixPointerTargets.java`). |
| `classtab.py` | Dump the Thing class/model dispatch tables with model names. |
| `gennames.py` | Regenerate `ghidra/names/carpet_names_tables.csv` from the tables (state -> type mapping inside). |
| `merge_names.py` | Merge CSV blocks from `docs/analysis/agent_*.md` into `carpet_names.csv` (conflict rules + overrides at the top). |

Typical naming round: edit/add names -> run the headless chain in the top-level README
(ApplyNames tables, then curated, then ExportAll) -> `mc.py` against the fresh export.
