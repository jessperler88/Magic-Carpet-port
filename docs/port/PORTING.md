# Porting conventions (Phase 2)

How decompiled carpet.exe code becomes C++ in `src/mcengine`. Read `docs/ENGINE.md` for the engine
itself and `docs/ROADMAP.md` for the plan; this file is about the mechanics of translation.

## Sources of truth, in order

1. **Disassembly** of the retail image: `python tools/analysis/img.py dis <addr> <end_addr|count>`
   (capstone, addresses as Ghidra shows them). Watcom passes the first arguments in EAX, EDX, EBX,
   ECX and the rest on the stack; Ghidra's C export drops register arguments and shows stack
   arguments as `in_stack_000000xx`, so **for any function that takes arguments, confirm the
   argument order in the disassembly of the caller** (look at the `push` sequence and the register
   loads right before the `call`).
2. **Decompiled C**: `python tools/analysis/mc.py body <addr>` (from `ghidra/export/carpet_all.c`).
   Good for control flow and for stack-argument functions; unreliable for register arguments,
   for 8/16-bit arithmetic (CONCAT11/CONCAT22 noise) and for anything the decompiler marked
   `extraout_*`.
3. **docs/ENGINE.md**: field meanings, table layouts, established facts. Quote it, do not re-derive
   it, but when the disassembly contradicts it, trust the disassembly and note the correction in
   your report.
4. Call graph: `mc.py tree <addr> [depth]`, `mc.py callers <addr>`, strings `mc.py strs <addr>`.

## Code conventions

- One translation unit per subsystem (`terrain_gen.cpp`, `raster.cpp`, `render_landscape.cpp`,
  `tables.cpp`, ...). Headers with the public interface are already in place: `mc_types.h`
  (exact-layout structs, offsets asserted), `mc_globals.h` (state, maps, tables), `mc_math.h`
  (trig / isqrt / RNG), `raster.h`, `render.h`, `terrain.h`, `tables.h`, `engine.h`. Do not change
  another subsystem's header without saying so in your report; add private helpers as `static`.
- Function names: the original name **without** the address suffix (`terrain_carve_rivers`), and a
  comment on the definition giving the original name with the address
  (`// terrain_carve_rivers_31430`). Globals keep the `g_` names from `carpet_types.txt`; a global
  that only exists as `DAT_xxxxxxxx` gets a descriptive `g_` name plus the DAT address in a comment.
- Integer semantics must match the original: use `int32_t`/`uint16_t`/`uint8_t` where the code
  used 32/16/8-bit operations, keep wrap-around (`& 0xff`, `& 0x7ff`, `(uint16_t)` casts), keep
  arithmetic shifts on signed values, keep division as the CPU did it (`idiv` truncates toward zero;
  `sar` floors). 16.16 fixed point stays integer; never introduce floating point into translated
  logic.
- Map addressing: cell index = `(y_cell << 8) | x_cell` (`mc_cell()`); the maps wrap at 256, so
  neighbour arithmetic is on `uint8_t` coordinates. World coordinates are 16-bit with the cell in the
  high byte. Height byte * 0x20 = world height.
- Pointer fields of the original structs are 32-bit indices in the port (see `mc_types.h` header
  comment). `Thing.next`, list heads and `GameState.free_list` hold thing indices.
- Tables that live in the executable's data segment are extracted, not retyped: add a line to the
  `.tables` spec file your subsystem owns (`src/mcengine/tables/<subsystem>.tables`, format in
  `tools/port/gen_exe_tables.py`), run `python tools/port/gen_exe_tables.py <subsystem>`, include
  `gen/<subsystem>_tables.h`. Tables that the game *computes* at run time are computed by the port
  (never dump run-time memory from the exe image).
- No DOS: no segment games, no fixed addresses, no `int` calls. Where the original reads
  `DAT_0012ed74` (the VGA back buffer) the port takes a `FrameBuffer`; file access goes through
  `mcdata/mcfile.h` (`mc_load_rnc_into`, `mc_read_unpacked`, `mc_path_join`).
- Build: `cd src && cmake --preset msvc-x64 && cmake --build --preset debug --target <target>`.
  Every `tests/*_test.cpp` becomes a test executable automatically (argv[1] = game dir, exit code
  0 = pass). Run it after building: `..\build\Debug\<name>.exe "C:\Magic Carpet\MagicCarpet\magic"`.
  Warnings are errors in spirit: fix them.

## Things and handlers (round 2 on)

- Game code passes **all arguments on the stack** (caller cleans up, result in EAX): read the `push`
  sequence before every `call` for order and width. EBX/ESI/EDI/EBP are callee-saved locals.
