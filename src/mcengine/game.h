// Game flow (round 5, task D): the in-level part of game_main_32a00 (level start, the per-frame loop
// with the player-status state machine, level end), level_finish_3d4e0, player_compute_level_stats_3ef10,
// campaign progress, video_toggle_resolution_33600. No SDL: mcport drives it.
//
// CONTRACT between task D (owner), task B (front end: result screen, new game / load) and the
// integrator (mcport): the declarations below are fixed for round 5; task D implements them and may
// add more.
#pragma once
#include <cstdint>
#include "world_set.h"

enum GameStatus {
    GAME_RUNNING = 0,
    GAME_LEVEL_WON = 1,      // the level was completed (status bits as game_main tests them)
    GAME_LEVEL_LOST = 2,     // the local wizard lost / gave up
    GAME_QUIT = 3,           // the player left the game (Shift+Q / quit)
};

// level_load_and_init_3d3b0 and the level start that game_main does around it (sound / music banks,
// level track, mouse centre, ...). `level` is the 0-based campaign level index.
bool game_level_begin(int level);
// One game tick of the in-level loop (engine_tick + the status state machine). Returns the status
// after the tick; anything but GAME_RUNNING ends the level loop.
GameStatus game_level_tick();
// level_finish_3d4e0 and whatever game_main does after the level loop (stats, campaign progress).
void game_level_end();

// ==== additions by task D (round 5) ====================================================================
// How the pieces map onto game_main_32a00 (docs/analysis/port_game.md has the whole function):
//
//   game_before_frontend()                      0x32ad1..0x32b3d  (fade, 640x480 -> 320x200, F1/F2 on)
//   fe_frame() until FE_START_LEVEL / FE_QUIT   0x32b43 frontend_menu_loop_52070 loop (task B)
//   level = game_after_frontend(level)          0x32b51..0x32c06  (level skip, fade, title, banks)
//   game_level_begin(level)                     0x32c09..0x32cb7  (level load + loop preamble)
//   while (game_level_tick() == GAME_RUNNING)   0x32cb7 game_tick_32f90 (one game_tick_update_32e80
//       present a frame;                         per call; a level *restart* - status & 6 == 4 - is
//                                                handled inside: level_finish_3d4e0, new track)
//   game_level_end()                            0x32cf6..0x32d68  (status 2 / 8, stats, level + 1)
//   GAME_QUIT -> leave the program (the original's outer loop ends on PlayerRec.quit != 0);
//   otherwise fe_enter(5) (the front end shows the result screen, task B) and loop.
//
// Pacing: the original has none - one simulation tick per rendered frame, as fast as the machine
// draws (no vsync wait in the blit, no timer limiter); see port_game.md "Pacing".

// game_main 0x32ad1..0x32b3d: what runs before every front-end visit: fade to black, a 640x480 game
// is switched to 320x200 for the front end (remembered for game_after_frontend), sound / music "on"
// = "available" (F1 / F2 toggles are reset), mapmode palette save (TODO(port), task C).
void game_before_frontend();
// game_main 0x32b51..0x32c06, after the front end left with a level: level_skip_number_329c0 (the
// returned level is the one to play), fade, the "smatitle" loading screen state (+0x245 flags,
// g_hook_game_title_screen), sound bank set 0 and music bank set 0, back to 640x480 if the game ran
// there before the front end (not in demo playback, Config.flags & 4).
int  game_after_frontend(int level);

// level_skip_number_329c0: campaign levels 8, 17, 28, 33 and 39 (0-based) are never played - the
// number after them is played instead. Pure function of `level`.
int  game_level_skip_number(int level);
// The local player's level statistics player_compute_level_stats_3ef10 wrote into its P block
// (PlayerBlock.kills / pct_*; valid after game_level_end of a won level - the result screen reads them
// from there).
struct GameLevelStats {
    int32_t pct_kills;       // P+0x167 creatures killed * 100 / level creatures (100 when the level has none)
    int32_t pct_spells;      // P+0x16b level spells found * 100 / spells in the level (100 when none)
    int32_t pct_accuracy;    // P+0x16f hits * 100 / shots (100 without shots)
    int32_t pct_mana;        // P+0x173 (castle mana + mana in transit) * 100 / world mana, max 100
    int32_t pct_overall;     // P+0x177 mean of the applicable percentages (mana always counts)
    uint32_t elapsed_ticks;  // P+0x17b g_timer_ticks (119.06 Hz) since the wizard's first spawn
};
void game_level_stats(GameLevelStats *out);
// player_compute_level_stats_3ef10 on its own (the local player). game_level_end calls it for a win.
void player_compute_level_stats();
// level_finish_3d4e0: reload the current level (Config.level) in place - the original's restart after
// "castle gone and wizard dead" / Shift+R. game_level_tick calls it; exported for tests.
void level_finish();

// Campaign progress (Config.level + players[local].blk.spell_found[24] = GameState+0x3bd6, the block
// the save game stores). New campaign = fe_menu_new_or_resume_game_57480's "yes" branch.
void game_campaign_new();
int  game_campaign_level();                 // Config.level: the next level to play
// The front end shows the outro (state 10): Config.level == 50, or (port) past Hidden Worlds level 25.
inline bool game_campaign_complete() { return world_campaign_complete(game_campaign_level()); }
// Status of the last level loop (what game_level_tick returned when it ended), GAME_RUNNING while in
// a level or before the first one.
GameStatus game_last_result();
bool game_in_level();
int  game_level_restarts();                 // level_finish_3d4e0 calls since game_level_begin

// video_toggle_resolution_33600 (R key, INPUT_REQ_TOGGLE_RESOLUTION; also around the front end):
// fade, g_video_mode_flags 1 <-> 8, second frame buffer g_frame2 re-allocated for the new size
// (64000 / 0x4b000), mouse re-centred (the int 33h reset), Config.fade_stage = 0 (the level palette
// fades in again). The platform part (window / frame buffer size, ui_draw_set_video_mode, pointer
// sprite, clear screen) goes through g_hook_game_video_mode_changed.
void game_toggle_resolution();

// ---- hooks (null = skipped), installed by the platform / renderer side ----------------------------
// vga_palette_fade_61510(NULL, 0x10, 0): fade the display to black in 16 vsync steps. The original
// blocks; the port should present frames without ticking while palette_fade_active() (palette_fx.h).
extern void (*g_hook_game_fade_out)();
// The renderer / platform half of video_toggle_resolution_33600, after g_video_mode_flags changed.
extern void (*g_hook_game_video_mode_changed)();
// title_screen_show_32db0's image: data/smatitle.dat + smatitle.pal, 32-step fade in (task C / B).
// game_after_frontend sets the GameState flags (+0x245 = 1, +0x249 = +0x24d = 0) itself.
extern void (*g_hook_game_title_screen)();
// One game tick (game_tick_update_32e80). Null: game_tick_sim() (player.h) - the simulation only, what a
// renderer-free test links. mcport installs engine_tick (texture animation + game_tick_sim + g_anim_tick).
extern bool (*g_hook_game_tick)();

// ==== additions by task B (round 6) ====================================================================
// mouse_cursor_set_sprite_5ba5c(DAT_000adfc0 + entry * 6): the in-game pointer (data/pointers). game_main
// sets entry 0 after the front end (game_after_frontend) and video_toggle_resolution_33600 after the
// mode switch (game_toggle_resolution). mcport: g_hook_game_mouse_cursor = mouse_cursor_set_pointer
// (frontend.h). game_level_begin also arms the network desync check (net_sync_arm, net.h).
extern void (*g_hook_game_mouse_cursor)(int entry);
