# Port round 5 briefing (2026-10-07): sound output, front end, FLI / palette, game flow, more references, render reference, naming debt

Read `docs/port/PORTING.md` first (sources of truth, code conventions, report format), then
`docs/port/BRIEFING_round4.md` (rules, verification ladder). The round-4 rules apply unchanged except
where this file says otherwise. The engine is described in `docs/ENGINE.md`; the per-subsystem reports of
rounds 2-4 are `docs/analysis/port_*.md` (they show the expected depth of a report);
`docs/ROADMAP.md` has the plan and status.

## State at the start of round 5

- Every Thing handler of the game is ported (rounds 1-4). `reference_test` (a patched carpet.exe in
  DOSBox dumped the GameState every tick of movie 0, `docs/analysis/port_reference.md`) shows **every
  Thing, every player record and the RNG byte-identical over the whole movie**. It is a ctest gate. All
  25 tests pass (`cd build && ctest -C Debug`).
- `mcport <dir> play <level>` flies one level with the original controls and HUD in the original's
  320x200 mode (`src/mcport/main.cpp`). There is no sound output, no front end, no level-to-level flow,
  no saves.
- The game-side sound manager is done (`sound.h` / `sound.cpp`, `port_sound.md`): everything below the
  HMI API is `struct SoundBackend` (null default). Nothing implements it yet.
- New placeholders, already compiled through the `mcengine` glob (`src/mcengine/*.cpp`) or the new
  `mcport` glob (`src/mcport/*.cpp`, mcport now also links `winmm` on Windows):
  `frontend.h/.cpp`, `savegame.h/.cpp` (B), `fli.h/.cpp`, `palette_fx.h/.cpp` (C), `game.h/.cpp` (D),
  `mcport/audio_sdl.h/.cpp`, `mcdata/hmp.h/.c` (A; `hmp.c` is in the mcdata library).
  **`fli.h`, `palette_fx.h`, `frontend.h` and `game.h` contain contracts between tasks**: the
  declarations in them are fixed for this round, the owner implements them and may add more; nobody
  changes an existing signature.

## Rules (round 4 rules, plus what is new)

- Seven agents work in the same tree at the same time (no git). **Only create / modify the files your
  task names.** Everything else is read-only; a wanted change to a shared file goes into your report
  (work around it locally with a `static` helper or a hook declared in your own header).
- Shared files nobody may edit this round (the integrator does it afterwards): `src/CMakeLists.txt`,
  `engine.h/.cpp`, `sim.h/.cpp`, `sim_all.cpp`, `mc_types.h`, `mc_globals.h/.cpp`, `thing.h`,
  `player.h`, `gen/*` (regenerated only by the integrator), `src/mcport/main.cpp`,
  `src/mcport/platform*.{h,cpp}`, `docs/ROADMAP.md`, `README.md`, `docs/port/PORTING.md`.
- Ownership of existing files for **bug fixes** (only fixes backed by evidence from the disassembly /
  a reference; no refactors, no API changes):
  - task E: every game-logic file in `${MC_SIM_ALL}` except `sound.cpp` (`player.cpp`, `thing.cpp`,
    `creature*.cpp`, `ai_wizard.cpp`, `projectiles.cpp`, `spells.cpp`, `effects.cpp`, `castle.cpp`,
    `scenery.cpp`, `spatial.cpp`, `constructors.cpp`, `level_features.cpp`, `terrain_*.cpp`,
    `input.cpp`, `demo.cpp`, `text.cpp`);
  - task F: the renderer (`render_landscape.cpp`, `render_things.cpp`, `sprite_cache.cpp`,
    `raster.cpp`, `tables.cpp`, `hud.cpp`, `ui_draw.cpp`);
  - task A: `sound.h/.cpp`, `mcdata/sndbank.h/.c`.
  Any fix by E or F must keep `reference_test` on movie 0 byte-identical (run it after every change;
  F: `hud_tick_state` writes game state).
- Build in **your own build directory** `build_X` (X = your letter; G builds nothing):
  `cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_X`, then
  `cmake --build ../build_X --config Debug --target <your targets>`. Build only your own targets.
  `mc_test(...)` links the whole `mcengine` library, which contains the other agents' half-written
  files: prefer `mc_unit_test(name test.cpp <explicit source list>)` (as `sound_test` does with
  `${MC_SIM_ALL}`); list the renderer / UI sources explicitly when you need them. Retry when a build
  trips over another agent's file mid-edit, and say so in the report if it persists. Zero warnings (/W4).
