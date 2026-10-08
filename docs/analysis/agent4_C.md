# Round 4 (C): 0x40000-0x4c000 - resolved names

All 14 functions were read from the decompiled C, the capstone disassembly, their callers and the data they touch.
Class/state numbers were re-checked against `classtab.py`; spell ids follow `state = spell*3 + phase` with the
Table B create order (0 Fireball, 1 Heal, 2 Speed-up, 3 Possession, 4 Shield, 5 Beyond sight, 6 Earthquake,
7 Meteor, 8 Volcano, 9 Crater, 10 Teleport, 11 Rubber band, 12 Invisible, 13 Steal mana, 14 Rebound,
15 Lightning, 16 Castle, 17 Skeleton, 18 Thunderbolt, 19 Mana magnet, 20 Fire wall, 21 Reverse speed,
22 Smart bomb, 23 Mini fireball). Note that `classtab.py` prints the per-model label list against Table A
*state* indices for class 2 and class 12, so its labels on those rows are off by the factor 3 (state 12 is
printed as "Invisible" but is spell 4 Shield); the handler bodies confirm the spell*3 mapping in every case.

```csv
0x00040b50,player_set_combat_music_timer_40b50,"(thing) if the thing has a player record (+0xa0): P+0x2e = 100. player_flyer_move_3fc00 decrements P+0x2e every tick for the local player and passes mood 2 (combat) to music_update_1f800 while it is > 0, mood 1 (calm) otherwise, so this starts 100 ticks of combat music. Called by player_apply_hits_40b70 (3 sites, when the player is hit) and by projectile_pick_target_45f00 when a projectile locks onto a class 3 type 0 (human) player; zeroed in player_spawn_3f360"
0x00042000,castle_build_seq_set_done_42000,"(castle thing) i16 +0x30 = 2. In the castle state-5 build/upgrade sequencer (player_flyer2_s5_update_41500) step 2 means build finished -> state 4 (active castle); counterpart of castle_begin_build_stage_41f00 (+0x30 = 4 wait) and the build effects (3 -> 4, 5 -> 6). No callers in the export (dead helper), reached by no table"
0x00042010,castle_collapse_level_42010,"(castle thing) called from player_respawn_start_416d0 (class 3 state 6 destroyed castle). If level +0x1a > 0: capacity +0x88 is lowered by 10% so castle_spill_mana_41720 spills that 10% as class 10 type 0x27 mana balls, then restored; sound 0x1e; scratch Thing things[0] (state+0x7463) gets the castle pos +0x96..+0x9a, castle_size +0x47 = level, owner +0x18, type 0, aux 0, caster = castle index and effect_type51_s53_update_27930 is run on it (collapse effect); level -1; thing_set_castle_extents_353f0 + castle_set_level_stats_42200 + castle_spell_reset_charge_41310. If level is now 0: reset charge again, owner P+0x32 (castle thing idx) = 0 and thing_mark_delete_3e3e0"
0x00043db0,scenery_standing_stone_update_43db0,"class 2 state 3 = Standing stone (type 1; scenery_create_standing_stone_35f90 sets +0x46 = 3): flags |= 0x20000 (byte +0x12 |= 2, the recyclable bit effect constructors set) and snap z +0x4c = terrain_height_at_10bc0(pos +0x48). No damage or burn handling (unlike scenery_tree_update_43ba0); byte-identical to scenery_badstone_update_43e60"
0x00043de0,scenery_dolmen_update_43de0,"class 2 state 6 = Dolmen (type 2; scenery_create_dolmen_36010 sets +0x46 = 6): for each player p < g_state->player_count whose thing (players[p].thing) has health >= 0, thing_collide_105c0(player thing, dolmen) -> set flag 0x1000 on the player thing; player_type0_s0_update_402c0 and player_ai_wizard_tick_11f20 treat 0x1000 like standing in the own castle (mana regen +0x84 = +0x88/200 min 1000, health regen max/200 instead of max/500) and clear it each tick. Then snap z +0x4c to terrain height. Does not set the recyclable bit"
0x00043e60,scenery_badstone_update_43e60,"class 2 state 9 = Bad stone (type 3; scenery_create_bad_stone_360a0 sets +0x46 = 9): flags |= 0x20000 (recyclable) and snap z +0x4c = terrain_height_at_10bc0(pos +0x48); byte-identical to scenery_standing_stone_update_43db0, no damage handling"
0x00046960,creature_proximity_wake_timer_46960,"(creature or mana ball; called every tick from creature_sound_tick_468e0 for the 20 creature lists + mana-ball list) awake countdown +0x3a: if nonzero decrement it and copy the value to every +0x36 chain segment; if zero and snooze +0x3b is zero: dist^2 (pos_dist_sq_xy_3e970) from the local player's thing (players[g_state->local_player].thing) to +0x48 < 0x2400000 (6144 units = 24 cells) -> +0x30 = isqrt(dist), +0x3a = 0x10 (segments 0x12); else +0x3b--. +0x3a != 0 is the activity gate: creature handlers (creature_ai_step_18870, archer 1a0e0, crab 1a830, skeleton 1bb70 ...) only process damage slots and think while it is nonzero and projectile_pick_target_45f00 only targets things with +0x3a != 0; thing_alloc default 0xfa, mana ball create 0x80, dead creature 0xfa (468e0)"
0x00047760,spell_shield_update_47760,"class 12 state 12 = spell 4 Shield phase 0 (table B model 4 = spell_create_shield_3a450): while +0x30 > 0 and caster +0x2a is a valid thing: spell_can_cast_46e70 ok -> caster flags |= 0x4000 (shield active: player_apply_hits_40b70 quarters the damage and charges the quarter to mana) and spell_charge_mana_46f20; cannot cast -> +0x30 = 1 (abort next tick); +0x30--. The flag is re-set every cast tick and not cleared by this handler"
0x00047840,spell_earthquake_update_47840,"class 12 state 18 = spell 6 Earthquake phase 0: on the first cast tick (+0x30 == +0x32) thing_create_35690(caster pos, class 9, type 2) projectile with speed +0x7e += caster speed, spell_projectile_origin_46f90, impact effect +0x44/+0x45 = 10/0xf (Earthquake effect), owner, yaw/pitch, mana +0x8c and damage +0x2c from the spell, aim +0x1a = P+0x146 (then cleared), target +0x96 = caster pos rotated by math_rotate_offset_3e420 with z snapped to terrain_height_at_10bc0; sound; every tick spell_charge_mana_46f20 and +0x30--; cannot cast -> +0x30 = 1"
0x000479f0,spell_meteor_update_479f0,"class 12 state 21 = spell 7 Meteor phase 0: on the first cast tick (+0x30 == +0x32) thing_create_35690(caster pos, class 9, type 3) projectile with speed += caster +0x7e, spell_projectile_origin_46f90, impact effect +0x44/+0x45 = 10/0x11 (Meteor effect), owner, mana +0x8c, damage +0x2c, aim +0x1a = P+0x146 (cleared), target +0x96 = caster pos rotated (3e420, no terrain snap), yaw/pitch from caster; sound; then spell_charge_mana_46f20 and +0x30-- each tick; cannot cast -> +0x30 = 1"
0x00047d40,spell_crater_update_47d40,"class 12 state 27 = spell 9 Crater phase 0: same shape as the earthquake handler: first cast tick creates class 9 type 5 projectile with impact effect +0x44/+0x45 = 10/0xb (Crater effect), speed += caster +0x7e, origin 46f90, owner, yaw/pitch, mana, damage +0x2c, aim byte P+0x146, target +0x96 = rotated caster pos with z from terrain_height_at_10bc0; sound; spell_charge_mana_46f20 and +0x30-- each tick; cannot cast -> +0x30 = 1"
0x00048490,spell_rebound_update_48490,"class 12 state 42 = spell 14 Rebound phase 0: while +0x30 > 0: spell_can_cast_46e70 ok -> caster flags |= 0x8000 (rebound active; projectile_fly_and_impact_44150 reflects fireball/meteor-type projectiles off a 0x8000 target) and spell_charge_mana_46f20, else +0x30 = 1; +0x30--. When +0x30 <= 0 clears caster flag 0x8000 (byte +0x11 &= 0x7f) - the only handler of this family that turns its own flag off"
0x00048510,spell_lightning_update_48510,"class 12 state 45 = spell 15 Lightning phase 0: on the first cast tick thing_create_35690(caster pos, class 9, type 9) = lightning projectile (projectile_lightning 44fc0) with speed += caster +0x7e, origin 46f90, impact effect +0x44/+0x45 = 10/0x17 (Lightning effect), owner, mana +0x8c, yaw/pitch, aim +0x1a = P+0x146 (cleared), damage +0x2c, target +0x96 = rotated caster pos (no terrain snap); sound; spell_charge_mana_46f20 and +0x30-- each tick; cannot cast -> +0x30 = 1"
0x000492e0,spell_mini_fireball_update_492e0,"class 12 state 69 = spell 23 Mini fireball (rapid fireball) phase 0: code-identical to spell_fireball_update_47130: while +0x30 > 0 and can cast, loop while burst counter +0x3d >= 0: on the first cast tick create a class 9 type 0 projectile (impact effect 10/0 explosion, speed += caster +0x7e, origin 46f90, owner, damage +0x2c, mana, yaw/pitch, aim P+0x146, target = rotated caster pos, sound) and spell_charge_mana_46f20 per iteration, +0x3d--; then +0x3d = 0 and +0x30--; cannot cast -> +0x30 = 1"
```

