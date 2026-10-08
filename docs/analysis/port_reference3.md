# References with an active local player, and movie recording (task C, port round 6, 2026-10-07)

Every per-tick reference before this round had an idle local player (movie 0 aside). This round adds
**movie recording to the port** (byte-compatible with the original's `movie/mvi|gam|map%05d.dat`),
**scripted input** for the local player, and four recordings made by the port, **played back by the
original** in DOSBox with per-tick dumps and compared with the port's own playback.

Result in one line: **the port is byte-identical to the original on all four scripted recordings**
(15 595 compared ticks: every spell id 0..23 cast, castle build + upgrades, possession / mana
collection by the balloons, duels with AI wizards, deaths and a respawn at the castle, the book / quick-select /
toggle / zoom / chat commands, quick save and quick load) after one game-logic fix (cheat notice text,
first divergence at tick 12). The port's recorder writes the idle level-0 recording byte for byte like
the original's own recorder (packets and map identical; GameState identical except the renderer's
`texture_needed` bytes).

## Files

| file | what |
|---|---|
| `src/mcengine/demo.h/.cpp` | recorder: the recording half of `demo_record_playback_step_3c540`, `demo_save_state_3c2c0`, `demo_save_terrain_3c430`, the index -> pointer conversion (`demo_state_to_original`), record directory, pointer bases |
| `src/mcengine/player.cpp` | command 10 (quick save) calls `demo_save_state(10000)`; cheat notice fix |
| `src/tests/reference_player_test.cpp/.cmake` | the script runner / port recorder (`record` mode) and the round-trip test (ctest `reference_player_test`); ctest `reference_player` (reference_test suite over the recordings) |
| `src/tests/reference_test.cpp` | suite mode takes any subdirectory with an index.json naming a level; quick-load aware comparison; quick-save directory; `MC_REF_STEPS`, `MC_REF_CELLCHECK` diagnostics |
| `tools/reference/run_player.py` (new) | port record run + original play run per script -> `extracted/reference/player/<name>/` |
| `tools/reference/run_reference.py` | `gam10000.dat` (the quick save) is not a dump |
| `tools/reference/player_scripts/*.txt` (new) | the four scripts |
| `extracted/reference/player/<name>/` | `script.txt`, `movie/{mvi,gam,map}M.dat` (the port's recording the original played), `tick%05d.gam/.map` (original's play run), `index.json`; p4 also `orig_gam10000.dat` (the original's quick save). 3.5 GB in all |

## Functions translated

- `demo_record_playback_step_3c540`, recording branch (0x3c6a7..0x3c7ac): first packet of a tick with
  `Config.flags & 2` and no open file: open `movie/mvi%05d.dat` (Config.movie), on failure drop bit 2;
  `texture_mark_needed_4c130` (hook `g_hook_demo_textures_mark`), `demo_save_state_3c2c0(movie)`,
  `demo_save_terrain_3c430(movie)`, **`models_initialise_354c0` + GameState+0x11f1 (active_top) = -1**,
  Config+0xa1 = 3, Config+0xa2 = 200 (credits roll); then every packet is written (10 bytes; a write
  error closes the file) and a packet with command 0xc is cleared after it was written.
- `demo_save_state_3c2c0` (raw 0x38d03-byte GameState, original pointer layout), `demo_save_terrain_3c430`
  (type, height, light, flags, cell heads, corner table: 0x612c2 bytes), `demo_close_3c7c0` (also closes
  the file being written).
- Command 10 of `player_commands_process_3a8b0` (0x3ac13): `demo_save_state_3c2c0(10000)` unless network.

New port API (demo.h): `demo_set_record_dir(dir)` (where recordings and the quick save go; reads prefer
it, then the `demo_open` directory - the original has one movie directory), `demo_recording()`,
`demo_packets_written()`, `demo_save_state/terrain`, `DemoPointerBases` + `demo_set_pointer_bases()` /
`demo_pointer_bases()`, `demo_state_to_original()`, hook `g_hook_demo_textures_mark`.

### Snapshot pointers (why a recording must carry the player's addresses)

`demo_relink_state_pointers_3dc10` re-points only the players' `Thing.player` and rebases `Thing.desc`
relative to player 0's; `models_initialise_354c0` rebuilds the free / recyclable stacks. **`Thing.next`
and the `Thing.player` of every other Thing (the dummy block 0x21de90 for unowned ones) are used as
absolute addresses**, so a snapshot only plays in a run whose GameState sits where the recording run's
did. The port writes the DOSBox run's addresses (identical in every reference dump of tools/reference):
GameState 0x000190d0 (things[0] 0x00020533), `g_move_desc[0]` 0x001fda10, dummy player block
0x0021de90. Inverse of `thing_relink_snapshot`: index -> `things_base + i * 0xa4` (0 -> NULL), desc
index -> `move_desc + i * 0x20`, P-block offset -> `state + off`, 0 -> dummy block.

## The script runner

`reference_player_test <game> record <script> <out>` reproduces the original's record run of
run_level.py (flags 0x100, level_load_file resets, `sim_load_level(L)`, after tick 1 `flags |= 2` so
tick 2's first packet opens the recording, `demo_close` at tick STOP) with `g_hook_player_local_input`
replaced by the script. The packet is built as `player_local_input_16660` builds it: a pending command
(the join packet) is left alone; key bits travel with command 6; an explicit command wins over bits.
Verbs: `steer X Y`, `keys B`, `cast left|right`, `cmd C A [P2]`, `cheat N`, `left|right S` (command
0x15 / 0x16 with the book slot holding spell id S), `book`, `close`, `respawn` (only while dead and
with a castle - see below), `rebuild`, `face X Y [Z]`, `fly X Y`, `faceth N`, `face_class C T [D]` (aim at
the nearest live Thing of a class / type, optionally chasing it to distance D; steering computed with
floating point in the test, the packets are integers). Spells come from the `cheat 1` packet (command
0x1e arg 1: every spell), mana from `cheat 2` (a 100 000 mana ball): the packets go into the recording,
so the original executes them without the RATTY / "chronicle" gate (that gate is in the input queue).
Script ticks count record-loop steps (= PlayerRec.tick until a quick load sets it back).

`tools/reference/run_player.py SCRIPT...`: port record run (3-5 s), then the recording is copied into
`extracted/refgame/magic/movie` and played by `carplay -roll M+1 -level L` (play patch, dumps every tick
up to `--every-until`, then every `--stride`), ~50 s per 4000 ticks; `index.json` gets level / movie and
where the playback really ended.

## Recordings and results

First divergent tick of the port's playback against the original's dumps (`reference_test <game>
extracted/reference/player/<name>`), before -> after this round's fix.

| recording | level | ticks (dumps) | content | before -> after |
|---|---|---|---|---|
| p1_castle_spells_l0 | 0 (townies, no wizard) | 4000 (3999, every tick) | castle 16 cells ahead + upgrades at 300 / 600 / 2740, book open / select (0x14, 0x15), close, quick-select assign / use (0x17, 0x18, 0x19), PlayerRec.flags toggles M / F (cmd 4), zoom (8), camera log (7), chat begin / keys / backspace / send (0x10, 0x11, 0x13), fireballs at creatures, possession of mana balls (14 casts), **every spell id 0..23 once** (1 heal at full health, 2 / 21 speed spells move the flyer across the map), balloons carrying mana home | **12** (cheat notice) -> **0** |
| p2_duel_death_l38 | 38 (3 AI wizards) | 6000 (5999, every tick) | castle, chasing the nearest AI wizard, fireball / lightning / rubber band (11) / steal mana (13) / reverse speed (21) / rebound (14) / shield (4) / invisible (12), **heal (1) while damaged**, possession, meteor / volcano / smart bomb / thunderbolt / fire wall at an enemy castle, **2 deaths, 1 respawn at the castle (command 0xf)**, castle lost, final death | 0 -> 0 (with the fix) |
| p3_spells_vs_wizards_l44 | 44 (water, kraken, crab, emu, type 15, wyvern; 3 AI wizards) | 8000 (4799: every tick to 4000, then every 5th) | speed / strafe keys (1, 2, 4, 8), every spell id at the nearest enemy castle, castle lost and rebuilt attempts, 2 x 20 possession casts with balloons collecting, every attack spell at the wizards, creature hunt | 0 -> 0 |
| p4_quicksave_l1 | 1 (vultures, worms, dragons) | 1000 (798 compared) | castle, fight, **quick save (command 10) at tick 400, quick load (0xb) at 800** | 0 -> 0 (see below) |

Coverage of the 24 spell ids (Table B order): 0 fireball, 1 heal, 2 speed-up, 3 possession, 4 shield,
5 beyond sight, 6 earthquake, 7 meteor, 8 volcano, 9 crater, 10 teleport, 11 rubber band, 12 invisible,
13 steal mana, 14 rebound, 15 lightning, 16 castle, 17 skeleton, 18 thunderbolt, 19 mana magnet, 20 fire
wall, 21 reverse speed, 22 smart bomb, 23 mini fireball: all cast in p1 and p3 (heal really heals in
p2), each followed by its projectiles / effects / terrain changes (map cells compared every 500 ticks:
0 differ). Castle: built (state 5 -> 4), upgraded three times (class-10 type 43 / 42 effects, the 32
mana balls of an upgrade), balloons (class 3 type 3) carry up to 100 000 mana; castle mana 10 000 ->
40 000. Deaths: player states 2 (dying) -> 3 (dead) -> respawn via command 0xf, the spells dropped
(re-armed with cheat 1 in the script). The AI wizards fight back (p2 / p3): their spells, castles and
balloons are compared as well.

Thing classes, PlayerRec / P blocks (all 8 records), the RNG, the live count and the maps were compared
at every dumped tick: no difference after the fix. `thing_dispatch_report`: 0 handlers without a port.

**Round trip (port recorder -> port playback)**, ctest `reference_player_test`: the scripts are recorded
again (into `%TEMP%/mc_reference_player_roundtrip`), played back in the port and compared per tick with
the record run (FNV hash of the GameState with free-slot pointers and the HUD counters normalised, see
below): identical on all four (18 996 ticks); the new recordings are byte-identical to the stored ones
(the line turns into "DIFFERS" when game logic changes - re-run run_player.py then).

**Recorder vs the original's recorder**: an idle level-0 recording by the port vs the original's
`level00/movie/*20000.dat` (round 5): `mvi` (4999 packets) and `map` byte-identical; `gam` identical
except `texture_needed[]` (GameState+0x2c, 184 bytes: `texture_mark_needed_4c130` reads the sprite
cache; not connected in the test - see requested changes).

**Quick save**: the original's `gam10000.dat` written during the p4 playback vs the port's (same tick):
identical except (a) the stale entries above the free-stack top and stale fields of free slots
(rebuilt / unused on load), (b) option bytes +0x2195 / +0x2197 (1 in the port, 0 in the DOSBox run;
`demo_load_state` keeps the current options), (c) **`Thing.next` of six list tails: the original has
`&things[0]` (0x00020533), the port 0** - the original ends some lists with the sentinel instead of
NULL; `thing_relink_snapshot` maps both to 0, so the port cannot reproduce which. No effect on any
comparison.

## Fixes

1. **Cheat notices (`player.cpp` `set_notice`, command 0x1e)**: the original copies the notice with
   `strcpy`, so the tail of a longer previous text stays in `PlayerMsg.text` after the terminator
   (".. CHEAT: more mana\0" over ".. CHEAT: access all spells" leaves "spells" behind); the port used
   `strncpy`, which zero-pads. Evidence: p1 / t2 dump of tick 12, player 0 +0x30 (messages[0].text[0x14]).
   First divergence tick 12 -> none. Movie 0 / levels / gen unaffected (no cheat there).
2. Command 10 now performs the quick save (was a TODO); quick load (0xb) already existed.

No other game-logic divergence was found.

## Findings about the original (corrections to ENGINE.md)

- **Recording branch of `demo_record_playback_step_3c540`**: after the snapshot pair it calls
  `models_initialise_354c0` and sets active_top = -1 (exactly what a playback's load does), then the
  credits-roll bytes Config+0xa1 = 3 / +0xa2 = 200. The file is created with mode 0x222.
- **Record vs play differences** (round 5 saw P+0x188 only): `render_frame_1fab0` skips the status
  panels, the hand labels and the message lines while a movie plays (`Config.flags & 4`, before
  `ui_draw_status_bars`), so `hit_flash`, `castle_hit_flash`, `damage_flash`, `spell_flash[]` and
  `PlayerMsg.ticks` count down in a record run and stay put in its playback. The port does the same
  (hud.cpp); the round-trip test masks exactly these bytes.
- **Command 0xf (respawn) without a castle** sets status 0xc (level lost): the original's game_main then
  leaves the level **without closing the movie file** and the next level start goes on reading packets
  into a freshly generated level (dumps restart at tick 1). The script runner never sends 0xf without a
  castle; run_player.py flags a tick-1 dump as "playback restarted".
- **Quick load inside a level is broken in the original**: `demo_load_state_3c200` restores the Things
  (with their `cell_next` / `cell_prev` links) but not `g_cell_things`, the cell heads (they are in the
  map file, which the quick load does not read). The cell lists become inconsistent and some turn into
  cycles; `MC_REF_CELLCHECK` shows a non-terminating cell list in the port at tick 404 of p4 (and 466 /
  516 in the experiments), and **the original stops dumping right there** (p4: 3 ticks after the load;
  experiments: 117 / 159 ticks) - it hangs in a cell walk (the renderer draws Things per cell; DOSBox
  stays alive with no new dump). The port's simulation does not hang (no cell walk on that cell in the
  game logic during the compared ticks) but **mcport's renderer could** if it walks a cyclic cell list
  unbounded (task D's file; worth a bound). The GameState up to the original's last dump is identical
  in both. run_player.py finds where the original's playback really ended from the write order of the
  dumps (`playback_end_tick` / `playback_end_loads`), reference_test compares only up to there and only
  the last pass of every tick.
- Some `Thing.next` list tails hold `&things[0]` rather than NULL (quick save, see above).
- The cheat notice texts are copied with `strcpy` (see fix 1); the texts are English also in the French
  movie text set.

## Deviations / gaps

- The port's snapshots carry the DOSBox addresses by default; another original run (other memory
  layout) needs `demo_set_pointer_bases()`. Free-slot stale fields, the stale stack entries and the
  NULL-vs-`&things[0]` list tails are not reproduced (none of them is read by a load).
- `texture_needed[]` is written as the port has it (zeros unless the hook is installed).
- Quick load is verified only up to the original's hang (p4: the loaded state and 3 further ticks).
- The scripted recordings use cheats for spells and mana; finding spells in the level (pickups) is
  covered only where the AI does it.
- 640x480, network and the front end are not part of these recordings (tasks D / B).
- Steering in scripts uses floating point (test code only; the packets are integers).

## Hooks declared / installed

- Declared: `g_hook_demo_textures_mark` (demo.h, `texture_mark_needed_4c130` when a recording starts).
- The script runner installs `g_hook_player_local_input` (test only).

## TODO(port) left

- none in demo.cpp; `player_cd_check_quit` (player.cpp, copy protection) unchanged.

## Requested shared-file changes

1. `src/mcengine/engine.cpp`, `engine_init`, next to `g_hook_demo_textures_reload = texture_load_needed;`:
   ```cpp
   g_hook_demo_textures_mark = texture_mark_needed;      // texture_mark_needed_4c130 when a recording starts
   ```
2. `src/mcengine/sim.cpp` `sim_load_level`: the round-5 request (level_load_file_3d160 Config resets) still
   stands; `reference_player_test.cpp` carries a third local copy (`level_reset_config_local`).

## mcport integration (main.cpp)

Recording (Alt+R = command 0xc, `Config.flags |= 2`), the quick save (Alt+S = command 10) and the quick
load (0xb) need a writable directory; never the game directory. Right after the save directory is set
up (main.cpp ~line 300, where `fe_set_save_dir` is called):
```cpp
#include "demo.h"
...
        demo_set_record_dir(save_dir.c_str());            // <save_dir>/movie/mvi%05d.dat, gam10000.dat
        {   std::error_code ec; std::filesystem::create_directories(std::filesystem::path(save_dir) / "movie", ec); }
```
A recording made in mcport plays with `mcport <dir> demo N` (N = the movie number shown by the HUD's
"MOVIE: n" line): `demo_open(s_game, N)` finds `movie/mviN.dat` in the record directory first. Cycle
caution: after a quick load the original's cell lists can contain a cycle (see findings); the renderer's
per-cell walk should be bounded in the port.

## Verification gates (all after the last change)

`reference_test` (movie 0, 1396 ticks), `reference_levels` (8 levels), `reference_gen` (69 levels),
`render_reference_test`, `render_reference_hud`: identical / pass. New: `reference_player` (4
recordings, 15 595 ticks identical), `reference_player_test` (4 round trips identical). Also sim_test,
player_test, input_test, spells_test, ai_wizard_test, game_test pass. Zero warnings in my files
(net.cpp, task B's, shows two C4996 warnings in build_C - not mine).

## How to regenerate

```
cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_C
cmake --build ../build_C --config Debug --target reference_player_test reference_test
cd "C:/Magic Carpet/tools/reference"
python run_player.py player_scripts/p1_castle_spells_l0.txt --every-until 4000
python run_player.py player_scripts/p2_duel_death_l38.txt --every-until 6000
python run_player.py player_scripts/p3_spells_vs_wizards_l44.txt --every-until 4000 --stride 5
python run_player.py player_scripts/p4_quicksave_l1.txt --every-until 3000
cd "C:/Magic Carpet/build_C" && ctest -C Debug -R reference_player
```
A recording's own events (casts, new Things, player state / castle changes) are printed by
`reference_player_test <game> record <script> <out>` (unset `MC_REC_QUIET`).
