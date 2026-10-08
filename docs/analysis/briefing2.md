# Briefing 2: naming the remaining carpet.exe functions (Magic Carpet 1, Watcom C, DOS/4GW)

Read first: C:\Magic Carpet\docs\ENGINE.md (everything established so far: layout, Thing struct, dispatch tables,
tick order, renderer, input/sound/net, front end, CRT). Then C:\Magic Carpet\docs\analysis\briefing.md (facts and
naming rules from round 1; still valid). Round-1 agent reports in docs/analysis/agent_*.md hold per-function detail.

Exports (read-only): C:\Magic Carpet\ghidra\export\carpet_all.c (decompiled C; each function starts with
`// ==== <addr8> <name> size=N`), carpet_functions.csv, carpet_calls.csv, carpet_strings.csv.
Helpers (python 3, run from anywhere):
   python "C:/Magic Carpet/tools/analysis/mc.py" body <addr>      decompiled C (addr like 29050 or 0x00029050)
   python "C:/Magic Carpet/tools/analysis/mc.py" tree <addr> [d]  call tree with sizes and caller counts
   python "C:/Magic Carpet/tools/analysis/mc.py" callers <addr>
   python "C:/Magic Carpet/tools/analysis/mc.py" strs <addr>      strings referenced
   python "C:/Magic Carpet/tools/analysis/img.py" dis <addr> <end_addr|count>   raw disassembly (capstone) when the
                                                                   decompiler output is garbled (register args, asm)
Name files: ghidra/names/carpet_names.csv (curated), ghidra/names/carpet_names_tables.csv (generated handler names:
`<class>_<model>_s<state>_update_<addr>` / `<class>_create_<model>_<addr>`).

Your task list is docs/analysis/todo2_<X>.txt: (1) every unnamed `FUN_` >= 32 bytes in your address range,
(2) existing names in the range marked UNCERTAIN (confirm or correct). Work through (1) in descending size order
first, then (2). If time remains, read the generated-name handlers in your range and add a one-line behaviour
comment for each (same name, new comment).

Caveats:
- Watcom register convention (EAX, EDX, EBX, ECX, then stack). `in_EBX`/`extraout_EBX`/`unaff_*`/`in_stack_*`
  are register or stack arguments the decompiler did not model. Thing handlers get the Thing in EBX (shown as
  param or in_EBX). Read the disassembly (img.py dis) when the C is confusing.
- About 45 functions start on 2-9 padding bytes (`lea eax,[eax]`, `mov ebx,ebx`, `nop`) because the gap finder
  started early; name them at the address given in the todo list anyway (the start is fixed separately).
- Some tiny `FUN_` are tail fragments; those are not in your list. If a listed function is clearly a fragment of
  its predecessor (starts mid-flow, falls into/jumps into neighbours, no prologue) say so instead of naming it:
  put `FRAGMENT` as the name.
- Code above 0x5E000 is the Watcom 10 CRT, DOS/4GW glue, HMI Sound Operating System (sos*), Gravis UltraSound
  driver, VESA/VGA and Watcom's graphics/text console. Name by behaviour: crt_, dos_, dpmi_, hmi_, gus_, opl_,
  vesa_, vga_, con_. Known: printf core 6a0e6, sprintf 603bc, malloc 6b186, open 66296, read 664ec, write 6af03.

Deliverable: write ONE file, C:\Magic Carpet\docs\analysis\agent2_<X>.md (X = your region letter), nothing else.
Contents:
1. A fenced ```csv block with lines `0x<addr8>,<name>_<addr hex lowercase, no leading zeros>,<comment>` for every
   function you read (confidence >= medium; append `?` to the name for low confidence). Names: snake_case
   verb_noun with subsystem prefix (render_, thing_, creature_, spell_, projectile_, effect_, player_, castle_,
   terrain_, level_, input_, sound_, music_, ui_, fe_, net_, demo_, fli_, mem_, file_, crt_, dos_, vga_, vesa_,
   math_, dbg_). Keep the address suffix. The comment is one line, no commas problems (wrap the comment in
   double quotes if it contains commas), concrete: what it does, key constants/offsets/strings, evidence.
2. A Markdown section for docs/ENGINE.md: new struct fields / globals / algorithms you established, with
   evidence, plus open questions. Be concrete; flag speculation.
Breadth beats depth: a short evidenced line on every function in your list is the goal. Do not modify any file
other than your deliverable.