## Notes for ENGINE.md

### Player record P+0x2e = combat-music timer (established)

`player_set_combat_music_timer_40b50` writes 100 into P+0x2e; the only reader is `player_flyer_move_3fc00`
(disassembly 0x401b8-0x401e9): for the local player (`P+0x30 == g_state->local_player`) it decrements P+0x2e while
positive and calls `music_update_1f800(mood)` with mood **2 while P+0x2e > 0, else 1**. So P+0x2e is "ticks of
combat music left"; a hit (40b70) or being locked onto by a projectile (45f00) keeps the combat track playing for
100 ticks. ENGINE.md's player record table (~line 1200) should list `+0x2e i16 combat music timer (100)`.

### +0x3a is an "awake / near the local player" gate, not a sound timer (established)

`creature_sound_timer_46960` and its caller `creature_sound_tick_468e0` were named after the +0x30 "distance for
sound" field. The consumers show the real role of +0x3a:

- 20 creature handlers (`creature_ai_step_18870`, `creature_archer_s25_1a0e0`, `creature_crab_s31_1a830`,
  `creature_kraken_s38_update_1b000`, `creature_skeleton_s55_update_1bb70`, builders, townies, trader, wyvern ...)
  start with `if (timer_a != 0) { apply damage slots; think ... }` - a dormant creature neither takes pending
  damage nor runs its AI.
