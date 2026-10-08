# Port round 7 briefing (2026-10-07): Phase 3 round 1 - draw distance, native resolution, Thing pool, frame pacing, settings

Read `docs/port/PORTING.md` first (sources of truth, code conventions, report format), then
`docs/port/BRIEFING_round6.md` (parallel-work rules, which apply unchanged unless this file says
otherwise). The engine is described in `docs/ENGINE.md` (renderer: sections 2.1-2.8, especially **2.5
"Landscape algorithm and the draw-distance limits"**); the per-subsystem reports are
`docs/analysis/port_*.md`; `docs/ROADMAP.md` Phase 3 lists the goals this round starts.

## State at the start of round 7

- Phase 2 is closed: `mcport.exe <game dir>` plays the whole game; 40 ctest tests pass, including the
  per-tick state references (`reference_test`, `reference_levels`, `reference_gen`, `reference_player*`)
  and the pixel references (`render_reference_test`, `render_reference_hud`, `render_reference2_test`,
  `render_reference_options`, `render_reference_fe_test`).
- Phase 3 changes the game on purpose, so **everything new is a setting whose default is the original's
  behaviour**. New this round, already compiled (integrator scaffolding):
  - `src/mcengine/settings.h`: `PortSettings g_settings` (defined in `mc_globals.cpp`, so every unit test
    has it). Default-constructed = faithful. Each task owns one section and may add fields only there.
  - `render.h` seams: `g_render_view_override` + `render_view_frame()` (hud.cpp's three view calls go
    through it), `RenderInterp g_render_interp` (camera + per-slot previous positions for render-time
    interpolation; hud.cpp's frame_pass already uses `g_render_interp.camera` when `have_camera` and it is
    only drawing).
  - `thing.h`: `thing_pool_slots()` (returns `MC_THING_SLOTS` until task C changes it).

## The user's requirements (from docs/ROADMAP.md Phase 3 and the project memory)

1. **Draw distance - high priority.** The original shows ~20 cells (≈10 m) before fog. The user wants a
   *massive*, configurable increase, ideally most of the 256x256 map. Keep the original renderer as the
   faithful mode (the pixel references depend on it).
2. **Modern resolutions**: the 3D view at the display's native resolution (1080p / 1440p / 4K, widescreen
   and ultrawide), HUD / book / map scaled cleanly. Texture look stays **nearest-pixel by default** (user
   decision); filtering / upscaling / HD packs are later options.
3. **Thing pool limit**: the original's 1000 slots fill up in busy late levels, spawning (including the
   mana balls needed to win) silently fails and the level becomes uncompletable. 1000 stays as the
   faithful setting; an extended setting for play.
4. Fixed-timestep simulation with render interpolation and an uncapped frame rate.
5. Robustness: bound the renderer's per-cell Thing walk (the original's quick load can leave cycles).
6. Config file, controller support, save anywhere.

## Rules (round-6 rules, plus what is new)

- Five agents (A..E) work in the same tree at the same time (no git). **Only create / modify the files your
  task names.** Everything else is read-only; a wanted change to a shared file goes into your report as
  exact code (file, place) - the integrator applies it afterwards.
- Shared files nobody edits this round (integrator only): `src/CMakeLists.txt`, `engine.h/.cpp`,
  `sim.h/.cpp`, `sim_all.cpp`, `mc_globals.h/.cpp`, `render.h` **except task A**, `player.h`, `gen/*`,
  `docs/ROADMAP.md`, `README.md`, `docs/port/PORTING.md`, `docs/ENGINE.md`.
  `settings.h`: each task edits only its own section.
- **Faithful by default, references are the gate.** With a default-constructed `g_settings` every
  reference test must stay byte / pixel identical: run `reference_test`, `reference_levels`,
  `reference_gen`, `reference_player_test`, `render_reference_test`, `render_reference_hud`,
  `render_reference2_test`, `render_reference_options`, `render_reference_fe_test` (whichever your files can
  affect) after every change. Faithful-mode code paths should stay the translated code, not a
  generalisation that happens to give the same pixels - extended mode may be a separate function / file.
- New behaviour is checked by your own test (`src/tests/<name>_test.cpp` + `.cmake`, exit 0 = pass, SKIP
  without data that is not in the package) plus screenshots you look at (`MC_SHOT`, PPM -> PNG with
  `python -c "from PIL import Image; ..."`), and timing numbers where performance matters (Release build:
  `cmake --build ../build_X --config Release --target ...`; say which config your numbers are from).
