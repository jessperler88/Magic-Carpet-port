// Castles and balloons (class 3 type 2 / type 3, states 4..10; code 0x41290..0x42960): the active
// castle, the build / upgrade sequencer, destruction, damage, mana spill, balloons and guards.
// Owner: castle.cpp (round 3). Report: docs/analysis/port_castle.md.
//
// The castle Thing (constructor player_create_type2_35bc0, state 5):
//   aux (+0x1a)        level 0..7              cast_ticks (+0x30)  build sequencer step (state 5)
//   duration (+0x32)   hit / rebuild timer     z_vel (+0x2e)       guard spawn cooldown (0x10 ticks)
//   home (+0x96)       cell-aligned site       mana / mana_total   stored mana / capacity of the level
//   flags 0x40         upgrade requested       flags 2             player colour added to the sprite
//   damage_slots[5].attacker (+0x7c) == owner  upgrade request written by the castle spell's effect
// The owner's player block (reached through things[owner].player, never through the castle's own
// +0xa0) holds castle (+0x32), balloons[3] (+0x34), guards[34] (+0x54), castle_level (+0x1a0).
//
// The balloon (constructor player_create_type3_35ca0, state 7 -> 9 when a castle adopts it):
//   target (+0x92)     mana ball to fetch or the castle to unload at (0 = stay)
//   mana / mana_total  cargo / cargo capacity (10000)       speed_cur (+0x7e)  0x30
#pragma once
#include "thing.h"
#include "spatial.h"

// Binds this subsystem's handlers to the class tables (by original address). Also calls
// scenery_register_handlers() as long as engine_init does not (see the report).
void castle_register_handlers();

// ---- Table A handlers (class 3) ------------------------------------------------------------------
void castle_active_update(Thing *t);        // player_flyer1_s4_update_413a0 (state 4: active castle)
void castle_build_update(Thing *t);         // player_flyer2_s5_update_41500 (state 5: build / upgrade sequencer)
void castle_destroyed_update(Thing *t);     // player_respawn_start_416d0 (state 6: health < 0, loses a level)
void balloon_update(Thing *t);              // balloon_update_42530 (state 9)
// player_update_shared_42520 (states 7, 8), player_flyer8_s10_update_42760 (state 10) and
// class8_update_shared_42960 (class 8 states 0..5) are single `ret` instructions.
void castle_update_none(Thing *t);

// ---- helpers --------------------------------------------------------------------------------------
// castle_find_free_mana_ball_41290(from, excl_a, excl_b): the mana ball (effect 0x27) owned by
// from's owner that is nearest to `from` (squared 3D distance) and is neither excl_a nor excl_b.
// The original caller passes the *balloon* as `from`, not the castle.
Thing *castle_find_free_mana_ball(const Thing *from, const Thing *excl_a, const Thing *excl_b);
void castle_spawn_build_effect_2a(Thing *castle);        // castle_spawn_build_effect_2a_41610: effect 0x2a, step := 4
void castle_spawn_build_effect_29(Thing *castle);        // castle_spawn_build_effect_29_41670: effect 0x29, step := 6
void castle_spill_mana(Thing *castle);                   // castle_spill_mana_41720
void castle_manage_balloons_and_guards(Thing *castle);   // castle_manage_balloons_and_guards_419a0
void castle_begin_build_stage(Thing *castle);            // castle_begin_build_stage_41f00: level + 1, effect 0x2a
void castle_build_seq_set_done(Thing *castle);           // castle_build_seq_set_done_42000 (no caller in retail)
void castle_collapse_level(Thing *castle);               // castle_collapse_level_42010
int  castle_take_damage(Thing *castle);                  // castle_take_damage_42460: 2 dead, 1 hit, 0

// Balloons / guards a castle of `level` keeps (the jump table at 0x41978): 0,1,1,1,2,2,3,3 and
// 0,0,0,4,6,14,18,34; 0 for a level outside 0..7.
int castle_level_balloons(unsigned level);
int castle_level_guards(unsigned level);
