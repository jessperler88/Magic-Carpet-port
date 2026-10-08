# Port round 10, task E: desync tooling (checksum parts, parts exchange + dumps, diff tools, replay check)

Files: `src/mcengine/net.h/.cpp` (checksum / sync parts only), new `src/mcengine/replay_check.{h,cpp}`, new
`src/mcport/replay_run.{h,cpp}` (mcport glue), new `tools/reference/diff_state.py`, new
`tools/reference/tick_log_diff.py`, `settings.h` section "round 10 task E" (one field), tests
`src/tests/desync_test.{cpp,cmake}`, `src/tests/replay_check_test.{cpp,cmake}`. Build dir `build_E`.
Nothing here is in carpet.exe; no original function was translated.

## What was built

### 1. Checksum parts (`net.h`)

`net_state_checksum()` is now computed by one pass (`state_checksum` in net.cpp) that feeds the same bytes in
the same order into the total **and**, when asked, into one FNV-1a 32 per named part. FNV does not compose, so
the total is not a fold of the parts; it is the round-9 sequential hash, byte for byte (verified below).

```cpp
enum : int { NCP_RNG, NCP_RNG16, NCP_POOL, NCP_POOL_EXT, NCP_THINGS /* +cls 0..15 */, NCP_PLAYERS = NCP_THINGS + 16 /* +p */,
             NCP_MAP_TYPE = NCP_PLAYERS + 8, NCP_MAP_HEIGHT, NCP_MAP_LIGHT, NCP_MAP_FLAGS, NCP_CELL_HEADS, NCP_MODE,
             NCP_AI_SEED, NCP_COUNT /* 35 */ };
struct NetChecksumParts { uint32_t total; uint32_t part[NCP_COUNT]; };
void        net_state_checksum_parts(NetChecksumParts *out);      // total == net_state_checksum()
const char *net_checksum_part_name(int part);
int         net_checksum_parts_first_diff(const NetChecksumParts &, const NetChecksumParts &, int *count);
int         net_checksum_parts_format(const NetChecksumParts &, char *buf, size_t cap);   // " name=xxxxxxxx" ...
extern uint32_t (*g_hook_net_ai_seed)();
```

| part | bytes (each part starts at the FNV basis 0x811c9dc5) |
|---|---|
| `rng` | GameState.rng |
| `rng16` | g_rng16 |
| `pool` | free_top, free_list[1000], active_top, active_list[1000] |
| `pool_ext` | extended pool: slots, ext free stack, ext active stack (basis with the original 1000-slot pool) |
| `things.free`, `things.c1`, `things.scenery`, `things.player`, `things.c4`, `things.creature`, `things.c6..c8`, `things.projectile`, `things.effect`, `things.switch`, `things.spell`, `things.c13`, `things.c14`, `things.c15+` | per Thing.cls (15 = 15 and above): u32 slot index + the Thing with the masked flags bit (slot index in the part only, not in the total) - both pools |
| `player0..7` | record head (win_timer, status, active, index, is_computer, thing) + P block minus start_tick / HUD flash counters / spell_flash (exactly as the total) |
| `map.type`, `map.height`, `map.light`, `map.flags`, `map.cells` | the four maps, the u16 cell-list heads |
| `mode` | `mode_checksum(basis)` while a mode runs, basis otherwise |
| `ai_seed` | `g_ai_rand_seed` value (**not in the total** - the round-9 checksum never had it); 0 until `g_hook_net_ai_seed` is installed |

### 2. Parts exchange on the first mismatch, dumps (`net.cpp net_exchange_frame`)

The side channel (`MC_NET_SYNC`) is unchanged: one 8-byte `{exchange, checksum}` message per direction after
the packets. New: when the host's comparison with client b (and therefore b's comparison with the host - the
two compare the same two numbers) mismatches **for the first time since `net_sync_arm`**, both do one more
message each, in the same exchange, right after the checksum messages:

- the client sends first, then receives; the host, after its send loop, receives from b and then sends (no
  crossing waits, works with the message semantics of MemLan and the TCP transport);
- message `"PRTS"` (u32 0x53545250), u32 exchange, u32 count, u32 total, count x u32 parts - size-agnostic
  (a peer with another part count compares the common prefix; a part list up to ~400 entries fits one
  message < 0x800);
- once per peer pair (`parts_done[remote]`), so every later exchange keeps the round-9 message pattern
  (mismatches are still counted); the lockstep is never broken by it (verified with two processes over TCP
  playing 200 ticks past the desync).

Both sides print `net: DESYNC parts at exchange N with player P: first differing part things.creature (local
x, player P y), 1 part(s) differ: things.creature` to stderr, record it in `NetSyncStats` (new fields
`first_part`, `part_peer`, `parts_differ`, `parts_exchanges`, `dumped`; `net_sync_last_parts(local, remote,
peer, exchange)` returns the two part sets for the console `sync` readout / C's overlay), and call the dump
hook once per peer. The state hashed, the parts and the dump are of the same moment (the exchange is inside
`player_commands_process` before any packet of the tick is applied; nothing changes the hashed state in
between).

Hooks: `net_sync_set_parts(fn)` (tests: per-peer view; null = the real parts), `net_sync_set_dump(fn)`.
`replay_check.h`: `desync_dump_state(exchange, player)` writes `<dir>/desync_<exchange>_p<player>.mcs` with
`savestate_save_file` (GameState, Config, maps + cell heads + corner table, GLOB, CAMP, RULE, MODE, POOL),
dir = `desync_set_dump_dir()` or `savegame_save_dir()`; off with `PortSettings::desync_dump = false`.
`desync_tools_install()` installs the dump hook (calling thread's net context) and the ai_seed reader.

### 3. `tools/reference/diff_state.py`

`python tools/reference/diff_state.py A B [--all] [--max N] [--cells N] [--types mc_types.h]`. Inputs: `.mcs`
save states / desync dumps, raw GameState images (movie `gamNNNNN.dat`, reference `tick%05d.gam`), raw
GameState + maps (net_test `MC_NET_DUMP`). **Field tables are generated at run time from `mc_types.h`** (and
`mode.h` for the MODE chunk): a small parser of the pack(1) structs (scalars, arrays with constant
expressions, nested structs, the PlayerRec union - the typed `blk` is used), each size checked against the
header's `static_assert(sizeof(X) == N)`; reference_test's hand-written Thing table was not reused (it only
covers Thing). Output: `first difference (hashed): things[141].health (creature type 9 state 55): 1000 ->
1007`, then every hashed difference (Things by slot with class / type / state, players by `players[p].blk.*`,
the stacks, maps by `map.height[cell x,y]`, GLOB globals, mode block, pool extension), then the count of
differences the checksum leaves out on purpose (Config, local_player, PlayerRec tick / messages / camera log,
start_tick, flash counters, flags bit 0 of the local flyer / spells, level data ...; `--all` lists them).
Exit 0 = no hashed difference, 1 = differences, 2 = error.

### 4. `tools/reference/tick_log_diff.py`

`python tools/reference/tick_log_diff.py A B [--context N] [--all-parts]`: reads `"<tick> <hex>[ name=hex
...]"` lines (MC_TICK_LOG, MC_TICK_LOG_PARTS, net_test's peer files; other lines ignored), matches ticks by
number, prints the first differing tick, and with parts: the parts differing there (first one named, an
ai_seed-only difference is reported too) and the first tick each other part diverges. Exit 0 / 1 / 2.

### 5. Replay check (`replay_check.h`)

```cpp
bool replay_check_movie(const char *game_dir, int number, const ReplayCheckOptions &, ReplayCheckResult *);
bool replay_check_state(const char *state_path, long ticks, const ReplayCheckOptions &, ReplayCheckResult *);
```

Plays the movie (via `demo_open` / `demo_step` / `demo_close`, so v1 / v2 now and A's v3 when it lands) twice
from its start and compares the total and all parts after every tick, stopping at the first difference
(`first_diff_tick`, `first_part`, `parts_differ`, both totals, a message). Default preparation per pass:
Config reset, `sim_prepare_movie`, level 38 for movie 0 (`carpet -roll 1 -level 38`); `restore_globals`
(default on) saves a save state before pass 1 and loads it before pass 2, so the simulation globals outside
the movie snapshot (GLOB: g_rng16, g_ai_rand_seed, ...; CONF; RULE) start equal - off = report what leaks
between passes. The state variant loads the level of the header (`sim_load_level`; a mode level needs a
`prepare` callback) + the state, runs N ticks without local input, twice. `after_tick(pass, tick, user)` lets
tests perturb. mcport: `--replay-check N | state:<file> [--replay-ticks T] [--replay-raw]` (replay_run.h,
headless, exit 0 = identical).

## Verification

All in `build_E`, Debug and Release, zero warnings (/W4) for every target built (desync_test,
replay_check_test, net_test, config_test, reference_*, render_reference*, sim_test, mcport).

- **desync_test** (Release 3.8 s, Debug ~4 s): part 1: a verbatim copy of the round-9 `net_state_checksum`
  (+ the round-10 mode block) == `net_state_checksum()` == `parts.total` at **1203 states** (level 38 300
  ticks, movie 0 600 ticks, level 20 with a 3000-slot pool 300 ticks, a mode block active): 0 differ. Part
  mapping: a one-bit change in rng, g_rng16, free_list, active_top, a Thing of each of 7 classes present,
  the pool-extension Thing and stack, players[1].blk.mana, players[7].status, each map, the cell heads, the
  mode block -> exactly that one part and the total change; start_tick, hit_flash, spell_flash, PlayerRec.tick,
  local_player, the local flyer's flags bit 0 -> nothing changes; g_ai_rand_seed -> `ai_seed` only, total
  unchanged. Part 2 (MemLan, 3 peers, shared simulation, player 2 sees things[141] (creature) health +7 from
  exchange 15 of 40): host and player 2 detect at exchange 15, both name `things.creature` (1 part), both
  dump (`desync_15_p0.mcs`, `desync_15_p2.mcs`, no p1), player 1 sees nothing, 25 mismatches each, 1 parts
  exchange each, all 40 host checksums equal the round-9 function; diff_state.py: `first difference (hashed):
  things[141].health (creature type 9 state 55): 1000 -> 1007`. Part 3 (two processes over TCP, level 50,
  300 ticks, process 1 adds 7 to creature slot 51 before tick 100): both detect at exchange 100, both name
  `things.creature`, both dump, both play to tick 300 (200 mismatches, 1 parts exchange each); diff_state.py:
  `things[51].health (creature type 2 state 15): 3000 -> 3007`.
- **replay_check_test** (Release 25.6 s, Debug 34 s): movie 0 played twice in full: **8551 ticks identical**
  (total + 35 parts each; 24.6 s Debug); perturbed second pass (creature health +3 after tick 777): caught at
  tick 777, `things.creature`, 1 part; a port mvx v2 movie (level 12, possession 150 %, 400 ticks recorded
  here): 401 ticks identical; a save state (level 5 tick 150) 300 ticks twice: identical, with g_rng16
  changed at tick 50 of pass 2: caught at tick 50, `rng16`.
- **net_test** passes unchanged (part 1's injected fake-checksum mismatch now also triggers the parts exchange:
  "every part agrees", the message order of round 7 and all counts unchanged; parts 2 / 3 over TCP: 2000 /
  3201 checksum pairs, 0 differ).
- Gates with default settings, Release: reference_test (movie 0: 0 divergences over 1396 dumped ticks),
  reference_levels, reference_gen, reference_player_test, reference_player, config_test, net_test,
  render_reference_test, render_reference2_test, render_reference_hud, render_reference_options,
  render_reference_fe_test, sim_test: all pass.
- diff_state.py on two consecutive reference dumps (tick00500 / tick00501.gam): rng, free_top, the players'
  steering, Things by slot - sensible; tick_log_diff.py on synthetic logs: first tick, part, later parts.

## Findings (for the integrator / later rounds)

1. **False desync at exchange 0 after an earlier level in the same process.** A process that played another
   level before keeps stale entries above `GameState.active_top` in `active_list` (and possibly `free_list`
   above `free_top`), and the checksum hashes the whole 1000-entry arrays. Two peers with different histories
   (e.g. the second network game of a session, or a campaign level played before the lobby) report `DESYNC
   at exchange 0`, part `pool` (diff_state: `active_list[0]: 477 -> 0` ...), although nothing reads those
   entries. Found by desync_test part 3 before I ran it first (the parent had loaded levels 38 / 20 before).
   Fix options (both change something, so not done here): clear both stacks above their tops at the level
   start (`sim_load_level_data`, reference-safe only if the references do not compare the dead entries -
   reference_test compares Things / rng / players, not the stacks), or hash only `[0..top]` (changes the
   checksum value: would need a versioned checksum and new stored tick logs). Recommended: the first.
2. **Movies do not carry the globals outside GameState.** With `restore_globals` off, movie 0's second pass
   differs from tick 1 in `ai_seed` (the AI's `rand()` seed continues from pass 1). The mvi / mvx / gax
   snapshots carry neither `g_ai_rand_seed` nor `g_rng16`; a port movie of a level with computer wizards
   played in a process whose seed differs can diverge. **Task A: let the v3 `gax` snapshot carry the GLOB
   values** (g_rng16, g_ai_rand_seed at least), and seed `g_ai_rand_seed` from the match seed at the mode level
   start. **Should E hash it?** Not in the total (the value must stay); it is the `ai_seed` part now. When the
   mode is active, A can add it to the total through `mode_checksum` (only mode runs change), which I recommend
   for online bot games.

## Deviations / gaps

- The total is computed as before, not as a fold of the parts (FNV is sequential).
- The parts exchange is only between host and client (the only pairs that compare); a desync between two
  clients that both match the host cannot happen (the host's value is the reference for both).
- A parts message that does not arrive (peer gone) is reported ("no parts message") and nothing is dumped by
  that side; there is no receive timeout (as the rest of the exchange, port_net.md).
- The replay check compares two runs of the port, not the original; for v3 / rts movies it relies on A's
  `demo_open` starting the mode run.
- `diff_state.py` names GLOB fields from a fixed list mirroring savegame.cpp `write_globals` (version 1);
  update it with a GLOB version bump.

## Settings / ini keys

`PortSettings::desync_dump` (bool, default true): write the desync dump on the first parts exchange. Local
only (no effect on the game or the messages). Requested `k_desc[]` line for `src/mcport/config.cpp` (after the
`quicksave_full` line):

```cpp
    {"net", "desync_dump", K_BOOL, PS(desync_dump), 0, 1, "MC_DESYNC_DUMP",
     "network games: on the first desync write the state to <save dir>/desync_<exchange>_p<player>.mcs"},
```

Environment (mcport, read by `replay_run.cpp`): `MC_TICK_LOG_PARTS=1`.

## Requested shared-file changes

1. `src/mcengine/sim_all.cpp`, in `sim_register_gameplay()` (anywhere; needs `#include "net.h"` and
   `"ai_wizard.h"`): so every program / test that links the gameplay has the ai_seed part:

   ```cpp
   g_hook_net_ai_seed = [] { return g_ai_rand_seed; };   // net.h checksum part "ai_seed" (round 10 task E)
   ```
   (until then `desync_tools_install()` / `tick_log_format` / the replay check install it themselves.)
2. Nothing in `src/CMakeLists.txt`: replay_check.cpp is picked up by the mcengine glob, replay_run.cpp by the
   mcport glob, the tests by their .cmake files.

## main.cpp integration (task D, exact code)

```cpp
#include "replay_run.h"                                   // with the other mcport includes
```

1. In `main()`, **before** `config_load(...)` (config_load refuses unknown `--options`):

   ```cpp
       // round 10 task E: --replay-check N | state:<file> [--replay-ticks T] [--replay-raw] (replay_run.h)
       ReplayRunArgs replay;
       {
           std::string err;
           if (!replay_run_parse(&argc, argv, &replay, &err)) { std::fprintf(stderr, "mcport: %s\n", err.c_str()); return 1; }
       }
   ```
2. Right after `engine_init` succeeded (before `g_video_mode_flags = 1;` / the platform init - the check is
   headless and needs no window):

   ```cpp
       desync_run_install();                               // desync dumps + ai_seed part + MC_TICK_LOG_PARTS
       if (replay.active) return replay_run(s_game.c_str(), s_save_dir.c_str(), replay);
   ```
3. In `sim_tick`, replace the MC_TICK_LOG line

   ```cpp
       if (s_tick_log) std::fprintf(s_tick_log, "%ld %08x\n", s_ticks_run, (unsigned)net_state_checksum());
   ```
   by

   ```cpp
       if (s_tick_log) tick_log_write(s_tick_log, s_ticks_run);   // + checksum parts with MC_TICK_LOG_PARTS=1
   ```
   (identical output without MC_TICK_LOG_PARTS.)
4. Nothing else: the dump directory defaults to `savegame_save_dir()`, which `fe_set_save_dir` sets in every
   game run (network games go through the front end).
5. Usage line: add `| --replay-check N|state:FILE [--replay-ticks T]`.

## Grammar / formats (user documentation)

- `mcport <game> --replay-check N [--replay-ticks T] [--replay-raw]`: movie N (`movie/mvi|mvx%05d.dat` in the
  game dir, then `<save dir>/movie`) played twice; prints `replay-check: OK movie N: identical, 8551 ticks
  compared (checksum + 35 parts each)` or `replay-check: DIFFERS movie N: pass 2 differs at tick T: first
  part <part> (k part(s) differ), totals a / b`; exit 0 / 1. `--replay-check state:<file.mcs>` runs a save
  state T (default 1000) ticks twice without input. `--replay-raw`: do not restore the globals between passes.
- `MC_TICK_LOG=<file>` + `MC_TICK_LOG_PARTS=1`: lines `<tick> <checksum> rng=... rng16=... pool=... pool_ext=...
  things.free=... ... player0=... ... map.cells=... mode=... ai_seed=...`. Compare two runs with
  `python tools/reference/tick_log_diff.py a.log b.log --context 3`.
- Network: `MC_NET_SYNC=n` as before; on the first desync each of the two peers prints the first differing
  part and writes `<save dir>/desync_<exchange>_p<player>.mcs`; compare them with
  `python tools/reference/diff_state.py desync_N_p0.mcs desync_N_p1.mcs` (run from anywhere except the repo
  root - `ghidra/` shadows a package - e.g. `cd tools && python reference/diff_state.py ...`).
- Parts message on the wire: `"PRTS"`, u32 exchange, u32 count, u32 total, count x u32 (little endian).

## Next round

- Fix finding 1 (clear the stacks above their tops at level start) and re-run the references.
- A's v3 snapshot with the GLOB values (finding 2); then `replay_check_test` can add an rts movie (`movie_v3_test`
  recordings) - the API already goes through `demo_open`.
- Console `sync` (task B) can print `net_sync_stats()` + `net_sync_last_parts()`; C's sync overlay the same.
- Online round (19): the parts exchange is the place to add a "send me your state" request (the dump is the
  state transfer a reconnect would need).