- Build in **your own build directory** `build_X`:
  `cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_X`, then
  `cmake --build ../build_X --config Debug --target <your targets>`. Zero warnings (/W4). Prefer
  `mc_unit_test(name test.cpp <explicit sources>)`; `mc_test` links the whole library (incl. other agents'
  half-written files - retry when a build trips over one mid-edit, say so in the report if it persists).
- Scratch: **your own subdirectory** `<scratchpad>/round7_X/` of
  `<scratchpad>`.
- Bash pitfalls: `\n` inside heredoc'd python / C strings becomes a real newline - write sources and
  scripts with Write / Edit and run scripts from files. Run python analysis tools from `tools/analysis`,
  never with `C:\Magic Carpet` as the working directory (the `ghidra/` folder shadows a package).
- The original game files under `MagicCarpet/` and the user's GOG install are never modified.
- Game logic stays integer / deterministic (no floating point in anything that feeds the simulation).
  Renderer-only extended code may use floating point if it helps, but keep it deterministic per frame.
- GPL: do not copy code from DOSBox, remc2 / MC2-HD, mgcarpet or similar (licence not decided). Reading
  documentation is fine; write your own.

## Deliverable (as before)

Your sources, your test (exit 0, zero warnings), and your report `docs/analysis/port_<name>.md` in the
PORTING.md format: what was built, verification (what, how, numbers), deviations / gaps, settings added
(with defaults and ranges), **requested shared-file changes (exact code, file and place)**, **mcport
integration (exact code for `src/mcport/main.cpp`, unless you own it)**, and what the next round should do.
Your final message is a short summary of that report.

## Tasks

### A - extended renderer: draw distance (`render_landscape.cpp`, `render_things.cpp`, `sprite_cache.cpp`, `raster.cpp`, `render.h`, new `render_ext*.{h,cpp}`, settings.h section A, `tests/render_ext_test.*`, report `docs/analysis/port_render_ext.md`, build dir `build_A`)

Make the 3D view see far, in the existing 8-bit software renderer (same textures, palette, shade table,
fill modes - the game must still look like Magic Carpet, just with the horizon pushed out).
- An extended path of `render_landscape` selected by `g_settings.render_extended`: a vertex grid sized from
  `draw_distance` (allocated outside `g_work_buf`, which stays the faithful 64 KB layout), covering the view
  frustum (wide aspect ratios included) instead of the fixed 40x21 rectangle, painter's order far to near
  kept correct, fog moved out (`fog_start_pct`, and fog to the *sky* colour / sky texture at the far end so
  the horizon does not end in a hard edge - say what you chose), the four squared-distance constants (now in
  `g_rcam`) from the settings. Watch the 32-bit products: `x^2 + z^2` overflows int32 beyond ~181 cells and
  the projection `x * focal` grows with resolution - use 64-bit where needed in the extended path.
- **Level of detail** (`lod`): beyond some distance merge cells into 2x2 / 4x4 / 8x8 quads (heights sampled
  at the coarse corners, texture of the cell - or the per-texture average colour `0xCD9B0` table as a
  flat fill far away), with crack-free joins between LOD rings (skirts or stitched edges). Distant terrain
  must not flicker as the camera moves (snap the coarse grid to world-aligned cells).
- **Map wrap**: the world is a 256x256 torus. Limit the radius so no cell is drawn twice (max 127) and make
  sure the grid wraps cleanly; report what the far edge looks like.
- **Things at long range** (`render_things.cpp`): cull distance and fog from the extended settings; sprites
  that become smaller than a pixel or two are skipped or drawn as a dot; the sprite cache (`sprite_cache.cpp`,
  LRU of scaled sprites) must cope with many more visible Things. Also the robustness item: **bound the
  per-cell Thing walk** (a cycle in a cell list must not hang the renderer - in both modes; in faithful mode
  only add the bound, do not change what is drawn).
- **Arbitrary view size**: the renderer must draw into any `FrameBuffer` size (up to 3840x2160 at least; task
  B renders the view at the display resolution through `g_render_view_override` and calls your entry point).
  Today `MC_ROLL_MAX` = 640, the roll list, the sprite column tables and the per-row clip triples live in
  `g_work_buf` at fixed offsets sized for 640x480 - give the extended path its own buffers. Provide
  `void render_view_ext(const FrameBuffer &fb, const Camera &cam)` (render.h) that draws the 3D view (sky +
  terrain + things, plus the 2x2 smoothing / motion blur options if cheap) into the whole of `fb` at its size;
  the vertical field of view must match the original's for the same zoom (focal = diagonal * zoom >> 8 is
  resolution independent in angle), widescreen widens the horizontal view.
