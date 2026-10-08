# Briefing 4: resolve the remaining UNCERTAIN names in carpet.exe (Magic Carpet 1, Watcom C, DOS/4GW)

Read first: `C:\Magic Carpet\docs\analysis\briefing2.md` (tools, Watcom register convention, caveats) and the parts of
`C:\Magic Carpet\docs\ENGINE.md` that cover your functions (Thing struct and dispatch tables ~line 160-500, spells
~line 480 and ~1250, castles, creatures, HMI/GUS/OPL drivers at the end). Round 1-3 reports in
`docs/analysis/agent*_*.md` hold per-function detail for the neighbours of your functions.

Exports (read-only, freshly regenerated): `C:\Magic Carpet\ghidra\export\carpet_all.c`, `carpet_functions.csv`,
`carpet_calls.csv`, `carpet_strings.csv`. Helpers (python 3, run from anywhere, never from `C:\Magic Carpet` itself):
   python "C:/Magic Carpet/tools/analysis/mc.py" body <addr>      decompiled C (addr like 0x102b0)
   python "C:/Magic Carpet/tools/analysis/mc.py" tree <addr> [d]  call tree with sizes and caller counts
   python "C:/Magic Carpet/tools/analysis/mc.py" callers <addr>
   python "C:/Magic Carpet/tools/analysis/mc.py" strs <addr>      strings referenced
   python "C:/Magic Carpet/tools/analysis/img.py" dis <addr> <end_addr|count>   raw capstone disassembly
   python "C:/Magic Carpet/tools/analysis/img.py" dwords <addr> <n>             data tables
   python "C:/Magic Carpet/tools/analysis/classtab.py"                           Thing class/model dispatch tables

Your list is `docs/analysis/todo4_<X>.txt`: every line is `address | current name | current comment`, the comment
starts with `UNCERTAIN:`. For each function decide, from the C, the disassembly, the callers and the data it touches:
1. Is the current name right? Keep it, or give a better one (same rules as before: snake_case verb_noun with the
   subsystem prefix, keep the `_<addr>` suffix, lowercase hex without leading zeros).
2. Write a NEW one-line comment that states what the function does with concrete evidence (constants, offsets,
   struct fields, strings, callers, which Table A state / Table B model it serves). Do not write "UNCERTAIN" or
   "not read" any more; the point of this round is to remove that marker. Only if the evidence is genuinely
   insufficient after reading the code, append `?` to the name and say in the comment exactly what is missing.
3. For table handlers whose comment says "class N state S (not read)": read the handler, confirm the class/state
   from `classtab.py` output, and name it after what it does (e.g. which spell/effect/scenery the state is, from
   the spell order in ENGINE.md: `state = spell*3 + phase`, spell ids 0 fireball ... 7 meteor, 8 volcano,
   0xf lightning, 0x10 castle; check the create-table order in ENGINE.md around line 958 before trusting the
   old guess).
4. Functions with no callers are usually real but unused (the game ships dead code); say "no callers" and name
   them by behaviour anyway. If the function is clearly a fragment of its predecessor (starts mid-flow, no
   prologue, falls into a neighbour) put `FRAGMENT` as the name.

Caveats: Watcom register convention (EAX, EDX, EBX, ECX, then stack); `in_EBX` / `extraout_*` / `unaff_*` /
`in_stack_*` are arguments the decompiler did not model; Thing handlers get the Thing in EBX. Typed globals
`g_state` (GameState), `g_cfg` (Config), `g_move_desc`, `g_sprite_desc`, `g_class_table` and the map arrays
`g_map_*` are defined in `ghidra/names/carpet_types.txt`; use their field names in comments.

Deliverable: write ONE file, `C:\Magic Carpet\docs\analysis\agent4_<X>.md`, nothing else. Contents:
1. A fenced ```csv block with one line per function in your list:
   `0x<addr8>,<name>_<addr>,<comment>` (wrap the comment in double quotes if it contains commas).
2. A short Markdown section with anything new you established that ENGINE.md should record (struct fields,
   globals, algorithms), with evidence, and open questions. Flag speculation.
Every function in your list must appear in the CSV. Do not modify any other file.
