# Round 4 (A): resolved names for 0x10000-0x1f6b0 (wizard AI, creature helpers, terrain probes)

Evidence from `mc.py body/callers/tree`, `img.py dis` and `classtab.py`; Watcom convention applies (Thing in EBX for
handlers, otherwise stack args as listed). Every function below was read in full; none is a fragment.

```csv
0x00010c30,terrain_max_corner_level_10c30,"(pos*) stack arg; cx = pos byte +1 and cy = byte +3 (cell coordinates); returns max of the 4 heightmap bytes DAT_000ddfb0 at [cy:cx] [cy:cx+1] [cy+1:cx] [cy+1:cx+1] shifted >> 5 (0..6 coarse level: world height is byte*0x20 so this is highest corner / 1024); whole-cell variant of terrain_height_at_71e00 without interpolation; real entry (push ebx) but no callers = dead code"
0x00010cb0,terrain_minmax_along_path_10cb0,"(pos*, yaw u16, step i16, count i16, out int[3]) all on the stack: out[0]=0 out[2]=0x40000000 then for count+1 samples h=(short)terrain_height_at_10bc0(pos); out[0]=max(out[0],h); out[2]=min(out[2],h); pos advanced in place by math_rotate_offset_3e420(pos, yaw, pitch 0, step); out[1] untouched; returns 1; no callers (dead ray probe for the max/min terrain height ahead)"
0x00011820,castle_near_thing_11820,"(thing*) stack arg: walks g_cfg->player_list (cfg+0x8e6e); returns 1 if any castle (player thing type 2) satisfies |castle.x - thing.x| <= castle.ext_x + thing.ext_x + 0x300 and the same on y (bboxes within 3 cells on both axes) else 0; first function of the wizard-AI source file; no callers (castle_footprint_clear_11980 is the site test actually used)"
0x00012d70,ai_goal_repair_castle_12d70,"(player thing) goal evaluator of the ai_goal_* family: castle = things[playerrec(+0xa0)->castle +0x32]; if it exists (idx != 0) and castle.health (+0xc) < castle.max_health (+8) set target +0x92 = castle idx and +0x94 = thing_signature_14080(castle) and return 1 else 0; not called by ai_choose_goal_12330 (cut goal; the shipped equivalents are ai_goal_upgrade_castle_12df0 / ai_goal_retreat_12f70)"
0x00013600,ai_goal_creature_near_rival_13600,"(player thing) goal evaluator with no callers: for each of the 20 creature lists cfg+0x8e1e+i*4 and each creature c whose owner +0x18 != own owner: for each player p (recs state+0x340b+p*0x801, thing idx rec+0xa) whose thing is type 1 (AI wizard) and p != own playerrec+0x30: r = ai_mode12_idle_12680(self) (0 cruise / 1 casting Alliance) and only if r == that wizard's AI mode byte rec+0x19f and pos_dist_sq_xy(things[that wizard's target +0x92].pos, own pos) < 0x1900000 (20 cells) -> +0x92 = c idx, +0x94 = thing_signature_14080(c), return 1; else 0; using the idle handler as a predicate (it casts a spell as a side effect) is garbled logic consistent with a cut goal"
0x00019a90,creature_attack_fire_homing_desc_19a90,"attack callback with the same (thing, target) stack signature and body as creature_attack_fire_193f0: thing_create(pos thing+0x48, class 9, type 0); +0x44=10 +0x45=0; +0x9c = DAT_00096a50 = g_move_desc[2] (descriptor of projectile types 1 homing / 0x11; 193f0 uses 96ad0); copies owner +0x18, filters +0x42/+0x43 and target +0x92; yaw/pitch from pos_angle_to_3e6b0/pos_pitch_to_3e6e0 to the target; z += +0x54; damage +0x2c left at the alloc default; then DAT_000adfc4 = thing pos, math_rotate_offset_3e420(pos, yaw, pitch, dist 0) which returns early (no offset) and thing_move_to_3e1d0(proj, pos) relinks it at the owner position; returns 1 if created; no code or data reference in the whole image (the only 0x19a90 byte match at 0x3fef5 is inside an instruction encoding) = dead variant of 193f0"
0x0001aef0,creature_target_nearest_mana_ball_1aef0,"(thing) scans the whole pool things[1..999] (state+0x7507..0x2f503) for class 10 type 0x27 mana balls, keeps the smallest pos_dist_xyz_3e8a0 below 0x10000 and writes its index to +0x92 (0 when none); placed between the crab s35 (1aee0) and s36 (1afa0) handlers; the shipped crab code creature_crab_s31_1a830 does the same search inline over g_cfg->mana_ball_list (cfg+0x8e72) with 2D distance, so this is the earlier full-pool version; no callers"
0x0001c1e0,skeleton_convert_villager_1c1e0,"(thing) per-tick body of skeleton state 55 while the summoning pose is active (+0x47 == 1, entered via 1c8a0; called only from creature_skeleton_s55_update_1bb70): if +0x3a != 0 apply damage slot 0 (+0x5a/+0x5e -> +0x28, min health along the +0x36 chain); health < 0 -> +0x26 killer, thing_set_state(thing, 0x3a = skeleton dead); hit -> +0x92 = attacker, state 0x38 = creature_skeleton_s56_attack_1c570; else +0x1a < 0 counts up and at 0 skeleton_reset_convert_timer_1c8c0 (+0x47=0, +0x1a=400, sprite 0xc9); +0x3a != 0 without damage -> +0x1a = -50 (pose interrupted); otherwise no movement and every desc->think_period (+0x1a) ticks list (tick/period)%3 = 0 Archer g_cfg->creature_lists[4] / 1 Builder [0xc] / 2 Townie [0xd] -> nearest within desc->sight_radius (+0x1c) squared; if math_isqrt(3D dist^2) <= 0x600: thing_mark_delete(victim), thing_create(victim pos DAT_000adfc4, class 5, type 9 = Skeleton) and new->owner +0x18 = own owner: skeletons turn villagers into skeletons (1bb70 inlines the same search for the +0x47 == 0 case)"
0x0001c8a0,skeleton_start_convert_pose_1c8a0,"(thing) thing_set_sprite_35240(thing, 0xf5) and +0x47 = 1: enters the summoning pose; called only by creature_skeleton_s55_update_1bb70 when the +0x1a countdown (reloaded to 400 by skeleton_reset_convert_timer_1c8c0 and whenever +0x3a != 0) reaches 0; while +0x47 == 1 the s55 handler runs skeleton_convert_villager_1c1e0 instead of moving; 1c8c0 is the counterpart (sprite 0xc9, +0x47 = 0)"
```

