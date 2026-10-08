// Game flow (round 5, task D): the parts of game_main_32a00 around the front end and the level loop,
// game_tick_32f90 (the level loop itself, one tick per game_level_tick call), level_skip_number_329c0,
// level_finish_3d4e0, player_compute_level_stats_3ef10, video_toggle_resolution_33600 (game-state half).
// Report: docs/analysis/port_game.md. The level load itself is sim_load_level (= engine_load_level).
#include "game.h"
#include "world_set.h"
#include "sim.h"
#include "player.h"
#include "input.h"
#include "sound.h"
#include "thing.h"
#include "mc_globals.h"
#include "palette_fx.h"
#include "net.h"
#include <cstring>
#include <vector>

void (*g_hook_game_fade_out)() = nullptr;
void (*g_hook_game_video_mode_changed)() = nullptr;
void (*g_hook_game_title_screen)() = nullptr;
bool (*g_hook_game_tick)() = nullptr;
void (*g_hook_game_mouse_cursor)(int entry) = nullptr;

namespace {

PlayerRec &local_rec() { return g_state->players[g_state->local_player & 7]; }
PlayerBlock *local_block() { return player_block(thing_at(thing_wrap(local_rec().thing))); }
uint8_t *cfg_bytes() { return reinterpret_cast<uint8_t *>(g_cfg); }
uint8_t *state_bytes() { return reinterpret_cast<uint8_t *>(g_state); }

void fade_out() { if (g_hook_game_fade_out) g_hook_game_fade_out(); }   // vga_palette_fade_61510(0, 0x10, 0)

bool       s_in_level = false;
bool       s_loop_ended = false;      // the level loop ended, game_level_end not called yet
GameStatus s_result = GAME_RUNNING;
int16_t    s_music_seed = 0;          // SI in game_main: Config.level at the load, one LCG step per loop start
int        s_restarts = 0;
bool       s_fe_was_hires = false;    // EBX in game_main: 8 when the game ran in 640x480 before the front end
std::vector<uint8_t> s_frame2;        // DAT_000adf70 "*WScreen" (g_frame2)

// mem_alloc_59870 of the second screen: 64000 bytes in 320x200, 0x4b000 in 640x480.
void frame2_alloc() {
    s_frame2.assign((g_video_mode_flags & 1) ? 64000u : 0x4b000u, 0);
    g_frame2 = s_frame2.data();
}
void frame2_free() {                  // mem_free_59a10(DAT_000adf70); DAT_000adf70 = 0
    if (g_frame2 == s_frame2.data()) g_frame2 = nullptr;
    s_frame2.clear();
    s_frame2.shrink_to_fit();
}

// The Config / GameState part of level_load_file_3d160 that sim_load_level does not do (it clears the
// Thing pool, the maps and GameState +0xc..+0x28; these memsets are the rest). All zero, so doing them
// before sim_load_level is equivalent.
void level_reset_config() {
    state_bytes()[0x244] = 0;                               // GameState+0x244, 1 byte
    g_cfg->fade_stage = 0;                                  // +0x17
    std::memset(cfg_bytes() + 0x5d, 0, 0x10);               // +0x5d..+0x6c tick parity bits
    g_cfg->substeps = 0;                                    // +0x96 (F3 back to 1 update per tick)
    g_cfg->palette_effect = 0;                              // +0x98
    std::memset(cfg_bytes() + 0xb8, 0, 0xe);                // +0xb8..+0xc5 (total_mana +0xbc, +0xc0)
    std::memset(cfg_bytes() + 0x8e1a, 0, 4);                // +0x8e1a
    std::memset(g_cfg->creature_lists, 0, sizeof g_cfg->creature_lists);
    g_cfg->player_list = 0;
    g_cfg->mana_ball_list = 0;
    g_cfg->wizard_list = 0;
    g_cfg->projectile_list = 0;
    // u32 at Config+0 &= 0xfffe3fff: flags bits 0x4000 / 0x8000 and the pause bit (+2 bit 0)
    g_cfg->flags &= 0x3fff;
    g_cfg->paused &= ~1;
}

// level_load_and_init_3d3b0 / level_finish_3d4e0 around the shared loader: the sound "on" switch
// (DAT_0009e321) is off while the level is generated (no sound requests from the spawns).
bool load_level(int level) {
    const uint8_t sound_on = g_sound_on;
    g_sound_on = 0;
    level_reset_config();
    const bool ok = sim_load_level(level);
    g_sound_on = sound_on;
    return ok;
}

// game_main 0x32c67..0x32cb7 + game_tick_32f90's preamble: the level track (only when music is
// available, on and a bank is loaded; the seed advances only then), input_mouse_center_4a000,
// Config.fade_stage = 0 (palette_effect_update_33010 fades the level palette in), status = 0.
void loop_start() {
    if (g_music_available && g_music_on && g_music_track_count != 0) {
        s_music_seed = (int16_t)(s_music_seed * 0x24a1 + 0x24df);
        const uint32_t track = (uint32_t)(int32_t)s_music_seed % 3u + 1u;   // movsx; div (unsigned)
        std::memcpy(&g_state->level_music_track, &track, 4);
        music_play_track((int16_t)track);                                    // movsx word [+0x240]
    }
    input_mouse_center();
    g_cfg->fade_stage = 0;
    local_rec().status = 0;
}

// game_tick_32f90's loop condition: run while quit == 0 and !(status & 8).
bool loop_over() {
    const PlayerRec &r = local_rec();
    return r.quit != 0 || (r.status & 8) != 0;
}

int32_t mul100(int32_t v) { return (int32_t)((uint32_t)v * 100u); }   // imul r32, 0x64 (wraps)

void one_tick() {                                           // game_tick_update_32e80
    if (g_hook_game_tick) g_hook_game_tick();
    else game_tick_sim();
}

// game_main 0x32cbc..0x32d58: after game_tick_32f90 returned.
GameStatus loop_end() {
    sound_stop_all();                                       // sound_stop_all_5c040
    music_stop();                                           // music_stop_1f960
    fade_out();
    PlayerRec &r = local_rec();
    if ((r.status & 6) == 4) {                              // lost bit without the won bit: restart
        level_finish();
        local_rec().status = 4;
        s_restarts++;
        if (local_rec().quit == 0) {                        // inner loop condition (0x32d40)
            loop_start();
            return GAME_RUNNING;
        }
        // quit during a restart: the outer loop ends too (the status stays 4), after the fade at 0x32d5e
        fade_out();
        s_loop_ended = false;
        return GAME_QUIT;
    }
    s_loop_ended = true;
    if (r.quit != 0) return GAME_QUIT;
    return (r.status & 2) ? GAME_LEVEL_WON : GAME_LEVEL_LOST;
}

} // namespace

