# Port round 7, task D: frame pacing - fixed timestep, render interpolation, uncapped frame rate

Phase 3 (docs/port/BRIEFING_round7.md task D). Port-only code: nothing here exists in carpet.exe (the
original draws exactly one frame per tick and has no limiter, `docs/analysis/port_game.md` "Pacing"), so
there are no original addresses to list. Files: `src/mcport/pacing.{h,cpp}` (new, no SDL),
`src/mcport/main.cpp` (owned this round), `src/mcengine/settings.h` section D (comments only),
`src/tests/pacing_test.{cpp,cmake}` (new).

## What was built

### Fixed timestep (unchanged in principle, now explicit)

The simulation keeps running at the fixed tick rate (`MC_TICK_HZ` / the config's `tick_hz`, default 25;
movies 20) from the main loop's tick clock (`next_tick += period`, at most 4 ticks per frame in a level /
32 in movies before the clock is reset, as before). The display runs at whatever vsync / `fps_cap`
allows. Every simulation tick of every mode now goes through one wrapper in main.cpp,
`sim_tick(game_level_tick | engine_tick)`, which brackets the tick with the interpolation snapshots and
the optional per-tick checksum log. Palette fades still step at 70 Hz without ticking, the front end still
runs `fe_frame` at 70 Hz, nothing changed there.

### Render interpolation (`g_settings.interpolate`, pacing.h)

With `interpolate` the frame drawn while tick N is the latest shows the state at
`alpha = (now - time tick N was due) / tick period` between tick N-1 and tick N (one tick behind, the usual
fixed-timestep scheme). `TickInterp` (pacing.h):

- `before_tick(local)`: copies x / y / z, cls, type, owner of every pool slot (`thing_at()` /
  `thing_pool_slots()`, so task C's larger pool is covered; the arrays grow with the pool) and
  `player_camera(local)`.
- `after_tick(local)`: per slot, `prev_pos[i]` = the pre-tick position when the slot **continues**
  (`pace_slot_continues`: live before and after, same cls / type / owner, moved at most `PACE_JUMP_XY` =
  0x400 (4 cells, 16-bit wrap-aware) on x and y and `PACE_JUMP_Z` = 0x800 on z), otherwise the current
  position (a freed / reallocated slot, a new Thing, a teleport: no lerp). The camera pair is the previous
  and the current `player_camera`; a camera that moved more than the same limits (respawn, teleport, level
  restart, the first tick after a spawn) snaps to the new tick.
- `fill(g_render_interp, alpha, camera)`: `active`, `alpha` (16.16), `prev_pos` / `prev_count` (task A's
  extended Thing renderer lerps `prev + (cur - prev) * alpha >> 16`), and in the game view `have_camera` +
  the interpolated camera, which hud.cpp's `frame_pass` already uses in the draw-only pass instead of
  `player_camera(local)`. Camera lerp: x / y the short way round the 0x10000 torus (result sign-extended as
  `player_camera` returns it, masked 0..0xffff for the free camera), yaw the short way round 0x800 (& 0x7ff),
  roll (`P->yaw_rate`, a signed rate the renderer masks) wrap-aware unmasked, z / pitch / zoom linear.
  alpha 0 and 0x10000 return the two ticks exactly (every lerp special-cases its end points).
- `hold()`: a due tick that did not run (paused / ended movie, paused viewer) sets prev = current so the
  picture does not swing back and forth; `reset()`: no previous tick (mode change: level start, movie
  start, back to the front end, viewer level switch, level end).
- main.cpp clears `g_render_interp` right after the frame is drawn, so nothing outside the draw sees it.

Render-only: TickInterp only reads the game state (verified below); the tick never reads
`g_render_interp` (hud.cpp uses the camera only when `!s_writes`; no game-logic file includes render.h).

What is interpolated where: the 3D view's camera (flight, help screen, the spell book / map screen's small
view) and - in task A's extended renderer - the Things. The HUD, radar, messages, spell book and map draw
the latest tick (they read the game state directly). The faithful renderer (`render_view`) is untouched:
with `interpolate` and the faithful renderer only the camera moves smoothly, Things stay at tick positions.
The mouse pointer is drawn every frame at the latest device position (it was already). The free-camera
viewer / demo free camera steps at 119 Hz as before and is drawn between its last two steps
(`pace_lerp_camera(cam_prev, cam, alpha_cam, masked)`); the right-button mouse yaw is applied to both so
it stays immediate.

### Input