- Your test: `src/tests/<name>_test.cpp` + `src/tests/<name>_test.cmake` (create the .cpp before the
  .cmake, re-run the configure line after adding it). Exit 0 = pass; SKIP (exit 0 with a message) when
  data that is not in the package (DOSBox dumps) is missing.
- Scratch: **your own subdirectory** `<scratchpad>/round5_X/` of the session scratchpad
  `<scratchpad>`.
- `tools/analysis/mc.py body|tree|callers|strs <addr>` and `tools/analysis/img.py dis|dwords|bytes`
  are the sources (run from `tools/analysis`, never with `C:\Magic Carpet` as the working directory: the
  `ghidra/` folder shadows a package). Task G refreshes `ghidra/export/` during the round; addresses do
  not change, only names / comments.
- Bash pitfalls: `\n` inside heredoc'd python / C strings becomes a real newline - write sources and
  scripts with Write / Edit and run scripts from files.
- The original game files under `MagicCarpet/` are never modified. Writable copies go under
  `extracted/` (per task, below) or your scratch directory. Saves never go into `MagicCarpet/`.
- No floating point in translated game logic (platform code - mixer, synth - may use it).

## Deliverable (as before)

Your sources, your test (exit 0, zero warnings), and `docs/analysis/port_<name>.md` in the PORTING.md
format: functions translated (name + address), verification (what, how, numbers), deviations / gaps,
hooks declared or installed, extra functions, TODO(port) call sites, **requested shared-file changes
(exact code to add, file and place - the integrator applies them)**, corrections to `docs/ENGINE.md` /
`mc_types.h` / `carpet_types.txt` / names. Your final message is a short summary of that report.

## Tasks

### A - sound output (`src/mcport/audio_*.h/.cpp`, `src/mcdata/hmp.h/.c`, fixes in `sound.h/.cpp` + `mcdata/sndbank.*`, `tests/audio_test.*`, report `port_audio.md`, build dir `build_A`)

Implement `SoundBackend` (sound.h) for the platform, and music.
- Split: **`src/mcport/audio_mixer.h/.cpp` without any SDL include** (the software mixer and the music
  sequencer, testable from a unit test that lists the file) and `src/mcport/audio_sdl.h/.cpp` (the thin
  SDL audio device + MIDI output layer). Every `.cpp` in `src/mcport/` is compiled into mcport.
- Digital: 32 voices of 8-bit unsigned mono PCM at the bank's rate (22050 Hz for quality 1, 11025 Hz
  for 0 / 3), volume 0..0x7fff, pan 0..0xffff with 0x7fff centre, loop counts (-1 forever), the HMI
  flags word the game passes (0x200 panning, 0x4000 looping, 0x100 ...: read `port_sound.md`
  "Deviations" and decide from the HMI SOS semantics in ENGINE.md "HMI sound drivers"), `is_playing`,
  `set_volume`, `stop`. Mix to the device format SDL gives you (resample with at least linear
  interpolation), no clicks on stop (short ramp is fine - platform code).
- Music: `mcdata/hmp.c` parses HMP ("HMIMIDIP" files inside `music<set>-<device>.dat`, see
  `music_load_bank`) into tracks of timed MIDI events (delta encoding, tempo / division, loop
  markers, whatever the HMI format has - find it from the HMI driver code in the exe, ENGINE.md
  "HMI sound drivers" / agent2_E / agent3_H notes, and the files themselves). Bank device suffixes:
  0 General MIDI, 1 OPL2 FM, 2 AWE32 (sound.h). Play the General MIDI bank through the Windows MIDI
  mapper (`midiOutOpen(MIDI_MAPPER)`, winmm is linked) from a sequencer driven by a timer thread or
  the audio callback; honour `music_set_layer_volume` (controller 7 on channels 3..5), master volume,
  `music_done`, looping as the backend contract in `sound.h` asks. Design the sequencer so a software
  OPL / soft-synth output can replace winmm later (an interface with `send(status, d1, d2)`).
- Platform timers: `music_fade_tick(which)` at `music_fade_rate(which)` Hz.
- Startup: what the platform must set (`g_sound_available`, `g_music_available`, `g_sound_on`,
  `g_music_on`, `g_sound_quality`, `g_music_device`) - expose one `audio_init(game_dir)` /
  `audio_shutdown()` for the integrator.
