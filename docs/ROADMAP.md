# Magic Carpet 1 (1994) decompilation and native Windows x64 port

## Goal

1. Recover the game logic of `carpet.exe` (Watcom C, DOS/4GW, 1994) as readable C/C++.
2. Build a native Windows x64 executable that plays the original campaign from the
   original data files, with modern quality-of-life changes:
   - draw distance raised massively (the DOS renderer culls at a few hundred units);
   - simulation decoupled from frame rate (fixed-timestep logic, interpolated render);
   - arbitrary resolution, modern input, no 1000-slot entity pool limit.
3. Then: a battle-royale style multiplayer mode on a large map with events and
   buildable castles.

## Prior art we build on (do not reinvent)

| Project | What it gives us |
|---|---|
| `thobbsinteractive/magic-carpet-2-hd` (remc2) | Full asm-to-C++ reconstruction of **Magic Carpet 2**. Same engine lineage; function naming (`sub_XXXXX`), data structures, renderer and sound layers are directly reusable as a Rosetta stone when reading MC1 decompilation. |
| `raincz/mgcarpet` (Rust, GPL-3) | From-scratch engine that plays MC1 end to end, verified bit-exact against retail recordings. Its gameplay rules, terrain generator and entity behaviours are a second reference for MC1 semantics. |
| `michaelhoward/MagicCarpetFileFormat` | Level file format (copied to `docs/reference`). |
| `Moburma/MCLevelReader`, `MCDatExtractor` | Level tables, entity names. |
| `lab313ru/rnc_propack_source`, PyPI `propack` | RNC compression reference; we validate our decoder against it. |

Licensing note: remc2 / MC2-HD and mgcarpet are GPL. Reading them to understand
behaviour is fine; copying code into this project makes this project GPL. Decide
the licence before importing any code.

## Toolchain (installed and working)

- Ghidra 12.0.4 at `C:\tools\ghidra_12.0.4_PUBLIC` with
  - `ghidra-lx-loader` 12.0.1 extension (DOS/4GW LE loader) in `Ghidra/Extensions`;
  - a custom **Watcom register calling convention** `x86watcom.cspec`
    (`__watcall`: EAX, EDX, EBX, ECX, then stack) registered in `x86.ldefs` as id `watcom`.
- Ghidra project `ghidra/project/MC1` containing `carpet.exe` and `main.exe`
  (both LE, x86:LE:32, cspec watcom, auto-analysed).
- `ghidra/scripts/ExportAll.java`: headless export of functions, call graph,
  strings, decompiled C -> `ghidra/export/`.
- `tools/mctools` (Python 3.13): `rnc`, `lefile`, `dattab`, `level`, `tmaps`,
  `sprites`, `palette`, `extract`.
- Compilers available for the port: MSVC (VS 18 Community/BuildTools, VS 2022),
  MinGW gcc 15.2, CMake 4.3, Ninja. No Rust toolchain installed.

## Phases

### Phase 0 - tooling and data (DONE)
- [x] Identify binaries: `magic/carpet.exe` is the launched retail game
      (DOSBox runs `carpet`); `magic/data/main.exe` is a second build
      (has Gravis/Adlib Gold strings, lacks the credits) - keep for diffing.
- [x] RNC method-1 decoder validated on all 245 compressed files.
- [x] LE parser: memory map, fixups (15,930 in carpet.exe), flat relocated image.
- [x] Full data extraction to `extracted/` with manifest.
- [x] Levels: 70 levels parsed, GEN_MAP + THING_INIT, verified against loose files.
- [x] tmaps.dat container (528 RNC chunks, 10-byte tab entries) decoded to PNG.
- [x] Ghidra import with Watcom cspec; export pipeline.
- [x] Coverage: 2,046 functions (94% of game code bytes) via `FindGapFunctions.java`;
      Ghidra's Aggressive Instruction Finder hangs on this binary, do not enable it.
- [x] Watcom CRT FunctionID database built from Open Watcom 2.0 libs
      (`ghidra/fid/watcom_dos32.fidb`); it matches nothing in the 1994 runtime,
      so CRT identification must be done by string/behaviour instead.
- [x] 21 seed names applied (main, game loop, level load, terrain build, ...).
- [x] All 446 handler functions referenced only from the data-segment dispatch
      tables defined (`FixPointerTargets`); false no-return damage repaired.