// ---- level_skip_number_329c0 -------------------------------------------------------------------------
int game_level_skip_number(int level) {
    const uint16_t l = (uint16_t)level;
    if (l == 8 || l == 0x11 || l == 0x1c || l == 0x21 || l == 0x27) return (uint16_t)(l + 1);
    return l;
}

// ---- game_main before / after the front end -------------------------------------------------------------
void game_before_frontend() {
    fade_out();                                             // 0x32ad6
    mapmode_palette_save();                                 // mapmode_palette_save_30350
    s_fe_was_hires = (g_video_mode_flags == 8);
    if (s_fe_was_hires) game_toggle_resolution();           // the front end runs in 320x200
    // TODO(port): mem_check_lowmem_59760 / mem_set_owner_tag_59860(2) (memory manager, not needed)
    g_sound_on = g_sound_available;                         // DAT_0009e321 = DAT_0009e320
    g_music_on = g_music_available;                         // DAT_0009e30d = DAT_0009e30c
    // DAT_0012ebdc = 1 and DAT_0009e504 = 0 are front-end globals (task B: fe_init / fe_enter).
}

int game_after_frontend(int level) {
    if (!(g_cfg->flags & 0x10)) net_release_rules();       // port: a level outside a network game - own settings
    g_cfg->level = (uint16_t)level;
    g_cfg->level = (uint16_t)game_level_skip_number(g_cfg->level);   // level_skip_number_329c0
    fade_out();
    if (g_hook_game_mouse_cursor) g_hook_game_mouse_cursor(0);   // mouse_cursor_set_sprite_5ba5c(pointers[0])
    if (local_rec().quit == 0) {                            // title_screen_show_32db0
        g_state->title_flag_a = 1;
        g_state->title_flag_b = 0;
        g_state->title_flag_c = 0;
        if (g_hook_game_title_screen) g_hook_game_title_screen();
    }
    // video_alloc_buffers_33480 / file_free_resource_list_610c0 x2 / mem_grow_check_59810: memory
    // management of the original (the port's buffers are static).
    if (local_rec().quit == 0) {
        sound_load_bank(sim_game_dir(), 0);                 // sound_load_bank_5c990(0)
        music_load_bank(sim_game_dir(), 0);                 // music_load_bank_5c870(0)
    }
    if (!(g_cfg->flags & 4)) {
        mapmode_palette_save();                             // mapmode_palette_save_30350
        if (s_fe_was_hires) game_toggle_resolution();
        mapmode_palette_restore();                          // mapmode_palette_restore_303b0
    }
    s_fe_was_hires = false;
    return g_cfg->level;
}