- `thing.h` is the Thing core (pool, cell lists, sprites / extents, `thing_create`, `thing_update_all`,
  level spawning, terrain probes, position / angle helpers). Call it instead of re-translating.
- Handlers are bound to the extracted class tables (`gen/dispatch_tables.h`, regenerate with
  `python tools/port/gen_dispatch.py`) **by original address** from the subsystem's
  `<subsystem>_register_handlers()`: `thing_register_update(0x23c20, effect_fire_update)` (Table A,
  `void f(Thing *)`), `thing_register_create(0x38730, effect_create_explosion)` (Table B,
  `Thing *f(const Pos *)`). `engine_init` calls every register function; a unit test calls the ones it
  links. `thing_dispatch_report(stdout)` lists handlers that were dispatched but are not ported.
- Calls into a subsystem that may not be linked go through a null-default function-pointer hook
  (`g_hook_*`), installed by the callee's register function.
- Pointer fields are indices / offsets: thing index for `Thing.next`, list heads and the free /
  active stacks; MoveDesc index for `Thing.desc` (`mc_move_desc()`); GameState byte offset of the
  owner's P block for `Thing.player` (`thing_player_block()`, `player_block_offset()`, 0 = dummy).
- Briefing of the round with the parallel-work rules: `docs/port/BRIEFING_round2.md`.

## Simulation core (round 3 on)

- `spatial.h`: collision searches, area damage, pending-damage slots (`Thing.damage_slots[type]`,
  dealers write, the victim consumes in its own update), `cell_kill_things`, `creature_check_terrain`,
  small aim helpers. `level_features.h`: castle footprints and site tests. `terrain_paint.h`: terrain
  painting and the spiral cell walk. Call these instead of re-translating the originals.
- `sound_request(thing, player, sound)` / `sound_fade(...)` (thing.h) at every original call site.
- `sim.h`: `sim_init`, `sim_load_level`, `sim_load_snapshot` (no renderer), `sim_register_gameplay()`
  (every gameplay subsystem + the hooks between them), `sim_prepare_movie()`. CMake: a unit test lists
  `${MC_SIM_CORE}` plus the sources under test (other subsystems are stand-ins or absent), an
  integration test lists `${MC_SIM_ALL}`. `tests/spatial_test.cpp` and `tests/sim_test.cpp` are the
  models; `sim_test` prints the handlers still dispatched without a port.
- A new subsystem: own `.h` / `.cpp` with `<name>_register_handlers()`, added to `sim_all.cpp` and
  `MC_SIM_ALL`, own `tests/<name>_test.cpp` + `.cmake`, report `docs/analysis/port_<name>.md`.
- Briefing of round 3 (parallel-work rules, verification ladder): `docs/port/BRIEFING_round3.md`.

## Verification targets

- **Per-tick reference (round 4 on, the main check):** `extracted/reference/movie0*/tick%05d.gam` are the
  original's GameState after every tick of movie 0, dumped by a patched carpet.exe in DOSBox
  (`tools/reference/`, `docs/analysis/port_reference.md`). `reference_test` replays the port and prints,
  per slot, the first tick and field that differ; `MC_REF_TRACE=slot,slot` / `MC_REF_PTRACE=player`
  (with `MC_REF_TRACE_FROM=tick`) trace fields per tick. Any change to game logic must keep it
  byte-identical as far as it was before. The original renders once per tick and its render writes
  game state (`hud_tick_state`); the dump point is before that render.

- `MagicCarpet/magic/movie/map00000.dat` is the engine's own terrain dump for level 38 (0-based
  index 38 in `levels.dat`), taken by the demo recorder: `g_map_type[0x10000]`,
  `g_map_height[0x10000]`, `g_map_light[0x10000]`, `g_map_flags[0x10000]`,
  `g_cell_things[0x10000] (u16)`, then the 0x12c2-byte compact corner-class texture table `g_corner_tex_table` (0xb58b0).
  `movie/gam00000.dat` is the matching raw `GameState` (0x38d03 bytes). Both were taken **413 ticks
  after level start** (PlayerRec.tick), so level-start features, castles built since and anything that
  moved differ from a freshly generated level; everything static must match bit for bit.
  `engine_load_snapshot()` / `thing_relink_snapshot()` load the pair into the port (449 live things).
- `data/tables.dat` is the shipped output of the table generator: regenerate and compare.
- Rendering has no byte-exact reference yet; compare against DOSBox screenshots by eye and keep the
  algorithm identical so pixel-exact comparison is possible later.

## Report format

End with a short Markdown report (it is pasted into `docs/analysis/port_<subsystem>.md` by the
integrator): what was translated (function list with addresses), what was verified and how, open
questions / deviations from the original, and any correction to `docs/ENGINE.md` or
`carpet_types.txt` you found.