### Phase 1 - understand the executable (DONE 2026-10-06, two naming rounds)
- [x] Watcom C runtime / DOS/4GW glue identified by behaviour (~250 `crt_`/`dos_`/
      `vga_`/`vesa_`/`hmi_` names: printf/scanf cores, malloc/free, file I/O via
      INT 21h, DPMI helpers, HMI SOS digital/MIDI API, Gravis driver). FID was useless.
- [x] Top-level structure mapped: tick order in `game_tick_update_32e80`
      (palette fx, texture FLIC anims, local input, per-player command
      processing, win check, Thing update x1/4/16, sound, frame render, debug
      overlay, screenshot, present). Front-end state machine
      (`frontend_menu_loop_52070` states 0..10), config options, save format,
      NetBIOS network, "movie" demo system all in `docs/ENGINE.md`.
- [x] Core data structures: Thing record (0xa4 bytes), class/model dispatch
      tables (Table A update-by-state, Table B create-by-type), spatial cell
      index (0x10DFB0), 5 terrain maps, player record, config struct, game-state
      block, vertex grid of the landscape renderer, rasteriser edge table.
- [x] Function inventory cleaned (round 2): 442 + 24 + 59 bogus functions removed
      (padding runs, tail fragments, entries on padding bytes or inside
      instructions), 54 recreated at the real entry, 124 never-created functions
      added (HMI driver thunks, dead code), the three asm ISRs defined
      (`timer_isr_4ac03`, `input_keyboard_isr_4fa28`, `mouse_event_callback_5b86c`),
      the 26 KB rasteriser `poly_fill_triangle_722e3` wired through its 30 jump
      tables (16 edge setups + 25 span fillers labelled, fill modes 0..26
      decoded). PIT rate confirmed: 0x2726 -> 119.06 Hz.
- [x] Struct types applied in Ghidra (`ghidra/names/carpet_types.txt`,
      `ApplyTypes.java`): Thing, GameState, Config, PlayerRec, CmdPacket,
      ThingInit, SpriteDesc, VertexRec, QuadStep, SpanRec, ClassEntry/TableRec,
      FontDesc; `g_state`/`g_cfg` and the big tables typed, so the C export reads
      `g_state->things[i].health`.
- [x] All 1,714 functions named (`carpet_names.csv` 1,356 curated +
      `carpet_names_tables.csv` 435 generated; three agent rounds, reports in
      `docs/analysis/agent*_*.md`). About 60 names carry an `UNCERTAIN:` comment.
- [x] Round 4 (2026-10-06 evening): `creature_check_terrain_102b0` decompiles again (two
      JMPs carried stale CALL_RETURN overrides from a deleted fragment; `ClearJumpOverrides`
      found 9 such jumps). `game_main_32a00` hits a Ghidra 12.0.4 decompiler bug: any struct
      at GameState+0x340b (`players PlayerRec[8]`, even an empty one) makes it spin on this
      function's `(p<<11)+p` indexing; `ExportAll` now retries a failed function with the
      pointer-to-struct globals untyped, so its C is in the export (marked FALLBACK). 163
      functions had an "unknown" calling convention (`FixConventions`), 9 functions sat on
      `switchdataD_` jump tables and 22 more on padding bytes in front of real entries
      (`RemoveTableFunctions`, `delete_stubs_7.txt`); the int 8 tick stub moved to its real
      entry 0x34120 and the un-inventoried asm FLI player `fli_file_play_5c264` was created.
      All 47 `UNCERTAIN:` names resolved by four agents (`docs/analysis/agent4_*.md`):
      the `snd_gus_*` driver is the AWE32 EMU8000 driver (renamed `snd_awe32_*`), skeletons
      convert villagers, creature +0x3a is an awake gate, Config+4 is the tick counter.
      Inventory: 1,685 functions, all named, none uncertain, all `__watcall`.
- [x] Leftovers (round 6, `docs/analysis/names_round6.md`): padding "flow targets" resolved; spiral ring
      tables (0xade28, 6-byte records), level names (0x97490, 101 entries), texture directory (0xb84f4) and
      the PlayerRec+0x24a camera log typed; open questions answered where the binary allows (table at the
      end of `docs/ENGINE.md`). Inventory: 1,690 functions (`repair_functions_9.txt`: the HMI song timer
      callback 0x66898 and its lock markers), all decompiled.