- `projectile_pick_target_45f00` skips creatures, mana balls and wizards whose `timer_a == 0` (5 tests), so
  auto-aim only locks onto things near the local player.
- Defaults: `thing_alloc_35560` sets +0x3a = 0xfa (250 ticks awake after creation), `effect_create_mana_ball_39840`
  0x80, dead creatures are reset to 0xfa/0 by 468e0 so death animations run.
- 46960 reloads +0x3a = 0x10 only when the thing is within 24 cells (dist^2 < 0x2400000) of
  `players[g_state->local_player].thing`; segments of a chain (+0x36) get 0x12 and are kept in step while the head
  counts down. +0x3b is a snooze: while nonzero the distance test is skipped and +0x3b-- (only writers: 46960
  itself and the mana-ball constructor, which zeroes it).

Suggested renames outside my list: `creature_sound_tick_468e0` -> `creature_wake_tick_468e0`. The +0x30 = isqrt(dist)
written by 46960 has no reader in the export other than the debug overlay `dbg_draw_thing_timers_23050`; the
"distance for sound" meaning in the Thing table (+0x30) is therefore unconfirmed (speculation: leftover from a
distance-based creature-call sound that was moved into `sound_request_49720`, which computes its own distance).

### Scenery handlers

- Standing stone (state 3) and Bad stone (state 9) handlers are byte-identical: set flag 0x20000 every tick and
  snap z to the terrain. 0x20000 is the bit effect constructors set (`flags = (flags & ~0x20008) | 0x20000`), i.e.
  "may be recycled when the pool is exhausted" (thing_alloc fallback stack). Trees set it only once they burn
  (43ba0 line 51-53). Dolmens never set it, so they are never recycled.
- Dolmen (state 6) is a regeneration site: it sets flag **0x1000** on every player thing whose bbox overlaps it;
  `player_type0_s0_update_402c0` and `player_ai_wizard_tick_11f20` test `(inside own castle) || flag 0x1000` for the
  fast regen branch (mana +0x84 = +0x88/200 min 1000, health regen max/200 vs max/500 outside) and clear the flag.
  ENGINE.md line 1225 already records the flag; this adds its effect.

### Spell cast handlers (class 12 phase 0) - projectile/effect pairing

All five projectile spells in my list follow the fireball template (first tick when `+0x30 == +0x32` creates the
projectile, every tick `spell_charge_mana_46f20`, `+0x30--`, cannot-cast sets `+0x30 = 1`). The class 9 projectile
type and the impact effect written to +0x44/+0x45 are:

| spell | state | projectile class 9 type | impact effect 10/x | terrain-snap target z |
|---|---|---|---|---|
| 4 Shield | 12 | none - sets caster flag 0x4000 | - | - |
| 6 Earthquake | 18 | 2 | 0xf Earthquake | yes |
| 7 Meteor | 21 | 3 | 0x11 Meteor | no |
| 9 Crater | 27 | 5 | 0xb Crater | yes |
| 14 Rebound | 42 | none - sets/clears caster flag 0x8000 | - | - |
| 15 Lightning | 45 | 9 (lightning projectile 44fc0) | 0x17 Lightning | no |
| 23 Mini fireball | 69 | 0 (burst of +0x3d+1) | 0 Explosion | no |

Shield (0x4000) is re-asserted every cast tick but no code clears it (grep for `& 0xbf` on +0x11 / `0xffffbfff`
finds only the mana-ball handler, a different thing). Open question: where the shield flag is dropped when the
spell ends (possibly never, i.e. shield lasts until the player dies and the thing is re-allocated - speculation).

### Castle

- `castle_build_seq_set_done_42000` (+0x30 = 2) is the "build finished" step of the state-5 sequencer but has no
  caller; the sequencer reaches step 2 some other way or this is dead code from an earlier build flow.
- `castle_collapse_level_42010` spills exactly 10% of the capacity +0x88 by temporarily lowering the capacity
  before calling `castle_spill_mana_41720` (which spills `+0x8c + P+0x134 - +0x88`) and restoring it afterwards; the
  collapse visual is produced by calling the class-10 state-53 handler directly on the scratch Thing `things[0]`
  (state+0x7463) filled with the castle's position, level (+0x47) and index (+0x2a), not by creating a Thing.