Devices are polled every frame (as before) and the game reads them at the next tick: key presses go to
`input_key_event` at once, releases / button-ups stay deferred until a tick (or `fe_frame`) has run
(`apply_releases`, unchanged), so a tap between two ticks is still seen at any frame rate. The level's
relative mouse steering now keeps the sub-pixel remainder of `mouse_dx * 640 / fb_w` (and the y
equivalent) between frames, so many small per-frame motions at high frame rates add up to exactly the
same pointer travel as few large ones (with today's 320 / 640 frames the division was exact anyway; it
matters once the platform scales motion, e.g. task B's composed mode). Interpolation adds one tick of
display latency to the 3D view (40 ms at 25 Hz) - inherent to the scheme, since the tick rate is the game
speed it cannot be raised instead.

### Frame cap and frame-time display

- `g_settings.fps_cap` (0 = uncapped: vsync decides; 1..1000): after the present the loop waits until
  `frame_start + 1e6 / cap` (`SDL_Delay` down to ~1 ms, then a short spin; `pace_cap_wait_us`).
- Frame-time display: the **title bar**, updated every 60 frames in all modes (front end included): fps,
  average / worst frame time of those 60 frames, simulation ticks per second, and "lerp" while
  interpolating, e.g. `level 0 health 300/300 mana 0  143.9 fps  6.95 / 7.40 ms  25.0 ticks/s  lerp`;
  plus **F11** (port-only key, not a game key: `set1_scancode` maps F1..F10 only) toggles the same line as
  an overlay in the bottom-left corner of the game view / viewer (HUD font 1, white, drawn after the HUD
  inside the 2D pass, so it is part of the HUD layer when composed; visible in borderless fullscreen).
  Checked on a screenshot (`<scratch>/round7_D/ovl/ovl.png`). The game's own frame-time overlay
  (`ui_draw_debug_overlay`) stays as the original's.

### Port-only switches (main.cpp, documented in its header)

| env | meaning |
|---|---|
| `MC_INTERPOLATE=0/1`, `MC_FPS_CAP=n` | read by task E's `config_load` (env layer of `pacing.interpolate` / `pacing.fps_cap`; my own env reads were removed by E's merge) |
| `MC_COMPOSE=0/1` | overrides `g_settings.compose` (task B's stand-in until the config) |
| `MC_TICK_LOG=<file>` | after every simulation tick: `<tick> <net_state_checksum>`; at exit `frames N lerped M ticks T` |
| `MC_FRAME_LOG=<file>` | per drawn game-view frame: tick, time (us), alpha, camera x / y / z / yaw |
| `MC_QUIT_AFTER_TICKS=n` | quit after n simulation ticks (headless checks) |

## Verification

`pacing_test` (mc_unit_test: `tests/pacing_test.cpp ${MC_SIM_ALL} mcport/pacing.cpp`, no renderer / SDL;
zero warnings in my files), **PASS** in Debug and Release:

1. **Math** by construction: `pace_alpha` (0, half, clamp, period 0); `pace_lerp16` across the 0x8000 /
   0x10000 edges both ways, exact end points and the half-way point on the short path for a 2-D sweep of
   a / b; `pace_lerp_angle` across 0x7ff -> 0, end points for a sweep; roll (signed, wrap-aware, exact
   ends); camera lerp halfway values per field, the sign-extended and the masked representations across
   the map edge, jump detection (4 cells + 1 snaps at any alpha, exactly 4 cells lerps, z); slot identity
   (freed, newly allocated, other type, other owner, jump, across the map edge); the frame limiter.
2. **Level 38, 400 ticks** of `game_tick_sim` with TickInterp around every tick: an FNV hash of the whole
   GameState + Config + cell lists + height map + g_rng16 is identical before and after `before_tick`,
   `after_tick` and `fill` (the interpolation writes nothing); every slot's `prev_pos` equals its pre-tick
   position when it continues and its current one otherwise; the camera at alpha 0 is the previous tick's
   `player_camera` (unless snapped), at alpha 0x10000 the current one. Per tick 544.5 live slots lerped,
   4.32 snapped (new / freed / jumped); 1 camera snap in 400 ticks: tick 1, the player's spawn
   (0,0 -> 640,30080). Injected cases: a slot freed and reallocated (LIFO free stack: same slot) with
   another type -> no lerp; a 0x3000 teleport -> no lerp; a 0x40 move -> lerped from the old position;
   `hold()` and `reset()` behave. Cost of `before_tick + after_tick` over the 1000-slot pool: **5.0 us
   (Release), 97 us (Debug)** per tick.
3. **Headless mcport** (`SDL_VIDEODRIVER=dummy`, sound / music off, `MC_TICK_HZ=100`,
   `MC_QUIT_AFTER_TICKS=300`, `MC_SAVE_DIR=<test dir>`, `play 0 --faithful --set pacing.interpolate=0|1`): **0 of 300 per-tick
   state checksums differ**; the interpolated run drew 2894 of 2899 frames (Release; Debug 1325 of 1327)
   between two ticks. Screenshots 150-152 of that run looked at (`<scratch>/round7_D/run/shots_150_152.png`).

Additional runs (`<scratch>/round7_D/`):
- **Movie 0, Release mcport, `MC_FPS_CAP=144`, 200 ticks** (`demoR0` / `demoR1`, `MC_FRAME_LOG`): per-tick
  checksums identical with and without interpolation; the camera path per drawn frame (frames 100..1426,
  143.9 fps both):

  | | frames with no camera motion | mean step | max step | stdev of step |
  |---|---|---|---|---|
  | interpolate off | 87 % | 10.45 | 239.7 | 34.60 |
  | interpolate on  | 7 %  | 10.30 | 34.0  | 8.41  |

  (step = camera x / y distance between consecutive frames in world units; same mean = same speed, the
  motion is spread over every frame instead of jumping every ~6th.)
- **Composed + interpolated** (`MC_COMPOSE=1 MC_INTERPOLATE=1`, movie 0, 1280x800 dummy window): runs, 184
  of 185 frames between ticks, screenshot `<scratch>/round7_D/comp/shot100.png` (spell book page with the
  hi-res small view, pillarboxed) - task B's stand-in view renderer until A's `render_view_ext`.
- Reference tests: main.cpp, pacing.* and the section-D comment of settings.h are in none of the reference
  tests' sources (`reference_*`, `render_reference_*` do not link mcport), so they cannot change; not re-run
  for this task.
- Build note: for part of the round mcengine / `${MC_SIM_ALL}` did not compile in `build_D` because of
  task E's in-progress `demo.cpp` (a `\n` turned into a real newline in a string literal); fixed by its owner,
  everything above was built from the fixed tree.

## Deviations / gaps

- **Slope low-pass and interpolation.** The faithful `render_view` advances its terrain-slope camera nudge
  (`g_slope_x/y += (slope - g_slope) >> 3`) and the motion-blur blend once per *drawn frame* (the original
  drew once per tick), so at 144 fps they settle ~6x faster in time than at 25 Hz - already so since Phase 2
  (vsync 60 Hz), faithful path left alone. Task A's `render_view_ext` advances its own copy once per tick
  (`slope_camera`, keyed on `g_anim_tick`) - right for the rate, but with interpolation the nudge then
  **jumps at every tick boundary** while the camera glides: measured on movie 0 (Release, 144 fps cap,
  horizontal image shift between consecutive frames 600..615), the extended renderer shows one frame per
  tick with no motion (`2, 2, 0, 2, 2, 2, 2, 2, 0, ...` px) where the faithful renderer with the same
  interpolated cameras moves every frame (`1, 2, 1, 1, ...`). Fix (A's file): lerp the nudge between its
  last two per-tick values with `g_render_interp.alpha` - exact code under "requested changes". The water
  phase (`F.phase = g_anim_tick << 6`) also steps per tick; it could be lerped the same way.
- Thing identity is (cls, type, owner) + the jump limit; a slot freed and reallocated in one tick to a Thing
  of the same class / type / owner within 4 cells (e.g. two consecutive fireballs of the same caster) is
  lerped for that one tick from the old one's position - a one-tick smear of at most 4 cells, harmless. An
  exact identity needs a per-slot generation from `thing_alloc` (optional change below).
- F3 "fast" / "super fast" (thing_update_all x4 / x16 per tick): fast Things exceed the 4-cell limit per tick
  and are drawn unlerped (as without interpolation); the player's flyer can too, then the camera snaps.
- With the faithful renderer only the camera is interpolated (Things are A's extended path only, as the
  briefing specifies); moving Things then step at 25 Hz against a smooth camera.
- The front end still presents every loop iteration (its picture changes at 70 Hz); with vsync off and no
  `fps_cap` it spins a core. MC_SHOT frame numbers count presented frames as before (they are already
  frame-rate dependent); with task B's change the shot is written after the present.
- `vsync` is task B's / E's platform option (`Platform::set_vsync`, `PlatformOptions.vsync`); "uncapped"
  means vsync off and `fps_cap = 0`.

## Settings (settings.h section D - fields existed, comments refined)

| field | default | range | meaning |
|---|---|---|---|
| `interpolate` | false | bool | draw the 3D view between the last two ticks (camera + Things, render-only); false = every frame shows the last tick (the original: one frame per tick) |
| `fps_cap` | 0 | 0..1000 | frame-rate limit, 0 = uncapped (vsync decides) |

Proposed playing defaults (task E's `config_playing_defaults`): `interpolate = true`, `fps_cap = 0` with
`vsync = true`; `--faithful` -> `interpolate = false`.

## Requested shared-file changes

None required. Optional improvements:

1. `render_ext.cpp` (task A), `slope_camera`: interpolate the per-tick slope nudge (removes the stall frame
   per tick measured above; without interpolation `alpha` is unused and the result is today's):
   ```cpp
   int32_t s_slope_px = g_slope_smooth_default[0], s_slope_py = g_slope_smooth_default[1];  // previous tick's
   ...
       if (s_slope_tick != g_anim_tick) {
           s_slope_tick = g_anim_tick;
           s_slope_px = s_slope_x; s_slope_py = s_slope_y;               // new: keep the previous value
           ... (unchanged low-pass) ...
       }
       int32_t ox = s_slope_x, oy = s_slope_y;
       if (g_render_interp.active) {
           const int64_t a = g_render_interp.alpha;
           ox = s_slope_px + (int32_t)(((int64_t)(s_slope_x - s_slope_px) * a) >> 16);
           oy = s_slope_py + (int32_t)(((int64_t)(s_slope_y - s_slope_py) * a) >> 16);
       }
       cam.cam_x += ox;
       cam.cam_y += oy;
   ```
   Optionally the water phase likewise: `F.phase = g_render_interp.active ? (int32_t)(((g_anim_tick - 1) << 6) +
   (g_render_interp.alpha * 64 >> 16)) : (int32_t)(g_anim_tick << 6);`.
2. `thing.cpp` (task C), exact slot identity: a port-only generation counter outside GameState
   (`uint16_t g_thing_gen[]` sized with the pool, `++g_thing_gen[idx]` at the end of `thing_alloc`), exposed
   in thing.h; `TickInterp` would compare it instead of / besides (cls, type, owner).

## mcport integration

main.cpp is mine; merged this round:
- **Task B (compose)**, from `docs/analysis/port_compose.md` "mcport integration", all 7 items:
  `#include "compose.h"`, `present_frame()` / `draw_view_frame()`, `compose_install()` when
  `g_settings.compose == 1` (plus `MC_COMPOSE` env as the stand-in), the composed `MC_SHOT`, both presents
  through `present_frame()`, the level / demo 2D pass and the viewer through `draw_view_frame` (the viewer
  now calls `render_view_frame`, identical to `render_view` without the override). Builds, runs (above).
- **Task E (config / gamepad / save anywhere)**: `<scratchpad>/round7_E/integrate.py` applied unchanged to
  main.cpp (all anchors matched): `config_load` at start (playing defaults < mcport.ini < env < command line,
  `MC_SAVE_DIR`), `load SLOT` mode, tick rate from `s_opts.tick_hz`, gamepad init / poll / shutdown and its
  key / pointer / button feed, port keys (save / load slots, quit-now replaces the hard-wired F12), save /
  load between ticks (a load calls `s_interp.reset()`), my MC_INTERPOLATE / MC_FPS_CAP env reads removed.
  Added by me: `plat.set_vsync(false)` when `[video] vsync = 0` and `plat.set_fullscreen(true)` for
  `[video] fullscreen` (B's platform calls, which E's diff left out); the window size options are not
  applied (`plat.init` still opens 320x200 x4). Builds with zero warnings; `pacing_test` passes; a Release
  run with the **playing defaults** (`play 38`, extended renderer at 80 cells, composed 1280x800, pool 4000,
  interpolation) vs the same with `--set pacing.interpolate=0`: 250 tick checksums identical, 1347 of 1349
  frames between ticks, screenshot `<scratch>/round7_D/playdef/shot1.png` looked at.
- **Task A**, from `docs/analysis/port_render_ext.md` "mcport integration": without the compositor,
  `g_render_view_override = render_view_ext_target` when `g_settings.render_extended` (env `MC_RENDER_EXT`
  overrides it). Composed, B's override calls `render_view_ext` itself. A's Thing renderer reads
  `g_render_interp.prev_pos` / `alpha` filled here. Checked headless on movie 0 (Release, 144 fps cap,
  200 ticks): extended + interpolated and extended + composed + interpolated both run, per-tick checksums
  identical to the plain run, 1425 / 1426 and 1373 / 1374 frames drawn between ticks; screenshots
  `<scratch>/round7_D/ext/grid.png`, `extc/grid.png`, strips `ext0/`, `fai1/`.
- **Task C**: the title bar still uses `r.thing % MC_THING_SLOTS` for the local player's Thing (round-6
  code); C's report may want `thing_at(r.thing)` there.

## Next round

- Measure the whole frame (A's extended renderer + B's present) with interpolation at 1080p / 1440p and
  decide the playing defaults (vsync on, cap off).
- The interpolated slope nudge / water phase in the extended path (change 1 above) and the exact slot
  generation (change 2); then re-measure the per-frame image shift (it should have no stall frames).
- If the extended renderer moves to threads / GPU: keep `TickInterp` as the single source of the lerp
  data (it is engine-independent; it could move to mcengine if a second front end needs it).
- F11 could become a config key binding (task E's `[keys]`); apply `[video] window_width / height`.
