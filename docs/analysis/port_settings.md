# Port round 7, task E: settings, config file, controller, save anywhere

(Written by the integrator from task E's final message; the agent could not write .md files. The exact
main.cpp integration diff is `<scratchpad>/round7_E/main_integration_final.diff`, `integrate.py` there
applies it by anchor.)

## What was built
- `src/mcport/config.h/.cpp` (new): `PlatformOptions` (window, audio, tick, movie dir, keys, pad),
  `bool config_load(const char *save_dir, int *argc, char **argv, PortSettings*, PlatformOptions*)`, a
  71-key description table covering every PortSettings field (A..E) and the platform options, ini
  reader / writer, key-chord and pad-action parsing, `config_key_action()`, `config_playing_defaults()`.
- `src/mcport/gamepad.h/.cpp` (new): `GamepadMapper` (pure mapping pad -> the game's keyboard / mouse
  device state, no SDL) + SDL half (`gamepad_init / poll / shutdown`, hot-plug; compiled out with
  `MC_GAMEPAD_NO_SDL` for the test).
- `savegame.h/.cpp`: save anywhere (`savestate_*`, slots 0..9), `savestate_install_quick_hooks()`.
- `demo.h/.cpp`: full quick save / load hooks (`g_hook_demo_quick_save` / `_load`), `demo_repair_cell_lists()`,
  extended-pool movies, `thing_pool_ext_reset()` after a snapshot load (task C's request), pool-size
  override while a movie plays.
- `settings.h` section E: `bool quicksave_full = false`.
- `tests/config_test.cpp/.cmake`.

## Config file and precedence
- `mcport.ini` in `SDL_GetPrefPath("Bullfrog","MagicCarpet")` (or `MC_SAVE_DIR`); the first run writes every
  key commented out at its default (`# key = value`; descriptions are `## ...`), so commented keys follow
  future defaults. Keys added by newer versions are appended (commented); unknown keys are kept and warned.
- Precedence (lowest first): playing defaults < ini < environment < command line. Env kept: `MC_TICK_HZ`,
  `MC_SOUND`, `MC_SOUND_VOLUME`, `MC_MUSIC` (0 = off), `MC_MUSIC_VOLUME`, `MC_MOVIE_DIR`, `MC_INTERPOLATE`,
  `MC_FPS_CAP`, new `MC_FAITHFUL=1`. Command line, left to right: `--faithful` (PortSettings{}),
  `--set section.key=value` (repeatable), `--config file`. Recognised options are removed from argv;
  an unknown `--option` makes config_load fail. Out-of-range values are clamped, malformed ones ignored
  (with warnings). Final audio / tick / movie values are exported to the environment so existing env
  readers work unchanged.

## Settings and proposed playing defaults
- Section E: `quicksave_full` (0): Alt+S (cmd 10) / quick load (cmd 0xb) save / load the complete state in
  slot 0 instead of the original's gam10000.dat.
- Playing defaults (tests / references keep faithful): `render.extended=1 draw_distance=80 fog_start_pct=75
  lod=1`, `display.compose=1 view 0x0 hud_scale_mode=0 hud_corners=1`, `game.thing_slots=4000
  quicksave_full=1`, `pacing.interpolate=1 fps_cap=0`.
- Platform: window 1280x960, fullscreen 0, display 0, vsync 1; sound 1 / 96, music opl / 256; tick_hz 25.
- Keys: quit F12; Ctrl+F1..F9 save slots 1..9, Shift+F1..F9 load; Ctrl/Shift+F10 = slot 0 (the quick slot).
  The game reads F1..F10 only without modifiers. F11 = task D's pacing overlay.
- Pad: dead zones 0.25 (left) / 0.12 (right), stick threshold 0.5, trigger 0.3, steer sensitivity 1, curve
  1.5, invert_y 0, cursor speed 700, 17 `bind_<button>` keys.

## Controller
- Flight: right stick = pointer offset from the centre (the mouse steering; radial dead zone, power curve,
  re-centres on release); left stick = held Up/Down/Left/Right; A = Enter (book / map), B = Space,
  LT / RT = cast left / right, LB / RB = quick-select prev / next into the left hand (keys 1..0), X / Y the
  same into the right hand (Ctrl+1..0), Back = Esc, Start = P, d-pad up / down = `[` / `]`. Actions are
  bindable: any game key chord, cast_left/right, spell_next/prev[_right], save_quick, load_quick, none.
- Spell book / menus: stick and d-pad move the pointer, A = left click, X = right click, B = Enter in the book
  or Esc in menus.
- Every press is released with the action that started it (no stuck keys over context changes / unplug).
  Hot-plug rescans twice a second while no pad is open.

## Save anywhere
- `<save dir>/state%02d.mcs`: 96-byte header (level, tick, thing slots, time, 640x480 flag, checksum, name)
  + chunks `GAME` (port-form GameState), `CONF` (Config without demo_file, record / play / network flag bits,
  pentium, language, frame / net time, texture pool, PIT fields), `MAPS` (4 maps, cell heads, corner table),
  `GLOB` (g_rng16, snapshot base, projectile null index, AI human wizard, AI seed, g_terrain_nearly_flat,
  g_timer_ticks, g_video_mode_flags, dummy player block), `CAMP` (the 142-byte front-end save record +
  g_fe_game_in_progress), `POOL` (only for pools > 1000: extension Things and stacks via task C's
  `thing_pool_ext_*`).
- Load: all chunks validated before anything changes, unknown chunks skipped, written via `.tmp` + rename;
  refused in network games and while a movie plays / records; option bytes +0x2195..+0x21b8 kept (as the
  original's load); an extended-pool save brings its own pool size, a 1000-slot save loads into any pool;
  cell lists checked / repaired, then the texture reload hook.
- After a restart: `mcport <dir> load N`, or Shift+Fn in the front end (starts the level first, switches the
  resolution if the save was made in the other mode - castle sizes depend on it). Campaign state returns too.
- `demo_repair_cell_lists`: rebuilds broken lists from the Things' own links (repairs the original's
  quick-load damage exactly), otherwise relinks everything in index order.

## Extended-pool movies
- With `thing_pool_slots() != 1000` the recorder writes `mvx%05d.dat` (16-byte "MCPX" header: version 1,
  thing_slots, flags; then the packets), `gax%05d.dat` (port-form GameState + u32 slots + extension Things),
  `max%05d.dat` (map file). The original only opens `mvi` files, so it cannot pick them up. `demo_open`
  plays `mvi` with 1000 slots forced, else `mvx` with the header's pool size; bad headers are refused.
  The override is cleared in `demo_close`. Faithful recordings are unchanged.

## Verification (Debug, build_E)
- `config_test` passes (~5 s): all 71 keys round-trip; clamping / unknown / malformed warnings; precedence
  file 30 < env 40 < --set 50; `--faithful` order; `MC_FAITHFUL`, `--config`, argv compaction, appending
  missing keys; gamepad mapping incl. context switches and unplug.
- Save anywhere: save at T, run N, load, run N again (and again after loading another level in between):
  identical per-tick checksums on level 3 (T 300, N 300), level 44 (T 400, N 300), level 49 with a full
  1000-slot pool (T 100, N 200) and level 49 with 4000 slots (36 Things in the extension).
- Quick load: the original's gam10000.dat load leaves 181 cell-list problems on level 1; the full quick save
  via commands 10 / 0xb gives an identical state, 0 problems, the next 100 ticks identical.
- Cell repair: injected cycle repaired, stale heads rebuilt exactly.
- Extended movie: level 2 with 3000 slots, 240 packets, plays back under faithful settings at 3000 slots; a
  999-slot header is refused.
- Reference gates pass: reference_test (1396 ticks, 0 divergences), reference_levels, reference_gen,
  reference_player (4 levels), reference_player_test (4 round trips byte-identical), frontend_test. Zero
  warnings. (At the very end reference_test.cpp did not compile in build_E because another agent was editing
  it; the passing runs used E's final demo.cpp.)
- mcport with the integration (headless): Ctrl+F10 saved, Shift+F10 loaded, `load 0` in a fresh process
  restores the same view after the fade-in (`scratchpad/round7_E/saveload_grid.png`).

## Gaps
- SDL controller path not run on real hardware (mapping only); no rumble. Left stick digital (the game has
  no analog speed input).
- `[video]` options parsed but not wired into task B's platform (main.cpp still `plat.init(320, 200, 4)`).
- Loading another level's state while in a level skips `game_level_end` (no stats / campaign step).
- After a load that started a level main.cpp sets `fade_stage = 0` (palette only).
- Not saved: `g_spell_pickup_keep_flags` (always 0 in retail), game.cpp statics.

## Requested shared-file changes
None required. Each new PortSettings field needs a line in `k_desc[]` (config.cpp) and in
`config_playing_defaults` if its playing default differs. `MC_COMPOSE` (B's env override) could become a
config env name like the pacing ones.

## mcport integration (main.cpp, owned by D)
See the diff. Includes config.h / gamepad.h / savegame.h / <cstdarg>; `s_opts`, `s_pad_map`, `s_save_dir`,
helpers `notice`, `state_slot_level`, `load_state_slot`; at start SDL_GetPrefPath / MC_SAVE_DIR,
`config_load`, settings summary print; removes D's MC_INTERPOLATE / MC_FPS_CAP env reads (config keys now);
`load SLOT` mode; `tick_hz = s_opts.tick_hz`; `gamepad_init()` after plat.init,
`savestate_install_quick_hooks()` after demo_set_record_dir; per frame: port keys through
`config_key_action` (swallowed; replaces the F12 check; D's F11 stays), `gamepad_poll` +
`s_pad_map.update` (pad key events fed like keyboard events, pad mouse buttons ORed, steering / cursor into
`s_level_mx/my`, menus via warp_pointer), save / load before the viewer keys; `gamepad_shutdown()` at exit.
Usage line: `mcport [game dir] [play N | load SLOT | demo N | network | LEVEL] [--faithful] [--set section.key=value] [--config file]`.

## Next round
Wire `[video]` into the platform; in-game slot menu; controller on real hardware, analog speed; audio / net
reading PlatformOptions directly; network negotiation of thing_slots (task C).