### Phase 2 - port (C++17, CMake, SDL2) - DONE (2026-10-06 night .. 2026-10-07, six rounds)
- [x] `src/` layout: `mcdata` (C data layer), `mcengine` (translated game, no SDL; exact-layout
      structs in `mc_types.h` with asserted offsets, pointer fields -> 32-bit indices), `mcport`
      (SDL2 window/input/present, fixed 119 Hz tick loop), per-subsystem unit tests
      (`src/tests/*_test.cpp` + `.cmake`) and the integration test `engine_test`.
      Conventions for translation work: `docs/port/PORTING.md`. Data tables that live in the exe are
      extracted by `tools/port/gen_exe_tables.py` from `src/mcengine/tables/*.tables`.
- [x] Terrain generation (`terrain_gen.cpp`, 17 functions): bit-exact against the engine's own dump
      of level 38 (`movie/map00000.dat`) on every cell not touched by level-start features
      (`terrain_test`). Report: `docs/analysis/port_terrain.md`.
- [x] Rasteriser (`raster.cpp`): poly_fill_triangle with all 27 fill modes, viewport, line drawer;
      fill convention and clipping verified by construction (`raster_test`). `port_raster.md`.
- [x] Landscape renderer (`render_landscape.cpp`): vertex grid, projection, fog, water animation,
      second surface, sky, roll table, view window, mono render_view with the post filters
      (`render_test` on synthetic data). Things are a hook (`g_render_cell_things`). `port_render.md`.
- [x] Tables and textures (`tables.cpp`): palette, block16/32 atlas + texture pointer table, UV
      rescale, tables.dat load or byte-exact regeneration of shade/blend/circle, sky. `port_tables.md`.
- [x] `engine_test` renders the demo's first camera on level 38 and one frame of every level from the
      real data (`build/Debug/engine_test_level38.ppm`); `mcport.exe [game_dir] [level]` is a
      free-fly viewer over the generated terrain (controls in `src/mcport/main.cpp`).
- [x] Round 2 (2026-10-06/07; briefing `docs/port/BRIEFING_round2.md`, reports
      `docs/analysis/port_{thing,features,constructors,sprites,player}.md`):
      - Thing core (`thing.cpp`): pool, cell lists, sprite extents, `thing_update_all`, level
        spawning, position / angle helpers; class tables extracted to `gen/dispatch_tables.h`
        (`tools/port/gen_dispatch.py`), handlers bound by original address, hooks between subsystems.
      - Level features + run-time terrain painting (`level_features.cpp`, `terrain_paint.cpp`):
        walls, paths, canyons, ridges, volcano / crater / dent, wizard castles, castle footprints.
        Level 38 matches the engine's dump on 64088 / 64969 / 64992 / 65137 of 65536 cells (height /
        light / flags / type); every remaining cell is attributed to play during the 413 ticks before
        the dump (the test replays most of those events and gets to 65509 heights).
      - All Table B constructors (`constructors.cpp`, 178 records): static things of level 38 equal
        the snapshot in all fields; all 70 levels spawn.
      - Sprite cache + Thing renderer (`sprite_cache.cpp`, `render_things.cpp`): scaled / rolled
        sprites with shadows, reflections, fog, animated sprites (FLIC frames).
      - Players + movie (`player.cpp`, `demo.cpp`): records, spawn, command packets, the flyer's
        movement / hits / death, mana totals, `game_tick_sim`, playback of `movie/mvi00000.dat`
        (all 8551 ticks). `engine_test` checks that a generated level 38 puts scenery, switches,
        wizard castles and the player things into the snapshot's pool slots and renders the movie's
        view; `mcport <dir> demo` plays it.
