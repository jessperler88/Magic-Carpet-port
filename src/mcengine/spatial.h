// Spatial queries and damage delivery of carpet.exe (0x102b0..0x117c0) plus the small shared helpers
// several subsystems call (0x3da30, 0x41310, 0x43ea0..0x43ee0). Owner: spatial.cpp (round-3 core).
//
// Damage model (docs/ENGINE.md "Damage slots"): Thing+0x5a holds six 6-byte slots indexed by damage
// type, {i32 amount, u16 attacker thing index}. Dealers only write a slot; the victim consumes it in
// its own update (thing_apply_pending_damage_27f90, creature_ai_step_18870, player_take_damage_42770,
// castle_take_damage_42460, ...). Thing.prop_flags (+0x1c) is the mask of damage types a thing
// accepts (1 << type); bit 0 = "damageable" = type 0.
#pragma once
#include "thing.h"

// ---- collision searches (spiral walk over the cells within ext_x of the searcher) ----------------
// All return the first match or null. `t`'s filter (filter_cls / filter_type, 0xff = any) selects the
// candidates; a candidate with t's owner never matches.
Thing *thing_find_collision(Thing *t);              // thing_find_collision_105f0: collidable (flag 8) things
Thing *thing_find_collision_other_owner(Thing *t);  // thing_find_collision_other_owner_10980: without the flag-8 test
Thing *thing_find_mana_near(Thing *t);              // thing_find_mana_near_10730: effect 0x27 / 0x28 / 0x2d not owned (+0x90) by t's owner
Thing *thing_find_mana_ball_touching(Thing *t);     // thing_find_mana_ball_touching_10870: any collidable mana ball (0x27)
// thing_exists_near_pos_10ac0(pos, cls, type): a thing of that class / type within 3D distance 0x80
// in the 2x2 cells around pos - 0x80.
int    thing_exists_near_pos(const Pos *pos, int cls, int type);

// ---- damage --------------------------------------------------------------------------------------
// thing_area_damage_10d20(t, slot, amount): every thing whose box overlaps t's (cells within ext_x),
// other owner, collidable, accepting damage type `slot`, passing t's filter. Slot 0 also hits enemy
// castles (player type 2) through the player list and skips them in the cell walk.
void thing_area_damage(Thing *t, int slot, unsigned amount);
// thing_area_damage_11160: slot 0 only; trees (class 2 type 0) take amount / 10 (fire).
void thing_area_damage_fire(Thing *t, int slot, unsigned amount);
// thing_area_damage_11450: slot 0 only; every castle touched (the owner's too) gets its hit timer
// (+0x32) set to 0x1e (volcano / crater / meteor).
void thing_area_damage_quake(Thing *t, int slot, unsigned amount);
// thing_add_pending_damage_117c0(attacker, victim, slot, amount): unconditional slot write. Note the
// original's test is the reverse of the area functions': amount is *added* when no attacker is
// recorded yet and *replaced* when one is.
void thing_add_pending_damage(const Thing *attacker, Thing *victim, int slot, unsigned amount);
// thing_try_damage_116e0: the same write behind the victim / filter / owner / overlap checks (dead code
// in retail, kept for completeness).
void thing_try_damage(const Thing *attacker, Thing *victim, int slot, unsigned amount);
// cell_kill_things_3da30(cell, owner): everything in the cell not owned by `owner`: scenery (class 2)
// is deleted, creatures (class 5, except types 6, 8, 0x10) get health -1 and killer = attacker = owner.
void cell_kill_things(unsigned cell, unsigned owner);

// ---- terrain checks ------------------------------------------------------------------------------
// creature_check_terrain_102b0(t, pos, flags): non-zero when `pos` is not acceptable for t's MoveDesc.
// flags: 2 = height above ground outside [desc+0xc, desc+0xa], 1 = terrain type not in
// desc.terrain_mask (returns the offending mask bits), 4 = pitch from t to pos beyond desc+0x12 (up)
// / desc+0x10 (down).
uint32_t creature_check_terrain(const Thing *t, const Pos *pos, unsigned flags);
int  terrain_max_corner_level(const Pos *pos);      // terrain_max_corner_level_10c30 (dead code in retail)
// terrain_minmax_along_path_10cb0: out[0] = max, out[2] = min ground height over count + 1 samples;
// `pos` is advanced in place (dead code in retail).
int  terrain_minmax_along_path(Pos *pos, unsigned yaw, int step, int count, int32_t out[3]);

// ---- small shared helpers ------------------------------------------------------------------------
// thing_z_add_half_height_43ea0 / thing_z_sub_half_height_43ec0: z +=/-= ext_z0 unless type == 2
// (bracket an aim computation so it targets the middle of the box).
inline void thing_z_add_half_height(Thing *t) { if (t->type != 2) t->z = (int16_t)(t->z + t->ext_z0); }
inline void thing_z_sub_half_height(Thing *t) { if (t->type != 2) t->z = (int16_t)(t->z - t->ext_z0); }
// thing_aim_at_43ee0(a, b): a.target_yaw / target_pitch := direction to the middle of b.
void thing_aim_at(Thing *a, Thing *b);
// castle_spell_reset_charge_41310(castle, mode): the Castle spell Thing of the castle owner's player
// block (P+0x2c4): mode 0 -> cast_ticks = 0, else cast_ticks = duration - 1.
void castle_spell_reset_charge(const Thing *castle, int mode);

// ---- hooks into round-3 subsystems (null until the owner's *_register_handlers() installs them) ---
extern void (*g_hook_thing_drop_mana_ball)(Thing *t);     // thing_drop_mana_ball_25fe0 (effects)