// ---- the level loop ---------------------------------------------------------------------------------------
bool game_level_begin(int level) {
    s_in_level = false;
    s_loop_ended = false;
    s_result = GAME_RUNNING;
    s_restarts = 0;
    if (local_rec().quit != 0) return false;                // 0x32c18: no level when quitting
    g_cfg->level = (uint16_t)level;
    s_music_seed = (int16_t)g_cfg->level;                   // mov si, [cfg+0x11] before the load
    if (!load_level(g_cfg->level)) return false;            // level_load_and_init_3d3b0
    if (g_video_mode_flags == 1 && g_frame2 == nullptr) frame2_alloc();   // 0x32c35
    s_in_level = true;
    net_sync_arm();                                         // port: desync check of a network game from tick 0
    loop_start();
    return true;
}

GameStatus game_level_tick() {
    if (!s_in_level || s_loop_ended) return s_result;
    if (!loop_over()) one_tick();
    if (!loop_over()) return GAME_RUNNING;
    s_result = loop_end();
    if (s_result == GAME_QUIT && !s_loop_ended) s_in_level = false;   // quit inside a restart: nothing left to finish
    return s_result;
}

void game_level_end() {
    if (!s_in_level) return;
    if (s_loop_ended) {
        PlayerRec &r = local_rec();
        if ((r.status & 6) != 4) {                          // 0x32cf6
            if (r.status & 2) {
                r.status = 2;
                player_compute_level_stats();
                // campaign progress: the next level (port: base level 50 continues with Hidden Worlds
                // level 1 when it is installed, world_set.h)
                if (g_cfg->flags & 0x10) g_cfg->level++;
                else g_cfg->level = (uint16_t)world_campaign_next(g_cfg->level);
            } else {
                r.status = 8;
            }
        }
        fade_out();                                         // 0x32d5e
    }
    s_in_level = false;
    s_loop_ended = false;
}

GameStatus game_last_result() { return s_result; }
bool game_in_level() { return s_in_level; }
int  game_level_restarts() { return s_restarts; }

// ---- level_finish_3d4e0 ---------------------------------------------------------------------------------
// Same loader as level_load_and_init_3d3b0 without the progress messages and with an unconditional
// GameState.player_count = level.player_count (the start also keeps the count in a network game,
// Config.flags & 0x10; sim_load_level always does the former - network games are not ported).
void level_finish() {
    load_level(g_cfg->level);
}

