# Port round 10, task A: mode skeleton, headless `rts` runs, JSON dumps, movie format v3

Phase 4, round 10 (`docs/port/BRIEFING_round10.md`, task A). Nothing here exists in carpet.exe; with no mode
running every code path is the one of round 9 (all references identical, see Verification).

## What was built

| file | what |
|---|---|
| `mcengine/mode.h/.cpp` | `ModeParams.map` (template level, 0 = from the seed; was `reserved[0]`), `MODE_PARAM_RESEED`; `ModeState.template_level` and `.ai_seed` (were reserved words; sizes unchanged, `MODE_STATE_VERSION` still 1); `g_hook_mode_level_start` (end of `mode_level_start`); `g_hook_mode_checksum_globals` (end of `mode_checksum`) |
| `mcengine/mode_level.h/.cpp` | the mode level (template choice, `mode_build_level`), the level-start work, `mode_level_register()` (installs the mode / movie hooks), `mode_start_run / mode_stop_run / mode_run_active`, `mode_start_run_for_state(game_dir, state_path)` |
| `mcengine/mode_dump.h/.cpp` (new) | `mode_dump_json(std::string *, ticks_run, extra_members)`, `mode_dump_line`, `mode_dump_write`: read-only JSON writer, no SDL, no dependencies |
| `mcengine/demo.h/.cpp` | movie format v3 (mvx version 3: mode header, variable-length records, mode block + globals in gax), `demo_version()`, hooks `g_hook_demo_mode_start / _stop`, `g_hook_demo_sim_globals_get / _set`, `g_hook_demo_order_out / _in` |
| `mcport/rts_run.h/.cpp` | `rts` options (`--map`, `--reseed`, `--ticks`, `--dump-every`, `--dump-dir`, `--dump-final`), dumps of any run (`dump_configure_from_env`, `dump_after_tick`, `dump_final`, `dump_now`, `dump_set_extra`), `rts_quit_after_ticks()` |
| `tests/rts_headless_test.*`, `tests/movie_v3_test.*` | the two tests (below) |

No `settings.h` section A and no ini keys: everything is a command-line option or an environment switch.

### The mode level (256 x 256 until round 11)

- **Template**: `ModeParams.map` when 50..69, else one of the multiplayer maps 50..69 with enough player slots
  (level footer `player_count >= humans + bots`; every map has 8 except 51 with 5), picked by
  `mix(seed) % candidates` (a splitmix-style integer finaliser) - `mode_template_level()`. Seed 1 -> map 62,
  seed 3 -> 50, seed 7 -> 68.
- **Terrain**: the template's own `GenMap` (its castle sites, villages and creatures were placed for that
  terrain - a re-seeded terrain leaves castles in the sea). `--reseed` (`MODE_PARAM_RESEED`) replaces
  `gen.seed` by `seed & 0xffff` for variety.
- `player_count = humans + bots` (1..8). Records `0..humans-1` are humans (offline: record 0 = local), the rest
  computer wizards - `players_init_records` already makes every non-local record `is_computer = 1` outside a
  network game.
- **Level start** (`g_hook_mode_level_start`, after `players_init_records`, before the first spawn):
  `g_ai_rand_seed = mix(seed ^ 0x5eeda1)` (also stored in `g_mode.ai_seed`), `g_ai_human_wizard = 0`,
  every record's campaign `spell_found` cleared, human records get the level's start spells (player block
  `+0x10` allowed by `+0x74`) exactly as `players_init_records` does in a network game (the campaign branch would
  give a human only spells found earlier in the campaign: none), and the recyclable stack's entries above its
  top are zeroed. The last one matters for determinism inside one process: the level reset does not clear
  `active_list[active_top+1..]` (original behaviour), nothing reads those entries, but `net_state_checksum`
  hashes the whole stack, so the first run of a process and a later one had different checksums from tick 0
  (found by `rts_headless_test`; the campaign has the same property - see "Gaps").
- **The AI wizards act** (rts_headless_test, seed 1, 3 bots, 2000 ticks): all 3 build a castle, change AI mode
  (`P.ai_mode` 13 -> 6 / 11 / 1), move, collect mana (5500 / 2500 at tick 1000), fight (health drops, one dies
  and respawns at its castle). The headless human (no input) is killed around tick 900 and, without a castle
  and without a respawn request, stays dead; the level does not end.
