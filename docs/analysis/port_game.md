# Game flow port (game.h / game.cpp) - agent D report, port round 5, 2026-10-07

Files: `src/mcengine/game.h` (contract kept unchanged, additions below it), `src/mcengine/game.cpp`,
`src/tests/game_test.cpp` + `game_test.cmake` (`mc_unit_test` with `${MC_SIM_ALL}` + `mcengine/game.cpp`,
no renderer). `game_test.exe` -> exit 0, zero warnings (/W4), build dir `build_D`, ~0.5 s.

## game_main_32a00, as it really is

The ENGINE.md sketch ("in-level loop ... state machine on player->status") is wrong in its shape: the
per-tick loop is inside `game_tick_32f90`, and the status test runs once after that loop. Full structure
from the disassembly (0x32a00..0x32dac; `rec` = `players[local]`, `quit` = PlayerRec+4, `status` = +2):

```
config_parse; data_load_all; if (cfg.flags & 8) { ret_stub_23766; goto shutdown }
cfg.fade_stage = 0; cd_check; sprite sizes; mem_init_pools; video_input_init
if (!(cfg+1 & 1)) fe_init_state_51ed0  else { sound/music off; load ftext.dat }     (cfg+1 bit0: front end skipped)
rec.quit = 0
while (rec.quit == 0) {                                   // outer loop: one front-end visit + one level
    fade(black, 0x10)
    if (!(cfg+1 & 1)) {                                   // ---- game_before_frontend
        mapmode_palette_save; was_hires = (video == 8); if (was_hires) video_toggle_resolution   // FE is always 320x200
        mem_check_lowmem; mem_set_owner_tag(2)
        sound_on = sound_available; DAT_0012ebdc = 1; music_on = music_available; DAT_0009e504 = 0
        do frontend_menu_loop_52070 while (!DAT_0009e504) // ---- task B, fe_frame
        level_skip_number_329c0                           // ---- game_after_frontend
        fade(black); mouse_cursor_set_sprite(pointers[0])
        if (rec.quit == 0) title_screen_show_32db0        // smatitle.dat loading screen, +0x245 = 1
        video_alloc_buffers; free resource lists x2; mem_grow_check
        if (rec.quit == 0) { sound_load_bank(0); music_load_bank(0) }
        if (!(cfg.flags & 4)) { mapmode_palette_save; if (was_hires) video_toggle_resolution; mapmode_palette_restore }
        mem_set_owner_tag(3)
    }
    if (rec.quit == 0) {                                  // ---- game_level_begin
        si = cfg.level                                    // 16-bit music seed
        level_load_and_init_3d3b0
        if (video == 1 && !g_frame2) g_frame2 = alloc(64000)
    }
    while (rec.quit == 0) {                               // inner loop: one pass per (re)start of the level
        if (music_available && music_on && track_count) {
            si = si * 0x24a1 + 0x24df                     // 16-bit
            state+0x240 = (uint32)(int32)(int16)si % 3 + 1; music_play_track(state+0x240)
        }
        input_mouse_center_4a000
        game_tick_32f90:  cfg.fade_stage = 0; rec.status = 0
                          while (rec.quit == 0 && !(rec.status & 8)) game_tick_update_32e80()   // ---- game_level_tick
        sound_stop_all; music_stop; fade(black)
        if ((rec.status & 6) != 4) {                      // ---- game_level_end
            if (rec.status & 2) { rec.status = 2; player_compute_level_stats_3ef10; cfg.level++ }
            else rec.status = 8
            break
        }
        level_finish_3d4e0                                // RESTART: the same level is regenerated
        rec.status = 4
    }
    fade(black)
}
sound_timer_shutdown; fade(black); game_shutdown_all; net_shutdown
```

### Status bits (PlayerRec.status, GameState+0x340d + p * 0x801)

| bit | set by | meaning in game_main |
|---|---|---|
| 2 | game_check_level_won (win_timer reached 0x10: the 17th consecutive tick above `level.win_percent`), Shift+C cheat | "World restored. Press the space bar to continue." - the level does **not** end by itself |
| 8 | command 0x1b (Space / Esc when bit 2 is set: status = 10), 0x1c (status = 0xc), 0x1d leave game (Esc: status = 8), respawn command 0xf without a castle (status \|= 0xc), Shift+R (status \|= 0xc, castle 0), movie end (status = 8) | ends `game_tick_32f90`'s loop |
| 4 | the same writers as 8 when the level is lost (0xc), Shift+F cheat | with bit 8 and without bit 2: **restart** (level_finish_3d4e0) |