- [x] Round 3 (2026-10-06/07; briefing `docs/port/BRIEFING_round3.md`, reports
      `docs/analysis/port_{core3,projectiles,spells,effects,castle,creatures,input}.md`): the game now
      simulates.
      - Core (`spatial.cpp`, `sim.cpp`): collision searches, the three area-damage functions, pending
        damage slots, `cell_kill_things`, `creature_check_terrain`, castle site tests; renderer-free
        `sim_init` / `sim_load_level` / `sim_load_snapshot`, `sim_register_gameplay` (`${MC_SIM_CORE}` /
        `${MC_SIM_ALL}` in CMake), `sound_request` wrapper.
      - Projectiles (`projectiles.cpp`): all 21 class-9 records, steering, impact, target selection.
        The snapshot's 4 arrows re-fly to their recorded positions bit-exactly.
      - Spells (`spells.cpp`): all 72 class-12 records (24 spells x pickup / phase / cast), cast helpers.
        All 39 spell Things of the snapshot are reproduced byte for byte (idle paths); no cast path has
        original data to compare with.
      - Effects (`effects.cpp`): every remaining class-10 record (58 of 58 bound): explosions, fire,
        smoke, mana balls (38 of 39 comparable balls match the snapshot in position and velocity),
        teleport, meteor / crater chain, castle levelling and raising, spell area effects.
      - Castles, balloons (`castle.cpp`), scenery and switches (`scenery.cpp`): all 150 trees and the
        5 switches of the snapshot reproduced; castle upgrade equals the snapshot's level-2 castle.
      - Creatures (`creature_common.cpp`, `creatures.cpp`): shared movement / AI / attack / death code,
        wake timers, and skeleton, builder, townie, trader, dragon, vulture, bee, worm, archer.
      - Local input (`input.cpp`): the original's key / mouse -> command packet code with the device
        state fed by the platform; reproduces all 8551 recorded packets of the human player.
      - Integration (`sim_test`): level 38 run for the 412 ticks before the engine's snapshot gives
        347 of 449 Things byte-identical in the same pool slot (scenery 150/150, creatures 128/169,
        effects 51/74, switches 5/5, spells 13/39 - the rest of the spells exist but in other slots);
        the rest differs; the likeliest cause is the computer wizards, which do not act yet (inferred, not proven). The shipped movie plays
        to its end (8551 ticks) with everything linked; nine campaign levels run 3000 ticks.
        `mcport <dir> play <level>` flies a level with the original controls (no HUD yet).