- `mode_checksum` (the mode part of `net_state_checksum`) now also hashes, through
  `g_hook_mode_checksum_globals`, the globals outside GameState a mode run depends on: `g_terrain_nearly_flat`,
  `g_ai_rand_seed`, `g_ai_human_wizard` (the same values a v3 snapshot carries). **Answer to "should E hash
  the AI seed": it is hashed in a mode run already, as part of the mode block; E's `ai_seed` part may hash it
  again, but the value of `net_state_checksum()` while a mode runs includes it through `mode_checksum`.** The
  original's checksum (no mode) is unchanged.

### `MODE_LEVEL_INDEX` (0x100) audit - what keys on `Config.level`

| place | effect with 0x100 |
|---|---|
| `game.cpp` music seed (`s_music_seed = (int16)level`) | fine (an LCG seed) |
| texture / sky / palette choice | nothing keys on the level index (they come from GameState / the data files) |
| `game_level_end` `Config.level++` on a win | 0x101 - harmless because mcport quits when an rts level ends (`s_rts`); round 12's result screen must not use it for a mode |
| `level_finish` (restart after a loss) | `load_level(0x100)` -> `sim_load_level` -> `g_hook_level_source` -> the same level (tested) |
| `game_level_skip_number` | 0x100 is not in its list |
| `hud.cpp` debug line, window title, `game_menu.cpp` slot label (`level %d` with `si.level + 1`) | cosmetic: shows 256 / 257 - suggestion for D below |
| savestate header `level` | 0x100; loading needs the mode run first (main.cpp code below; `mode_start_run_for_state`) |
| `savegame_capture` (CAMP chunk checksum = (level + ...) * 4) | harmless |
| `frontend.cpp` level names / lobby | never reached in an rts run |

## JSON dumps

`MC_DUMP_EVERY=n` / `rts --dump-every n` writes `dump_<ticks_run>.json` every n simulation ticks into
`MC_DUMP_DIR` / `--dump-dir D` (default: the current directory, created if missing); `MC_DUMP_FINAL=1` /
`--dump-final` adds `dump_final.json` when the run ends (skipped when the periodic dump of that tick exists).
Every dump prints one line on stdout (through `mclog`, flushed):

