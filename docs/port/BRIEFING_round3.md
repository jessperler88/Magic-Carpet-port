# Port round 3 briefing (2026-10-06): damage, projectiles, spells, effects, castles, creatures

Read `docs/port/PORTING.md` first (sources of truth, code conventions, report format) and skim
`docs/port/BRIEFING_round2.md` (dispatch by original address, hooks, pointer fields as indices, the
level-38 snapshot). This file adds what is new in round 3 and lists the five tasks. The engine itself
is described in `docs/ENGINE.md` (sections "Things: update, spatial index, spawning", "Region A / B /
C", "Round 4 findings") and, with more per-function detail, in `docs/analysis/agent*_*.md`.

## What exists now (round-3 core, written by the integrator)

- `spatial.h` / `spatial.cpp` - the query and damage layer every handler uses:
  `thing_find_collision` (105f0), `thing_find_collision_other_owner` (10980), `thing_find_mana_near`
  (10730), `thing_find_mana_ball_touching` (10870), `thing_exists_near_pos` (10ac0),
  `thing_area_damage` (10d20), `thing_area_damage_fire` (11160), `thing_area_damage_quake` (11450),
  `thing_add_pending_damage` (117c0), `thing_try_damage` (116e0), `cell_kill_things` (3da30),
  `creature_check_terrain` (102b0), `thing_z_add_half_height` / `thing_z_sub_half_height` /
  `thing_aim_at` (43ea0 / 43ec0 / 43ee0), `castle_spell_reset_charge` (41310) and the hook
  `g_hook_thing_drop_mana_ball` (25fe0, installed by the effects owner). **Read the header before
  translating: when the original calls one of these addresses, call the port function.**
- Damage model: `Thing.damage_slots[6]` (+0x5a, `{i32 amount, u16 attacker}` per damage type); dealers
  only write a slot, the victim consumes it in its own update. `Thing.prop_flags` (+0x1c) is the mask of
  accepted damage types. Mind the two write flavours (area functions accumulate while an attacker is
  recorded; 117c0 does the reverse) - both are in spatial.cpp, do not re-derive them.
- `level_features.h` gained the castle site tests: `castle_near_thing` (11820), `castle_crush_wizards`
  (118c0), `castle_footprint_clear` (11980), `castle_site_clear_at_pos` (11be0). It already had
  `thing_set_castle_extents` (353f0), `castle_stamp_footprint` (26320), `effect_wizard_init` (35090) and
  the terrain helpers (0x34250..0x34bb0); `terrain_paint.h` has the painting functions and now exports
  the spiral walk (`SpiralSearch`, `spiral_search_begin` / `spiral_search_next`; there is no `_end`).
- Sound: call `sound_request(thing_index, player, sound_id)` (49720) and `sound_fade(...)` (49c40) from
  `thing.h` at every original call site (they forward to a hook; no TODO comments for these two).
- `sim.h` / `sim.cpp` - the renderer-free simulation glue: `sim_init(game_dir)` (globals, data files,
  handlers of thing / level features / constructors / players, hooks), `sim_load_level(index)`,
  `sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat")`. One tick of the simulation is
  `game_tick_sim()` (player.h); a movie is played with `demo_open` / `demo_step` (demo.h).
- CMake: `${MC_SIM_CORE}` = thing, terrain_gen, terrain_paint, spatial, level_features, constructors,
  player, demo, sim. Declare your test as
  `mc_unit_test(<name>_test tests/<name>_test.cpp ${MC_SIM_CORE} mcengine/<your sources>...)`.
  `tests/spatial_test.cpp` is the model for a test on a generated level 38 and on the snapshot.
- Placeholder files exist for every round-3 subsystem (`effects.*`, `projectiles.*`, `spells.*`,
  `castle.*`, `scenery.*`, `creatures.h` + `creature_common.cpp` + `creatures.cpp`); each has a
  `<name>_register_handlers()` that `engine_init` already calls. The owner replaces the bodies.
- All constructors (Table B) are ported, so `thing_create(pos, cls, type)` works for every class;
  what is missing are the Table A update handlers listed per task below (`gen/dispatch_tables.h` has
  every record with its original address and name; `thing_dispatch_report(stdout)` prints the handlers
  a run dispatched without finding a port).

## Rules (as in round 2, tightened)

- Five agents work at the same time in the same tree (no git). **Only create / modify the files your
  task names.** Everything else is read-only: if a shared file needs a change (missing helper, wrong
  field name or type, a bug), work around it locally (a `static` helper, a raw-offset accessor with
  the offset in a comment) and describe the wanted change in your report. Use Edit, never a whole-file
  Write, on a file you did not create (your own placeholder files you may overwrite).
- A function outside your address range that is not ported and that you need: if it is small and
  clearly belongs to nobody else's task below, translate it as a `static` helper in your file and list
  it in the report ("extra functions"); if it belongs to another task, call it through a null-default
  hook declared in **your** header (`extern R (*g_hook_xxx)(...)`, defined null in your .cpp, tested at
  the call site) and list the hook in the report so the integrator can connect it. Handlers of other
  subsystems are reached through the tables anyway (`thing_create`, `thing_update_fn(cls, state)`).
- Not ported and not yours (AI wizard, HUD / ui_*, network, palette effects that are not already in
  player.h, music): keep every game-state side effect you can, leave
  `// TODO(port): <original name>(args)` at the call site, list it in the report.
- Calling convention: game code passes **all** arguments on the stack (see BRIEFING_round2); read the
  pushes before each call in `python tools/analysis/img.py dis <addr> <end>`; Ghidra's C
  (`python tools/analysis/mc.py body <addr>`) hides them. `extraout_EBX` etc. are the caller's own
  callee-saved locals. Integer widths and signedness (`movsx` vs `movzx` / `xor eax,eax; mov ax,..`,
  `sar` vs `idiv`, `jl` vs `jb`) must be kept exactly; the per-thing RNG is `t->rng = mc_lcg(t->rng)`
  (0x24a1 / 0x24df) and every draw the original makes must be made in the same order.
- Jump tables inside a function (`jmp dword ptr cs:[eax*4 + X]`): read the targets with
  `python tools/analysis/img.py dwords <X> <count>`.
- Build in **your own build directory** (X = your letter): once
  `cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_X`, then
  `cmake --build ../build_X --config Debug --target <name>_test` and run
  `../build_X/Debug/<name>_test.exe "C:/Magic Carpet/MagicCarpet/magic"`. Build only your own test
  target (the `mcengine` library and `engine_test` contain the other agents' half-written files).
  Create your source files before your `.cmake` file; re-run the configure line after adding it (if
  configure fails on another agent's missing file, wait a moment and retry). Zero warnings (/W4).
- Bash tool pitfalls on this machine: a `\n` inside a heredoc'd python / C string becomes a real
  newline and long heredocs with quotes fail - write sources and scripts with the Write / Edit tools
  and run scripts from a file in your scratchpad directory. Never run python with `C:\Magic Carpet`
  itself as the working directory (`tools/analysis` is fine).

## Verification

There is no per-tick reference from the original yet (the snapshot is a single moment, 413 ticks into
level 38: 169 creatures, 4 projectiles, 74 effects, 39 spells, 5 switches, 150 scenery, 4 players).
So, in this order:

1. Translate from the disassembly, function by function, and keep the structure recognisable.
2. Unit-test the pure pieces by construction (hand-computed cases from the disassembly: a state
   transition, a timer, a damage amount, an RNG-driven value).
3. Use the snapshot as a source of *real* in-flight objects of your classes: every field value you see
   there must be producible by your code (e.g. a spell's `cast_ticks` / `duration`, a projectile's
   speed and desc, a mana ball's sprite), and invariants must hold when you step them
   (`thing_update_all()` with your handlers registered: no crash, pool lists stay consistent, things
   that should die do, nothing leaves the map, RNG draws only where the original draws).
4. Smoke run: `sim_load_level(38)` (and a few other levels), your handlers registered, 2000+ ticks of
   `game_tick_sim()` (optionally with the movie: see `tests/player_test.cpp`), then
   `thing_dispatch_report(stdout)`; print statistics a reader can judge (counts per type / state over
   time). Report honestly what is verified by which of these means and what is only translated.

## Deliverable

Your sources, your unit test (exit code 0, zero warnings), and a report written to
`docs/analysis/port_<name>.md` in the PORTING.md format: functions translated (name + address),
verification (what, how, numbers), deviations and known gaps, hooks you declared, extra functions you
translated outside your range, TODO(port) call sites, requested shared-file changes, corrections to
`docs/ENGINE.md` / `mc_types.h` / `carpet_types.txt`. Your final message is a short summary of that
report.

## Tasks

### A - projectiles (`projectiles.h`, `projectiles.cpp`, `tables/projectiles.tables`, `tests/projectiles_test.*`, report `port_projectiles.md`, build dir `build_A`)

Class 9, code 0x43f30..0x468e0: `projectile_steer_to_target_43f30`, `thing_turn_toward_43ff0`,
`projectile_record_hit_stats_440a0`, `projectile_fly_and_impact_44150`, every class-9 Table A handler
(44510, 448b0, 44a40, 44a50, 44a90, 44aa0, 44fc0, 45360, 45530 castle seed, 457a0, 45b60, 45c70,
45c90, 45e60), `projectile_step_44ea0`, target selection (`projectile_pick_target_45f00`,
`projectile_target_score_46470`, `target_aim_score_465b0`, `projectile_line_of_fire_clear_466af`).
Export in your header what other subsystems will want (creature attacks and the AI call the target
selection and line-of-fire functions; spells create projectiles through `thing_create`). The
constructors are in `constructors.cpp` (read them for the fields a projectile starts with).
ENGINE.md: "Region C" (projectiles / impact effects) and agent2_C.md.

### B - spells (`spells.h`, `spells.cpp`, `tables/spells.tables`, `tests/spells_test.*`, report `port_spells.md`, build dir `build_B`)

Class 12, code 0x46ae0..0x494b0: `spell_dropped_update_46ae0`, `spell_phase2_pickup_46dd0`,
`spell_dropped_dispatch_46e50`, `spell_can_cast_46e70`, `spell_charge_mana_46f20`,
`spell_projectile_origin_46f90` and every class-12 Table A handler (47130 fireball .. 492e0 mini
fireball, including the shared phase handlers 472f0 / 47300 / 49140). How a spell Thing is driven by
its owner (hands, `cast_ticks`, `duration`, the P block) is in `player.cpp` (`player_cast_spell`,
`player_apply_controls`, `player_rebuild_spell_index`) and ENGINE.md "Region C" (spell phases).
`sound_update_494b0` and the `sound_*` / `music_*` callees are the sound system: TODO(port).

### C - effects (`effects.h`, `effects.cpp`, `tables/effects.tables`, `tests/effects_test.*`, report `port_effects.md`, build dir `build_C`)

Class 10, code 0x2376f..0x284c0, every Table A handler that is not ported yet: explosions (238b0,
23a80), 23c00, fire (23c20), 23d40, splash (23d60), 240b0, smoke (24100, 241f0), 242d0, earthquake
(242e0), 243b0, meteor (24630), 24810, 24a90, 24c10, 24c20, 24ca0, lightning (24cb0), rain of fire
(24d10), teleport (253b0), 25550, 25630, 25670, 257e0, mana ball (25980) with
`thing_drop_mana_ball_25fe0` (install `g_hook_thing_drop_mana_ball`) and `mana_ball_merge_26120`,
25f10, castle building (26b50, 26f10 raise terrain, 27d20, 27e90), 27ff0, 28050, 280d0, 28270, 28320,
284c0, plus the helpers in the range (`terrain_max_drop_around_2376f`, `effect_age_tick_25600`,
`terrain_ring_find_height_ne8_24d70`, ...). Already ported and **not** yours (level_features.cpp /
constructors.cpp, read them as the style reference): 23d30, 23dc0, 23ec0, 23f20, 23fc0, 24e20, 24eb0,
24fc0, 250b0, 251e0, 25270, 252f0, 25e20, 26320, 26680, 27660, 27710, 27930, 27f90 (check with
`grep -n "_<addr>" src/mcengine/*.cpp` before translating anything). The ui_* / movie_* / dbg_*
functions at 0x23050..0x23766 are not game logic: skip. ENGINE.md: "Region B" and agent2_B.md,
agent4_B.md.

### D - castles, balloons, scenery, switches (`castle.h`, `castle.cpp`, `scenery.h`, `scenery.cpp`, `tables/castle.tables`, `tests/castle_test.*`, report `port_castle.md`, build dir `build_D`)

Class 3 type 2 (castle) and the balloon, code 0x413a0..0x42a20: `player_flyer1_s4_update_413a0`
(state 4 = active castle), `player_flyer2_s5_update_41500` (state 5 = upgrade), 41610, 41670,
`player_respawn_start_416d0`, `castle_spill_mana_41720`, `castle_manage_balloons_and_guards_419a0`,
`castle_begin_build_stage_41f00`, 42000, `castle_collapse_level_42010`, `castle_take_damage_42460`,
`castle_find_free_mana_ball_41290`, `balloon_update_42530`, the one-byte shared handlers (42520,
42760, 42960). Already ported in player.cpp and **not** yours: 42170, 42200, 42370, 42770, 427d0,
428e0 (and 41310 in spatial.cpp). `ui_draw_radar*_42a20..43910` are HUD: skip.
`scenery.cpp`: class 2 (0x43ba0..0x43e80: trees incl. burning / regrowth, standing stones, dolmen,
bad stone) and class 11 switches (0x4a2a0..0x4a940: every Table A handler, `switch_creature_dead_trigger_4a660`,
`switch_test_player_4a8b0`, `switch_test_any_player_4a940`; `switch_activate` is in thing.h).
ENGINE.md: "Region C" (castle state machine), agent2_C.md, agent4_C.md.

### E - creatures (`creatures.h`, `creature_common.cpp`, `creatures.cpp`, `tables/creatures.tables`, `tests/creatures_test.*`, report `port_creatures.md`, build dir `build_E`)

Class 5. `creature_common.cpp`: the shared code 0x18050..0x19b70 (`creature_segment_update_18050`,
`terrain_slope_at_18150`, `creature_move_step_181e0`, `creature_idle_seek_leader_18610`,
`creature_ai_step_18870`, `creature_attack_target_18c20`, `creature_follow_leader_18e90`,
`creature_die_191d0`, `creature_dead_drop_mana_19310`, `creature_adopt_leader_19360`, the attack
callbacks 193f0..19a90) and the wake timers `creature_wake_tick_468e0` (install
`g_hook_creature_wake_tick`) / `creature_proximity_wake_timer_46960`. Declare all of it in
`creatures.h` with exact signatures: the remaining creature types are next round's work and will be
written against that header. `creatures.cpp`: the per-type handlers of the creatures level 38 uses,
0x1bb00..0x1c8e0 (skeleton, states 54..60, incl. `skeleton_convert_villager_1c1e0` and the skeleton
helpers) and 0x1d9d0..0x1ea50 (builder states 73..78, townie 79..84, trader 85..90, with the helpers
between them), then - only if everything above is done and tested - more types in address order from
0x19b70 (dragon, vulture, bee, worm, archer ...). Target selection / line of fire belongs to task A:
call it through hooks (`g_hook_projectile_pick_target` style, declared in creatures.h) and say in the
report which original functions they stand for. ENGINE.md: "Region A" (creature shared states, damage
slots), "Round 4 findings" (awake gate +0x3a), agent2_A.md, agent4_A.md.

### F - local input (`input.h`, `input.cpp`, `tables/input.tables`, `tests/input_test.*`, report `port_input.md`, build dir `build_F`)

The per-tick local input of the human player, code 0x15590..0x17a80: `player_mouse_steer_15590`,
`player_function_keys_156b0`, `player_local_input_16660` (install `g_hook_player_local_input`, declared
in player.h; `game_tick_sim` already calls it when no movie is playing), `player_queue_command_17270`,
plus `creature_kill_all_17ff0` (cheat command, called from `player_commands_process_3a8b0`: expose it,
list the hook the integrator should add). The original reads raw device state that its ISRs maintain;
in the port the platform layer (SDL, `src/mcport`, not yours) fills the same state, so define it in
`input.h` under the original meaning: the key-down table `g_key_down[128]` (0x12ee20, DOS set-1
scancodes), the last pressed scancode (DAT_0012eea0, cleared by the consumer), the mouse position in
640x400/480 space (DAT_0009e5dc / 5de), the click events (DAT_0012ee0e left, DAT_0012ee0c right,
cleared by the consumer) and the held buttons (DAT_0012ee14 / ee12), shift state, and anything else
these functions read. Provide the small feeding API the platform will call (`input_key_event(scancode,
down)`, `input_mouse_move(x, y)`, `input_mouse_button(button, down)`) written to do what the ISRs do
(`input_keyboard_isr_4fa28`, `mouse_event_callback_5b86c`: read them), and a header-only table of the
set-1 scancodes the game uses with their meaning (the integrator maps SDL scancodes onto it and
writes the controls help from it). Joystick (`input_joystick_poll_5a4e0`), the VFX1 headset, video
mode switches, sound / music toggles and the HUD button table (`ui_button_table_dispatch_17a80`) are
TODO(port) call sites; keep every game-state side effect (pause flag, sub-step count F3, option
bytes, on-screen messages through the PlayerRec message slots). The consumer of the packets is
`player_commands_process` in player.cpp (read it: it tells you which cmd / arg values exist), and
`movie/mvi00000.dat` (format in port_player.md, loader in `tests/player_test.cpp`) holds 34,201 real
packets recorded from a human player and three AI players: every packet of the human player must be
producible by your `player_local_input` from some device state - use that as the check (reconstruct
a device state per packet, feed it, compare the packet). ENGINE.md: "Input", "Player command
packet", agent_tick.md.