- Verification: `audio_test` (mc_unit_test with `${MC_SIM_ALL}` + `mcport/audio_mixer.cpp`, no SDL):
  mixer by construction (volume / pan law, loop, stop, resampling length), HMP parse of all four tracks
  of every music bank (event counts, duration in seconds, tempo), and an **offline render**: play the
  movie from the snapshot with the mixer as the backend and write the first 60 s of mixed audio to
  `<scratch>/round5_A/movie0.wav`, and each level track rendered through a trivial square-wave synth
  (or just the event dump) - the user will listen. Do not edit mcport/main.cpp: put the integration
  code (exact lines) into the report.

### B - front end and save games (`frontend.h/.cpp`, `savegame.h/.cpp`, new `fe_*.cpp` files if you want, `tables/frontend.tables`, `tests/frontend_test.*`, report `port_frontend.md`, build dir `build_B`)

`frontend_menu_loop_52070` and its screens (ENGINE.md "Front-end state machine", agent_fe_crt.md,
agent2_D "Front end helpers"): language (6), config (1: input device only - the sound-setup wizard is
skipped as the original skips it when sndsetup.inf exists), main menu (2) with all items (new game /
resume, load, save, quit, start level, the six save slots and the slot-name dialog), level result (5:
pperf.dat, level name, stats from `player_compute_level_stats_3ef10` / the GameState - task D computes
them, read them from where the original reads them), multiplayer (4: draw the screen, but only "back"
needs to work; no network), the attract counter (intro / title / demo level), the logo / title / intro
/ outro screens (0, 7, 8, 9, 10) through the FLI player API in `fli.h` (task C implements it; until
then it returns nullptr - your screens must skip to the next state when `fli_open` fails), palette fades
through `palette_fx.h` (task C), mouse pointer sprites (sptrs.dat, `mouse_cursor_set_sprite_5ba5c`;
the port draws the pointer into the frame buffer at the end of `fe_frame`), text through `ui_draw.h`.
- Input: the device state of `input.h` (`g_mouse_x / y` in 640-wide virtual coordinates, buttons, the
  key buffer) - the same globals mcport already feeds. Timing: `now_ticks` (119.06 Hz).
- The 320x200 path (`g_video_mode_flags & 1`, what mcport runs) must be complete; the 640x480 path
  where it is cheap (same code with the doubled tables), otherwise TODO(port).
- Save games: `savegame.h` - read / write the original format (`save\carpet%02X.gam`, ENGINE.md),
  into a directory the platform gives you (`fe_set_save_dir(path)` or similar; default: none =
  saving disabled); loading may also read DOS saves from `MagicCarpet/magic/save` read-only.
- Campaign start: "new game" / "start level" -> `FE_START_LEVEL` with the level index the original
  would start; the campaign progress block (GameState+0x3bd6, DAT_0012ed30/31) is shared with task D:
  front-end globals are yours (define them in frontend.h with their DAT names); D reads them through
  your header (agree by the contract: D only calls what is in frontend.h at the start of the round plus
  what you add - D will look at your header during the round).
- Verification: `frontend_test` renders each screen (320x200) to `build_B/Debug/fe_<state>.ppm` with
  scripted input (click main-menu items by their hit mask, open the load dialog, go back), save ->
  load round trip in the scratch dir, and loading any DOS save you can construct from the format;
  look at the PPMs yourself (Read renders images).

### C - FLI player, cue scripts, palette (`fli.h/.cpp`, `palette_fx.h/.cpp`, `mcdata/flic.h/.c` if you want a C decoder, `tests/fli_test.*`, report `port_fli.md`, build dir `build_C`)

- FLI: `fli_play_508f0` / `fli_next_frame_50dfd` / `fli_decode_frame_50e88` (chunks 4 COLOR256,
  7 SS2, 15 BRUN, and whatever else the files use: COLOR64 11, LC 12, BLACK 13, COPY 16, PSTAMP 18)
  / `fli_present_frame_50600`, the asm player `fli_file_play_5c264` (which files use which player?),
  pacing (DAT_0012eab4). Implement `fli.h` exactly (the contract with task B).