- Thing access only via `thing_at()` / `thing_pool_slots()` (task C makes the pool resizable); interpolated
  positions: when `g_render_interp.prev_pos` is set and the slot is below `prev_count`, draw each Thing at
  `prev + (cur - prev) * alpha >> 16` (16-bit wrap-aware on x / y) - extended path only.
- **Performance**: measure frame time (Release) at 640x480, 1920x1080, 2560x1440, 3840x2160 for draw distances
  20 / 48 / 80 / 127 on a busy level (e.g. 44) and report a table. Target: 1080p at draw distance >= 80 above
  60 fps. If single-threaded falls short, consider splitting the frame into horizontal bands rendered on
  worker threads (the rasteriser's globals `g_rt_*`, `g_fill_mode`, `g_texture_ptr` would need to become
  per-thread - `raster.cpp` is yours; keep the faithful path's code unchanged in behaviour). Report whether
  the software renderer can reach the targets or whether round 8 should build a GPU renderer.
- Test `render_ext_test` (mc_test or mc_unit_test with the renderer sources): faithful mode unchanged (the
  reference tests are the real check), extended mode renders level 38 / 44 at several sizes and distances
  without crashing, no cell drawn twice, far terrain present (non-sky pixels above the old 20-cell horizon
  line), a cell-list cycle does not hang, write PNG / PPM screenshots to your scratch dir and look at them.
  Report before / after screenshots (paths).

### B - native-resolution frame composition and presentation (new `src/mcengine/compose.{h,cpp}`, `src/mcport/platform.h`, `src/mcport/platform_sdl.cpp`, settings.h section B, `tests/compose_test.*`, report `docs/analysis/port_compose.md`, build dir `build_B`)

The game draws everything (view, HUD, spell book, map, messages, mouse pointer) into one 8-bit frame of
320x200 or 640x480 (`render_frame_draw`, hud.cpp). Make the extended mode show a crisp 3D view at the
display's resolution with the game's 2D layer on top:
- Suggested design (improve on it if you find better): the view override (`g_render_view_override`,
  render.h) renders the 3D view with task A's `render_view_ext` into a **high-resolution 8-bit view buffer**
  (display size), then downsamples it into the game frame's view window (so translucent HUD parts that blend
  with the view still have something to blend with). After `render_frame_draw` (+ debug overlay + pointer)
  the frame is compared with a copy taken right after the view was placed: pixels the 2D pass changed are
  the **HUD layer**. Presentation = hi-res view, with the HUD layer scaled on top (nearest by default,
  integer scale where possible), palette applied at the end (palette fades / flashes keep working because
  everything stays 8-bit until the final lookup). The full-screen 2D screens (spell book, map, help, result
  screens, front end) simply cover the view. Until A's `render_view_ext` exists, test with a stand-in that
  calls `render_view` at the low resolution and upscales.
- Aspect ratio: the game frame is 4:3 (320x200 is shown at 4:3, as on a CRT); on a 16:9 / 21:9 display the 3D
  view fills the whole screen and the 4:3 HUD layer is centred (pillarboxed) - or, better, keep the HUD's
  corner elements (radar top-left, spell labels / status bars top-right) at the screen corners if you can do
  that robustly by splitting the layer into regions; report what you did. The front end and the full-screen
  2D screens are pillarboxed with black bars.
- Mouse: pointer coordinates map from the window to the game's 640-space through the same transform
  (the HUD layer's rectangle); give main.cpp a function for it.
- Platform (`platform.h` / `platform_sdl.cpp`, yours): resizable window, borderless-fullscreen toggle
  (Alt+Enter), present of the composed image (an 8-bit hi-res view + 8-bit HUD layer + mask + palette ->
  ARGB texture; keep it fast: 4K is 8.3 Mpixels - multithread or use SDL_Renderer textures sensibly, measure),
  vsync on / off, the faithful mode keeps today's path (the 320x200 / 640x480 frame scaled to the window,
  nearest). Report frame-time numbers for the present at 1080p / 1440p / 4K (Release).