```
dump tick=1000 run=1000 checksum=0x52c8cbdc mode=conquest seed=1 things=812 players=4 p0=hu:h-2010/10000,m1000,c-1,ai0 p1=ai:h10000/10000,m1000,c1,ai6 p2=ai:h7450/10000,m5500,c1,ai6 p3=ai:h10000/10000,m2500,c1,ai6
```
(`p<n>=hu|ai:h<health>/<max>,m<mana_total>,c<castle level, -1 none>,ai<P.ai_mode>`). The document
(`mode_dump.h` has the field list): `format "mcport-dump"`, `version 1`, `tick` (PlayerRec.tick), `ticks_run`,
`level`, `mode`, `mode_id`, `seed`, `checksum` ("0x%08x"), `checksum_parts` (**null - the marked place for task
E's `NetChecksumParts`**, `mode_dump.cpp` TODO), `rules {possession_range_pct, mode}`, `mode_block {version,
mode, tick, rng, debug_flags, template_level, ai_seed, params {...}, player_flags[8]}`, `pool {slots, live,
free_top, active_top, alloc_failures}`, `world_mana`, `players[player_count] {index, active, is_computer, local,
name, status, thing, alive, state, health, max_health, mana, wizard_mana, x, y, z, ai_mode, kills,
kills_of_player[8], shots, hits, spells, mode_flags, castle null | {thing, level, health, max_health, mana}}`,
`census {total, by_class, creatures (by name), player_things (flyer / ai_wizard / castle / balloon), effects (by
type)}`, then the host's extra members (`dump_set_extra`, for D's tick profile). ~2.4 KB. Dumps work in any run
once main.cpp calls `dump_after_tick` (below); in `rts` they work now through `rts_after_tick`.

`rts --ticks N` = `MC_QUIT_AFTER_TICKS=N` (needs the one main.cpp line below).

## Movie format v3

`demo.h` documents it; summary (all little endian):

```
mvx%05d.dat  DemoExtHeader {"MCPX", version 3, thing_slots (>= 1000), flags 0}           16 bytes
             DemoExtRules  {possession_range_pct, mode, reserved[2]}                        16 bytes
             DemoModeHeader{mode, ModeParams (32 bytes), record_format = 1, reserved[2]}    48 bytes
             records, one per player per tick in player order (as v1 / v2):
               CmdPacket (10 bytes) + u16 blob_len + blob_len bytes (the player's order blob, 0 = none)
gax%05d.dat  GameState (port form) + u32 slots + Things 1000..slots-1                        (as v1 / v2)
             + u32 mode_len + mode_serialise() (u32 version + ModeState)
             + u32 n (= 6) + n x u32: g_rng16, g_terrain_nearly_flat, g_ai_rand_seed, g_ai_human_wizard, 0, 0
max%05d.dat  as map%05d.dat
```

- Recording writes v3 whenever a mode runs at the first recorded packet (`mode_active()`), else v1 / v2 exactly
  as before (byte-identical files; `config_test` checks the sizes).
- Order blobs: `g_hook_demo_order_out(player, buf, cap)` is asked for every packet written (null = 0 bytes);
  playback hands every record's blob to `g_hook_demo_order_in(player, data, len)` (null = skipped). Up to 65535
  bytes per player per tick. Round 15's order stream plugs in here.
- `demo_open` of a v3 movie: checks the three headers (mode known, `rules.mode == mode`, record format 1), calls
  `g_hook_demo_mode_start(game_dir, mode, params)` (= `mode_start_run`, refused cleanly when the hook is not
  installed), forces the recorded rules (possession + mode) as for v2. The level comes from the gax snapshot (like
  every movie); `g_hook_level_source` is installed for anything that reloads the level. The snapshot load
  restores the mode block (`mode_deserialise`) and the globals. `demo_close` releases the rules and stops the mode
  run (`g_hook_demo_mode_stop`). `demo_packets_read / _total` count records for v3 (the file is scanned once).
- v1 / v2 / the original's mvi movies take exactly the old code paths (the v3 branches test `s_version == 3`).

## Verification

- **rts_headless_test** (mc_unit_test over `${MC_SIM_ALL}` + savegame, no SDL; Release 3.1 s, all passed):
  run 1 = seed 1, 3 bots, 2000 ticks (template 62, 4 players, record 0 human, 1..3 computer); a campaign level
  (2) played 300 ticks in between with `spell_found` and `g_ai_rand_seed` disturbed; run 2 identical
  `net_state_checksum` every tick, identical start checksum. The dump at tick 1000 parses with the test's own
  JSON parser (strict: no trailing garbage, no duplicate keys) and has the expected fields / values (4 players,
  is_computer 0/1/1/1, census total = pool live, 3 ai_wizards, castles), checksum before == after the dump.
  Savestate at tick 600, 400 ticks, load, 400 ticks: identical (and equal to run 1's ticks 601..1000), mode
  block equal after the load; the same state loaded through `mode_start_run_for_state` from a campaign level:
  identical; a campaign state loaded during a mode run ends the mode. Restart (`sim_load_level(0x100)` again) =
  the start checksum; flipping `g_ai_rand_seed` changes the mode run's checksum; seed 7 / bots 2 / `map 57 +
  reseed` build what they should; without a mode, index 0x100 is no level. **Simulation speed: 1760 ticks/s
  (Release, 2000 ticks, map 62, no rendering).**
- **movie_v3_test** (all passed, 0.8 s): an rts run (seed 1, 3 bots, possession 120 %) recorded 500 ticks with
  a scripted local player (weaving, speed, both hands cast, book open / close) and 72 order blobs (1..37 bytes,
  1345 bytes total): file size = headers + records + blobs exactly, headers as written; replayed after loading
  campaign level 5, 50 ticks run and `g_ai_rand_seed` overwritten, with the settings back at 100 %: the mode run
  starts from the header, the rules are the recorded ones, **500 ticks identical `net_state_checksum` (mode
  block + AI globals included)**, every blob back with its packet and tick, the AI seed restored, the mode
  stopped and the rules released at the end; without the mode hook the movie is refused. v1 (pool 3000) and v2
  (possession 150 %) recordings of level 2: same file sizes as before, 300 ticks identical (GameState image);
  the original's movie 0 opens as format 0 with faithful rules, plays 100 ticks.
- **mcport headless** (Release, `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy MC_SAVE_DIR=<scratch>
  MC_TICK_HZ=1000 MC_QUIT_AFTER_TICKS=2000 MC_TICK_LOG=..`, `rts --seed 1 --bots 3`) twice: the 2000 tick lines
  identical (`1 fc2ae076 ... 1000 82da6016 ... 2000 c0974966`; the line after them is D's frame statistics);
  with `--dump-every 1000` the dumps of both runs are byte-identical, and a run with dumps has the same tick log
  as one without. Wall time 8.4 s for 2000 ticks including start-up, the same at `MC_TICK_HZ=100000`
  (~300 ticks/s: the main loop renders a frame every 4 ticks with the playing defaults - extended renderer,
  compose; D's fast-forward is the way to go faster).
- **Gates** (Release, build_A): `reference_test` (0 divergences over 1396 ticks), `reference_levels`,
  `reference_gen`, `reference_player`, `reference_player_test`, `config_test` (ext_movie, rules_movie,
  rules_state), `sim_test`, `player_test`, `net_test`, `game_test`, `pool_test` - all passed. Zero warnings
  (Debug and Release) in every file of this task.

## Gaps / deviations

- `checksum_parts` in the dump is `null` until E's `net_state_checksum_parts` exists (marked TODO in
  `mode_dump.cpp`).
- The human of a headless run does nothing (no scenario / console yet - B); it dies and stays dead. Bots never
  respawn a missing human. The level is not "won" by anyone: there is no mode win rule yet (round 12).
- `game_level_end` would advance `Config.level` to 0x101 on a "win" (the original's single-player win check
  still runs for the local player in a mode run); harmless while mcport quits at the level end.
- The stale recyclable-stack entries (`active_list` above `active_top`) also make the campaign's
  `net_state_checksum` depend on what the process played before (two loads of the same level in one process can
  differ from tick 0 although they play identically). Only the mode clears them (faithful paths untouched); E's
  replay check / diff tools should know it (or E's checksum parts could hash only `active_list[0..active_top]` -
  that would change the original's checksum value, so only as a separate part).
- Rendering-bound tick rate of mcport (above); the simulation itself does ~1760 ticks/s on map 62.

## Requested shared-file changes (integrator)

1. `src/mcengine/sim_all.cpp`, `sim_register_gameplay()`: register the mode hooks for every program and test
   that links the whole simulation (so `mcport demo N` / E's replay check can play a v3 movie without an rts
   run having been started first):

   ```cpp
   #include "mode_level.h"
   ...
   void sim_register_gameplay() {
       ...                             // existing registrations
       mode_level_register();          // round 10 task A: mode level start, movie v3, mode checksum globals
   }
   ```
2. `src/CMakeLists.txt`: add the two files to `MC_SIM_ALL` (needed by 1; `mode_level.cpp` uses `ai_wizard`,
   `demo`, `sim`, mcdata's `level.h` - all in `MC_SIM_ALL` / linked):

   ```cmake
   set(MC_SIM_ALL ${MC_SIM_CORE} mcengine/sim_all.cpp ... mcengine/debug_cmd.cpp
       mcengine/mode_level.cpp mcengine/mode_dump.cpp)
   ```
   then drop `mcengine/mode_level.cpp mcengine/mode_dump.cpp` from `tests/rts_headless_test.cmake` and
   `tests/movie_v3_test.cmake` (they list them explicitly today).
3. Task E: `mode_checksum` (net.cpp's mode part) now includes the AI / terrain globals through
   `g_hook_mode_checksum_globals`; `net_state_checksum()` outside a mode is unchanged. The mode part of
   `NetChecksumParts` = `mode_checksum(FNV basis)`.

## main.cpp integration (task D)

1. `--ticks N` - right after `rts_parse_args(...)` succeeded (main.cpp `} else if (rts) {` branch):

   ```cpp
   if (rts_quit_after_ticks() > 0) s_quit_after_ticks = rts_quit_after_ticks();
   ```
2. Dumps in every run (not only rts) - at start-up next to the other env switches (`MC_TICK_LOG` /
   `MC_QUIT_AFTER_TICKS`), and in `sim_tick` instead of the current `if (s_rts) rts_after_tick(...)` line; at
   the end next to `if (s_rts) rts_end();`:

   ```cpp
   dump_configure_from_env();                                  // MC_DUMP_EVERY / MC_DUMP_DIR / MC_DUMP_FINAL
   ...
   if (s_rts) rts_after_tick(s_ticks_run); else dump_after_tick(s_ticks_run);   // sim_tick
   ...
   if (s_rts) rts_end(); else dump_final(s_ticks_run);         // end of main
   ```
   The console's `dump` command (task B's result): `const std::string f = dump_now(s_ticks_run);` (prints the line
   itself). D's tick profile in dumps: `dump_set_extra([] { return std::string("\"profile\": ") + <json>; });`.
3. Loading an rts state (`load_state_slot`): the state's level is `MODE_LEVEL_INDEX`; its mode run must exist
   before `game_level_begin`, and a campaign state loaded during an rts run leaves the run. Insert after
   `if (lv < 0) { ... }` and **before** `const bool started = ...`:

   ```cpp
   if (lv == MODE_LEVEL_INDEX) {                               // a game-mode state (rts): start its run first
       char path[1024];
       if (!savestate_slot_path(slot, path, sizeof path) || !mode_start_run_for_state(s_game.c_str(), path)) {
           notice("slot %d: the game mode of this state cannot be started", slot);
           return false;
       }
       s_rts = true;
       if (in_level) *level = -1;   // an rts level runs: begin the state's level anyway (its map / seed may differ)
   } else if (s_rts) {                                         // a campaign state ends the mode run
       rts_end();
       s_rts = false;
   }
   ```
   (`*level = -1` makes the existing `else if (lv != *level)` branch call `game_level_begin(lv)`, which builds the
   state's template level and its textures before `savestate_load` replaces the state.) `mcport <dir> load N` of an
   rts state then works through the same function (the `!in_level` branch). Includes: `"mode_level.h"`
   (`"savegame.h"`, `"rts_run.h"` are there).
4. Cosmetic: the pause menu's slot label (`game_menu.cpp:199`, `level %d` with `si.level + 1`) and the window
   title print 257 / 256 for an rts state / run; e.g. `si.level == MODE_LEVEL_INDEX ? "rts" : ...`.
5. The rts usage line: `rts [--seed S] [--bots B] [--map 50..69] [--reseed] [--no-debug] [--ticks N]
   [--dump-every N] [--dump-dir D] [--dump-final]`.

## Commands / formats (user documentation)

```
mcport <game dir> rts [--seed S] [--size 256] [--bots B] [--map 50..69] [--reseed] [--debug | --no-debug]
                      [--ticks N] [--dump-every N] [--dump-dir D] [--dump-final]
environment: MC_DUMP_EVERY=n  MC_DUMP_DIR=dir  MC_DUMP_FINAL=1  (any run once main.cpp item 2 is in)
```
Defaults: seed 1, 3 bots, 1 human, map from the seed, the map's own terrain, debug commands on, no dumps.
Headless example (Git Bash):
`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy MC_SAVE_DIR=/tmp/mc MC_TICK_HZ=1000 mcport.exe <game> rts --seed 1
--bots 3 --ticks 2000 --dump-every 500 --dump-dir dumps` - tail stdout for the `dump ...` lines, read
`dumps/dump_<n>.json`. Movie v3 and the dump document: sections above.

## Next round

- Round 11 (world): `ModeParams.world_size` and `mode_build_level` become the world module's generator + placer;
  the template path stays for 256. `MODE_STATE_VERSION` 2 when ModeState grows (the reserved words are 11 u32
  + 15 per player).
- Round 12 (launch): a mode win / lose rule in `mode_tick` setting the status bits (instead of the original's
  single-player win check), the result screen, no `Config.level++`; lobby: `ModeParams` in `NetGameRules`, a
  per-slot human / bot mask; `mode_level.cpp` assigns humans to records `0..humans-1` today.
- Round 15 (orders): the order blobs of movie v3 (`g_hook_demo_order_out / _in`) and the same bytes in the net
  exchange.
- E / B: `checksum_parts` in the dump; a scenario `dump` command -> `dump_now`.
