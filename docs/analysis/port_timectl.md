# Port round 10, task D: time control, debug camera, screenshots, log, tick profiler, main.cpp integration (2026-10-08)

Briefing: `docs/port/BRIEFING_round10.md` task D. Everything here is port-only and outside the simulation:
with default settings and no key pressed mcport runs exactly as before (the x1 schedule is the old loop, the
profiler markers are clock reads behind a null pointer, the debug camera is only read in the draw-only pass).
All gates I can affect pass (list in "Verification").

## Keys (the defaults the user learns)

None of these is a key the game reads (`input.h g_input_bindings`; `set1_scancode` maps none of Pause / End /
PageUp / PageDown / Insert / Delete, F11 is not a game key). They work in `play`, `rts`, `load`, the viewer and
`demo` (not the attract demo); time control is refused in a network game.

| key | action |
|---|---|
| **Pause** | pause / resume the simulation (the view, HUD, menus, console keep running; interpolation holds) |
| **End** | step one tick (pauses first); **Shift+End** ten ticks |
| **Page Up** | faster: x2, x4 .. x64, then "max" (as fast as possible) |
| **Page Down** | slower: .. x1/2 .. x1/16 |
| **Insert** | normal speed (x1) |
| **Delete** | (a level) free debug camera on / off: W/S/A/D move, Q/E turn, R/F up / down, Up/Down pitch, Z/X roll, +/- zoom, Shift fast, the mouse looks around; the game's keys and buttons are not fed while it is on |
| **Shift+Delete** | teleport the wizard to the debug camera (task B's debug packet 0x40: recorded / replayed; refused where debug packets are not allowed) |
| **Ctrl+F11** | screenshot of the next presented frame (PNG; the composed native-resolution image when composing) into `<save dir>/screenshots/` (or `MC_SHOT_DIR`) |
| **F11** | cycles: off -> the frame-time line -> + the tick profile line -> off |

The other round-10 tools merged into main.cpp have their own keys: **Backquote** (`) the console (task B),
**Home** / middle mouse the cursor mode with click-to-inspect, **keypad 1..6 / 0** the overlays (task C;
`port_inspect.md` section 6).

The state shows in the bottom-left corner of the view whenever it is not the default (`TIME paused, 3 steps`,
`TIME x4`, `CAMERA free (follow 123)`), on the F11 line (`[x4]`) and in the window title (movies: "(paused)").
Each command is logged (`time: x4 (speed x4)`).

Environment (headless): `MC_TIME_SPEED=max|4|1/2|x1.5` the starting speed; `MC_TICK_PROFILE=n` profile every
tick and log a line every n ticks (0 = no lines, dumps / F11 only); `MC_SHOT_DIR=dir`; `MC_LOG_LEVEL=error|warn|
info|debug`. `MC_QUIT_AFTER_TICKS` is now checked before every tick (a fast-forward frame runs many), so a tick
log has exactly N lines.

## What was built

| file | what |
|---|---|
| `src/mcport/timectl.{h,cpp}` (new) | `TimeControl`: pause / step N / speed in 1/16 steps (x1/16 .. x64, 0 = as fast as possible), `faster` / `slower` / `normal`, `set_speed_text` ("4", "x1/2", "0.5", "1/4", "max", "normal"), `describe`; the frame scheduler `begin_frame` / `want_tick` / `tick_ran` / `end_frame` / `period_us` / `holding`. Pure maths, no SDL. |
| `src/mcport/mclog.{h,cpp}` (new) | the log: `mclog(level, fmt, ...)`, levels error / warn / info / debug, `<save dir>/mcport.log` with rotation to `mcport.1.log`, console mirror (stdout for info / debug, stderr for warn / error, the bare message as before), a 512-line ring buffer with sequence numbers (`mclog_since`) and callback sinks (the console's scrollback), thread-safe. |
| `src/mcport/screenshot.{h,cpp}` (new) | PNG writer (8-bit RGB, stored deflate blocks: no zlib), PPM writer, a reader for its own PNGs (tests), `shot_resolve_path`. |
| `src/mcengine/tick_profile.{h,cpp}` (new) | the tick profiler: `TickProfile` (per phase of `game_tick_sim`, the list pass and per Thing class of `thing_update_all`), `g_tick_profile` (null = off), `TickProfileWindow` (sums, `format()` for the F11 line, `json()` for dumps), `g_tick_profile_dump` (for A's JSON dumps). The markers are header-only (inline variable + inline functions), so every `${MC_SIM_CORE}` test links unchanged. |
| `src/mcengine/debug_camera.h` (new) | `DebugCamera g_debug_camera` {active, cam} (header-only inline variable). |
| `src/mcengine/hud.cpp` | the camera override seam (below). |
| `src/mcengine/player.cpp`, `thing.cpp` | the profiling markers (below). |
| `src/mcport/main.cpp` | time control in the three tick loops, the debug camera, `shot`, the log (every `printf` / `fprintf(stderr)` / `notice` is `mclog` now), the profiler, the keys, the integration of A / B / C / E. |
| `src/tests/timectl_test.{cpp,cmake}`, `src/tests/mclog_test.{cpp,cmake}` (new) | tests. |

### Time control (main.cpp + timectl.h)

The tick loops of a level (`RUN_LEVEL`), a movie (`RUN_DEMO`) and the viewer ask `TimeControl` whether to run
the next tick: `tc.begin_frame(now, &next_tick); while (!quit_ticks_reached() && tc.want_tick(now, &next_tick,
base_period, cap, t, spent_us)) { t++; tick; tc.tick_ran(); ... } tc.end_frame(now, &next_tick);` and the
interpolation alpha uses `tc.period_us(base_period)`.

- **x1, running = the old loop exactly**: up to `cap` due ticks per frame (4 in a level, 32 for movies / the
  viewer), the clock restarted at `now` after the cap-th tick. Checked frame by frame against the old loop
  over 20000 random frame intervals (timectl_test).
- **Paused**: no tick; the tick clock follows `now`, so resuming starts with one tick, not a burst;
  `s_interp.hold()` (pacing.h) keeps the view still. **Steps** run while paused, all in the next frame, up to
  a 40 ms budget per frame (the rest in the following frames).
- **Faster** (x2 .. x64): the period divided by the speed, the cap scaled with it (x64 in a level: 256), and a
  40 ms tick budget per frame so the window stays responsive; a frame that hits the cap / budget restarts the
  clock (no catching up). **Slower**: the period multiplied. **As fast as possible**: ticks back to back until
  the budget, then one frame (a headless rts run at "max": 2000 ticks in 1.7 s with the playing defaults
  including start-up, 93 frames; at `MC_TICK_HZ=1000` the same run took 6.5 s, 503 frames).
- The pause menu still holds the level as before; the attract demo and network games use a neutral
  `TimeControl` (a network game also resets the user's to x1 with a log line). The viewer's / movie's Space
  pause is now the time control's pause.
- Never inside the simulation: the same ticks in the same order. `Config.substeps` (F3) is untouched.

### Debug camera (hud.cpp seam + main.cpp)

`hud.cpp frame_pass`:
```cpp
    // port (round 10): the debug camera (debug_camera.h) replaces the view in the draw-only pass
    const Camera cam = (g_debug_camera.active && !s_writes) ? g_debug_camera.cam
                     : (g_render_interp.have_camera && !s_writes) ? g_render_interp.camera : player_camera(local);
```
(+ `#include "debug_camera.h"`). Only the draw-only pass (`render_frame_draw`) reads it; `hud_tick_state` /
`render_frame` (the passes that write game state) use the player's camera as before. The flight view, the help
screen and the map screen's view window draw from it; the radar / HUD stay the player's. Works with both
renderers and composed output (the compositor's override receives the camera through `render_view_frame`).

main.cpp: `Delete` turns it on at the player's camera (`dcam_enable`): every game key / button is released, the
level pointer is centred (no steering), and while it is on the keyboard / mouse / controller are not fed to the
game; the viewer's `free_camera_step` moves it at 119 Hz (interpolated between steps with
`g_settings.interpolate`), the relative mouse turns / pitches it. `dcam_follow(slot)` keeps it 3 cells behind
and 2 above a Thing along the camera's yaw (ends when the slot's `thing_slot_generation` changes);
`dcam_to_cell(x, y)` puts it 4 cells above a cell; task C's follow mode supplies its own camera
(`inspect_tool_follow_camera`). `Shift+Delete` queues `debug_cmd_teleport(cam_x, cam_y, cam_z)` with
`debug_cmd_queue` (task B). The camera turns off when the level ends.

### Screenshots

`request_screenshot(name)` (key, console `shot [name]`) -> after the next `present_frame()` the presented image
(`capture_rgb`: `compose_to_rgb` at the drawable size when composing, else the palette-converted game frame)
is written by `shot_resolve_path`: no name -> `shot_YYYYmmdd_HHMMSS_<n>.png`; a name without extension gets
`.png`; `.ppm` writes PPM; relative names go into `MC_SHOT_DIR` / `<save dir>/screenshots`. Works with
`SDL_VIDEODRIVER=dummy`. `MC_SHOT=n,m` is unchanged (`mcport_shot_<n>.ppm` in the working directory; it now
shares `capture_rgb` / `shot_write_ppm`).

### Log

`mclog_open(save_dir, "mcport <args>")` at start-up (before `config_load`; its warnings, printed by config.cpp,
are added to the file only). File lines `[   12.345] I message` (seconds since start, E / W / I / D), header
`# mcport log 2026-10-08 08:08:08 - mcport <args>`. Console output unchanged in content (the bare message on
stdout, errors / warnings on stderr). Every `std::printf` / `std::fprintf(stderr, ...)` / `notice` of main.cpp
goes through it; the tick log / frame log files stay plain files. `mclog_close()` at exit.

### Tick profiler

Markers (exact code, the only changes to these two files):

`player.cpp game_tick_sim` (+ `#include "tick_profile.h"`):
```cpp
    TickProfile *const tp = g_tick_profile;
    int64_t tp_t = tp ? tick_profile_now_ns() : 0;
    ... palette_effect_update ...   if (tp) tick_profile_mark(tp, TP_PALETTE, &tp_t);
    ... local input ...             if (tp) tick_profile_mark(tp, TP_INPUT, &tp_t);
    player_commands_process();      if (tp) tick_profile_mark(tp, TP_COMMANDS, &tp_t);
    ... game_check_level_won ...    if (tp) tick_profile_mark(tp, TP_WIN, &tp_t);
    ... thing_update_all x substeps if (tp) tick_profile_mark(tp, TP_THINGS, &tp_t);
    ... g_hook_mode_tick ...        if (tp) tick_profile_mark(tp, TP_MODE, &tp_t);
    ... g_hook_sound_update ...     if (tp) tick_profile_mark(tp, TP_SOUND, &tp_t);
    ... g_hook_frame_state ...      if (tp) tick_profile_mark(tp, TP_FRAME, &tp_t);
```
`thing.cpp thing_update_all` (+ `#include "tick_profile.h"`), after the pause check:
```cpp
    TickProfile *const tp = g_tick_profile;
    const int64_t tp_t0 = tp ? tick_profile_now_ns() : 0;
    if (tp) tp->substeps++;
    ... free pass, list pass, wake / threat / mana hooks ...
    if (tp) tp->lists_ns += tick_profile_now_ns() - tp_t0;
    ... handler pass:
            if (fn) {
                if (tp) {
                    const int64_t a = tick_profile_now_ns();
                    fn(t);
                    tick_profile_add_class(tp, cls, tick_profile_now_ns() - a);
                } else {
                    fn(t);
                }
            }
```
Host (main.cpp `sim_tick`): `tick_profile_begin` / `_end` around the tick, the per-tick profile added to the
F11 window (`TickProfileWindow::format(3)` every 60 frames), to the log window (`MC_TICK_PROFILE=n`: `profile
ticks 1-100: tick 0.09/0.27 ms: things .08 (lists .01 scenery .02 creature .02 effect .00 player .00)`) and to
`g_tick_profile_dump` when set (MC_TICK_PROFILE). The profiler is on while F11 shows its line or
MC_TICK_PROFILE is set. The F11 line: `tick <avg>/<worst> ms: in cmd win things (lists <class> <class> ..) mode
snd hud` (phases under 0.005 ms left out, classes above 1 % of the tick, most expensive first).

In task A's JSON dumps (merged in main.cpp through A's `dump_set_extra`, with MC_TICK_PROFILE set) the member
`"tick_profile"` covers the ticks since the previous dump:
```cpp
    dump_set_extra([] {                                         // task D's tick profile in every dump (MC_TICK_PROFILE)
        if (!g_tick_profile_dump || g_tick_profile_dump->ticks <= 0) return std::string();
        std::string j = "\"tick_profile\": " + g_tick_profile_dump->json();
        g_tick_profile_dump->clear();
        return j;
    });
```
JSON: `{"ticks":300,"avg_us":112.13,"worst_us":232.00,"phases_us":{"palette":0.08,"input":0.23,"commands":0.47,
"win":0.08,"things":111.08,"mode":0.03,"sound":0.04,"frame":0.03},"lists_us":9.92,"classes":{"scenery":{"us":6.05,
"calls":150.0},"player":{"us":5.45,"calls":8.5},"creature":{"us":17.78,"calls":191.9},...}}` (per-tick averages).

