# Per-tick reference from the original (task E, port round 4, 2026-10-07)

Per-tick GameState dumps of the retail `carpet.exe` playing movie 0 in the bundled DOSBox 0.74-3, and a
port-side test that replays the same movie and reports where each Thing slot first diverges.

Files:

| file | what |
|---|---|
| `tools/reference/make_refgame.py` | copies `MagicCarpet/magic` + DOSBox.exe / SDL dlls to `extracted/refgame/`, then patches |
| `tools/reference/patch_carpet.py` | writes the patched `extracted/refgame/magic/carpet.exe` (4 patches, below); `--verify` disassembles them |
| `tools/reference/run_reference.py` | writes `extracted/refgame/dosbox_ref.conf`, runs DOSBox headless, moves the dumps to `extracted/reference/movie0/` + `index.json` |
| `tools/reference/dump_info.py` | per-dump summary (tick, players, census) and `--diff A B` between two dumps, no port needed |
| `tools/reference/compare_reference.py` | runs `reference_test.exe`, prints per-tick lines and the first divergence grouped by class / type / state |
| `src/tests/reference_test.cpp` + `.cmake` | `mc_unit_test(reference_test tests/reference_test.cpp ${MC_SIM_ALL})`, built in `build_E` |
| `extracted/reference/movie0/tick%05d.gam` | raw GameState (0x38d03 bytes, original pointers), **every tick 413..1013 (601 files)** and every 10th tick 1020..8960 (795 files), 1396 in all, 345 MB |
| `extracted/reference/movie0/tick%05d.map` | `demo_save_terrain_3c430` layout (0x612c2 bytes: 4 maps, cell heads, corner table), every 100th tick (85 files) |

## Approach: option 1, a patched copy of carpet.exe

Chosen because it needs nothing downloaded, uses the game's own `demo_save_state_3c2c0` (exactly the
format `sim_load_snapshot` / `demo_load_state` read; it is **not** RNC-compressed - `file_save_61db0`
writes the raw block, only the loader accepts both) and the bundled DOSBox runs it headless at full
speed. Options 2 (DOSBox-X debugger) and 3 (DOSBox source patch) were not needed and not tried; both
would need a download / build, and the debugger needs an interactive console.

Patches (all in the copy, no new fixup records, `patch_carpet.py` refuses to touch a fixup source and
checks the original bytes):

1. **Dump cave.** `game_tick_update_32e80` calls `sound_update_494b0` at 0x32f11 right after the
   thing-update passes; the call goes to a 124-byte cave written over the dead
   `dead_castle_site_search_11cef` (0x11cef..0x11d6b; the dead block's only fixup is at 0x11d77).
   The cave is position independent: `call $+5; pop esi` gives the load delta, and the run-time
   addresses of `g_cfg` / `g_state` are read from the already relocated operands at 0x32e95 / 0x32e82
   of game_tick_update itself. It does `pushad`, and while Config.flags & 4 and the demo handle
   Config+9 is open, takes the local PlayerRec.tick and calls `demo_save_state_3c2c0(tick)` (and
   `demo_save_terrain_3c430(tick)` when tick % 100 == 0) according to the policy (every tick <= 1013,
   then tick % 10 == 0), `popad`, `jmp sound_update_494b0`. Only reads code memory, so the read-only
   code object question never arises. **Dump point = end of the port's `game_tick_sim()`** (after
   `thing_update_all`, before sound / render), confirmed by the first comparison: 448 of 449 slots
   identical at tick 413.
2. **Start the movie from the command line.** The retail game plays movies only from the main menu's
   attract mode (3 idle periods of 0x12c0 timer ticks; `fe_screen_main_menu` sets Config.movie = 0,
   flags |= 0x24). `movie N` is no option at all: options need a `-` or `/` prefix
   (`carpet movie 0` prints "ERROR : Incorrect command : 1" and quits), `-movie N` only stores N
   (and ignores 0) and `-roll N` sets flags |= 0x120 without the playback bit. The 29 bytes of the
   `-roll` branch of config_parse_33750 (0x33fd9..0x33ff5) become `Config.movie = N - 1; flags |=
   0x124` (0x100 skips the front end, 0x20 blocks recording, 4 = playback). `carpet -roll 1 -level 38`
   then generates level 38 and the first tick loads the snapshot pair - no key press, no front end.
   **ENGINE.md "Command line" correction**: options are `-x` / `/x`; `movie n` sets cfg+0xd only when
   n != 0; `roll n` sets cfg+0xf and 0x120 but nothing enables playback; the shipped attract mode is
   the only movie path.
3. **`input_changed_34090` call in `demo_record_playback_step_3c540` (0x3c64a) -> `xor eax, eax`**, so
   a mouse event from DOSBox cannot end playback (not the cause of the early stop below, kept as a
   safety net).
