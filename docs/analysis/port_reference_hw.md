# Per-tick references of the Hidden Worlds executable (and the 1995 base exe)

The round-4/5 harness (`port_reference.md`, `port_reference2.md`) patches a copy of the 1996
`carpet.exe` so that DOSBox dumps the GameState every tick. This adds the same harness for the two
executables of the 1995 CD (the user's own GOG copy, extracted to `extracted/gog_cd/CARPET`):

* `HIDDEN.EXE` (Hidden Worlds: data/blk1-*, pal1-0, build1-0, sky1-0, mspr1-0, hspr1-0, tmaps1-0,
  levels/ddlevels.dat), and
* the CD's `CARPET.EXE` (1995 base game), used as a control: does the 1995 engine generate base
  levels exactly as the 1996 one?

Results in short:

* The patched HIDDEN.EXE produces generation dumps of all 25 campaign levels (DDLEVELS 0..24) and
  5000-tick idle-player playbacks of DDLEVELS 0 and 12.
* The 1995 GameState dump is **0x38d09 bytes**, 6 more than 1996's 0x38d03. They are appended after
  `spells_present`; everything before is laid out as in 1996 (see "Layout").
* **Control: the 1995 CARPET.EXE generates base levels 0, 12 and 38 exactly as the 1996 exe**: tick-1
  state, tick-2 snapshot and terrain are identical once the pointers are converted to index form.
  The recordings (`mvi`) are byte-identical and so are the terrain files. The only differences are
  the 6 extra bytes (zero) and three bytes after the NUL of `players[0].name`. The port, which
  matches the 1996 exe, therefore also matches CARPET.EXE-1995 on those levels
  (`reference_test ... cd95_gen`: OK, 0 of 3 diverge).
* The port on Hidden Worlds, with the current code: the level data is right (same Thing count, same
  `GameState.rng`), but all 25 generations diverge (terrain textures, heights, trees, the
  Fire-wall spell). The playbacks diverge from tick 3, on the AI wizards' speed. Details are in
  "Port: first divergences". These divergences are expected until the HIDDEN.EXE code changes are
  ported.

## How to run

```
python tools/reference/make_refgame_hw.py              # once: extracted/refgame_hw/ (copy + DOSBox)
python tools/reference/run_level_hw.py 0 1 ... 24 --gen-only          # -> extracted/reference/hw_gen/levelNN
python tools/reference/run_level_hw.py 0 12                           # -> extracted/reference/hw_level00, hw_level12
python tools/reference/run_level_hw.py 0 12 38 --exe cd95 --gen-only  # control -> extracted/reference/cd95_gen/levelNN
python tools/reference/patch_hidden.py --exe hidden --verify-only     # address checks only
python tools/reference/norm_state.py A.gam B.gam [--map-a A.map --map-b B.map] [--all]   # cross-exe diff
```

The options of `run_level_hw.py` are those of `run_level.py`: `--stop` (default 5000),
`--every-until 1000`, `--stride 10` and `--gen-only`. Each DOSBox run is headless and takes 6 to 40
s. LEVEL is the `-level` argument. For HIDDEN.EXE it is the DDLEVELS entry k, which is port campaign
index 100 + k (`world_set.h`). For `--exe cd95` it is the levels.dat index, as for the 1996 exe.

Port side (`MC_HIDDEN_DIR` must point at the Hidden data, e.g. the extracted CD folder):

```
set MC_HIDDEN_DIR=C:\Magic Carpet\extracted\gog_cd\CARPET
build\Release\reference_test.exe MagicCarpet\magic extracted\reference\hw_gen          (suite: 25 generations)
build\Release\reference_test.exe MagicCarpet\magic extracted\reference\hw_gen\level00  (one level; MC_REF_GEN_TRACE=1 lists fields)
build\Release\reference_test.exe MagicCarpet\magic extracted\reference\hw_level00      (playback + generation)
build\Release\reference_test.exe MagicCarpet\magic extracted\reference\cd95_gen        (control)
```

## Files

| file | what |
|---|---|
| `tools/reference/make_refgame_hw.py` | `extracted/refgame_hw/magic` = copy of `extracted/gog_cd/CARPET`, plus `CARPET.CD/` (what the CD installer leaves on C:: TMAPS* and LEVELS/* from the CD, SNDSETUP/INTRO.PLD/LANGUAGE.INF from `extracted/refgame/magic/CARPET.CD`; the copy is mounted as C:, so it is `C:\CARPET.CD`), DOSBox + SDL dlls from `MagicCarpet/DOSBOX`. Runs the address checks. |
| `tools/reference/patch_hidden.py` | `LAYOUTS["hidden"]` / `LAYOUTS["cd95"]`: the 1995 addresses. `verify_layout()` checks the original bytes at every one of them. The patch itself is done by a **private module instance** of `patch_carpet.py` with its address constants (and `INPUT_ORIG`) replaced, so `patch_carpet.py` and its defaults are untouched. |
| `tools/reference/run_level_hw.py` | `run_level.py` / `run_reference.py` through private module instances, with `REFGAME = extracted/refgame_hw`, `STATE_SIZE = 0x38d09` and the patch shim. Moves the results to `hw_gen/`, `hw_levelNN/`, `cd95_gen/`, `cd95_levelNN/` and adds `exe`, `exe_file`, `state_size` and (hidden) `port_level` / `ddlevels_index` to index.json. |
| `tools/reference/norm_state.py` | Converts a dump of either size to the port's index form (`thing_relink_snapshot` in Python), cuts it to 0x38d03 bytes (prints the extra tail), optionally appends the terrain, and runs `diff_state.py`. This compares dumps of different executables field by field. |
| `src/tests/reference_test.cpp` | Accepts 0x38d09-byte dumps (compares the common 0x38d03 bytes). Loads index.json `port_level` instead of `level`. Plays a 0x38d09-byte snapshot from a truncated scratch copy (`%TEMP%/mc_reference_test_1995/movie`), because the port's `demo_load_state` takes only 0x38d03. For 1995 dumps it ignores the bytes after the NUL of `PlayerRec.name`. Suite mode runs 1995 references only in a directory without 1996 ones, so `reference_levels` on `extracted/reference` skips `hw_level*` with a note. All 60 ctest entries still pass. |

`patch_carpet.py`, `run_reference.py`, `run_level.py` and `make_refgame.py` are unchanged.

## Addresses patched (and how they were verified)

The four patches of `patch_carpet.py`, at 1995 addresses. The fm.csv function match was only a
starting point. Every address below was checked by disassembly (capstone) of the 1996, CD-CARPET and
HIDDEN code side by side. `verify_layout()` re-checks them on every run (13 checks; any mismatch
aborts).

| what (1996 name) | 1996 | CD CARPET.EXE | HIDDEN.EXE | check |
|---|---|---|---|---|
| cave: body of `dead_castle_site_search` | 0x11cef..0x11d77 | 0x1307f..0x13107 | 0x1307f..0x13107 | 15-byte "return 0" stub directly before (`53 56 57 83ec08 31c0 83c408 5f 5e 5b c3`). The only fixup source in or next to the range is the jump-table operand at CAVE_END. The only fixups into the range are the 4 entries of its own jump table at 0x13054 (as 0x11cc4 in 1996). A raw scan of the code object finds no rel32 call/jmp into the range. |
| call site `call sound_update` in `game_tick_update` | 0x32f11 | 0x345c1 (fn 0x34530) | 0x34981 (fn 0x348f0) | `game_tick_update` is instruction-for-instruction the same in all three. `E8` rel32 targets sound_update, whose prologue is `53 56 57 80 3d`. |
| `sound_update` | 0x494b0 | **0x55100** (fm.csv: 0x550fa) | **0x55630** (fm.csv: 0x5562a) | call target of the site above |
| g_state operand (`mov eax,[g_state]`) | 0x32e82 | 0x34532 → [0xae400] | 0x348f2 → [0xae3f0] | `A1` imm32. The same variable is the one `demo_save_state` reads. |
| g_cfg operand (`mov eax,[g_cfg]`) | 0x32e95 | 0x34545 → [0xae408] | 0x34905 → [0xae3f8] | `A1` imm32. The same variable is the one `demo_close` and the -roll branch use. |
| `demo_save_state(tick)` | 0x3c2c0 | 0x3e410 | 0x3e750 | prologue `83ec40 0fbf442444`, `push 0x38d09`, `mov edx,[g_state]`. Format string "movie" + "%s/gam%05d.dat". |
| `demo_save_terrain(tick)` | 0x3c430 | 0x3e580 | 0x3e8c0 | prologue `53 83ec40 0fbf5c2448`, open mode 0x222. It writes the same 4×0x10000 + 0x20000 + 0x12c2 bytes (terrain dump 0x612c2 bytes, unchanged). |
| `demo_close` | 0x3c7c0 | 0x3ec50 | 0x3ef90 | `mov eax,[g_cfg]; mov edx,[eax+9]; test; je` |
| `-roll N` branch of `config_parse` | 0x33fd9 | 0x35659 | 0x35a19 | The 29 original bytes are a unique match in each exe, preceded by `mov eax,[g_cfg]`. Config.flags +0, file +9 and movie +0xd are the same offsets. |
| `call input_changed` in `demo_record_playback_step` | 0x3c64a | 0x3eada (fn 0x3e9d0, callee 0x35710) | 0x3ee1a (fn 0x3ed10, callee 0x35ad0) | The 10 calls of the function come in the same order in all three exes. This is the 9th one (`E8` → input_changed). |
| `jne` of `dos_is_cdrom_drive` → `jmp` | 0x1001f | 0x1105f (fn **0x11050**; fm.csv: 0x1104d) | 0x1105f (fn 0x11050) | `31db 84e4 754f`. Callers are `player_cd_check_quit` (HIDDEN 0x3e065, CD 0x3dd25) only. |

The cave code is position-independent and stays inside the function's 136 bytes. It uses only
offsets that are the same in all three exes: `players` 0x340b, 0x801 per player, tick at +0x12,
`players[0].thing` at +0xa, local player at +8. That `demo_record_playback_step` addresses
`g_state+0x7413` (= `players` + 8×0x801) in all three confirms it. Record cave 135 bytes, play cave
124 bytes.

Other findings from the verification:

* The CD exe reads the HD-install folder `C:\CARPET.CD\` (sndsetup.inf, intro.pld, language.inf,
  `save\`). The in-game save is `c:/CARPET.CD/save/gam%05d.dat`. The movie dumps go to the relative
  `movie/` as in 1996.
* The CD's `LEVELS.DAT` differs from 1996's only in level 49 (2 bytes: header word 0 and footer word 0,
  0x55 vs 0x63). The control levels 0, 12 and 38 have identical level data.
* The recordings (`mviNNNNN.dat`) have the same format: the control recordings are byte-identical to
  the 1996 ones.

## Layout: 1995 GameState = 1996 GameState + 6 bytes

Every 1995 dump, from either exe, is 0x38d09 bytes. The best alignment against the 1996 dump of the
same level puts the insertion at 0x38d03, i.e. at the end. All other differences are original
pointers (Thing.next / desc / player, free and recyclable stacks), and `norm_state.py` removes them.
The code says what the 6 bytes are. HIDDEN.EXE 0x16bd8 / 0x16be4 (CD exe: same addresses) store
`u16 Config.level` at GameState+0x38d03 and `u32 [0xac5c4]` at +0x38d05, just before the in-game save
writes the state to `c:/CARPET.CD/save/gam%05d.dat` (function 0x3ea90). 0x16ce9 / 0x16cfc restore
them after a load. The 6 bytes are a save-game trailer. They are zero in all 2864 dumps made here
(nothing saves).

The second per-instance difference: the 1995 exes leave bytes of an older string after the NUL of
`players[0].name` ("Zanzamar\0rah", "...\0ius"). The checksum does not cover them.

## Dump sets produced

| directory | content |
|---|---|
| `extracted/reference/hw_gen/level00..24` | HIDDEN.EXE `-level k`, k = 0..24 (index.json `port_level` 100..124): `tick00001.gam` (state after the first tick of the freshly generated level) + `movie/{mvi,gam,map}2000k.dat` (the tick-2 snapshot pair and a 10-tick recording). 21 MB. That these are DDLEVELS levels is checked: `GameState.level` (0x2f503) differs from DDLEVELS entry k in 32..526 bytes (run-time changes) and from LEVELS entry k in 979..5386 bytes. |
| `extracted/reference/hw_level00`, `hw_level12` | idle-player playback of DDLEVELS 0 / 12: recording to tick 5000, play-run dumps every tick 2..1000, then every 10th up to 5000 (1399 dumps), terrain every 250 ticks, `tick00001.gam`, `rec_tick*.gam`. 321 / 323 MB. Level 0 has 2 players, level 12 has 8. Record-run and play-run dumps agree in the hashed state. The `rec_tick*` files differ only in `messages[].ticks` and `hit_flash`, the same record-vs-playback artefacts as in 1996. |
| `extracted/reference/cd95_gen/level00`, `12`, `38` | control: the CD CARPET.EXE, base levels 0, 12, 38, generation only |

## Control: 1995 CARPET.EXE vs 1996 carpet.exe

`norm_state.py gen/levelNN/X cd95_gen/levelNN/X --map-a ... --map-b ... --all` for X = tick00001.gam
and the snapshot `movie/gam200NN.dat`, NN = 00, 12, 38: **no difference in the hashed state**. The
only per-instance field that differs is `players[0].name` (the bytes after the NUL), plus the zero
tail. `movie/map200NN.dat` and `movie/mvi200NN.dat` are byte-identical. `reference_test` on
`cd95_gen`: "OK: 0 of 3 level references diverge". So the 1995 engine generates base levels
identically. Its many differing functions do not touch level generation, at least on these three
levels (one each of the early, mid and movie campaign, with 1, 3 and 4 players).

## Port: first divergences (current code, MC_HIDDEN_DIR = extracted CD folder)

> Historical: this section describes the port before the Hidden Worlds code (hidden_worlds_re.md) and
> the 1995 engine (port_hidden_engine1995.md) were ported. Now all 25 generations and the playbacks
> hw_level00/05/09/12/18/24 match, and cd95_level00/12 match with MC_ENGINE1995=1.

**hw_gen level 0 (port level 100)**. Thing count 492/492 and `GameState.rng` are the same, so the
level file and the generation's draws line up. Things 1..32 are identical. The first difference is
slot 33, a tree (scenery 2/0). 63 slots differ, all of them trees except three z values:

* every tree's per-thing `rng` differs, and about half of them also differ in sprite and extents:
  ref `sprite 83, ext_x/ext_y 171`, port `sprite 84, ext 1`;
* terrain, port vs the tick-2 snapshot:
  * `type`: 57469 of 65536 cells differ. The values are relabelled rather than random: ref 6 is port
    3 on 51954 cells (also 60/61/63), ref 76..83 is port 44..51, and ref 109..111 is port 114..118.
  * `flags`: 62649 cells differ, mostly ref = port + 3 in the low bits (6/3, 22/19, 38/35, 54/51, ...).
  * `height`: 2494 cells, small deltas (±1..4) in two patches (x 136..245, y 13..127 and y 160..221).
  * `light`: 13 cells (x 214..215, y 64..71).
  * This points at HIDDEN.EXE's new terrain / texturing pass, and at a set-1 sprite table (tree
    sprites and extents from mspr1/tmaps1).

**All 25 generations** (`reference_test ... hw_gen`, "25 of 25 diverge"). The recurring patterns:

* Every level that has it: spell 20 (Fire wall) Things have `damage 5000 / duration 26 / mana_cost
  60000 / mana 192` in HIDDEN.EXE vs `24464 / 51 / 12000 / 98` in the port. HIDDEN.EXE has
  different spell parameters (or a different spell 20).
* Trees: `rng` (+ sprite 83/84, ext 171/1), on levels 0, 1, 2, 3, 4, 6, 10, 11, 13, 14 and 18.
* `z` of creatures and effects, which follows from the height differences.
* Terrain type / flags differ on every level except 9; height on 17 of 25.
* DDLEVELS 9 is the only level with identical terrain: only its 5 Fire-wall spells differ.
* DDLEVELS 21: the Things are identical, and only type (511) and flags (479) cells differ.

**Playback hw_level00 / hw_level12**: the first divergent tick is **3**. The AI wizard's flyer
(player Thing cls 3 type 1) has `speed_cur 160` in HIDDEN.EXE vs 80 in the port. Its spell Thing
(cls 12 type 2, state 6) has `flags 0x85` vs `0x05`, and the wizard's PlayerRec (player 1, +0x45b =
P+0xc) differs from the same tick. On level 12 one wizard shows the same pattern at tick 3 and five more at tick 5.
Effects follow from tick 5 to 8. The terrain first differs at tick 500 (the first terrain dump after
tick 250, which still matched). Level 0 ends with 262 divergences over 1399 compared ticks, level 12
with 928. HIDDEN.EXE's AI wizards apparently fly twice as fast.