// ---- player_compute_level_stats_3ef10 ------------------------------------------------------------------
void player_compute_level_stats() {
    PlayerBlock *P = local_block();
    int found = 0, present = 0;
    for (int id = 0; id < 24; id++) {
        if (g_state->spells_present[id] != 0) {
            present++;
            if (P->spell_thing[id] != 0) { found++; P->spell_found[id] = 1; }
        }
        if (P->spell_thing[id] != 0) P->spell_found[id] = 1;
    }
    if (present == 0) P->pct_spells = 100;
    else {
        P->pct_spells = mul100(found) / present;
        if (P->pct_spells > 100) P->pct_spells = 100;
    }
    const int32_t creatures = (int32_t)g_state->creature_count;
    if (creatures == 0) P->kills = 100;
    else {
        P->kills = mul100(P->kills) / creatures;            // P+0x167 is overwritten with the percentage
        if (P->kills > 100) P->kills = 100;
    }
    if (P->shots == 0) P->pct_accuracy = 100;
    else {
        P->pct_accuracy = mul100(P->hits) / P->shots;
        if (P->pct_accuracy > 100) P->pct_accuracy = 100;
    }
    // things + castle is never null in the original: castle 0 reads the scratch Thing 0's mana.
    const int32_t world = (int32_t)g_cfg->total_mana;
    const int32_t mana = thing_at(thing_wrap(P->castle))->mana + P->mana_in_transit;
    P->pct_mana = world != 0 ? mul100(mana) / world : 0;   // guard: the original divides unchecked
    if (P->pct_mana > 100) P->pct_mana = 100;
    P->pct_overall = 0;
    int n = 0;
    if (present != 0) { P->pct_overall += P->pct_spells; n = 1; }
    if (creatures != 0) { P->pct_overall += P->kills; n++; }
    if (P->shots != 0) { P->pct_overall += P->pct_accuracy; n++; }
    P->pct_overall += P->pct_mana;
    n++;
    P->pct_overall /= n;
    P->start_tick = g_timer_ticks - P->start_tick;          // P+0x17b becomes the elapsed time
}

void game_level_stats(GameLevelStats *out) {
    const PlayerBlock *P = local_block();
    out->pct_kills = P->kills;
    out->pct_spells = P->pct_spells;
    out->pct_accuracy = P->pct_accuracy;
    out->pct_mana = P->pct_mana;
    out->pct_overall = P->pct_overall;
    out->elapsed_ticks = P->start_tick;
}

// ---- campaign ---------------------------------------------------------------------------------------------
void game_campaign_new() {                                  // fe_menu_new_or_resume_game_57480, "yes"
    g_cfg->level = 0;
    std::memset(local_rec().blk.spell_found, 0, sizeof local_rec().blk.spell_found);   // GameState+0x3bd6, 0x18 bytes
}
int game_campaign_level() { return g_cfg->level; }

// ---- video_toggle_resolution_33600 -----------------------------------------------------------------------
void game_toggle_resolution() {
    fade_out();
    // stereo_mode_leave_2ff10: the video mode is set again and DAT_00093f74 (16-bit anaglyph) = 0 -
    // platform side.
    frame2_free();
    // mouse_reset_3d070 + ui_sprite_lists_unrelocate_49e40 / file_free_resource_list_610c0
    g_video_mode_flags = (g_video_mode_flags == 1) ? 8 : 1;
    frame2_alloc();
    // ui_sprite_lists_relocate_49de0, vga_set_mode_13h_613e0 / vga_set_mode_640x480_61480,
    // gfx_fill_rows_320/640 (clear), mouse_cursor_set_sprite_5ba5c: renderer / platform.
    if (g_hook_game_video_mode_changed) g_hook_game_video_mode_changed();
    // input_mouse_init_5bc14 after the int 33h reset: the driver puts the pointer at the centre of the
    // new range.
    input_mouse_center();
    if (g_hook_game_mouse_cursor) g_hook_game_mouse_cursor(0);   // mouse_cursor_set_sprite_5ba5c(pointers[0])
    g_cfg->fade_stage = 0;
}