## Verification

- `timectl_test` (Release, 11 s; exit 0, zero warnings):
  - x1: 20000 random frames (0.1 .. 250 ms), 8564 ticks, **0 frames differ from the old loop** (ticks per
    frame and the clock).
  - 10 s at 60 fps / 25 Hz: x1 250, x4 999, x1/2 125, x64 15974 (x64 at 2 ms per tick: 11981 = the 40 ms budget,
    20 per frame); "max" at 0.1 ms per tick: 24000 in 1 s (400 per frame). Pause / steps / budgeted steps /
    resume, ladder (`x2 x4 x8 x16 x32 x64 max` / `x64 .. x1/16`), 16 speed texts.
  - profiler: level 38, 300 ticks with and without `g_tick_profile`: **0 of 300 checksums differ**; every
    phase, the class calls (player / creature / effect) and sums consistent (classes + lists <= things <= total).
    Wall time of the 300 ticks (incl. a checksum per tick) 198 ms off / 187 ms on - within noise.
  - PNG: CRC-32 of "123456789" = 0xcbf43926, 4 random images round trip (one > 64 KiB: several stored blocks),
    a flipped byte is rejected.
  - mcport (Debug and Release) `play 0 --faithful`, 300 ticks at 100 Hz, three runs: plain; with injected keys
    (Pause, three End steps, resume, PageUp x2, PageDown, Insert, Pause, two steps, Ctrl+F11, resume); with
    `MC_TIME_SPEED=max`: **0 / 300 and 0 / 300 tick checksums differ** from the plain run; the second run's log
    has every `time:` line, the screenshot line and the profile lines; the PNG decodes (320x200, looked at:
    "TIME PAUSED" in the corner); the third run rotated the second's log to `mcport.1.log` byte for byte; the
    "max" run took 7 frames for 300 ticks.
