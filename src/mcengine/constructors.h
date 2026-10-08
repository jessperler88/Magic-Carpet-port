// Table B constructors of carpet.exe (0x359c0..0x3a7e0, one original source file): scenery,
// players, creatures, weather, projectiles, effects, switches, spells. Owner: constructors.cpp.
//
// Every constructor takes the spawn position (one stack argument) and returns the new Thing, or
// null when thing_alloc_35560 fails. Some originals return null on purpose (the "Flyer" start
// markers, the empty classes 1 / 6 / 8 and weather types 0..3).
//
// Not here (agent A, level_features.cpp): the terrain-effect constructors of class 10, types 9, 0xa,
// 0xb, 0x1b..0x20, 0x2d, 0x32, 0x33.
#pragma once
#include "thing.h"

// Binds every ported constructor to its Table B records (thing_register_create).
void constructors_register_handlers();

// ---- helpers of the same address range that other subsystems call --------------------------------
// crab_update_mana_sprite_36ac0: the crab grows with the mana it carries (sprite 0xb9 + level 0..7,
// +5000 max health per new level). Caller: creature_crab_s33_collect_mana_1ac80.
void crab_update_mana_sprite(Thing *t);
// creature_troll_init_variant_36ea0: even serial -> sprite 0xc7, variant 2, 2000 hp; odd -> sprite
// 0x55, variant 1, 4000 hp; mana = hp / 2, health = max.
void creature_troll_init_variant(Thing *t);
// mana_ball_update_sprite_25e20 (effects code, but effect_create_mana_ball_39840 needs it): size
// level = first threshold >= mana (7 when none), sprite base by the owner's player number
// (0x69 + player * 8; 0x34 unowned / player -1). Also used by effects.cpp's mana ball update.
void mana_ball_update_sprite(Thing *t);
// The sprite mana_ball_update_sprite would give the ball now (no state change; *size_level = 0..7).
// The simulation only refreshes it while the ball is awake (timer_a != 0, within 24 cells of the local
// player), so a far ball keeps the colour of an owner it no longer has; the extended renderer draws this.
int mana_ball_sprite(const Thing *t, int *size_level = nullptr);
// switch_create_common_39dc0(pos, type, state): the body of all 32 switch constructors.
Thing *switch_create_common(const Pos *pos, int type, int state);
// spell_create_common_3a210(pos, type, state, total_mana, levels, flags, unk3e, cost, damage): the
// body of all 24 spell constructors.
Thing *spell_create_common(const Pos *pos, int type, int state, int total_mana, int levels,
                           int flags, int unk3e, int cost, int damage);
// Constructors with their own thing_alloc that no Table B record points at (no callers in the
// export either; kept for completeness).
Thing *projectile_create_type13_long(const Pos *pos);   // projectile_create_type13_long_383cc (entry 0x383d0)
Thing *effect_create_type37(const Pos *pos);            // effect_create_type37_397c0
