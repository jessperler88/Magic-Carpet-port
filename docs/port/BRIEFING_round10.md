# Port round 10 briefing (2026-10-08): Phase 4 round 1 - debug and testing suite, mode skeleton

Read first: `docs/port/PORTING.md` (sources of truth, code conventions, **report format**),
`docs/ROADMAP_PHASE4.md` sections 2 ("Engineering decisions") and 3 / round 10, and
`docs/analysis/phase4_code_platform.md` sections 1-3 and 7 (where every hook point below comes from, with
line numbers). Then the files your task names. The engine is described in `docs/ENGINE.md`; earlier rounds'
reports are `docs/analysis/port_*.md` (`port_round8.md` / `port_round9.md` for the latest conventions).

## Why this round

The user's first requirement for Phase 4 (the "Conquest" RTS / RPG mode) is a **debug / testing suite usable
by the user on screen and by Claude headless**: start a game, drive it, observe it and assert on it, without
anybody playing. Every later round (big world, economy, units, bots, online) is verified with these tools.
This round also lays the plumbing every later round needs: the mode skeleton, a level from memory, movie
format v3, and per-part desync checksums.

## State at the start of round 10

- Phase 2 (the port) and Phase 3 (QOL: far view, native resolution, big Thing pool, interpolation, config,
  controller, save anywhere, pause menu) are done. **49 ctest tests pass** (Release, 2026-10-08), including the
  per-tick references against the original (`reference_test`, `reference_levels`, `reference_gen`,
  `reference_player*`), the pixel references (`render_reference*`, 33 front-end screens), `net_test`.
- Every Phase 3 change is a setting whose default is the original (settings.h `PortSettings g_settings`);
  game logic reads simulation-changing settings through `gameplay_rules()`, which movies / network sessions /
  save states override (`gameplay_force_rules`).

### Integrator scaffolding (already written and compiled; 49/49 still pass)

| what | where |
|---|---|
| `GameplayRules.mode` (0 = original, `GAME_MODE_CONQUEST` = 1); `gameplay_rules_faithful` also checks it | `settings.h`, `mc_globals.cpp` |
| **Mode block** `ModeState g_mode` (version, mode, `ModeParams` {seed, world_size, bots, humans, flags}, tick, `rng`, debug_flags, 8 x `ModePlayer` {flags}), `mode_begin / mode_end / mode_level_start / mode_tick`, `mode_rng_next`, `mode_checksum`, `mode_serialise / mode_deserialise`, `MODE_LEVEL_INDEX` (0x100), `MODE_PARAM_DEBUG`, `MODE_PLAYER_GOD` | `mcengine/mode.h/.cpp` (linked into every `mc_unit_test`, like mc_globals.cpp) |
| `net_state_checksum` hashes `g_mode` **only while a mode is active** (original checksum unchanged); `NetGameRules.mode` (was reserved[0]) agreed with the host's like the other rules | `net.h/.cpp` |
| Savestate `MODE` chunk (written while a mode runs; a state without it loads as the original game) and `RULE` chunk version 2 (+ u32 mode; version 1 still read); `MC_PORT_VERSION` 0x00040001 | `savegame.h/.cpp` |
| `sim_load_level_data(const LevelData &, int index)` (the level start from memory) and `g_hook_level_source(index, LevelData *)` asked first by `sim_load_level` (so `game_level_begin` / restart work for a generated level); `mode_level_start()` runs after `players_init_records` when a mode is active | `sim.h/.cpp` |
| `g_hook_port_command(player, Thing *, CmdPacket *)` for packet ids >= 0x20 (the original ignores them) in `player_commands_process`'s default branch; `g_hook_mode_tick()` after `thing_update_all`, before the sound hook | `player.h/.cpp` (`g_hook_mode_tick` is defined in mode.cpp) |
| Debug-command stub: ids 0x40..0x5f, `debug_cmd_allowed()` (mode run with `MODE_PARAM_DEBUG`, or any non-network game), `debug_cmd_apply` (does nothing yet), installed by `sim_register_gameplay` | `mcengine/debug_cmd.h/.cpp` |
| Mode level stub: `mode_build_level(game_dir, params, LevelData *)` = multiplayer map 50 with `gen.seed` = seed, `player_count` = humans + bots; `mode_start_run / mode_stop_run` | `mcengine/mode_level.h/.cpp` |
| `mcport <dir> rts [--seed S] [--size 256] [--bots B] [--debug / --no-debug]`: `rts_parse_args`, `rts_begin` (mode_start_run + `game_level_begin(MODE_LEVEL_INDEX)`), `rts_after_tick` (empty), `rts_end`; main.cpp `s_rts`, quits when the level ends; config.cpp passes unknown `--x` options through after `rts` | `mcport/rts_run.h/.cpp`, `mcport/main.cpp`, `mcport/config.cpp` |