After the loop: `status & 6 == 4` -> restart in place (no front end); bit 2 -> won (status 2, stats,
Config.level + 1); otherwise -> status 8 (lost / left; the result screen shows levelose). `quit != 0`
(Shift+Q = command 2, the front end's quit, the CD check with 2) ends the outer loop: the program exits.

## Functions translated

| port | original |
|---|---|
| `game_before_frontend()` | game_main_32a00 0x32ad1..0x32b3d |
| `game_after_frontend(level)` | game_main_32a00 0x32b51..0x32c06 (+ the GameState half of title_screen_show_32db0) |
| `game_level_begin(level)` | game_main_32a00 0x32c09..0x32c62 + the inner-loop preamble 0x32c67..0x32cb2 + game_tick_32f90's preamble |
| `game_level_tick()` | one pass of game_tick_32f90's loop (game_tick_update_32e80 through `g_hook_game_tick`), then 0x32cbc..0x32d58 when it ends (the restart branch is handled inside) |
| `game_level_end()` | game_main_32a00 0x32cf6..0x32d68 |
| `game_level_skip_number(level)` | level_skip_number_329c0 |
| `level_finish()` | level_finish_3d4e0 (a level **restart**, see corrections) |
| `player_compute_level_stats()` | player_compute_level_stats_3ef10 |
| `game_toggle_resolution()` | video_toggle_resolution_33600 (game-state half) |
| `game_campaign_new()` | fe_menu_new_or_resume_game_57480, "yes" branch (Config.level = 0, spell_found cleared) |
| static `level_reset_config()` | the Config / GameState memsets of level_load_file_3d160 that sim_load_level lacks |
| static `load_level()` | the DAT_0009e321 (sound on) save / clear / restore around the loader in 3d3b0 / 3d4e0 |

The level load itself is `sim_load_level` (`engine_load_level` is exactly that call; game.cpp calls the sim
side so the test links without the renderer).

## Pacing - how the original paces its loop

**It does not.** One simulation tick per rendered frame, as fast as the machine renders:

- `game_tick_32f90` calls `game_tick_update_32e80` back to back with no wait. `game_tick_update_32e80` =
  palette_effect_update -> texture_anim_update -> player_local_input -> player_commands_process -> win check ->
  thing_update_all x 1 / 4 / 16 -> sound_update -> render_frame_1fab0 -> frame time -> debug overlay ->
  screenshot -> `vga_present_frame_2f480`.
- `vga_present_frame_2f480` copies the back buffer to VRAM (`vga_copy_320x200_610f0`: `rep movsd` of 64000
  bytes to 0xa0000; `vesa_copy_banked_4f974`: 5 banks) with **no vsync wait** (no read of port 0x3da, no
  page flip).
- The 119.06 Hz PIT counter DAT_0012eab4 is read in-level only by: the frame-time display (Config+0x99 =
  ticks per frame, a u32, shown by the debug overlay), the network exchange time (+0x9d), PlayerBlock.start_tick
  (first spawn) and the level statistics (elapsed time). No limiter, no catch-up, no fixed rate.
- `vga_wait_vsync_65b10` has three callers: the movie / FLI presenter, the joystick calibration and
  `vga_palette_fade_61510`. So the only in-level waits are palette fades: the blocking fades of game_main
  (to black, steps + 1 = 17 vsyncs for 0x10), `palette_fade_out_and_load_32e40` at fade stage 0/1 of a level
  start (17 vsyncs), and the incremental fade of `palette_effect_update_33010` effect 1 (one vsync per tick
  for 4 ticks: the level fade-in, and the recovery after every damage / mana flash, because effects 2..7 set
  effect 1 after drawing). During those ticks the game is vsync-locked (70 Hz in mode 13h).
- The game speed is therefore the frame rate of the machine (in DOSBox: of the cycles setting). The movie
  playback runs through the same loop, also unpaced.
- **F3 "game speed"** (Config.substeps, 0 normal / 1 fast / 2 super fast; not in network / custom games)
  does not touch timing: `thing_update_all` runs 1 / 4 / 16 times per tick while command processing, the win
  check, sound and rendering still run once. Everything that is a Thing (creatures, spells, projectiles and
  the wizards' own flyer movement) advances 4x / 16x per frame; `player_mouse_steer_15590` returns at once
  while substeps != 0 (no mouse steering in the fast modes). It is a fast-forward, not a frame limiter.
  `level_load_file_3d160` resets it to 0 at every level start.
- Music is independent (HMI timer interrupt), sound effects are started per tick.

**For mcport:** the simulation is deterministic per tick (nothing in game logic reads real time except the
statistics' elapsed time), so the port must pick a rate. Recommended: a fixed tick rate (configurable;
~20-25 Hz is what mid-90s machines reached in 320x200, the current DEMO_TICK_HZ = 20 matches), one
`render_frame_draw` per tick (the original never draws without ticking; drawing in between shows the same
frame), F3 left as the original fast-forward (it multiplies Thing updates, not ticks). Palette fades are
frame-stepped at the vsync rate (70 Hz) **without ticking** while `palette_fade_active()` (the original
blocks inside them). `g_timer_ticks` (DAT_0012eab4) must advance in real time at 1193182 / 0x2726 = 119.06 Hz
(stats elapsed time, front-end idle counter, FLI pacing); mcport does not advance it today.

## Verification (game_test, numbers from the run)

1. `level_skip_number` for 0..59 by construction: 8, 17, 28, 33, 39 -> +1, all others unchanged.
2. Front-end bracket from 640x480: before -> video flags 1, g_frame2 allocated, hook called, F1 / F2 = available;
   after(8) -> level 9, title flags (1, 0, 0) + hook, 46 samples (snds0-1), music0-0 has **4** songs (1..3 are
   the level tracks), back to video flags 8; toggle back to 1 with fade_stage 0.
3. New campaign, level 0 through `game_level_begin`: status 0, level track = (int16)0x24df % 3 + 1 = 2 played
   through the backend (equals sound.cpp's `music_track_for_level(0)`). Level 0: win_percent 35, world mana
   48598, 16 creatures. Scripted win (a castle created with `thing_create(home, 3, 2)` for the local wizard,
   its mana set to the world mana before every tick): status bit 2 after **17** ticks (= 0x11, the original's
   win_timer delay), Space queued 3 ticks later through `player_queue_command(0x1b)` -> the next tick
   returns `GAME_LEVEL_WON`, status 10; further ticks keep returning WON.
   `game_level_end` with hand-set inputs (4 level spells present, 2 found + 1 found that is not in the level;
   5 kills of 20 creatures; 4 hits of 10 shots; castle with half the world mana; timer + 1234):
   spells 50 %, kills 25 %, accuracy 40 %, mana 50 %, overall (50 + 25 + 40 + 50) / 4 = 41 %, time 1234 ticks,
   status 2, Config.level 0 -> 1, spell_found[0, 1, 10] = 1, [2, 3] = 0. Level 1 started next keeps spell_found.
4. Esc in level 1 (0x1b refused because bit 2 is clear, 0x1d queued): GAME_LEVEL_LOST on the next tick,
   status 8, Config.level stays 1.
5. Level 2, 200 ticks, then status |= 0xc + castle 0 (Shift+R): the same `game_level_tick` returns RUNNING,
   1 restart, status 0, player tick < 5 (level regenerated), new track = seed 2 stepped twice (1); 100 more
   ticks, pool clean (288 live, 711 free). Shift+Q (command 2): GAME_QUIT, quit = 1, `game_level_begin`
   refuses while quit is set.
6. Levels 3, 4, 5 started one after the other: the Thing pool + height map right after `game_level_begin`
   are **byte-identical** to a fresh `sim_load_level` of the same level (0 differing bytes: nothing leaks
   from the previous level), 1500 ticks each, then Esc / end; cell chains, free stack (distinct empty
   slots, every empty slot on it) and recyclable stack clean after the run and after the end
   (524 / 333 / 591 live things).

Not verified against the original: no per-tick reference covers a level start through the front end or a
level end (task E's level references start straight into the level). The music seed, the status
classification, the statistics and the skip table are straight from the disassembly.

## Deviations / gaps

- `game_level_tick` runs exactly one `game_tick_update_32e80`; the restart branch (fade, level_finish, new
  track, mouse centre, status 0) happens inside the call that ends the loop, which therefore returns
  GAME_RUNNING. `game_level_end` must be called once after a non-RUNNING result (it does nothing otherwise).
- Fades are requests (`g_hook_game_fade_out`), not blocking; the platform must hold the ticks while the fade
  runs. The fade at the start of each outer pass is in `game_before_frontend`, the one after the inner loop
  in `game_level_end` (and in the restart-then-quit corner case in `game_level_tick`).
- `level_finish` uses `sim_load_level`, which sets `player_count = level.player_count` only without
  Config.flags & 0x10; the original's restart sets it unconditionally (differs only in network games, which
  are not ported). The sound-off window covers the whole loader instead of ending before players_init_records
  / mana_totals_update (they request no sounds).
- Division by world mana 0 is guarded (pct_mana 0); the original would fault.
- Not done (memory manager / platform): mem_check_lowmem / mem_set_owner_tag / video_alloc_buffers /
  file_free_resource_list / mem_grow_check (the port's buffers are static), the cursor sprite after the
  front end, DAT_0012ebdc / DAT_0009e504 (front-end globals, task B), the frame-time u32 at Config+0x99
  (overlay only; mc_types has it as u8), the debug screenshot, `ret_stub_23766` (cfg.flags & 8 path).
- g_frame2: game.cpp owns the buffer (a static vector, 64000 / 0x4b000 bytes by mode). Allocating it at
  the level start in 320x200 is what the original does, and it enables the renderer's g_frame2 paths
  (motion blur F-key option, render_landscape.cpp) in mcport from then on.

## TODO(port) call sites (game.cpp)

- `mapmode_palette_save_30350` / `mapmode_palette_restore_303b0` around the front end (task C's palette_fx;
  not in its contract header, so not called): game_before_frontend, game_after_frontend.
- `mouse_cursor_set_sprite_5ba5c(DAT_000adfc0)`: game_after_frontend (platform / front end).
- `mem_check_lowmem_59760` / `mem_set_owner_tag_59860`: not needed.

## Hooks declared (game.h, null = skipped)

- `g_hook_game_tick` - one game_tick_update_32e80; null = `game_tick_sim()`. mcport: `engine_tick`.
- `g_hook_game_fade_out` - vga_palette_fade_61510(NULL, 0x10): mcport -> `palette_fade_start(nullptr, 0x10)`.
- `g_hook_game_video_mode_changed` - the renderer / platform half of video_toggle_resolution_33600.
- `g_hook_game_title_screen` - title_screen_show_32db0's image (smatitle.dat / .pal, 32-step fade in).

## Extra API (game.h additions)

`game_before_frontend`, `game_after_frontend`, `game_level_skip_number`, `GameLevelStats` +
`game_level_stats`, `player_compute_level_stats`, `level_finish`, `game_campaign_new`, `game_campaign_level`,
`game_campaign_complete` (Config.level == 50: the result screen goes to the outro), `game_last_result`,
`game_in_level`, `game_level_restarts`, `game_toggle_resolution`.

What task B's result screen reads (as the original does): `players[local].status` (2 won / 8 lost), the
local wizard's P block `kills` (P+0x167, already a percentage), `pct_spells` (0x16b), `pct_accuracy` (0x16f),
`pct_mana` (0x173), `pct_overall` (0x177), `start_tick` (0x17b, now the elapsed time), the level name
`name[Config.level]` (Config.level was already incremented after a win: index 1..50 = "1. Al Jahan" ..),
and Config.level == 50 -> state 10 (outro).

## Integration (mcport/main.cpp) - exact code

```cpp
#include "game.h"
#include "frontend.h"
#include "palette_fx.h"
#include "sound.h"

// after engine_init(...) (and after sound_register_handlers installed its own hook):
static void platform_request(InputPlatformRequest what, int arg) {
    if (sound_handle_input_request(what, arg)) return;                 // F1 / F2 / pause: sound.cpp
    if (what == INPUT_REQ_TOGGLE_RESOLUTION) game_toggle_resolution();  // R
    // INPUT_REQ_3D_MODE_*: not handled yet
}
g_hook_input_platform = platform_request;
g_hook_game_tick = engine_tick;
g_hook_game_fade_out = [] { palette_fade_start(nullptr, 0x10); };
g_hook_game_video_mode_changed = [] {
    ui_draw_set_video_mode(nullptr);
    // resize the platform frame buffer to 320x200 / 640x480 by g_video_mode_flags (or keep 320x200 and
    // refuse: then do not route INPUT_REQ_TOGGLE_RESOLUTION here)
};
const uint64_t t0 = plat.ticks_us();
```

Main loop (campaign with front end; `mode` starts as FRONTEND after `fe_init(game, 6); game_before_frontend();`):

```cpp
g_timer_ticks = (uint32_t)((plat.ticks_us() - t0) * 1193182ull / (0x2726ull * 1000000ull));   // DAT_0012eab4
if (palette_fade_active()) {                       // the original blocks inside its fades: no ticks
    palette_fade_step();                           // at 70 Hz
    /* upload g_display_palette6 when palette_display_dirty(), present the last frame */
    continue;
}
if (mode == FRONTEND) {
    int lvl = 0;
    FeResult r = fe_frame(fb, g_timer_ticks, &lvl);
    if (r == FE_QUIT) break;
    if (r == FE_START_LEVEL) {
        lvl = game_after_frontend(lvl);            // level skip, banks, title state
        if (!game_level_begin(lvl)) break;
        mode = LEVEL;
    }
} else if (now >= next_tick) {                      // LEVEL: fixed tick rate, one frame per tick
    next_tick += tick_us;
    GameStatus s = game_level_tick();
    for (int sc : deferred_releases) input_key_event(sc, false);
    deferred_releases.clear();
    render_frame_draw(fb, g_state->local_player);
    ui_draw_debug_overlay();
    if (s != GAME_RUNNING) {
        game_level_end();                          // status 2 + stats + level++ / status 8
        if (s == GAME_QUIT) break;                 // PlayerRec.quit: the original exits to DOS
        game_before_frontend();
        fe_enter(5);                               // level result screen (then main menu / outro)
        mode = FRONTEND;
    }
}
plat.present();
```

`mcport play N` (no front end, like the original's `cfg+1 & 1` path of the `demo n` option): `game_level_begin(N)`; on a non-RUNNING
result `game_level_end()`, then quit on GAME_QUIT, else `game_level_begin(game_campaign_level())` (won:
the next level, lost: the same one again - what the original's outer loop does without the front end; it
also skips the sound / music bank loads in that path, mcport should still call `sound_load_bank(dir, 0)` /
`music_load_bank(dir, 0)` once). Replace the current `players[local].quit` check in main.cpp with this.

palette_effect_update (task C) must run at the start of each tick (game_tick_update_32e80 step 1, when
mode_3d == 0): game_level_begin / restarts set Config.fade_stage = 0 so it fades the level palette in.

## Requested shared-file changes

1. `src/mcengine/sim.cpp`, `sim_load_level`, in the "per-level reset (level_load_file_3d160)" block after
   `std::memset(g_work_buf, 0, 64000);` - the rest of level_load_file's resets (game.cpp does them before the
   call meanwhile; moving them makes `engine_load_level` / task E's level references match the original):
   ```cpp
   reinterpret_cast<uint8_t *>(g_state)[0x244] = 0;
   g_cfg->fade_stage = 0;
   std::memset(reinterpret_cast<uint8_t *>(g_cfg) + 0x5d, 0, 0x10);
   g_cfg->substeps = 0;
   g_cfg->palette_effect = 0;
   std::memset(reinterpret_cast<uint8_t *>(g_cfg) + 0xb8, 0, 0xe);
   std::memset(reinterpret_cast<uint8_t *>(g_cfg) + 0x8e1a, 0, 4);
   std::memset(g_cfg->creature_lists, 0, sizeof g_cfg->creature_lists);
   g_cfg->player_list = g_cfg->mana_ball_list = g_cfg->wizard_list = g_cfg->projectile_list = 0;
   g_cfg->flags &= 0x3fff;      // u32 at Config+0 &= 0xfffe3fff
   g_cfg->paused &= ~1;
   ```
   (Then `level_reset_config()` in game.cpp can go. Re-run sim_test / reference_test: all fields are 0 at
   that point in those runs.)
2. `src/mcengine/mc_types.h`, `Config`: `uint8_t frame_time; uint8_t pad9a[7];` (0x99..0xa0) ->
   `uint32_t frame_time; // 0x0099 timer ticks per frame (game_tick_update_32e80), a u32` +
   `uint32_t net_time; // 0x009d net_exchange_frame_4f530 duration in timer ticks`; `PlayerRec.quit` comment:
   "nonzero ends game_main's outer loop = quit to DOS (1 quit, 2 CD check failed)".
3. `src/mcport/main.cpp`: the integration code above.
4. Optional: `engine_init` could set `g_hook_game_tick = engine_tick` itself.

## Corrections to docs/ENGINE.md / names

- "Startup and main loop": replace the in-level sketch with the structure above. `game_tick_32f90` holds the
  per-tick loop (`while quit == 0 && !(status & 8)`); the status test runs after it; `timer_sync_61510` entries
  are palette fades; the music track seed (`si`) is Config.level, advanced once per (re)start of the level and
  only while music is available + on + loaded.
- **`level_finish_3d4e0` is the level restart** (castle lost and wizard dead / Shift+R / command 0x1c):
  it regenerates Config.level in place like level_load_and_init_3d3b0 (no progress messages, player_count
  unconditional). Its Ghidra comment "Called when the in-level loop exits with the level-complete state"
  is wrong; suggested name `level_restart_3d4e0`.
- Status bits: 2 = won (win check, needs Space to leave), 8 = leave the loop, 4 = lost -> restart. A won
  level does not end by itself.
- `PlayerRec.quit` (+4): any nonzero value ends the whole program loop (1 = quit, 2 = CD check); not "quit
  to menu".
- Campaign: Config.level is the levels.dat index; "new game" sets 0; +1 per win (after the loop, before the
  front end); the result screen shows `level_names[Config.level]` after that increment ("1. Al Jahan" for
  index 0); Config.level == 50 -> outro. Levels 8, 17, 28, 33, 39 are skipped only on the front-end path
  (level_skip_number runs after frontend_menu_loop; the path without front end - Config+1 bit 0, set by the
  `demo n` option - plays Config.level as it is).
- **Campaign progress block GameState+0x3bd6 = `players[local].blk.spell_found[24]`** (PlayerRec+0x7cb =
  P+0x37c), written by player_compute_level_stats_3ef10 and kept by players_init_records. DAT_0012ed30 /
  DAT_0012ed31 are the multiplayer lobby's (ed31 = player count from the slot clicks, ed30 cycles 0..9) and
  only appear in the save game's checksum.
- game_main: before every front-end visit sound_on / music_on are reset to "available" (F1 / F2 toggles do
  not survive a level), the front end always runs in 320x200 (a 640x480 game is switched and switched back
  unless demo playback), sound bank 0 and music bank 0 are loaded after every front-end visit.
- `title_flag_a` (+0x245) = loading screen shown: set by title_screen_show_32db0 (smatitle.dat), cleared by
  palette_fade_out_and_load_32e40 at the first tick (fade stage 0).
- `level_load_file_3d160` also clears Config +0x17, +0x5d[0x10], +0x96, +0x98, +0xb8[0xe], +0x8e1a, the
  per-tick lists, flags 0x4000 / 0x8000 and the pause bit, and GameState+0x244.
- `video_toggle_resolution_33600` game state: DAT_0012edae 1 <-> 8, DAT_000adf70 (g_frame2) reallocated
  (64000 / 0x4b000), DAT_00093f74 = 0 (stereo_mode_leave_2ff10; `video_restore_mode_2ff10` in input.h is that
  function), mouse reset + re-init (pointer recentred, range by mode), Config.fade_stage = 0.
- Config+0x99 frame time is a u32 (ticks per frame for the debug overlay).
