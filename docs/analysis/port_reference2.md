# Per-tick references of campaign levels (task E, port round 5, 2026-10-07)

Round 4 compared the port with the original only on movie 0 (level 38 from tick 413, one human
player's packets, three AI wizards). This round adds **per-tick references of campaign levels made
without a human**: the original records a level from its first tick with an idle local player, then
plays the recording back while dumping the GameState every tick. The port plays the same recording
and is compared slot by slot, and the **level generation** of every level is compared on its own.

Result in one line: **the port is byte-identical to the original on every level reference** (8 levels,
5000 to 20000 ticks each, AI wizards, every creature type of the list, full Thing pool on level 49),
and the level generation of all 69 levels the original can start is identical after two fixes
(terrain rivers, harness flags); a third fix makes the snapshot loader accept a full Thing pool.

## Files

| file | what |
|---|---|
| `tools/reference/patch_carpet.py` | new `--mode record` (record cave, `-roll N` = flags \|= 0x100), `--stop-tick`; fixup-overlap check corrected (`CAVE - 3 <= f < CAVE + len`); play mode unchanged (the movie-0 exe `carpet.exe` is produced exactly as before) |
| `tools/reference/run_reference.py` | new `--exe`, `--roll`, `--stop-tick`, `--no-movie-check`; dumps numbered >= 20000 (the level recordings' snapshot pairs) are never treated as stale dumps |
| `tools/reference/run_level.py` (new) | `python run_level.py L [L ...] [--stop 5000] [--every-until 1000] [--stride 10] [--gen-only]`: record run + play run per level, output `extracted/reference/levelNN/` (or `gen/levelNN/`) |
| `src/tests/reference_test.cpp` | level mode (index.json names a level), level-generation check, suite mode (a directory of levelNN/), map differences now count as divergence, `sim_load_level(38)` before movie 0 (task F's g_rng16 request), Config flags 0x100 as in the reference runs |
| `src/tests/reference_test.cmake` | now `mc_unit_test` with an explicit source list (`${MC_SIM_ALL}` + hud / ui_draw / raster / tables / render_landscape / render_things / sprite_cache) instead of the whole mcengine; new ctest entries `reference_levels`, `reference_gen` |
| `extracted/reference/levelNN/` | `movie/{mvi,gam,map}2NNNN.dat` (the recording), `tick%05d.gam` (play run: every tick 2..1000, then every 10th / 25th), `tick%05d.map` every 250 ticks, `tick00001.gam` (record run, before the recording), `index.json`, `rec_tick*.gam` (record-run dumps that differ from the play run, see below) |
| `extracted/reference/gen/levelNN/` | generation-only references (tick00001.gam + the tick-2 snapshot pair), levels 0..69 except 17, 58 MB |

## How the original runs a level without a human

**Record cave** (`build_record_cave`, 135 of the 136 bytes of the dead `dead_castle_site_search_11cef`,
called instead of `sound_update_494b0` at 0x32f11 like the play cave):
- tick 1: `demo_save_state_3c2c0(1)` -> `gam00001.dat`, the generated level after its first tick;
- not recording, tick < STOP and `players[0].thing != 0`: `Config.flags |= 2` - the record bit the
  in-game command 0xc sets. The next tick's first packet opens `movie/mvi%05d.dat` (Config.movie) and
  saves the snapshot pair (`demo_record_playback_step_3c540`), so recordings start at tick 2;
- recording: dump every STRIDE ticks (progress and the record-vs-play cross-check), and at tick STOP
  call `demo_close_3c7c0` so DOS flushes the file. Killing DOSBox with the file open loses the buffered
  tail (a first attempt lost ~1.5 KB of 42 KB).
- `-roll N` in record mode only sets `Config.movie = N - 1` and flags |= 0x100 (skip the front end).

Why `players[0].thing != 0`: `demo_relink_state_pointers_3dc10` rebases every `Thing.desc` relative to
`things[players[0].thing].desc`. A snapshot taken before player 0's Thing existed (first attempt:
record bit at parse time, snapshot at tick 1 before the join packet spawned anyone) crashes the
playback with a divide by zero in the townie update (`desc->[0x1a]`, 0x1e2fc). In practice player 0
exists after tick 1 on every level.

Movie numbers 20000 + level keep the recordings' `gam`/`map` names clear of the tick dumps. The local
player is a human flyer without input (DOSBox with the dummy SDL drivers delivers no events): it
hovers at its start, gets attacked, dies and respawns; the AI wizards, creatures, castles and spells
run normally. **Level-start randomness: there is none** - `terrain_build_303f0` seeds `g_rng16` and
`GameState.rng` from the level header, and the port's `sim_load_level` + one tick reproduces the
original's tick-1 state byte for byte (RNG included) on all 69 levels. The snapshot route is still
used for the playbacks (it is what the original's play run does).

**Record vs play.** The record run's dumps (every 500th tick) were compared byte for byte with the
play run's: identical except, on some levels at some ticks (16: 500/1500/2000, 24: 1000/1500/2000, 49:
500/1000, 0: 1500), the single byte `PlayerBlock.hit_flash` (P+0x188) of the local player: 0..3 while
recording, always 4 in playback. A render / palette-side difference between the two modes, not
simulation; the port is compared with the play run, which is what it reproduces.

**Briefing question "demo level" in the attract mode**: `cfg.flags |= 0x24` is movie playback (4)
with recording blocked (0x20); `cfg+0xa1 = 3, cfg+0xa2 = 200` is `Config.credits_state`: the
scrolling credits of `render_frame_1fab0` (state 3 waits 200 frames, 2 resets, 1 scrolls the text).
The recorder sets exactly the same bytes when a recording starts. There is no computer-controlled
local player.

**Level 17** cannot be referenced: the original itself dies at tick 1-2 under `carprec -roll .. -level
17` (divide by zero in `ui_draw_status_bars_219f0` at 0x22328, `health * 64 / max_health` of a Thing
with max_health 0) - an original bug of starting that level without the front end, run_level.py
writes `gen/level17/FAILED.txt` and skips it.

Timings: DOSBox runs ~500 ticks/s headless; a 20000-tick level is ~45 s recording + ~60 s playback +
20 s idle timeout; a gen-only level ~10 s. `reference_test` on a 20000-tick level: ~1.5 s.

## Port side

`reference_test <game> <dir>`:
- **movie mode** (no "level" in index.json): unchanged comparison; now generates level 38 first, as
  `carpet -roll 1 -level 38` does (task F found that this leaves `g_rng16` = 0x2fea, which the
  run-time retexturing draws from; it is in neither the GameState nor the map dump).
- **level mode**: Config flags 0x100, `sim_prepare_movie()` (320x200 - the original's default
  `DAT_0012edae = 1`, French notices as in any playback), the Config / GameState resets of
  `level_load_file_3d160` (see requested changes), `sim_load_level(L)`, `demo_open(<dir>, 20000 + L)`,
  `demo_step` per tick, comparison at every dumped tick (Things, players, RNG, live count, maps every
  250 ticks). Then the **level-generation check**: resets, `sim_load_level(L)`, one `game_tick_sim()`
  against `tick00001.gam`, and the generated maps against the recording's tick-2 map file (terrain does
  not change in tick 1). `MC_REF_GEN_TRACE=1` lists every differing field, `MC_REF_GEN_DUMP=file`
  writes the port's four maps. Summary line `LEVEL L: first divergent tick T (0 = none) over N ticks;
  level generation identical|differs`; exit 1 on any divergence.
- **suite mode** (a directory of levelNN/): runs itself per level and prints the summary lines.
- ctest: `reference_test` (movie 0, unchanged gate), `reference_levels` (extracted/reference),
  `reference_gen` (extracted/reference/gen); all SKIP / pass without the dumps.

## Results per level

First divergent tick before -> after this round's fixes (0 = none; "before" = the port as it was at
the start of the round, with the harness corrections applied).

| level | content | ticks compared (to) | playback before -> after | generation before -> after |
|---|---|---|---|---|
| 0 | 1 player, townies / traders / builder | 1399 (5000) | 0 -> 0 | identical |
| 1 | 1 player, vultures / worms / dragons | 1399 (5000) | 0 -> 0 | identical |
| 12 | griffons, 2 AI wizards | 1758 (19975) | 0 -> 0 | identical |
| 16 | genie, type 15, 4 AI wizards | 1758 (19975) | 0 -> 0 | identical |
| 24 | trolls, wyverns, 4 AI wizards | 1758 (19975) | 0 -> 0 | identical |
| 38 | the movie level from tick 1, 3 AI wizards | 1758 (19975) | 0 -> 0 | identical |
| 44 | kraken, crab, emu, type 15, wyvern, 3 AI wizards | 1758 (19975) | 0 -> 0 | **differs (terrain) -> identical** |
| 49 | 5 genies, 7 AI wizards, **full Thing pool from tick 2** | 1758 (19975) | unloadable -> 0 | identical |
| gen 0..69 (not 17) | level generation + tick 1 only | - | - | 44 differed -> all 69 identical |

Census at tick 19975 shows the AI at work (e.g. level 16: players 0 and 2 dead, 12 class-3 Things,
genie and 14 type-15 creatures alive; level 49 keeps the pool full). The kraken / crab / emu / type 15
/ wyvern (44), troll (24), griffon (12) and genie (16, 49) handlers, the AI wizard on 6 levels and the
castles of other wizards now have ground truth and match it. No handler was dispatched without a port.

## Fixes

1. **`terrain_carve_rivers` (terrain_gen.cpp, `terrain_carve_rivers_31430`)**: the 1000-try counter
   (`mov ecx, 0x3e8` at 0x31461) is reset for every river - it sits inside the river loop. The port
   shared one counter between all rivers, so on level 44 (39 rivers above height 132) it stopped
   carving early: 1258 height / 1486 class cells in two river clusters, and through the shifted RNG
   stream the light and flag (texture rotation) maps almost everywhere. Only level 44 of 69 is
   affected. Evidence: `level generation maps vs the tick-2 snapshot` 1258 height cells -> 0.
2. **`thing_relink_snapshot` (thing.cpp)**: derived the original's Thing-pool base only from the free
   stack and refused a snapshot with a full pool (`free_top == -1`). New fallback: player 0's
   `Thing.player` (points at GameState + 0x340b + 0x44f). Level 49's dumps (pool full from tick 2) and 3
   dumps of movie 0 that were silently skipped before now load (movie 0 compares 1396 instead of 1393
   ticks, still identical). The port's `demo_load_state` uses the same function, so such snapshots now
   load in the game as well.
3. Harness, not game logic (found by the generation check, all in reference_test.cpp):
   - Config flags 0x100: the reference runs start through `-roll`, which sets 0x100 (front end skipped)
     and that disables `game_check_level_won_3db20` (`test flags, 0x110`). With flags 0 the port
     counted level-15 player 1's `win_timer` up. Note: the comment "not in network games and movies"
     on `game_check_level_won` is not accurate - the attract-mode movies run with flags 0x24 and do
     check the win; 0x100 is the "custom" / skip-front-end bit.
   - The Config resets of `level_load_file_3d160` before `sim_load_level` (stale `Config.player_list`
     from a previous level made `player_spawn` give player 0 threat entries 0x9fdf on level 44).
   - `sim_load_level(38)` before movie 0 (task F).

## Requested shared-file changes

1. **`src/mcengine/sim.cpp`, `sim_load_level`**: do the Config / GameState part of
   `level_load_file_3d160` that it skips (0x3d160: mem_set of GameState+0x244 (1), Config+0x17 (1),
   +0x5d (0x10), +0x96, +0x98, +0xb8 (0xe), +0x8e1a (4), +0x8e1e..+0x8e7d (the creature lists and the
   player / mana-ball / wizard / projectile list heads), Config dword +0 &= 0xfffe3fff). Task D's
   `game.cpp level_reset_config()` is exactly this; either call it from `sim_load_level` (move it into
   sim.cpp) or put this at the top of the "per-level reset" block of `sim_load_level`:
   ```cpp
   {   // level_load_file_3d160: the Config part (lists rebuilt per tick; stale heads reach player_spawn)
       reinterpret_cast<uint8_t *>(g_state)[0x244] = 0;
       uint8_t *cf = reinterpret_cast<uint8_t *>(g_cfg);
       g_cfg->fade_stage = 0;
       std::memset(cf + 0x5d, 0, 0x10);
       g_cfg->substeps = 0;
       g_cfg->palette_effect = 0;
       std::memset(cf + 0xb8, 0, 0xe);
       std::memset(cf + 0x8e1a, 0, 4);
       std::memset(g_cfg->creature_lists, 0, sizeof g_cfg->creature_lists);
       g_cfg->player_list = g_cfg->mana_ball_list = g_cfg->wizard_list = g_cfg->projectile_list = 0;
       g_cfg->flags &= 0x3fff;
       g_cfg->paused &= ~1;
   }
   ```
   Without it a second level in one process (level-to-level flow without task D's wrapper, or a test)
   starts with the previous level's list heads. reference_test has a local copy
   (`level_reset_config_local`) until then.
2. None else. `src/CMakeLists.txt` is untouched (the new ctest entries live in `tests/reference_test.cmake`).

## Corrections to ENGINE.md / names

- `terrain_carve_rivers_31430`: at most 999 candidate cells **per river** (counter reset per river);
  running out ends the whole pass.
- `game_check_level_won_3db20` returns when `Config.flags & 0x110` (network 0x10, front end skipped /
  "custom" / `-roll` 0x100) - not "movies".
- `Config.credits_state` (+0xa1..+0xa7): the credits scroller of `render_frame_1fab0`; the attract
  "demo level" is movie playback with the credits, the recorder starts the same scroller.
- `demo_relink_state_pointers_3dc10` needs player 0's Thing to exist in the saved state (all `desc`
  pointers are rebased relative to it): a recording started before the first spawn cannot be played.
- `-level 17` without the front end crashes the retail exe in `ui_draw_status_bars_219f0` (0x22328).
- `PlayerBlock.hit_flash` (P+0x188) differs between recording and playback of the same game
  (render-side), the only such byte seen in 8 levels.

## Not done / caveats

- The local player is idle in every level reference; the human flyer's controls and spells are still
  verified only by movie 0. A scripted local player (packets written by a cave) would close that gap.
- 20000 ticks per level, strided every 25 ticks after tick 1000; a divergence found later needs a dense
  re-run (`run_level.py L --every-until <T>`).
- Disk: 8 level references ~3.2 GB, gen 58 MB (155 GB free).
- Builds: `reference_test` builds with zero warnings in build_E; no build trouble from other agents'
  files (the explicit source list avoids frontend / game / fli).
