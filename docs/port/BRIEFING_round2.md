# Port round 2 briefing (2026-10-06): Things, level features, sprites, players

Read `docs/port/PORTING.md` first (sources of truth, code conventions, report format); this file adds
what is new in round 2. The engine itself is described in `docs/ENGINE.md` (sections "Things: update,
spatial index, spawning", "Region B" / "Region C" of round 2, "Round 4 findings") and in the per-function
evidence files `docs/analysis/agent*_*.md`.

## What exists now

- `src/mcengine/thing.h` / `thing.cpp` (done, tested by `tests/thing_test.cpp`): Thing pool
  (`thing_alloc`, `thing_free`, `thing_mark_delete`, `thing_pool_reset`, `models_initialise`), per-cell
  lists (`thing_link_cell`, `thing_unlink_cell`, `thing_move_to`), `thing_set_sprite*`,
  `thing_create(pos, cls, type)`, `thing_update_all`, `level_run_terrain_effects`,
  `level_spawn_thing_record`, `switch_activate`, terrain probes (`terrain_height_at`, ...), and the
  position / angle helpers of 0x3e420..0x3ea70 + `math_atan2` + `math_bbox_overlap`. Read the header
  before translating anything: if the original calls one of these, call the port function.
- Dispatch: the class tables are extracted into `src/mcengine/gen/dispatch_tables.h` (Table A = update
  handlers by state, Table B = constructors by type, with the original handler address and name of every
  record). A translated handler is bound **by original address** in your subsystem's register function:
  `thing_register_update(0x23c20, effect_fire_update);` / `thing_register_create(0x38730, effect_create_explosion);`.
  Signatures: update `void f(Thing *t)`, create `Thing *f(const Pos *pos)` (null when thing_alloc fails).
  Unported handlers are no-ops / failed creates and are listed by `thing_dispatch_report(stdout)`.
- Structs: `mc_types.h` (`Thing`, `Pos`, `GameState`, `PlayerRec`, `Config`, `LevelData`, `ThingInit`,
  `SpriteDesc`, `MoveDesc`). Pointer fields are indices: `Thing.next`, list heads in `g_cfg`,
  free / active lists = thing index; `Thing.desc` = MoveDesc index (`mc_move_desc(t->desc)`);
  `Thing.player` = byte offset of the owner's "P" block (PlayerRec+0x44f) inside the GameState, 0 = the
  dummy block: use `thing_player_block(t)` to get the `uint8_t *` and `player_block_offset(p)` to set it.
  `g_sprite_desc[]` is the run-time sprite descriptor table (extents / draw_type completed from
  tmaps.dat by `sprite_table_init_sizes`).
- `engine_load_level()` now mirrors level_load_and_init_3d3b0 (terrain_build, thing_pool_reset,
  `terrain_generate_features()`, models_initialise, `switch_activate(0, true)`, `players_init_records()`)
  and `engine_load_snapshot("movie/gam00000.dat", "movie/map00000.dat")` loads the engine's own state
  snapshot. Placeholder files exist for every round-2 subsystem (`level_features.*`, `constructors.*`,
  `player.*`, `sprites.h` + `sprite_cache.cpp`); the owner replaces the placeholder body.

## Calling convention (important)

Game code in this binary passes **all arguments on the stack** (caller cleans up: `push c; push b;
push a; call f; add esp, 0xc`), result in EAX. At function entry the first argument is `[esp+4]`; after
N register pushes it is `[esp+4+4N]`. Ghidra's export shows these as `in_stack_00000004`... and often
shows no parameters at all at call sites, so **read the pushes before each call in the disassembly**
(`python tools/analysis/img.py dis <addr> <end>`) to get the argument order and width (a `movsx` / `cwde`
before a push = signed 16-bit value, `xor eax,eax; mov ax,..` = unsigned). EBX/ESI/EDI/EBP are
callee-saved, so `extraout_EBX` in the C is just the caller's own local that survived the call.
The few `__watcall` register-argument functions are CRT / asm helpers (`math_isqrt_4cd7a`, the
rasteriser entry points), not game logic.

## Ground truth for verification

- `MagicCarpet/magic/movie/gam00000.dat` = raw GameState of level 38 (0-based) taken by the demo
  recorder **413 ticks after level start** (player 0's `PlayerRec.tick` = 413), with 449 live things
  (150 scenery, 8 class-3, 169 creatures, 4 projectiles, 74 effects, 5 switches, 39 spells) and 4
  players. `map00000.dat` = the five maps at the same moment (`g_map_type`, `g_map_height`,
  `g_map_light`, `g_map_flags`, `g_cell_things`, then the 0x12c2 bytes of `g_corner_tex_table`, 0xb58b0). So static things
  (scenery, switches, spell pickups, wizard effects) and every field a constructor writes and nobody
  changes afterwards must match a freshly generated level 38 exactly; creatures have moved, timers
  have run. `thing_relink_snapshot()` converts the pointers; `tests/thing_test.cpp::test_snapshot` shows
  how to load the pair in a unit test without the whole engine.
- `movie/mvi00000.dat` (342,010 bytes) is the input recording that goes with it (format: see
  `demo_record_playback_step_3c540`).
- The recording was made by a slightly different build (its data segment is shifted by 4 bytes around
  the MoveDesc table); thing_relink_snapshot already compensates.

## Rules for working in parallel

- Four agents work at the same time in the same tree (no git). **Only create / modify the files your
  task names.** Shared files (`thing.h`, `thing.cpp`, `mc_types.h`, `mc_globals.*`, `engine.*`,
  `render.h`, `terrain.h`, `CMakeLists.txt`, `PORTING.md`) are read-only unless your task says
  otherwise; if you need a change there (a missing helper, a wrong field name or type, a bug in
  thing.cpp), work around it locally (a `static` helper, an accessor) and describe the change you want in
  your report. Use the Edit tool, never a whole-file Write, on any file you did not create.
- A call into another round-2 subsystem goes through a function-pointer hook that the *callee's* owner
  installs (pattern: `g_hook_effect_wizard_init` in thing.h). Declare the hook in your own header,
  default it to null, test for null at the call site, and list it in your report so the integrator can
  connect it. Do not include another round-2 subsystem's header.
- Things that are not ported yet (sound, creature AI, spells, HUD, network): leave a one-line
  `// TODO(port): sound_request_49720(...)` at the call site, keep every side effect on game state
  that you can keep, and list the omissions in the report.
- Build in **your own build directory** so parallel builds do not collide (replace X by your letter):
  `cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_X` once, then
  `cmake --build ../build_X --config Debug --target <your>_test` and run
  `../build_X/Debug/<your>_test.exe "C:/Magic Carpet/MagicCarpet/magic"`. Your test is declared in
  `src/tests/<your>_test.cmake` with `mc_unit_test(<your>_test tests/<your>_test.cpp mcengine/thing.cpp
  mcengine/terrain_gen.cpp mcengine/<your sources>...)` (compiles only the listed engine sources, so the
  other agents' half-written files cannot break your build; re-run the `cmake --preset ... -B` line after
  adding the .cmake file). Zero warnings.
- Data tables that live in the exe: `src/mcengine/tables/<subsystem>.tables` +
  `python tools/port/gen_exe_tables.py <subsystem>` (never retype a table, never edit another
  subsystem's .tables file).
- Bash tool pitfalls on this machine: a `\n` inside a heredoc'd python / C string is turned into a real
  newline, and long heredocs with quotes fail; write source and scripts with the Write / Edit tools and
  run scripts from a file in your scratch directory. Never run python with `C:\Magic Carpet` itself as
  the working directory (its `ghidra/` folder shadows the pyghidra package); `tools/analysis` is fine.

## Deliverable

Your sources + unit test (exit code 0), and a report written to `docs/analysis/port_<subsystem>.md`
in the format of PORTING.md ("Report format": functions translated with addresses, verification
results with numbers, deviations, hooks you declared, open questions, corrections for ENGINE.md /
carpet_types.txt / mc_types.h). Your final message is a short summary of that report (what works, what
is verified, what is missing, which shared-file changes you request).