- `compose.h` contract for main.cpp: e.g. `compose_begin_frame(display_w, display_h)`,
  `compose_install()` / `compose_remove()` (the override), the hi-res buffers, `compose_end_frame(game_fb)`
  (HUD diff), the mouse transform. Keep it independent of SDL (mcengine has no SDL); the SDL half is in
  platform_sdl.cpp.
- Test `compose_test`: the diff / layer logic on synthetic frames, the downsample, the mouse transform
  (round-trip), a composed frame of level 38 with the HUD at 1920x1080 and 2560x1080 written to your scratch
  dir (look at it). The faithful path must be unchanged (no override installed = today's pixels).
- Report the exact main.cpp integration code (task D owns main.cpp; write it so D or the integrator can
  paste it).

### C - Thing pool without the 1000 limit (game-logic files: `thing.h/.cpp`, `mc_types.h`, `player.cpp`, `creature*.cpp`, `ai_wizard.cpp`, `projectiles.cpp`, `spells.cpp`, `effects.cpp`, `castle.cpp`, `scenery.cpp`, `spatial.cpp`, `constructors.cpp`, `level_features.cpp`, `terrain_*.cpp`, `input.cpp`, `net.cpp`, `hud.cpp`, `ui_draw.cpp`, settings.h section C, the game-logic unit tests `tests/*_test.cpp` that index the pool, new `tests/pool_test.*`, report `docs/analysis/port_pool.md`, build dir `build_C`)

- Make the pool size a setting (`g_settings.thing_slots`, 1000 = original, up to 65535): `GameState.things[1000]`
  stays where it is (the layout and the snapshots / saves depend on it); slots >= 1000 live in a separately
  allocated extension, `thing_at()` / `thing_pool_slots()` hide it (thing.h). The free stack and the
  "recyclable" active stack (`free_list[1000]` / `active_list[1000]` in GameState) need the same treatment.
- Find **every** place that assumes 1000: loops over `MC_THING_SLOTS`, `% MC_THING_SLOTS` index sanitising,
  direct `g_state->things[...]` indexing (replace with `thing_at()`), 16-bit index fields (fine up to 65535),
  the recycling of live effects when the pool is full (`thing_alloc_35560` and its callers), and any other
  fixed cap that starves spawning (per-type lists, the castle / mana-ball lists in the PlayerBlock, the
  sprite cache is task A's). List them in the report with the original addresses.
- Verify the bug and the fix: find (or build with a test that floods the pool) a situation where the
  original's pool is full and mana balls / creatures fail to spawn; show that with `thing_slots = 4000` they
  spawn and the level can be won. Measure how many Things a late campaign level reaches when left running
  (e.g. levels 40-49 for 30,000 ticks with the AI wizards active) in both settings - report the peak.
- Determinism and compatibility: the setting must be identical on all network peers (net.cpp: refuse or
  negotiate a mismatch - report how), movies recorded with a non-faithful pool cannot be played by the
  original (`demo.cpp` is task E's: report what E / the integrator must add, e.g. a header flag), save games
  (`savegame.*` original format, quick save snapshots) - say what happens to slots >= 1000. All reference
  tests stay identical at the default.
- `hud.cpp` / `ui_draw.cpp` are yours only for the pool-access changes (thing_at / thing_pool_slots);
  `render_things.cpp` is task A's (tell A in your report, A already uses thing_at()).
- Test `pool_test` (mc_unit_test with `${MC_SIM_ALL}`): allocation beyond 1000, free / reuse order identical
  to the original for the first 1000 slots, a flood level stays deterministic (two runs, same checksum).

### D - frame pacing: fixed timestep, render interpolation, uncapped frame rate (`src/mcport/main.cpp`, new `src/mcport/pacing.{h,cpp}` if useful, settings.h section D, `tests/pacing_test.*`, report `docs/analysis/port_pacing.md`, build dir `build_D`)

You own `main.cpp` this round: the other tasks report their integration code; merge B's and E's when their
reports arrive if time allows, otherwise the integrator does.
- The simulation runs at the fixed tick rate (`MC_TICK_HZ`, default 25 - check `docs/analysis/port_game.md`
  "Pacing" for why) and the display at whatever the monitor / vsync allows. Today every frame redraws the
  last tick, so motion is 25 Hz. With `g_settings.interpolate` the frame between tick N-1 and N is drawn at
  alpha = time since tick N / tick period: fill `g_render_interp` (render.h): the camera interpolated from
  `player_camera()` of the last two ticks (angles wrap at 0x800, positions wrap at 0x10000, zoom / pitch /
  roll lerped; teleports and respawns must not lerp across the map - detect big jumps), and `prev_pos` per
  Thing slot (x / y / z copied before each tick; a slot that was freed / reallocated between the ticks must
  not lerp - track the slot's identity, e.g. a per-slot generation from the Thing's fields, or compare type /
  class). Interpolation is render-only: nothing written by the tick may depend on it.
- The HUD and the 2D screens draw the latest tick (no interpolation there). The palette fades keep their
  70 Hz steps. Front end stays at 70 fps. The free-camera viewer gets smooth per-frame motion too.
- Input latency: sample the devices every frame and hand them to the tick that runs next (today's deferred
  release logic must keep working); the mouse steering in a level must not feel worse at high frame rates.
- `fps_cap` (0 = uncapped / vsync), a frame-time display (title bar or an overlay toggled by a key, port-only,
  say which).
- Test `pacing_test`: the interpolation math (wrap, alpha 0 / 1 give the ticks exactly, jump detection, slot
  reuse), and a headless mcport run (`SDL_VIDEODRIVER=dummy`, `MC_SHOT`) of `play 0` with interpolation on
  that shows no change in the simulation: the per-tick GameState checksums with and without interpolation
  are identical (add a port-only env switch that logs a checksum per tick if needed).
- Merge into main.cpp: B's compositor (when ready) and E's config / controller hooks. Keep every existing mode
  (`play`, `demo`, viewer, front end, network) working; `MC_SHOT` / `MC_TEST_INPUT` headless checks too.

### E - settings, config file, controller, save anywhere (new `src/mcport/config.{h,cpp}`, new `src/mcport/gamepad.{h,cpp}`, `demo.*`, `savegame.*`, settings.h comments only (fields belong to their owners - add a section E only for things engine code must read), `tests/config_test.*`, report `docs/analysis/port_settings.md`, build dir `build_E`)

- **Config file**: `mcport.ini` in the save directory (`SDL_GetPrefPath("Bullfrog", "MagicCarpet")`), created
  with commented defaults on first run, plus command-line overrides (`--set key=value`) and the existing env
  variables (`MC_TICK_HZ`, `MC_SOUND`, `MC_MUSIC`, `MC_MOVIE_DIR`, ... - keep them working, document the
  precedence). It carries every `PortSettings` field (read the sections of A..D as they appear; unknown keys
  are kept and warned about, not an error) and the platform's own options (window / fullscreen, vsync, display
  index, volumes, music device, tick rate, key / button bindings). Write a `config.h` API main.cpp calls once
  at start-up (`config_load(save_dir, argc, argv, &g_settings, &PlatformOptions)`). Defaults for *playing*
  (mcport) may differ from the engine's faithful defaults: propose them in the report (the user wants the far
  draw distance and the pool fix on when playing; references / tests keep faithful). A `--faithful` switch
  restores the original behaviour in one go.
- **Controller** (`gamepad.*`, SDL_GameController): map a modern pad onto the game's own input (input.h
  device state - the original has joystick code too, see `input.cpp` / ENGINE.md): steering on the right
  stick (as the mouse offset from the centre), speed / slide on the left stick, triggers / shoulders cast the
  left / right hand, face buttons for the spell book, map, quick-select; dead zones, sensitivity and bindings
  from the config. Hot-plug. Report the exact main.cpp hooks.
- **Save anywhere**: the original has quick save / load (command 10; `demo.cpp` writes the GameState + map
  snapshot; `docs/analysis/port_reference3.md` notes the original's quick load leaves cell-list cycles - make
  the port's load rebuild the cell lists cleanly, `thing_relink_snapshot`). Give the player several save slots
  usable at any moment in a level (keys from the config, e.g. F5 save / F9 load with a slot number, or the
  original's keys if it has them), stored in the save directory with a small header (level, time, port
  version, pool size), loadable after a restart of the port (the campaign state must come back too). Pool
  sizes > 1000: coordinate through the report with task C's design (the snapshot needs the extension slots).
- Movie recordings (`demo.*`): mark recordings made with non-faithful settings that change the simulation
  (pool size) so the original / the faithful port refuse them cleanly (report the exact format change).
- Test `config_test`: parse / write / round-trip, precedence (file < env < command line), unknown keys, ranges
  clamped, `--faithful`; a save-anywhere round trip in the engine (save at tick T, run on N ticks, load, run N
  ticks again: identical checksums) - needs `${MC_SIM_ALL}` + demo / savegame sources.