- `mclog_test` (exit 0): level parsing; threshold (debug hidden at info, info hidden at warn); file header and
  line format; multi-line and > 1024-byte messages; ring buffer (`mclog_since`, the 512-line limit); sinks
  (two sinks, removal, a sink that logs itself); 4 threads x 200 lines (all 800 whole lines in the file);
  rotation twice; no directory = no file.
- Headless looks (screenshots read): `play 0` with Delete + R / E / W held: the view rose and turned, "CAMERA
  FREE" in the corner; `rts --seed 1 --bots 3` composed (playing defaults, 1280x960): the debug camera flies over
  the generated level; Shift+Delete: `teleported player 0 to 28544,18036 z 5215 (cell 111,70)` (B's message).
- Two `rts --seed 7 --bots 3` runs, `MC_TICK_HZ=1000` vs `MC_TIME_SPEED=max`, 2000 ticks: identical tick logs.
- `mcport --replay-check 0` (E's, merged): `OK movie 0: identical, 8551 ticks compared (checksum + 35 parts each)`.
- Gates run in build_D (Release): reference_test, reference_levels, reference_gen, reference_player,
  reference_player_test, sim_test, pacing_test, game_menu_test, net_test, render_reference_test,
  render_reference_hud, render_reference2_test, render_reference_options, render_reference_fe_test, hud_test,
  timectl_test, mclog_test: **17 / 17 pass** before the merges; after merging A / B / C / E, the full ctest in
  build_D (Release, the whole tree as it stood at 08:40 incl. the other agents' new tests): **59 / 59 pass**;
  timectl_test / mclog_test also pass in Debug.
- Every mcport mode after the merges (Release, headless, with Pause / End / PageUp injected): `demo 0`, viewer
  `38`, `play 5`, `rts`, `load 1` (an rts state), the front end (runs until killed), `--replay-check 0`,
  `--scenario`, `--console-stdin`: all run; MC_SHOT / MC_TEST_INPUT / MC_TICK_LOG / MC_QUIT_AFTER_TICKS work.

## Deviations / gaps

- F11 used to toggle with any modifier; now plain F11 cycles three states (Ctrl+F11 is the screenshot). F11 with
  the pause menu open goes to the menu (before: toggled the line).
- The viewer's Space pause and the movie pause are the time control's pause now (same effect; the title shows
  "(paused)").
- The level's free camera does not collide with anything and has no pitch limit (as the viewer's).
- Time control speeds above x1 drop ticks the machine cannot run within the 40 ms budget (by design: the
  wall-clock rate is what changes, never the tick sequence).
- The tick profiler measures wall time with `std::chrono::steady_clock` (QPC on Windows); with the profiler on,
  each Thing update costs two clock reads (~50 ns): fine for the F11 line, slightly inflates small classes.
- Debug keys are main.cpp constants until the integrator moves them into `PlatformOptions` (below).

## Settings / ini keys (requested: config.h / config.cpp, integrator)

No `settings.h` field (nothing in the engine reads a time / log setting). Proposed `[keys]` chords and a
`[log] level` key, so the user can rebind them:

config.h `PlatformOptions` ([keys] block):
```cpp
    // round 10 (task D, main.cpp): time control, debug camera, screenshot
    KeyChord time_pause, time_step, time_step10, time_faster, time_slower, time_normal;
    KeyChord debug_camera, camera_teleport, screenshot;
    // [log]
    std::string log_level = "info";   // MC_LOG_LEVEL: error | warn | info | debug
```
config.cpp `PlatformOptions::PlatformOptions()`:
```cpp
    time_pause = {SDL_SCANCODE_PAUSE, 0};
    time_step = {SDL_SCANCODE_END, 0};
    time_step10 = {SDL_SCANCODE_END, KEYMOD_SHIFT};
    time_faster = {SDL_SCANCODE_PAGEUP, 0};
    time_slower = {SDL_SCANCODE_PAGEDOWN, 0};
    time_normal = {SDL_SCANCODE_INSERT, 0};
    debug_camera = {SDL_SCANCODE_DELETE, 0};
    camera_teleport = {SDL_SCANCODE_DELETE, KEYMOD_SHIFT};
    screenshot = {SDL_SCANCODE_F11, KEYMOD_CTRL};
```
config.cpp `k_desc[]` (after `{"keys", "menu", ...}`):
```cpp
    {"keys", "time_pause", K_KEY, PO(time_pause), 0, 0, nullptr, "pause / resume the simulation (not in a network game)"},
    {"keys", "time_step", K_KEY, PO(time_step), 0, 0, nullptr, "run one tick (pauses first)"},
    {"keys", "time_step10", K_KEY, PO(time_step10), 0, 0, nullptr, "run ten ticks (pauses first)"},
    {"keys", "time_faster", K_KEY, PO(time_faster), 0, 0, nullptr, "simulation faster: x2 .. x64, then as fast as possible"},
    {"keys", "time_slower", K_KEY, PO(time_slower), 0, 0, nullptr, "simulation slower: down to x1/16"},
    {"keys", "time_normal", K_KEY, PO(time_normal), 0, 0, nullptr, "simulation at normal speed (x1)"},
    {"keys", "debug_camera", K_KEY, PO(debug_camera), 0, 0, nullptr, "in a level: free debug camera on / off (the game's keys are not fed while it is on)"},
    {"keys", "camera_teleport", K_KEY, PO(camera_teleport), 0, 0, nullptr, "teleport the wizard to the debug camera (debug command)"},
    {"keys", "screenshot", K_KEY, PO(screenshot), 0, 0, nullptr, "screenshot of the next frame (PNG in <save dir>/screenshots)"},
    {"log", "level", K_STRING, PO(log_level), 0, 0, "MC_LOG_LEVEL", "mcport.log / console detail: error | warn | info | debug"},
```
main.cpp then: delete `struct DebugKeys` / `s_dkeys` and replace `s_dkeys.` by `s_opts.` in `debug_key_action`;
the log level: after `config_load`, `int lv; if (mclog_parse_level(s_opts.log_level.c_str(), &lv)) mclog_set_level(lv);`
(MC_LOG_LEVEL is read before config_load today so that config's own lines obey it; keep both).
`config_parse_key` needs no new names (Pause, End, PageUp, PageDown, Insert, Delete, F11 exist).

## Shared-file changes

- `player.cpp`, `thing.cpp`: the markers above (mine this round; reference / sim / net gates identical).
- `hud.cpp`: the camera seam above (mine this round; render_reference / hud_test identical).
- No change requested in `src/CMakeLists.txt`: the new mcengine / mcport files are picked up by the globs, the
  tests by their `.cmake` files.

## main.cpp integration (what I merged)

All four reports arrived while I was working; **everything they asked of main.cpp is merged** (exact code as in
their reports unless noted), built with zero warnings (Debug + Release, build_D) and exercised headless.

- **Task E** (`port_desync.md` "main.cpp integration"): `#include "replay_run.h"`; `replay_run_parse` before
  `config_load`; `desync_run_install()` + `if (replay.active) return replay_run(...)` right after `engine_init`;
  the MC_TICK_LOG line is `tick_log_write(s_tick_log, s_ticks_run)`; the usage line. Checked: `mcport <game>
  --replay-check 0` -> `OK movie 0: identical, 8551 ticks compared (checksum + 35 parts each)`, exit 0.
- **Task A** (`port_mode.md` "main.cpp integration"): `#include "mode_level.h"`; `rts --ticks N` ->
  `s_quit_after_ticks`; `dump_configure_from_env()` at start-up, `if (s_rts) rts_after_tick(..); else
  dump_after_tick(..)` in `sim_tick`, `if (s_rts) rts_end(); else dump_final(..)` at the end; loading an rts state
  in `load_state_slot` (`mode_start_run_for_state`, a campaign state ends the run). Deviation from the report:
  instead of `in_level = false` (which would run `game_after_frontend` in the middle of a level) a local
  `force_begin` makes the in-level branch call `game_level_begin(MODE_LEVEL_INDEX)` again. The rts usage line;
  the window title says `rts` instead of `level 256` (A's item 4; the pause menu's slot label in game_menu.cpp is
  left to the integrator). The tick profile goes into every dump through A's `dump_set_extra` (a lambda that
  writes `"tick_profile": {...}` from `g_tick_profile_dump` and clears it; MC_TICK_PROFILE turns it on).
  Checked: `rts --seed 3 --bots 2 --ticks 600 --dump-every 300` at "max": two dumps with `tick_profile`, exactly
  600 ticks; Ctrl+F1 in an rts run then `mcport <game> load 1`: `state loaded from slot 1 (level 256)`, runs.
- **Task C** (`port_inspect.md` section 5): includes `debug_overlay.h` / `inspect_tool.h`; `inspect_tool_key`
  first in the key loop (before D's keys); `cursor_mouse` (`inspect_tool_mouse`) before the device block; cursor
  mode keeps the game's pointer centred and hides the mouse buttons from the game; the inspector's follow camera
  overrides `g_debug_camera` for the frame; `debug_overlay_frame_begin` / `debug_overlay_draw` (after
  `ui_draw_debug_overlay`) / `debug_overlay_frame_end`; `debug_overlay_reset()` after a level start from the
  front end and after a state load. Checked: `play 3` with keypad 1 / 2 and Home: grid, labels and the cursor
  box drawn (screenshot looked at).
- **Task B** (`port_console.md` section 7): the whole diff - `console.h`, `s_console` / `s_con_opts`,
  `console_parse_args` before `config_load`, `s_console.init` + stdin reader, the `--scenario` header choosing
  the run (`play` / `rts` from the file), `run_file` before the loop, the open console taking every key
  (quit_now still works), the console key opening it (game input released), `poll_stdin` + the `HostCmd`
  executor (time / shot / save / load / sync / dump / cam / overlay / inspect / quit), the device block skipped
  while the console is open, `s_console.draw` in both draw lambdas, `s_console.after_tick()` in `sim_tick`,
  `return s_console.exit_code()`. Two fixes to B's executor: (1) the `time` branch toggled the pause for every
  sub-command other than `pause` while paused (`time speed 4` while paused resumed instead) - now `speed`,
  `toggle`, `pause` (only when running), `resume` / `run` (only when paused) are separate; (2) the debug camera
  does not read the keyboard / mouse while the console is open (typing W would fly it). `dcam_to_cell` logs the
  camera position. Checked: every `src/tests/scenarios/*.scn` through `mcport --scenario` (Release, playing
  settings, "max"): give_mana 8/8, input 4/4, spawn_kill 11/11, teleport 6/6; **god.scn 5/8 and rts_debug.scn
  12/13 fail as B predicted until the integrator adds `g_hook_debug_tick`** (B's section 6.1: player.h +
  player.cpp, exit code 1 reported correctly); stdin: `time pause`, `time step 5`, `time speed 4`, `time run`,
  `time speed 1/2`, `time normal`, `cam free`, `cam to 50 50`, `shot NAME`, `dump`, `sync`, `overlay labels on`,
  `cam off`, `quit` all act and log (screenshots looked at).
- Mine for B: `debug_cmd_pump()` before every tick in `sim_tick` and the applied-packet messages
  (`debug_cmd_take_messages`) logged after it; `Shift+Delete` uses `debug_cmd_queue(debug_cmd_teleport(...))`.

Still for the integrator (not main.cpp): B's `g_hook_debug_tick` (player.h / player.cpp - in `game_tick_sim`
put it right after `if (g_hook_mode_tick) g_hook_mode_tick();`, i.e. before my `tick_profile_mark(tp, TP_MODE,
&tp_t)`, so it is counted in the mode phase), A's `sim_all.cpp` / CMake items, the config keys of B / C / D.

## Next round

- Move the debug keys / log level into config (above) and the pause menu's options page (log level).
- A "time" row in the pause menu, and a gamepad binding for pause / step (e.g. Back+Start).
- The tick profiler per creature type / effect type (class x type table) once the RTS units land (round 15),
  and the render side (frame phases: landscape / things / HUD / compose / present) on the same F11 line.
- The debug camera as the RTS overview camera's starting point (round 16): ground collision, pitch limits,
  edge scrolling with the absolute pointer of task C's cursor mode.