## Notes for ENGINE.md

- **Skeleton conversion confirmed.** The tail of `skeleton_convert_villager_1c1e0` is `push 9; push 5; push 0xadfc4;
  call thing_create_35690` (0x1c542..0x1c54b), i.e. class 5 type 9 = Skeleton (type 9 * 6 = state 54 base), created
  at the deleted victim's position with `+0x18` = the skeleton's owner. The inline copy in
  `creature_skeleton_s55_update_1bb70` (0x1c164) does the same but copies the owner only when the owner thing is
  class 3 (a player). This closes the round-2 open question.
- **Skeleton state 55 layout.** `thing_set_state` constants: hit -> 0x38 (56, `creature_skeleton_s56_attack_1c570`),
  dead -> 0x3a (58); 1bb70 builds them as `[esp+0x34] + 2 / + 4` from the base 54. So for the skeleton, state 55 is
  the type-specific "wander, seek enemy castle/wizard, convert villagers" state and 56 is the attack state, not the
  generic +1 attack / +5 special layout used by the other creatures.
- **Thing field reuse by the skeleton:** `+0x47` (castle size index elsewhere) is the pose flag (1 = summoning pose,
  sprite 0xf5; 0 = normal, sprite 0xc9); `+0x1a` is the countdown to the next pose (400 ticks, reloaded while
  `+0x3a != 0`; set to -50 when the pose is interrupted). `+0x3a != 0` gates the damage-slot processing in both
  1bb70 and 1c1e0, so for creatures it behaves as a "was hit / in combat" flag rather than a plain animation timer.
- **Creature list indices** 4 / 0xc / 0xd in `g_cfg->creature_lists` are Archer / Builder / Townie, consistent with
  the creature type numbering (Dragon 0 ... Archer 4 ... Skeleton 9 ... Builder 12, Townie 13).
- **Correction for `ai_mode12_idle_12680`:** its stack argument is the player *thing*, not a spell id; the spell is
  hard-coded: `push 2; push ebx; call ai_spell_ready_14640` / `ai_cast_spell_14240` / `ai_get_spell_thing_13ac0`.
  Spell id 2 is **Alliance** (class-12 Table B index 2 = `spell_create_alliance_3a360`). So the idle mode casts
  Alliance when it is ready. The existing name comment ("stack arg = spell id") should be fixed.
- `math_rotate_offset_3e420(pos, yaw, pitch, dist)` returns immediately when `dist == 0`;
  `creature_attack_fire_homing_desc_19a90` passes 0, so its "offset" is disabled - probably why the function was
  left unreferenced.
- `creature_attack_fire_homing_desc_19a90` has **no reference anywhere in the image**: a byte search for the dword
  0x00019a90 over the whole image hits only 0x3fef5, which is inside `mov dx,[eax+0x19a]` in player_flyer_move_3fc00.
  It is dead code, not a register-passed callback.
- Dead AI goals `ai_goal_repair_castle_12d70` and `ai_goal_creature_near_rival_13600` are not in the
  `ai_choose_goal_12330` chain (confirmed by reading 12330). 13600 compares the 0/1 return of the idle handler with
  another wizard's mode byte - it would only ever match wizards in mode 0 or 1; speculation: a stale call target left
  in an abandoned "join another wizard's hunt" goal.
- `terrain_max_corner_level_10c30` returns `max corner height byte >> 5` (0..6); nothing else in the binary uses
  that scale (world height = byte * 0x20), so its intended consumer is unknown (speculation: castle/site level test).

## Open questions

- What sets the `+0x3a` creature flag (the write site is outside this region; `thing_add_pending_damage_117c0`
  does not touch it).
- Whether the dead `castle_near_thing_11820` (3-cell margin) was an early castle-site rule superseded by
  `castle_footprint_clear_11980`.