4. **CD check.** `dos_is_cdrom_drive_10010` always answers "CD" (0x1001f `jne` -> `jmp`). Without it
   the copy-protection probe `player_cd_check_quit_3bbd0` (called from `player_set_input_mode_3bb50`,
   i.e. on the movie's spell-book packets, every 8th tick) sets PlayerRec.quit = 2 on the hard-disk
   mount and the movie ended at tick 587 (the 0x15 packet of tick 588). The copy has to be a
   writable hard-disk mount for the dumps, so mounting it as a CD is no alternative. The probe has no
   GameState side effect besides `quit`, so the patch does not change the simulation.

DOSBox: own conf (`extracted/refgame/dosbox_ref.conf`, generated): windowed surface output,
`cycles=max`, SB16 emulated (the game's `CARPET.CD/SNDSETUP.INF` names it) but `nosound=true`, and
`SDL_VIDEODRIVER=dummy` / `SDL_AUDIODRIVER=dummy` - no window, no key press. Autoexec: mount the copy,
`carpet -roll 1 -level 38`, `exit`. After the movie the game does not return to DOS (status 8 -> the
level loop goes back towards the front end), so run_reference.py kills DOSBox after 30 s without a new
dump.

## Timings

- DOSBox run of the whole movie (8551 ticks, 1396 + 85 dumps): **~24 s** of dumping, plus the idle
  timeout (index.json says 144 s with the 120 s idle used then; 30 s is now the default).
- `reference_test` over all 1393 compared ticks: ~1-2 s (Debug).
- The first 600 ticks every tick = 140 MB; every tick of the whole movie would be ~2 GB, hence the
  stride. Change the policy with `patch_carpet.py --every-until / --stride / --stride-until
  / --terrain-stride`.

## How to regenerate

```
cd "C:/Magic Carpet/tools/reference"
python make_refgame.py            # copy + patch (MagicCarpet/ is only read)
python run_reference.py           # -> extracted/reference/movie0/tick*.gam, *.map, index.json
python dump_info.py --ticks 413,1013,8960
cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_E
cmake --build ../build_E --config Debug --target reference_test
cd "C:/Magic Carpet/tools/reference" && python compare_reference.py [--last-tick 1013]
```

`reference_test.exe <game dir> [ref dir] [last tick] [csv]` prints, per compared tick, live slots
identical / live (list links `next` / `cell_next` / `cell_prev` reported separately), per-class
differing / live, the GameState RNG, a `=`/`x` per PlayerRec, and port / reference Thing counts; at
the terrain ticks the number of differing map cells and cell-list heads; then the first divergence
per slot (tick, reference class / type / state -> port, first differing field with both values,
byte count), sorted by tick, and the per-slot CSV. The dumps are converted with
`thing_relink_snapshot` (the same conversion as for gam00000.dat). Exit code 0 unless the reference
is missing.

## First findings (port state of 2026-10-07 afternoon, with the round-4 work of the other agents
## as it was on disk at build time - re-run after integration)

- **The global RNG (GameState+4) matches for the whole movie** (all 1393 compared ticks), although
  Things diverge from tick 413 on: game-level RNG draws per tick do not depend on the divergent Thing
  logic. Thing counts are equal until tick 432.
- Tick 413 (first replayed tick): 448 / 449 identical. The one slot: the **castle** (class 3 type 2
  state 4) slot 895, `ext_x` 1664 in the original vs 1152 in the port; tick 414 the second castle
  (slot 485: 3328 vs 1920). Castle extents (`thing_set_castle_extents_353f0` / the castle size table
  0xadfb0, or `g_wizard_castle_capacity_shift`: the retail exe here is the reference, not the
  recording build) are the first divergence.
- Next, in order: tick 420 creature type 12 state 72 `aux` 4 vs 2; tick 433 creature
  type 9 state 56 -> port state 55, `aux`, and projectiles type 13 that are still alive in the
  original but already freed in the port (first live-count difference, 433); tick 439 the original
  creates a class 3 type 2 state 5 thing in slot 10 (castle construction of an AI wizard) that the
  port does not, and a castle spell (class 12 type 16 state 48) has `mana_total` 5000 vs 10000; tick
  444 the AI wizards' (class 3 type 1) `mana_cost` 1000 vs 100; tick 501 mana balls (effect 39 / 41)
  `dmg4.attacker` / `mana_owner`.
- Player records: player 3 first differs at tick 439 (P+0x32), player 0 at 442 (P+0x34c
  spell_flash), player 1 at 513, player 2 at 537 (P+0x14b invulnerability timer).
- First diverging tick per class: player 413, creature 420, projectile 433, spell 439, effect 440,
  switch 1110, scenery 3950. At tick 1000: 374 / 423 slots identical (scenery 150/150, creatures
  146/165, effects 36/48); at 2000: 244 / 455; at 8900: 16 / 433 (the worlds have separated).
- Maps: 59 cells differ at tick 500, 995 at 1000 (castle building / terrain spells).
- `thing_dispatch_report`: 0 handlers dispatched without a port during the movie.

So the order to chase is: castle extents (tick 413), the AI wizard's mana cost / castle spell
(439-444), then creature type 12 / type 9 `aux` and projectile lifetime (420-433).

## Not done / caveats

- Only movie 0 exists in the package; the scripts take `--movie N` for others.
- The dumps hold original pointers (free / active stacks, Thing.next / desc / player, and any pointer
  inside the P blocks); `reference_test` converts the Thing fields and stacks via
  `thing_relink_snapshot`, but PlayerRec bytes are compared raw, so a PlayerRec pointer field would
  always show `x` (none seen in the first 25 ticks: all eight records match).
- Not per tick past 1013 (stride 10); not RNC-compressed (the game's save writes raw).
- No shared files were changed. No downloads.