- Cue scripts: `fli_event_script_17d80` / `cue_script_step_17d80` - the 7-byte event records that
  start sounds, speech (sample banks `snds1..13`) and music at given frames. Find where the scripts live
  (exe tables -> `tables/fli.tables`), which FLI uses which script / bank, and expose
  `fli_set_cue_script(...)` (or attach them automatically in `fli_open` by file name) that calls
  `sound.h` (`sound_load_bank`, `sound_play_sample_loud`, `music_play_track`, ...).
- Palette: implement `palette_fx.h` (contract): the display palette, the stepped fader (the original
  `vga_palette_fade_61510` steps towards the target / black with vsync waits; reproduce its step
  arithmetic per frame), `palette_effect_update_33010` (the in-game flashes driven by
  `Config.palette_effect`, see `player_set_palette_effect` in player.cpp; called by the original
  from game_tick_update_32e80 when mode_3d == 0 - player.cpp:1201 has the TODO; export
  `palette_effect_update()` and ask the integrator for the call), `mapmode_palette_save_30350` /
  `mapmode_palette_restore_303b0`, and `title_screen_show_32db0` if it is just an image + fade.
- Verification: `fli_test` decodes every frame of every `intro/*.dat` (and any other FLIC the game
  plays): frame counts, sizes, chunk-type census, no unknown chunk, writes a few frames per file as
  PPM into `build_C/Debug/` (look at them), checks the palette of frame 0; the fader by construction
  (step values after n steps against the original's arithmetic); `palette_effect_update` by
  construction for each effect value.

### D - game flow (`game.h/.cpp`, `tests/game_test.*`, report `port_game.md`, build dir `build_D`)

The in-level part of `game_main_32a00` and what it calls that is not ported: `level_load_and_init_3d3b0`
is in engine.cpp (`engine_load_level`) - call it, do not copy it; the level-start code around it
(sound / music bank loads and the level track - `sound.h` already has `sound_load_bank`,
`music_load_bank`, `music_start_level`; `input_mouse_center_4a000`), the in-level loop with the
player-status state machine (`PlayerRec.status`, bits 2 / 4 / 8, player.cpp writes them: win timer,
quit, death), `level_finish_3d4e0`, `player_compute_level_stats_3ef10` (the numbers the result screen
shows), `level_skip_number_329c0`, campaign progression (which level comes next after a win, the
GameState+0x3bd6 block, password / `cheat` handling as far as the campaign needs it, level 50 ->
outro), `sound_stop_all` / `music_stop` at level end, and `video_toggle_resolution_33600` (320x200 <->
640x480 at run time: which game state it rewrites; the renderer / UI side is `ui_draw_set_video_mode`;
the platform frame buffer is the integrator's - export what mcport must call).
- Implement `game.h` (contract). Also document precisely **how the original paces its loop** (one
  simulation tick per rendered frame? a timer-based limiter? what F3 "game speed" changes) so the
  integrator can implement the same pacing in mcport.
- The front-end globals belong to task B (frontend.h); use them through that header.
- Verification: `game_test` (mc_unit_test with `${MC_SIM_ALL}`): level 0 started through
  `game_level_begin`, a scripted win (set what the win check tests, e.g. the mana target, through
  game state only) gives `GAME_LEVEL_WON` after the original's delay, `game_level_end` produces stats
  you can check by hand, campaign progress advances; a loss / quit path; 3 consecutive levels started
  and ended without leaks (`check_pool`-style sanity, free list intact).

### E - more per-tick references, and fix what they show (`tools/reference/*` incl. a new level mode, `tests/reference_test.*` + any new `tests/reference_*_test.*`, game-logic fixes per the ownership list above, report `port_reference2.md`, build dir `build_E`, DOSBox copy `extracted/refgame/`)

Movie 0 exercises only part of the game: the creature types kraken, griffon attack, genie, troll, crab,
emu, type 15, wyvern, the AI on other levels and the castle of other wizards have no ground truth.
- Get per-tick dumps of the original for other levels **without a human**: e.g. a patch that starts
  `-level N` straight into the level (no front end) with the dump cave active from tick 0 and the local
  player idle (or computer-controlled: the main menu's attract "demo level" sets cfg.flags |= 0x24,
  cfg+0xa1 = 3, cfg+0xa2 = 200 - find out what it does), or record movies with the game's own recorder
  (Config bit 2) and replay them. Handle the level-start randomness (what seeds it? a timer read? then
  dump tick 0 and start the port from that dump as from a snapshot, or patch the seed).
- Port side: generalise `reference_test` (or add `reference_level_test`) to start from the level
  dump / snapshot of tick 0 (or from `sim_load_level` if the generation is deterministic) and compare
  every tick. Movie 0 must keep working unchanged (it is the ctest gate).
- Levels: at least one each for kraken + crab + emu + type 15 + wyvern (44), troll (24), griffon (12),
  a genie level (scan the THING_INIT types), and two early campaign levels (0, 1); a few thousand ticks
  each (budget the disk: movie0_full is 1.9 GB; use a stride after the first 1000 ticks).
- Then chase the first divergences with the traces and the disassembly and **fix them** in the game
  logic (ownership list above); keep movie 0 byte-identical; report per level the first divergent tick
  before / after and what each fix was.

### F - render reference: pixel comparison against the original (`tools/reference/fb/*` (new), `tests/render_reference_test.*`, renderer fixes per the ownership list, report `port_render_reference.md`, build dir `build_F`, DOSBox copy `extracted/refgame_fb/`)

- Extend a **copy** of the reference setup (copy `tools/reference/make_refgame.py` / `patch_carpet.py` /
  `run_reference.py` into `tools/reference/fb/` at the start and work on the copies: task E edits the
  originals during the round) so the original also dumps what it **drew**: the 320x200 back buffer
  (`DAT_0012ed74`, pitch `DAT_0012ed70`) and the DAC palette at the end of `render_frame_1fab0` / before
  the blit, every tick for a window of movie 0 (e.g. 413..1013) and sparser after. Separate copy
  `extracted/refgame_fb/`, separate output `extracted/reference/movie0_fb/`.
- `render_reference_test`: for each dumped tick N load the matching GameState dump N (the existing
  `extracted/reference/movie0_dense` / `movie0` sets come from the same deterministic run - verify that)
  or replay the port to N, run what the original's render does (`hud_tick_state` + `render_frame_draw`),
  and compare pixel by pixel: counts, first differing rows, a diff image (PPM) for the worst ticks.
  Renderer state that carries over between frames (sprite cache, animation counters, water phase, any
  RNG used by the renderer) must be accounted for - find out what there is.
- Then fix the differences in the renderer files (ownership list), biggest first (landscape, sky,
  things, HUD), and report the match per region before / after. The free-camera viewer and the HUD
  test images must still work.

### G - naming debt and ENGINE.md (`ghidra/names/*`, `ghidra/scripts/*` if needed, `docs/ENGINE.md`, `docs/FORMATS.md`, `ghidra/export/*`, report `docs/analysis/names_round5.md`; no build dir, no `src/` changes)

- Merge into `docs/ENGINE.md` the "Corrections to ENGINE.md" sections of the round 2-4 port reports
  (`docs/analysis/port_*.md`) that are not there yet, and the facts the per-tick reference settled
  (`port_reference.md`, ROADMAP Phase 2 round-4 entry). Mark superseded statements as corrected rather
  than silently deleting them where a reader might have relied on them. Same for `FORMATS.md`
  (sound / music banks, save game, demo files).
- Naming debt (ROADMAP Phase 2 "Naming debt"): creature state handlers named after the previous type
  (`type * 6 + {0 own, 1 main, 2 attack, 3 follow, 4 dying, 5 dead}`, lists in port_creatures*.md),
  class-10 names off from state 0x17, projectile types 14..19 running states 15..20, castle handlers
  413a0 / 41500 / 416d0 named `player_flyer*`, `crab_target_nearest_mana_ball_1aef0`, and every name
  correction listed in the port reports (e.g. `music_mood_fade_cb_1f6d0`). Fix them in
  `carpet_names.csv` (curated wins over `carpet_names_tables.csv`; if the tables CSV is generated, fix
  the generator input too and say how to regenerate).
- Apply with the headless chain (README "Ghidra (headless) workflow": ApplyNames on the tables CSV,
  the curated CSV, the labels CSV, then ExportAll). Only you run Ghidra this round. **Export into a
  temporary directory and copy the files over `ghidra/export/` at the end in one step** - the other
  agents read `ghidra/export/carpet_all.c` through mc.py while you work.
- Do not touch `src/` (the integrator regenerates `gen/dispatch_tables.h` from the new names; tell
  the integrator the command).