Checked: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy MC_SAVE_DIR=. MC_TICK_HZ=1000 MC_QUIT_AFTER_TICKS=2000
MC_TICK_LOG=t.log mcport.exe <game> rts --seed 7 --bots 3` twice -> identical per-tick checksum logs.

## Rules (as in rounds 6-9, plus what is new)

- **Five agents (A..E) work in the same tree at the same time (no git).** Only create / modify the files your
  task names. Everything else is read-only; a change you need in someone else's file goes into your report as
  **exact code (file, place)** - the integrator applies it. If you are blocked on another task's API, write
  against the declared stub / your proposed signature and say so.
- Shared files nobody edits this round (integrator only): `src/CMakeLists.txt`, `engine.h/.cpp`, `sim.h/.cpp`,
  `sim_all.cpp`, `mc_globals.h/.cpp`, `mc_types.h`, `player.h`, `mode.h` **except task A**, `mcport/config.*`
  (new ini keys: give the `k_desc[]` lines in your report), `gen/*`, `docs/ROADMAP*.md`, `README.md`,
  `docs/port/PORTING.md`, `docs/ENGINE.md`. `settings.h`: add fields only in a new section headed with your
  task letter ("round 10 task X"). `player.cpp` / `thing.cpp`: only task D, only the profiling markers.
- `src/mcport/main.cpp` belongs to **task D**. Tasks A, B, C and E put their mcport glue in their own
  `mcport/<file>.cpp` and give D / the integrator the exact main.cpp calls in their report (where, what). D
  merges reports that arrive while D is still working; the integrator merges the rest.
- **Faithful by default, references are the gate.** With default settings and no mode active every reference
  must stay byte / pixel identical: run the references your files can affect (`reference_test`,
  `reference_levels`, `reference_gen`, `reference_player_test`, `render_reference_test`,
  `render_reference_hud`, `render_reference2_test`, `render_reference_options`, `render_reference_fe_test`,
  `net_test`) after your changes. Faithful code paths stay the translated code.
- **Determinism** (`phase4_code_platform.md` section 2): anything that changes the simulation is a **command
  packet** (recorded, replayed, lockstep-safe); read-only tools (overlays, inspector, dumps, the camera) run at
  draw time or between ticks and never write simulated state. Integers only in anything the tick runs; no
  `std::rand`; nothing in the tick reads the local player, the camera, settings or real time (except through
  `gameplay_rules()`). Draw code writes no game state (`render_frame_draw` rule, hud.cpp).
- Tests: your own `src/tests/<name>_test.cpp` + `.cmake` (exit 0 = pass, print `SKIP` and exit 0 without data
  that is not in the package). Prefer `mc_unit_test(name test.cpp <explicit sources>)` (it links mode.cpp and
  mc_globals.cpp itself); `mc_test` links the whole library including other agents' half-written files (retry
  when a build trips over one mid-edit; report it if it persists). Look at the screenshots you produce
  (`tools/port/ppm2png.py`, then read the PNG).
- Build in **your own build directory** `build_X` (they exist from earlier rounds; reconfigure):
  `cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_X`, then
  `cmake --build ../build_X --config Debug --target <your targets>` (Release for timings / long runs). **Zero
  warnings (/W4).** Never build in `build/` (the integrator's and the user's).
- Scratch: your own subdirectory `<scratchpad>/round10_X/` of
  `<scratchpad>`.
  Headless mcport runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy MC_SAVE_DIR=<your scratch>` (never the
  user's save directory).
- **Bash pitfall (bit the integrator twice while writing the scaffolding):** a `\n` inside a heredoc'd python
  script or a sed replacement becomes a real newline before the command runs, which breaks C string literals.
  Write sources with Write / Edit; run scripts from files. Run python tools from `tools/...`, never with
  `C:\Magic Carpet` as the working directory (the `ghidra/` folder shadows a package).
- The original game files under `MagicCarpet/` and the user's GOG install are never modified.
- GPL: do not copy code from DOSBox, remc2 / MC2-HD, mgcarpet or similar. Reading documentation is fine.

## Deliverable

Your sources, your tests (exit 0, zero warnings), and your report `docs/analysis/port_<name>.md` in the
PORTING.md format: what was built, verification (what, how, numbers), deviations / gaps, settings / ini keys
added (defaults, ranges), **requested shared-file changes (exact code, file and place)**, **main.cpp
integration (exact code)** unless you are D, the commands / grammar / file formats you defined (they become
the user's documentation), and what the next round should do. Your final message is a short summary.

## Shared contracts (agree on these; change them only through your report)

- **Debug packets** (task B defines the layout of each in `debug_cmd.h`): ids 0x40..0x5f, one per player per
  tick (`player_queue_command`: first command wins). Builders are plain functions in `debug_cmd.h`
  (`CmdPacket debug_cmd_teleport(x, y, z)`, `debug_cmd_spawn(cls, type, x, y)`, ...) that the console, the
  scenario runner, task C's cursor spawn and task D's teleport-to-camera call. Things are referenced by slot +
  `thing_slot_generation` (thing.h) where a packet names one.
- **Console results** that are not simulation (time control, screenshot, save / load, camera, overlays, run
  file): `console.h` returns them as a small command struct (like `GameMenuResult`) that main.cpp executes.
- **Projection** (task C): `render_project_world(...)` and the inverse `render_pick_ground(...)` in render.h, for
  the renderer in use (faithful or extended), using the camera of the last drawn frame.
- **Checksum parts** (task E): `net.h` `NetChecksumParts` with named parts (players, things per class, maps,
  cell lists, RNGs, pool stacks, mode block); `net_state_checksum()` keeps its exact current value (it is the
  fold of the parts in today's order, or computed as today alongside).
- **Movie v3 / the mode in movies** (task A): `demo_open` of a v3 movie starts the mode run (mode_start_run with
  the recorded `ModeParams`) like it forces the rules today, so a recorded `rts` run replays.
- **Log** (task D): `mclog(level, fmt, ...)` in `mcport/mclog.h` (mcport only; engine code keeps `printf` /
  `stderr`). Tasks A / B / C / E may call it from their mcport files.

## Tasks

### A - mode skeleton, headless `rts` runs, JSON dumps, movie format v3 (`mcengine/mode.{h,cpp}`, `mcengine/mode_level.{h,cpp}`, new `mcengine/mode_dump.{h,cpp}`, `mcengine/demo.{h,cpp}`, `mcport/rts_run.{h,cpp}`, settings.h section A, tests `rts_headless_test.*`, `movie_v3_test.*`, report `docs/analysis/port_mode.md`, build dir `build_A`)

- Own and finish the mode skeleton (you may change `mode.h` / `mode.cpp` / `mode_level.*` freely; keep the
  fields the others use: `g_mode.params.flags & MODE_PARAM_DEBUG`, `players[p].flags & MODE_PLAYER_GOD`, the
  serialise / checksum functions). Check that `MODE_LEVEL_INDEX` does not trip anything that keys on
  `Config.level` (music seed, texture / sky / palette choice, level stats, the `game_level_end` `level++`,
  `level_finish` restarts, savestate headers / `load_state_slot` in main.cpp - report what main.cpp must do to
  load an rts state). Make the rts level sensible for 256: template map chosen from the seed among the
  multiplayer maps (50..69) or one fixed map, `player_count` = humans + bots, every non-local player a computer
  wizard (`players_init_records` already does that outside network games - verify the AI wizards actually
  act), and seed the AI's `g_ai_rand_seed` from the match seed at the mode level start (it is outside
  GameState; say whether E should hash it).
- **JSON dumps**: `MC_DUMP_EVERY=n` (and `rts --dump-every n`, `--dump-dir D`, default the cwd; `--ticks N` as
  an alias of `MC_QUIT_AFTER_TICKS`) writes `dump_<tick>.json` every n ticks: tick, mode, seed, checksum (+ E's
  parts when they exist: use `net_state_checksum()` now and leave a clearly marked place), per player (active,
  is_computer, health / max, mana, castle level / health, Thing slot, position, AI mode `P.ai_mode`, kills if
  tracked), Thing census per class (and per class/type for creatures), pool use, mode block fields. The writer
  lives in mcengine (`mode_dump_json(std::string *)`, read-only, no SDL) so tests can call it. mcport prints
  **one line per dump** on stdout (`dump tick=1200 checksum=... players=...`), so a headless run is tail-able.
  Small hand-written JSON writer; no new dependencies. Optional `MC_DUMP_FINAL=1`: a dump when the run ends.
- **Movie format v3** (`demo.*`, `docs/analysis/port_settings.md` / `port_round8.md` describe v1 / v2): header
  carries mode id + `ModeParams` (+ the rules as v2 does); records become variable length so later rounds can
  carry order blobs (`{u16 len, bytes}` after the 10-byte packet block, or a per-tick record type - design it,
  document it, keep v1 / v2 playback unchanged); the `gax` snapshot carries the mode block. `demo_open` of a v3
  movie starts the mode run (`mode_start_run`) and the level comes from `g_hook_level_source`; demo_close stops
  it. The original's movies and the port's v1 / v2 movies must play exactly as before (`reference_test`,
  `reference_player_test`, `config_test rules_movie`).
- Tests: `rts_headless_test` (in-engine, no SDL: start an rts run with seed 1 and 3 bots, 2000 ticks, twice in
  the same process with a full reset in between, identical per-tick checksums; a JSON dump parses - write a
  tiny validator or check structure - and contains the expected players; a savestate round trip in the mode:
  save at T, run N, load, run N again, identical checksums including the mode block) and `movie_v3_test` (record
  an rts run of 500 ticks with a scripted local player, replay it, identical per-tick checksums; v1 / v2 still
  play). Also run `mcport rts` headless twice (Release) and compare `MC_TICK_LOG`s; report the ticks / second.

### B - console, scenario files, debug commands (`mcengine/debug_cmd.{h,cpp}`, new `mcengine/scenario.{h,cpp}`, new `mcport/console.{h,cpp}`, new `src/tests/scenarios/*.scn`, settings.h section B, tests `scenario_test.*`, `console_test.*`, report `docs/analysis/port_console.md`, build dir `build_B`)

- **Debug commands** (`debug_cmd.cpp`, applied in the tick through `g_hook_port_command`): teleport (x, y [, z];
  `thing_move_to` + `player_log_position`, phase4_code_platform.md section 7), spawn cls type [x y] (`thing_create`
  at a cell or in front of the wizard; names from `mc_class_name` / `mc_model_name`), give mana N (the wizard /
  the castle, reuse cheat 2's logic where it fits), god on/off (`MODE_PLAYER_GOD`: keep health / mana full each
  tick - in a mode run the bit lives in `g_mode`, in the campaign decide where the flag lives so that it is
  saved / replayed: report it), kill (slot / all creatures / a wizard), claim (a Thing for the player), heal,
  all spells (cheat 1), win / lose the level. Document each packet's byte layout in `debug_cmd.h`.
  `debug_cmd_allowed()`: keep or refine the rule (never silently in a network game).
- **Command parser** (`scenario.cpp`, engine side, no SDL): one line grammar shared by the console, scenario
  files and a stdin / pipe reader. Simulation commands become packets via the builders; platform commands
  (`time pause|step N|speed X|run`, `shot [name]`, `save N`, `load N`, `sync`, `dump`, `cam free|follow|to x y`,
  `overlay <name> on|off`, `inspect <slot>|cursor|off`, `run file.scn`, `quit`) become results for the host.
  Help text from the same table (`help`, `help <cmd>`).
- **Scenario files** (`*.scn`): the `reference_player_test` grammar (tests/reference_player_test.cpp header)
  extended: header `level L | rts seed S bots B | stop T`; timed lines `T <command>` and `T[-T2] ...` as there,
  plus `spawn / give / teleport / god / kill / wait N / assert_health <who> <op> <v> / assert_count cls [type]
  <op> <n> / assert_mana <who> <op> <v> / assert_alive / dump / shot`. Assertions read state directly between
  ticks; a failing assertion names the line. Runner API usable by both hosts: `scenario_load(file)`,
  `scenario_step(tick)` -> packets to queue / results / assertion failures.
- **Console** (`mcport/console.{h,cpp}`): opened by a port key (default backquote / F10; not a key the game
  reads - check input.h `g_input_bindings`), text line with cursor, history (up / down, kept in
  `<save dir>/console_history.txt`), scrollback of output lines, keys taken before the game sees them (main.cpp
  does that the same way as for the pause menu - give D the exact code), ASCII from scancodes + shift (no SDL
  text input needed), drawn after the HUD with `ui_draw_text` / `ui_shade_rect` in the 640-wide virtual
  screen. The level keeps running while it is open (the game's own keys are not fed). A **stdin reader** for
  headless runs (`MC_CONSOLE_STDIN=1` or `--console-stdin`: a thread reads lines, the main loop executes them
  between ticks) and `MC_SCENARIO=file` / `--scenario file` to run a scenario in mcport.
- Tests: `console_test` (parser: every command, errors, help; packets built byte-exactly; history file round
  trip; key-to-ASCII) and `scenario_test` (mc_unit_test over `${MC_SIM_ALL}`: runs every `src/tests/scenarios/
  *.scn` headless - at least `spawn_kill.scn` (spawn creatures, kill them, assert counts), `teleport.scn`,
  `god.scn` (wizard survives damage), `give_mana.scn`, one rts scenario; a deliberately failing assertion is
  reported with its line; the same scenario run twice gives identical checksums; a recorded movie of a
  scenario with debug packets replays identically).

### C - projection export, display pointer, Thing inspector, overlays (`render.h` (a new projection section only), `render_things.cpp` and `render_ext.cpp` (export of the projection / anchors only - drawing must stay pixel-identical), new `mcengine/debug_overlay.{h,cpp}`, `mcport/platform.h`, `mcport/platform_sdl.cpp`, `compose.{h,cpp}` (pointer mapping only), settings.h section C, tests `project_test.*`, `overlay_test.*`, report `docs/analysis/port_inspect.md`, build dir `build_C`)

- **Projection export**: `bool render_project_world(int32_t x, int32_t y, int32_t z, int *sx, int *sy, int
  *depth)` for the renderer that drew the last frame (faithful `thing_project` path with `g_rcam`, or the
  extended renderer's camera / focal at the display size), in the coordinates of the frame it drew (game frame
  or composed view) plus a helper to the 640-wide HUD space; a per-frame list of projected Thing anchors (slot,
  screen x / y, size, depth) collected by both renderers while drawing (render-only, cheap, off unless a
  consumer asked for it); the inverse `render_pick_ground(sx, sy, *cell_x, *cell_y)` (ray against the height
  field) and `render_pick_thing(sx, sy)` (nearest anchor). The faithful renderer's pixels must not change
  (`render_reference*` gates).
- **Display pointer**: today a level uses SDL relative mouse mode (mouse = steering). When a cursor tool is
  active (inspector / cursor spawn / later RTS), switch to absolute mode and keep the pointer at **display
  resolution** alongside the game's 640-space one (`compose_display_to_frame` / `window_to_drawable`); give
  D the main.cpp switch (cursor mode on a key, default Tab is taken: e.g. F9 or middle mouse; the game's
  steering is frozen while it is on - say how).
- **Thing inspector** (`debug_overlay.cpp`): pick by click (nearest anchor) or by slot; a panel with the
  `Thing` fields (mc_types.h, with names: class / model names, state, owner, health / max, mana, target,
  parent / child, flags), `PlayerBlock` for wizards (AI mode, goals), the `MoveDesc`, the cell list it is on;
  follow mode (the debug camera follows it - D's camera; give D the hook). Drawn into the 640-wide HUD layer
  with `ui_draw_text` / `ui_shade_rect`. Read-only.
- **Overlays** (toggleable, render-only, `debug_overlay_draw(...)` after the HUD): cell grid around the camera
  (projected cell corners), Thing labels (slot / class / state / AI mode at the projected anchor), spatial
  occupancy (cell-list lengths) on the map screen radar, recent damage events (if no hook exists, propose the
  `g_hook_damage_applied` sites for round 13 instead of adding them), network sync status (E's
  `net_sync_stats()`). A tiny overlay registry so later rounds add overlays by name (`overlay <name> on`).
- Tests: `project_test` (projection of known Things equals where the faithful renderer blitted them - the
  sprite probe `g_sprite_blit_probe` sees every faithful blit - and the extended renderer's anchors; ground pick
  round trip: project a cell centre, pick it back, same cell; both renderers, 320x200 / 640x480 / 1920x1080) and
  `overlay_test` (inspector panel and overlays rendered over level 38 into PPMs in your scratch dir - look at
  them; nothing in GameState changes while drawing: checksum before == after).

### D - main loop: time control, debug camera, screenshots, log, tick profiler, integration (`mcport/main.cpp`, new `mcport/mclog.{h,cpp}`, new `mcengine/tick_profile.{h,cpp}`, `hud.cpp` (the camera override seam only), `player.cpp` / `thing.cpp` (profiling markers only), settings.h section D, tests `timectl_test.*`, `mclog_test.*`, report `docs/analysis/port_timectl.md`, build dir `build_D`)

- **Time control** in the main loop (outside the simulation; `Config.substeps` stays the game's own F3):
  pause / resume, single step N ticks, fast-forward multiplier (x2 .. x64 or "as fast as possible" for
  headless), slow motion; works in `play`, `rts`, the viewer and movies; interpolation holds while paused;
  keys (port keys, not read by the game: propose defaults, e.g. Pause / F6 step / F7 slower / F8 faster - check
  `input.h` bindings and `config.h` keys) and the console's `time` results. Never in a network game (or only
  when all peers agree - not this round).
- **Debug camera**: a port-only `g_debug_camera` override (render-only) in `frame_pass` (hud.cpp, where it picks
  `g_render_interp.camera` / `player_camera`), driven by the viewer's `free_camera_step` keys while it is on,
  `cam follow <slot>` (C's inspector), `cam to x y`; teleport-the-wizard-to-the-camera via B's
  `debug_cmd_teleport` packet (write against the declared builder).
- **Screenshot on demand**: `shot [name]` (console result / a key) writes the next presented frame, composed
  output via `compose_to_rgb` when composing, as PNG if trivial or PPM (works with `SDL_VIDEODRIVER=dummy`);
  `MC_SHOT` keeps working.
- **Log**: `mcport/mclog.{h,cpp}`: levels (error / warn / info / debug), `<save dir>/mcport.log` (rotated:
  keep the previous run as `mcport.1.log`), mirrored to stdout / stderr as today, and a sink the console's
  scrollback reads. Convert main.cpp's `printf` / `notice` calls.
- **Tick profiler** (`tick_profile.{h,cpp}` + markers): per tick, the time of each phase of `game_tick_sim`
  (local input, commands, win check, thing_update_all, mode tick, sound, frame state) and per Thing class inside
  `thing_update_all`; a null pointer / disabled flag costs nothing; shown on the F11 line (extended) and in
  JSON dumps (A can include it); timing is never read by the simulation.
- **Integration**: you own main.cpp - merge the main.cpp code from A / B / C / E's reports when they arrive
  (ask nobody; read `docs/analysis/port_{mode,console,inspect,desync}.md` near the end of your work and merge
  what is there). Keep every mode working (`play`, `rts`, `load`, `demo`, viewer, front end, `network`) and the
  headless switches (`MC_SHOT`, `MC_TEST_INPUT`, `MC_TICK_LOG`, `MC_QUIT_AFTER_TICKS`).
- Tests: `timectl_test` (pause / step / fast-forward scheduling maths in a pure helper; a headless run with
  step / pause gives the same per-tick checksums as without), `mclog_test` (levels, rotation, sink). Report the
  key defaults so the user can learn them.

### E - desync tooling: checksum parts, dumps on mismatch, diff tool, replay check (`net.{h,cpp}` (the checksum / sync parts only), new `mcengine/replay_check.{h,cpp}`, new `tools/reference/diff_state.py`, new `tools/reference/tick_log_diff.py`, settings.h section E, tests `desync_test.*`, `replay_check_test.*`, report `docs/analysis/port_desync.md`, build dir `build_E`)

- **Checksum parts**: split `net_state_checksum` into named parts (`NetChecksumParts`: rng / g_rng16 / AI seed,
  pool stacks, things per class (16 classes), players (record head + P block, excluding the local-only fields
  exactly as today), maps type / height / light / flags, cell heads, pool extension, mode block). Keep
  `net_state_checksum()`'s value **exactly** as today (net_test and the stored tick logs compare it) - compute
  it as today and the parts alongside. `net_state_checksum_parts(NetChecksumParts *)` + names for printing.
- **Sync exchange with parts**: with `MC_NET_SYNC` the side channel still sends the 8-byte checksum; on the first
  mismatch the peers exchange the parts once (a second message, size-agnostic) so both name the first
  differing part, and each **dumps** its GameState + Config + maps + pool extension + mode block to
  `<save dir>/desync_<exchange>_p<player>.mcs` (reuse `savestate_save_file`, which writes all of that) - from a
  place where the lockstep is not broken by the extra message (design it; MemLan + TCP).
- **`tools/reference/diff_state.py`**: two state files (`.mcs` savestates / desync dumps, also raw GameState
  images) -> the first differing fields **by name** (GameState / Thing / PlayerRec / PlayerBlock / Config field
  tables: reuse the field table that `reference_test.cpp` uses for its diff, or generate one from `mc_types.h`
  offsets - say which), Things by slot with class / type, maps by cell. `tools/reference/tick_log_diff.py`: two
  `MC_TICK_LOG` files -> first differing tick (and with parts logged, the part).
  `MC_TICK_LOG_PARTS=1`: the tick log also writes the parts (give D the main.cpp line).
- **Replay check**: `replay_check(movie)` (mcengine): play a movie twice from its start (or a savestate +
  movie) and compare per-tick checksums and parts; `mcport <dir> --replay-check N` (give D the main.cpp code) /
  a test helper. Works for v1 / v2 movies now and for A's v3 when it lands (write against `demo_open`).
- Tests: `desync_test` (MemLan peers in one process - net.h - with one peer's state perturbed at a known tick
  in a known field: the mismatch is detected at that exchange, both name the right part, the dumps exist,
  `diff_state.py` names the field (run python from the test only if it is installed; else SKIP that part);
  unperturbed: no mismatch and checksums equal to today's values) and `replay_check_test` (movie 0 replays
  identically; a deliberately perturbed second pass is caught at the right tick).

## Gate for the round

`scenario_test`, `console_test`, `rts_headless_test` (seeded 256 run, 2000 ticks, two instances identical
checksums, JSON dumps parse), the other new tests, and all 49 existing gates identical with default settings.
On screen: `mcport <dir> rts --seed 1 --bots 3`, open the console, spawn / teleport / god, pause / step, free
camera, inspect a Thing, an overlay on, a screenshot - the user will try exactly that.