- [x] Round 4 (2026-10-07; briefing `docs/port/BRIEFING_round4.md`, reports
      `docs/analysis/port_{ai_wizard,creatures2,creatures3,hud,sound,reference}.md`): every Thing handler of
      the game is ported (`thing_dispatch_report` is empty in every tested level and the movie), and the
      port is verified tick by tick against the original.
      - AI wizard (`ai_wizard.cpp`, all 60 functions 0x11de0..0x15590, incl. the threat recorder and the
        C runtime `rand()` it uses).
      - Creatures crab / kraken / troll / griffon (`creatures2.cpp`) and emu / genie / type 15 / wyvern
        (`creatures3.cpp`).
      - HUD and frame composition (`hud.cpp`, `ui_draw.cpp`): render_frame_1fab0 in all input modes (flight
        HUD, spell book, map, help), radar, status panels, player list, messages, debug overlay, fonts.
        `hud_tick_state(player)` performs render_frame's game-state writes (spell flash, message timers,
        book selection) once per tick through `g_hook_frame_state`; `render_frame_draw` only draws, so
        mcport can draw at frame rate. mcport `play` / `demo` now show the HUD and run in the original's
        320x200 mode.
      - Sound manager (`sound.cpp`, `mcdata/sndbank.c`): sound_request with channels / priorities /
        positional volume and pan, fades, music track logic, over a null-default `SoundBackend`.
        `sound_update` / `music_update` run from the tick (hooks in player.h).
      - **Per-tick reference** (`tools/reference/`, `docs/analysis/port_reference.md`): a patched copy of
        carpet.exe in the bundled DOSBox dumps the GameState every tick of movie 0;
        `reference_test` replays the port and diffs every slot. Result: **every Thing, every player
        record and the global RNG are byte-identical to the original over the whole movie** (every tick
        413..8600 compared, plus every 50th to the end); `reference_test` is a ctest gate (fails on any
        divergence, skips when the dumps are absent). Bugs it found: an inverted rebound test and the
        castle-seed threat in the AI, hover skipped in modes 7 / 8, the attack-castle distance measured
        from the wrong wizard.
      - Facts the reference settled (all previously "build differences"): the movie and its snapshot were
        recorded and are played in 320x200; in that mode the original's tab relocation doubles
        building.tab w / h (castle code halves them again; wizard castle capacity w*h>>4 is taken before
        the halving) - `castle_footprint()` models it and `g_wizard_castle_capacity_shift` is gone;
        movie playback loads the French notices (ftext.dat); a projectile that hits nothing stores
        (NULL - &things[0]) / 0xa4 in its impact effect (`g_projectile_null_hit_index`, set from the
        reference run's pool base); the original's render writes game state once per tick (HUD).
      - Round-3 leftovers done: `mana_ball_update_sprite` exported (one copy), language strings in
        `text.h`, `PlayerBlock.castle_level` u16, GameState `volcano_thing` / `volcano_smoke` /
        `level_music_track` named.
- [x] Round 5 (2026-10-07; briefing `docs/port/BRIEFING_round5.md`, reports
      `docs/analysis/port_{audio,frontend,fli,game,reference2,render_reference}.md`, `names_round5.md`):
      the port is a playable game - front end, campaign, saves, sound and music - and its frames are
      verified pixel by pixel.
      - Sound output (`mcport/audio_mixer.*` SDL-free mixer + HMI song sequencer, `mcport/audio_sdl.*`
        SDL device + Windows MIDI mapper, `mcdata/hmp.*`): 32 voices of the 8-bit banks, HMP songs
        (`music<set>-2` is General MIDI; -0 OPL2, -1 MT-32), mood layer fades. `MidiOut` is the slot for
        a later OPL / soft synth. Env `MC_SOUND`, `MC_MUSIC`, `MC_SOUND_VOLUME`.
      - Front end (`frontend.*`, `savegame.*`): every screen of frontend_menu_loop_52070 frame-stepped
        (language, config, logos / intro / title / outro, main menu with all items and dialogs, level
        result, lobby screen, attract mode); save games in the original 142-byte format (written to
        `SDL_GetPrefPath`, DOS saves read from `magic/save`). No 640x480 front end, no network, no sound
        wizard, no joystick / VFX1.
      - FLI player + cue scripts + subtitles (`fli.*`), display palette / stepped fades / in-game flashes
        / map-mode palette / title screen (`palette_fx.*`). The package's intro / outro / level-result
        movies are all copies of the 41-frame Intel animation.
      - Game flow (`game.*`): game_main around the front end, the level loop with restart (Shift+R /
        castle gone and wizard dead), win (status 2, then Space), level stats, campaign order (levels
        8 / 17 / 28 / 33 / 39 skipped, outro after 50), run-time 320x200 <-> 640x480 (R).
      - mcport (`mcport [dir]` runs the whole game; `play N` starts in a level and continues through the
        front end): fixed tick rate `MC_TICK_HZ` (default 25; the original has no pacing at all - one tick
        per drawn frame), palette fades block ticks and step at 70 Hz, front end at 70 fps, real-time
        `g_timer_ticks`, window resize on the resolution toggle, `MC_SHOT=n,...` writes frames as PPM
        (headless checks with `SDL_VIDEODRIVER=dummy`).
      - **Level references** (`tools/reference/run_level.py`, `patch_carpet.py --mode record/play`): the
        original records each level with the local player idle and replays the recording with per-tick
        dumps; level start has no randomness (seed from the level header), so `sim_load_level` + one tick
        equals the original's tick 1 on all 69 levels (`reference_gen`). Levels 0, 1, 12, 16, 24, 38, 44,
        49 (5000-20000 ticks; every creature type and the AI on six levels) are byte-identical
        (`reference_levels`). Fixes: river carving retry counter per river (level 44), full-pool snapshots
        relink. Level 17 crashes the original itself when started directly (div by zero 0x22328).
      - **Render reference** (`tools/reference/fb/`, `render_reference_test`, `render_reference_hud`): the
        original's back buffer + DAC dumped per tick of movie 0; the port's frames are pixel-identical on
        all 1396 + 2734 dumped frames (view, sky, things, book / map, flight HUD). Fixes: colour cube is
        built from `c*4+3`, owned-creature radar blips use the owner's B colour. `g_rng16` (terrain RNG)
        is in neither snapshot file: movies start after generating their level.
      - Naming debt closed (226 renames, `music_mood_fade_cb_1f6d0` created, `gennames.py` fixed);
        ENGINE.md / FORMATS.md carry the round 2-4 corrections.
- [x] Round 6 (2026-10-07; briefing `docs/port/BRIEFING_round6.md`, reports
      `docs/analysis/port_{opl,net,reference3,render_reference2}.md`, `names_round6.md`): Phase 2 closed.
      - **OPL2 FM music** (`mcport/opl_chip.*` own YM3812 emulator, `mcport/opl_driver.*`): the HMI OPL2
        driver is built into carpet.exe (`snd_opl_*`, 23 functions; `hmimdrv.386` is an unrelated Sound
        Master II driver); translated and checked against the original machine code run in unicorn
        (`tools/port/opl_orig.py`): all 7 FM songs + a 40,000-event random stream give identical register
        writes. `MC_MUSIC=opl` is the default (the shipped SNDSETUP.INF selects SBLAST 388); `midi` keeps GM.
        No DOSBox OPL capture (the hotkey-only capture could not be driven headless).
      - **Network** (`net.cpp` the whole NetBIOS layer 0x4e4b0..0x4f620 over `NetTransport`,
        `mcport/net_tcp.*` Winsock): `mcport <dir> network` / `MC_NET_HOST`, lobby wired (frame-stepped
        join), per-tick game-state checksum side channel (`MC_NET_SYNC`). `net_test`: 3 peers in memory, 2
        processes x 2000 ticks and 3 processes with a host drop - all checksums identical. Not tried across
        two machines or through two GUI lobbies.
      - **References with an active player + movie recording** (`demo.cpp` recorder byte-compatible with
        the original, `reference_player_test` script runner, `tools/reference/run_player.py`,
        `player_scripts/`): four scripted recordings (all 24 spells, castle build / upgrades, possession,
        duels, deaths, respawn, quick save / load) recorded by the port and replayed by the original:
        15,595 ticks byte-identical (one fix: cheat notices use strcpy). Original bug found: quick load
        leaves cell lists with cycles (the original hangs a few ticks later).
      - **Render references 2** (`movie0_fb640`, `movie0_fbopt`, `fe` dumps; `render_reference2_test`,
        `render_reference_options`, `render_reference_fe_test`): 640x480 in game 3334 / 3334 frames, every
        render option (textured sky, reflections, smoothing, motion blur, view sizes, shadows, HUD parts,
        help, credits, pentium) 3634 / 3634, 33 / 33 front-end screens pixel-identical. Fixes: help-screen
        blank lines, credits state machine in the draw pass, pointer ghost under motion blur, front-end
        palette cleared on reload. The original never shows the front end in 640x480 (dead code): item
        closed.
      - Docs: round-5 corrections merged into ENGINE.md / FORMATS.md (HMP, FLI, bnk), 9 renames
        (`level_restart_3d4e0`, `fli_read_frame_50520`, ...), the in-game mouse pointer, sound-setup summary.
      - Known data gap: the GamesNostalgia package's `intro/intro.dat`, `outro.dat`, `levelw1/2.dat` and
        `levelose.dat` are byte copies of `intel.dat` (the Intel logo); the cue-script audio is right.
        The real movies come from the user's GOG Magic Carpet Plus CD image (`game.gog`, ISO 9660):
        `CARPET/INTRO` extracted to `extracted/gog_cd/CARPET/INTRO` (7-Zip); mcport reads it through
        `fli_set_movie_dir` (`MC_MOVIE_DIR`, default that folder). Same FLIC variant; intro 3165 frames.
- [ ] Left open (not needed for Phase 3): anaglyph / SIRDS / VFX1 stereo, joystick, the sound-setup wizard,
      the CD check; a test across two real machines.

### Phase 3 - quality-of-life
Every Phase 3 change is a setting (`src/mcengine/settings.h`, `PortSettings g_settings`) whose default is the
original's behaviour, so every reference stays byte / pixel identical; mcport plays with its own defaults from
`mcport.ini` (`--faithful` restores the original). Round 7 (2026-10-07, Phase 3 round 1; briefing
`docs/port/BRIEFING_round7.md`, reports `docs/analysis/port_{render_ext,compose,pool,pacing,settings}.md`):
- [x] **Draw distance** (`render_ext.cpp`, `render_ext_raster.cpp`): extended software renderer, radius up to
      127 cells (no cell drawn twice on the wrapping map), frustum / circle grid, texture mips + 2x2..8x8
      merged quads with skirts far away, fog scaled to the distance (`fog_start_pct`) and haze into the textured
      sky, things with the original's sprite modes, threaded in horizontal bands. Release, 24 threads, level 38:
      1080p dd 127 4.5 ms, 4K dd 127 5.9 ms (1 thread: 10 / 25 ms). No GPU renderer needed for distance /
      resolution; a GPU path is only worth it later for filtered textures / HD packs.
- [x] **Native resolution** (`compose.*`, `mcport/platform*`): the 3D view at the window's resolution, the
      game's 2D layer (pixels the 2D pass changed, detected by diff + an inverted second pass) scaled on top
      (integer / fit / filtered), widescreen HUD corner blocks moved to the display corners, 2D screens and the
      front end pillarboxed, palette applied last (fades work), Alt+Enter borderless fullscreen, D3D11. Present
      1080p 2.5 ms, 4K ~7 ms.
- [x] **Thing pool** (`thing.*`, game logic): `thing_slots` 1000..32768 (indices stay < 0x8000: the original
      sign-extends them), extension outside GameState, identical to the 1000-slot run until the original's pool
      would be full (movie 0 fills it at tick 1481; 21 of 70 levels fill it within 30000 ticks). Level
      generation stays at 1000 slots; `thing_cap_villagers` keeps towns at the original's size. Level 2 with
      the pool flooded: not won in 30000 ticks with 1000 slots, won at tick 7691 with 4000.
- [x] **Fixed timestep + interpolation** (`mcport/pacing.*`, main.cpp): ticks at `tick_hz` (25), frames
      uncapped / `fps_cap`, camera and Things interpolated (wrap-aware, jump / slot-reuse detection), tick
      checksums identical with and without; F11 frame-time overlay.
- [x] **Robustness**: both renderers bound the per-cell Thing walk; the port's full quick save
      (`quicksave_full`) / `demo_repair_cell_lists` leave no cell-list cycles.
- [x] **Config file, controller, save anywhere** (`mcport/config.*`, `mcport/gamepad.*`, `savegame.*`,
      `demo.*`): `mcport.ini` (defaults < ini < env < `--set`), `--faithful`; SDL game controller (right
      stick steers, triggers cast, hot-plug); save slots 0..9 (Ctrl+F1..F10 save, Shift+F1..F10 load,
      `mcport <dir> load N`) with campaign state, identical continuation after load; extended-pool movies in
      their own `mvx/gax/max` files.
- [x] Follow-up (2026-10-07, user play-test): play default draw distance 125; sleeping dragon / worm body
      segments (the original only wakes creatures within 24 cells, asleep the segments snap onto their
      parent every 4th tick) are laid out behind their parent by the extended renderer, render-only
      (`render_ext_segments_test`); `radar_zoom_pct` (play default 150) zooms the flight radar out.
- [x] **Round 8 (2026-10-07, report `docs/analysis/port_round8.md`):**
      - Segmented-creature flicker fixed (render-only). Cause, measured: an awake dragon / worm's wake timer runs
        out every 17th tick; on that tick every segment has `timer_a == 0`, a quarter of them (`(tick & 3) == 0`)
        snap onto their parent in the simulation (the original's own game state has that one-tick kink), and the
        extended renderer switched to its sleeping layout from scratch. The extended renderer now draws every
        body segment follow-the-leader from where it was drawn last frame, awake or asleep;
        `render_ext_segments_test` part 3 drives an awake creature through 7 resets with interpolation (the
        old code fails it: 170 units off). The faithful renderer still shows the original's kink.
      - `game.possession_range_pct` (faithful 100, play default 130): the Possession shot's lifetime (11 ticks,
        health 10 - `aux` 0xc8 and the 0x2800 look-ahead do not limit it) scales to the nearest whole tick
        (130 -> 14 ticks, +27 %), its first-tick target pick radius (0x1400) exactly. Read through
        `gameplay_rules()` (settings.h), which movies and network sessions override; movies recorded with
        non-faithful rules are `mvx` version 2 (DemoExtRules), the original's movies play faithful.
        `possession_range_test`, config_test `rules_movie`.
      - Network agreement: right after the level choice the lobby exchanges `NetGameRules` (pool size +
        Possession %) once; every peer adopts the host's until the session ends or a level starts outside a
        network game. net_test's scripted peers do the same exchange (host 3000 slots / 150 %, others the
        original's: 2000 / 3201 lockstep checksums identical); the lobby part checks the adoption.
      - Interpolation: `thing_slot_generation` (++ per thing_alloc of a slot) - a slot freed and reused within a
        tick never lerps from the Thing it held.
      - Keys (user request): `keys.wasd` (W / S / A / D = the arrow keys in flight) and `keys.book_tab` (Tab opens /
        closes the spell book like Enter), play default on, arrows and Enter keep working. In flight the original
        reads no plain W / A / S / D or Tab, and the Alt / Ctrl / Shift branches return before movement (Alt+S
        quick save unchanged); the chat line is its own input mode. input_test `test_flight_keys`.
      - Build: `/INCREMENTAL:NO` - incremental links twice produced test executables that crashed before main.
- [x] **Round 9 (2026-10-07, report `docs/analysis/port_round9.md`):**
      - Play-test bug: after a wizard's hoard was claimed (the original's rule: a dead wizard's balls pass to a
        class 10 type 0x28 hoard, its claimer gets them all) far balls kept the wizard's colour - the
        simulation only refreshes a ball's sprite while it is awake (24 cells). The extended renderer now
        draws mana balls with `mana_ball_sprite()` (render-only); `render_ext_mana_test`.
      - Save states record the gameplay rules (`RULE` chunk) and force them for the rest of the level.
      - In-level pause menu on Esc (`keys.menu`; `mcport/game_menu.*`): resume, save / load state slots,
        options (applied at once, written into mcport.ini in place by `config_set_keys`), leave level (the
        original's Esc), quit; the level is held (not in a network game); keyboard, mouse, controller.
        `game_menu_test`.
- [x] Controller confirmed working on real hardware by the user (2026-10-07).
- [ ] **Remaining (small, optional):** analog speed on the left stick; key rebinding and audio volumes in the
      menu; the faithful renderer's per-frame slope low-pass at high frame rates; play-test feedback.
- [ ] **GPU path** (filtered textures / HD packs): deferred - decided during Phase 4 design, when the battle
      royale's needs (map size, unit counts, effects) are known.

### Phase 4 - "Conquest" mode: PvP RTS / RPG hybrid on a generated world (bots + online)
Research and roadmap done 2026-10-07: **`docs/ROADMAP_PHASE4.md`** (rounds 10-21) built on five reports,
`docs/analysis/phase4_research_games.md` (Sacrifice, WC3, Dungeon Keeper, Diablo II, hybrids, netcode, bots,
procgen), `phase4_research_mc2.md` (MC2 spells / creatures / caves / GOG CD data - sprite, tmaps, palette and
RNC formats are identical to MC1; code is GPL, reference only), `phase4_code_world.md` (positions are u16, so a
bigger world is an int32 `Pos` refactor behind a world descriptor; 430 sites hard-wire 256), `phase4_code_gameplay.md`
(dispatch extension ids, creatures, AI, castles / villages, spells, damage hook, player record, traps) and
`phase4_code_platform.md` (lockstep, determinism, modes plumbing, front end, HUD, input, test tooling).
Summary of the plan (details, effort and gates in ROADMAP_PHASE4.md):
- [x] Round 10 (DONE 2026-10-08, briefing `docs/port/BRIEFING_round10.md`, reports `docs/analysis/port_{mode,console,inspect,timectl,desync}.md`; 59/59 ctest): in-game debug / testing suite (console, scenario files, inspector, overlays, time control,
      debug camera, headless `mcport rts` runs with JSON dumps, desync parts) + mode skeleton + movie format v3.
- [ ] Round 11: world module - int32 positions, N x N torus (512 / 768 / 1024 / 2048), generator on N, seeded
      symmetric feature placer (spawns, springs, wild lairs), deterministic wake-near-any-wizard.
- [ ] Round 12: main-menu urn + setup screen (seed, size, bots, sliders, minimap preview), lobby exchange, match
      rules v1 (teams, respawn, desecration, win conditions), bots v1 = existing wizard AI.
- [ ] Round 13: RPG layer - damage hook, health plates, floating damage numbers, rarity tiers with affixes, named
      elites, drop tables, spell and wizard levels.
- [ ] Round 14: economy - food / lumber / ore / mana, town centre / farm / lumber yard / mine / shrine / towers
      on fixed plots, gatherer villagers, organic town growth by re-stamping, upkeep.
- [ ] Round 15: lair, summoned units with orders, essence economy, order packets (variable-length exchange),
      four first-person orders.
- [ ] Round 16: RTS overview camera (oblique renderer on render_ext_raster), picking, native-resolution RGBA
      overlay + font, mouse / controller control schemes.
- [ ] Round 17: wizard bots (DK-style processes, influence maps, squad FSM, personalities) + headless balance
      harness.
- [ ] Round 18: new spells (thunderstorm, chain lightning, meteor shower, firestorm, earth wall, tornado, mind
      control, MC2 ports ...), spell levels, loadout.
- [ ] Round 19: online - relay / dedicated host, input delay, rejoin / spectators, replays.
- [ ] Round 20 (stretch): MC2 creatures and effects from the user's GOG data, surface dungeons.
- [ ] Round 21: match flow, progression, balance, polish.

## Working conventions
- Original game files stay untouched under `MagicCarpet/`.
- Anything regenerated from them goes under `extracted/` (ignored by git).
- Ghidra project is the source of truth for names/types; export after every
  naming session so the C dumps stay fresh.
