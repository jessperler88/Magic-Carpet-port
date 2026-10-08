// Players: record initialisation, spawning, command packets, the flyer's movement and state
// handlers (class 3 states 0, 2, 3), the mana totals and the simulation half of the game tick.
// Owner: player.cpp (+ demo.cpp for the movie recorder / player). Report: docs/analysis/port_player.md.
#pragma once
#include "thing.h"
#include "render.h"   // Camera

// Typed view of the owner block a Thing points at (PlayerRec+0x44f; the dummy block when
// Thing.player is 0).
inline PlayerBlock *player_block(const Thing *t) {
    return reinterpret_cast<PlayerBlock *>(thing_player_block(t));
}
// 8-byte threat entries at P+0x1cc; the last one overlaps PlayerBlock.ai_aggression, so index it raw.
inline uint16_t *player_threat(PlayerBlock *P, int player) {
    return reinterpret_cast<uint16_t *>(reinterpret_cast<uint8_t *>(P) + 0x1cc + 8 * player);
}

// Binds the class-3 Table A handlers of the flyer and installs g_hook_mana_totals_update.
void player_register_handlers();

// ---- records and spawning ----------------------------------------------------------------------
void players_init_records();                          // players_init_records_3bc10 (end of level load)
void players_clear_records();                         // players_clear_records_3bf60
// player_spawn_3f360(rec, thing): `t` == &things[0] creates the wizard (first spawn), anything
// else respawns the existing Thing at the start position / own castle.
void player_spawn(PlayerRec *rec, Thing *t);
void player_rebuild_spell_index(Thing *t);            // player_rebuild_spell_index_40240
void player_set_input_mode(PlayerRec *rec, int mode); // chat_message_show_3bb50 (sets PlayerRec.input_mode)

// ---- per tick ----------------------------------------------------------------------------------
// The simulation half of game_tick_update_32e80: local input hook, player_commands_process,
// win check, thing_update_all x 1 / 4 / 16 by g_cfg->substeps. No rendering, no sound.
void game_tick_sim();
void player_commands_process();                       // player_commands_process_3a8b0
void player_log_position(PlayerRec *rec, Thing *t);   // player_log_position_3e080
void game_check_level_won();                          // game_check_level_won_3db20
// The camera render_frame_1fab0 hands to render_view_2f6e0 for `player`: the log entry
// PlayerRec.view_entry as (x, y, yaw, z + 0x80, pitch, roll, zoom), every value sign-extended.
Camera player_camera(int player);

// ---- the flyer (class 3 type 0) ----------------------------------------------------------------
void player_type0_s0_update(Thing *t);                // player_type0_s0_update_402c0 (Table A state 0)
void player_dying_update(Thing *t);                   // player_dying_update_405f0 (state 2)
void player_type3_s3_update(Thing *t);                // player_type3_s3_update_40ab0 (state 3: dead)
void player_flyer_move(Thing *t);                     // player_flyer_move_3fc00
int  player_terrain_collide(Thing *t);                // player_terrain_collide_3fa40 (target in g_pos_scratch)
void player_apply_controls(Thing *t);                 // player_apply_controls_40e70
int  player_apply_hits(Thing *t);                     // player_apply_hits_40b70: 2 dead, 1 hit, 0
int  player_take_damage(Thing *t);                    // player_take_damage_42770 (balloons / shared)
void player_look_at_killer(Thing *t);                 // player_look_at_killer_409e0
void player_cast_spell(Thing *t, Thing *spell, uint32_t hand_flag, uint32_t input_bit);   // player_cast_spell_410f0
void player_set_combat_music_timer(Thing *t);         // player_set_combat_music_timer_40b50
void player_set_palette_effect(Thing *t, int effect); // player_set_palette_effect_3f210
void player_note_fire_distance(Thing *fire);          // player_note_fire_distance_3f240
void player_note_ridge_distance(Thing *node);         // player_note_ridge_distance_3f2c0

// ---- mana --------------------------------------------------------------------------------------
void   mana_totals_update(Thing *local_player);       // mana_totals_update_427d0
Thing *mana_add_to_owner(Thing *t);                   // mana_add_to_owner_428e0: the owner Thing or null

// ---- castle helpers player_spawn needs (the castle handlers themselves are not ported) ----------
void castle_apply_level_stats(Thing *castle, Thing *spell, int health, int mana);   // castle_apply_level_stats_42170
void castle_set_level_stats(Thing *castle);           // castle_set_level_stats_42200
void castle_spell_set_capacity(Thing *castle);        // castle_spell_set_capacity_42370

// ---- hooks into subsystems that are not this one (null = skipped) ------------------------------
extern void (*g_hook_castle_stamp_footprint)(Thing *scratch);          // castle_stamp_footprint_26320 (level features)
extern void (*g_hook_thing_set_castle_extents)(Thing *castle, int level);   // thing_set_castle_extents_353f0
extern void (*g_hook_player_local_input)();                            // player_local_input_16660 (input.cpp)
extern void (*g_hook_creature_kill_all)();                             // creature_kill_all_17ff0 (input.cpp)
extern void (*g_hook_input_mouse_center)();                            // input_mouse_center_4a000 (input.cpp)
extern void (*g_hook_sound_update)();                                  // sound_update_494b0 (sound.cpp), end of game_tick_sim
extern void (*g_hook_music_update)(int mood);                          // music_update_1f800 (sound.cpp)
// Game-state writes of render_frame_1fab0 (hud_tick_state, hud.cpp), once per tick after the sound
// update - where the original renders. Installed by engine_init (the renderer side).
extern void (*g_hook_frame_state)(int player);
// mouse_cursor_set_sprite_5ba5c from player_set_input_mode_3bb50: pointers entry 1 (the spell book) in input
// mode 2, entry 0 otherwise (null = no pointer handling). mcport: mouse_cursor_set_pointer (frontend.h).
extern void (*g_hook_player_mouse_cursor)(int entry);
// (g_hook_sound_request / sound_request() live in thing.h since round 3.)

// ---- Phase 4 (round 10) port-only hooks; null = the original (nothing happens) ------------------
// A command packet with cmd >= 0x20 (the original ignores those ids): port commands - debug commands 0x40..
// (debug_cmd.h), later the mode's orders. Called in player_commands_process for player p's packet, after the
// demo step, before the packet's steering is applied; `t` is the player's Thing.
extern void (*g_hook_port_command)(int player, Thing *t, CmdPacket *cmd);
// One tick of the active game mode (mode.h mode_tick), after thing_update_all, before the sound hook.
extern void (*g_hook_mode_tick)();
// Debug commands' per-tick work (god mode: debug_cmd.h debug_cmd_tick), right after the mode tick.
extern void (*g_hook_debug_tick)();
#define MC_HAVE_DEBUG_TICK_HOOK 1

// DAT_0012eab4: the 119 Hz timer tick counter (advanced by the platform layer; 0 in tests). Only
// PlayerBlock.start_tick is taken from it here.
extern uint32_t g_timer_ticks;

// Config fields this subsystem established that mc_types.h does not name yet.
inline uint8_t &cfg_tick_bit(int k) {           // Config+0x5d+k, k = 1..15: (local tick / k) & 1
    return reinterpret_cast<uint8_t *>(g_cfg)[0x5d + k];
}
