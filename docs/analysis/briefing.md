# Briefing: carpet.exe (Magic Carpet 1, Watcom C, DOS/4GW) analysis

Exports (read-only, do not modify): C:\Magic Carpet\ghidra\export\carpet_all.c (decompiled C, each function
starts with a line `// ==== <addr8> FUN_<addr8> size=N`), carpet_functions.csv, carpet_calls.csv (caller,callee),
carpet_strings.csv (address,length,xrefcount,xrefs,string).
Helper (python 3): C:/Magic Carpet/tools/analysis/mc.py
   python mc.py body <addr>      -> decompiled C of a function (addr like 29050 or 0x00029050)
   python mc.py tree <addr> [d]  -> call tree with sizes and caller counts
   python mc.py callers <addr>   -> who calls it
   python mc.py strs <addr>      -> strings referenced from it
Existing names: C:\Magic Carpet\ghidra\names\carpet_names.csv ; engine notes: C:\Magic Carpet\docs\ENGINE.md (read it first).

Established facts:
- Calling convention is Watcom register (__watcall: EAX, EDX, EBX, ECX then stack). Ghidra's parameter recovery
  is poor: `extraout_EBX`, `in_stack_0000000c`, `unaff_EBP` etc. mean the argument came in a register the
  decompiler didn't model. Treat `in_EBX`/`extraout_EBX` in handler-like functions as the Thing pointer.
- DAT_000adf74 -> config/flags struct (bit flags at +0, +2; byte +0x96 = detail/player count?; +0x99 frame time).
- DAT_000adf6c -> game-state block. Per-player records of 0x801 bytes at +0x340F indexed by *(short*)(state+8).
  Thing pool: 1000 records of 0xa4 (164) bytes from state+0x7507 to state+0x2f503.
  Thing fields known: +0x3f counter (incremented after handler call), +0x40 class (byte), +0x41 subtype?,
  +0x46 model (byte).
- Class/model dispatch: class table at 0x943da, 18-byte records: +0 ptr to model table A, +4 ptr to model table B,
  +8 parent ptr (0x943cc), +12 u16. Model table records are 14 bytes: {u32 parent(0x943cc), u16 model, u32 handler,
  u32 enabled}; code does `rec = tableA + model*0xe; if (rec->model == model && rec->enabled) rec->handler()`
  (see FUN_00035690 and the loop in FUN_0003dce0). Table A for class c is at *(int*)(0x943da + c*0x12).
  Level-file classes: 2 Scenery, 3 Player, 5 Creature, 7 Weather, 10 Effect, 11 Switch, 12 Spell
  (model names per class in C:\Magic Carpet\tools\mctools\level.py).
- Game loop: game_main_32a00 -> level_load_and_init_3d3b0 -> in-level loop calling game_tick_32f90 ->
  FUN_00032e80 which calls, in order: FUN_00033010 (if state+0x219b==0), FUN_0004be50, FUN_00016660 (if !(cfg&4)),
  FUN_0003a8b0, FUN_0003db20, FUN_0003dce0 (1, 4 or 16 times depending on cfg+0x96), FUN_000494b0,
  FUN_0001fab0, FUN_0004ad80, FUN_0003ca00 (if DAT_00094338), FUN_0002f480.
- Watcom CRT + DOS/4GW glue lives above ~0x5e000; FUN_000603bc (45 callers), FUN_0004a9a0, mem_clear_5afd0 are
  small helpers. Likely CRT: FUN_0006xxxx with printf-like string refs.
- Magic Carpet 2's reconstruction (remc2 / MC2-HD) uses names like `sub_XXXXX`; MC1 shares the engine lineage:
  landscape is a heightfield rendered with a software "voxel"/span renderer, sprites are span-encoded, 8-bit
  palette with blend tables.

Deliverable format (write it to the file named in your task, nothing else on disk):
1. A CSV block with lines `0x<addr8>,<name>_<addr5-6 hex lowercase>,<comment>` for every function you are
   confident about (confidence >= medium; mark lower confidence with `?` at the end of the name, e.g.
   `render_sky_2dac0?`). Names: snake_case verb_noun, subsystem prefix (render_, thing_, creature_, spell_,
   player_, input_, sound_, ui_, fe_ (front end), net_, crt_, dos_, vga_, math_, mem_, file_). Keep the address suffix.
2. A Markdown section for docs/ENGINE.md: data structures recovered (struct field offsets with meaning), the
   algorithm of each major function in a few lines, global variables identified (DAT_xxxxxx -> meaning), and
   open questions. Cite evidence (constants, strings, call sites). Be concrete, no speculation without a flag.
Read actual decompiled bodies; do not guess from names alone. Prefer breadth: a short, evidenced note on many
functions beats a deep essay on one.
