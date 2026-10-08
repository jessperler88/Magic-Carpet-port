# Table B constructors port (constructors.cpp) - agent C, round 2, 2026-10-06

Files: `src/mcengine/constructors.h`, `constructors.cpp` (one file, about 1670 lines),
`src/mcengine/tables/constructors.tables` -> `gen/constructors_tables.h`,
`src/tests/constructors_test.cpp` + `constructors_test.cmake`. No shared file was touched.
Build: `cmake --build ../build_C --config Debug --target constructors_test`, run
`../build_C/Debug/constructors_test.exe "C:/Magic Carpet/MagicCarpet/magic"`: exit 0 (Debug and
Release), zero warnings, about 2 s.

## Functions translated (from the disassembly of 0x359c0..0x3a840)

All 178 Table B records of classes 1, 2, 3, 5, 6, 7, 8, 9, 10 (minus agent A's 12), 11, 12, 13 are
bound by `constructors_register_handlers()`; 144 create a thing, 34 return null by design.

| port | original | notes |
|---|---|---|
| `player_create_flyer_start<0..7>` | player_create_flyer1_359c0, 359e0, 35a00, 35a20, 35a40, 35a60, 35a80, 35aa0 | class 3 types 4..11: **no thing**; the position is stored in `GameState.start_pos[type - 4]`, returns null |
| `player_create_flyer`, `player_create_type1` | 35ac0, 35b40 | 10000 hp, speed_base 0x50, prop 0x1d, sprite 0x2c, desc 7 / 8 |
| `player_create_type2` (castle) | 35bc0 | 40000 hp, prop 0x21, sprite 0xb1; position snapped to a cell corner (x cell + 1 when x cell + y cell is odd), home = that, z = terrain at the requested position; leaves the request in `g_pos_scratch` |
| `player_create_type3` | 35ca0 | state 7, 10000 hp, speed 0x30, desc 9, sprite 0xa9 |
| `create_alloc_discard` | class1 35d20..35e40 (10), class6 37aa0 / 37ac0, weather 37ae0..37b40 (4), class8 37be0..37c80 (6) | `thing_alloc; restore_health; return 0` - the slot is lost until the next models_initialise |
| `scenery_create_tree`, `_standing_stone`, `_dolmen`, `_bad_stone`, `_2d_dome_a/_b` | 35e60, 35f90, 36010, 360a0, 36120, 36190 | |
| `creature_create_dragon` .. `_wyvern` (17) | 362d0, 36510, 36610, 36750, 36980, 36b30, 36c80, 36f00, 37000, 37110, 37260, 37370, 374a0, 375e0, 37730, 37850, 37980 | dragon / worm: head + 16 segments, kraken: head + 2 |
| `crab_update_mana_sprite` (public) | crab_update_mana_sprite_36ac0 | called by the crab's update handler |
| `creature_troll_init_variant` (public) | creature_troll_init_variant_36ea0 | |
| `weather_create_wind` | 37b60 | not linked into a cell |
| `projectile_create_type0` .. `_type19` (20) | 37cb0 .. 386b0 | |
| `projectile_create_type13_long` (public, unbound) | 383cc (entry 0x383d0) | no Table B record |
| `effect_create_shared_none` | effect_create_shared_37ca0 | types 0x14, 0x15, 0x16, 0x18: `xor eax,eax; ret` |
| `effect_create_explosion`, `_big_explosion`, `_type2`, `_type3`, `_type4`, `_splash`, `_fire`, `_type7`, `_mini_volcano`, `_type12`, `_white_smoke`, `_black_smoke`, `_earthquake`, `_type16`, `_meteor`, `_type18`, `_type19`, `_lightning`, `_steal_mana`, `_type26`, `_type33`, `_teleport`, `_type35`, `_type36`, `_type38`, `_mana_ball`, `_type40`, `_type41`, `_type42`, `_type43`, `_type44`, `_crab_egg`, `_type53`.. `_type56` | 38730, 387b0, 38810, 38870, 388e0, 38950, 389d0, 38a70, 38b10, 38cb0, 38d40, 38de0, 38e80, 38f60, 38f10, 39050, 390a0, 39120, 391a0, 39220, 39770, 395a0, 39670, 39680, 39700, 39840, 398c0, 39a00, 39a50, 39990, 39aa0, 39b00, 39b80, 39c10, 39ca0, 39d30 | |
| `effect_create_type37` (public, unbound) | effect_create_type37_397c0 | no Table B record |
| `switch_create_common` (public) + `switch_create_stub<0..31>` | switch_create_common_39dc0, 39e10..3a1f0 | type N -> state N |
| `spell_create_common` (public) + `spell_create_stub<0..23>` | spell_create_common_3a210, 3a2e0..3a720 | parameter table `kSpells` (9 pushed arguments per stub) |
| `class13_create<0..3>` | 3a750, 3a780, 3a7b0, 3a7e0 | |
| `mana_ball_update_sprite` (file-private) | mana_ball_update_sprite_25e20 | outside the address range; needed by the mana-ball constructor. Thresholds `g_mana_ball_thresholds` (DAT_00093910) via constructors.tables |

Not translated (agent A): 38b70, 38bd0, 38c40, 392a0, 39300, 393c0, 39360, 39470, 39420, 39930, 39540,
394d0. The six stubs at 0x36200..0x362a0 (allocate-and-drop, class 4 has no table) are dead code.

Conventions: `Thing.desc` = `(addr - 0x96a10) / 0x20` (`desc_index()`); no constructor writes
Thing+0xa0; per-thing RNG steps and the per-type serial counter are in the original order; field
stores between two calls are grouped.

## Verification (`constructors_test`)

**Unit.** Each of the 178 bound records is called once on an empty pool: expected null / non-null,
class, type, a state that Table A accepts, desc < 30, cell-list head. Dragon / worm / kraken are
checked link by link (parent / child chain, owner = head, state 0x78, sprites, tick = segment number,
mana values), plus the 16-free-slots rule (dragon and kraken refuse at 15 free, the worm builds a
shortened body).

**Level 38 against `movie/gam00000.dat`** (snapshot kept in its own buffer; 449 live things).

Pass A, plain spawn (`terrain_build`, `thing_pool_reset`, `models_initialise`,
`switch_activate(0, true)`): 463 level-start records -> 457 things.

| class / type | ours | snapshot | explanation of the difference |
|---|---|---|---|
| 2 / 0 trees | 150 | 150 | |
| 3 / 0, 1, 2, 3 | 0 | 1, 3, 2, 2 | created by player_spawn_3f360 (agent D) from the start positions; the level's class-3 records are the four "Flyer" markers |
| 5 / 9 skeletons | 157 | 157 | |
| 5 / 0xc builders | 4 | 2 | 2 gone |
| 5 / 0xd townies | 50 | 0 | all gone; creature_townie_s79_update_1e140 walks a townie to the nearest type-0x2d thing and raises that thing's +0x1a, so they most likely entered those (inferred from the handler, not traced) |
| 5 / 0xe traders | 10 | 8 | 2 gone |
| 5 / 4 archers | 0 | 2 | spawned at run time |
| 9 / 0xd | 0 | 4 | projectiles in flight |
| 10 / 0x12 | 0 | 1 | run-time effect |
| 10 / 0x27 mana balls | 80 | 54 | 41 collected / merged, 15 new |
| 10 / 0x2d | 0 | 19 | agent A's (17 from feature generation, 2 built during play) |
| 10 / 9 volcano | 0 | 0 | 2 level-start records (agent A's constructor); gone again by tick 413 |
| 11 / 0, 2 switches | 3, 2 | 3, 2 | |
| 12 spells | 1 (type 0x12) | 39 | 38 are the four players' own spell things (run time); the level has **one** pickup |

All 156 static things (150 trees, 5 switches, 1 spell pickup) are found in the snapshot at the same
class / type / cell. **Index mapping** snapshot - ours: **+19** for our #1..#385, **+21** from our
#386 on. Reason: in the original 17 things of feature generation (class 10 type 0x2d) own slots
1..17 and two more own slots 364 and 365 (feature generation used the pool up to there; what
survives keeps its slot and models_initialise rebuilds the free stack around it), and two volcano
records (class 10 model 9, DisId 0) precede the first static thing.

Pass B, aligned spawn: slots 1..17, 364, 365 pre-occupied, a test stand-in for the volcano
constructor, `GameState.rng` = 0x1d3b50ab (what all 5 switches imply; it is the level seed 0xcc4c
stepped once, i.e. the one step of level_run_terrain_effects_34fa0), terrain from `map00000.dat`.
Then every thing sits in its snapshot slot and is compared by index over all 61 fields (the whole
0xa4 bytes). Before comparing, our thing is advanced by what the per-tick code is known to do:
`tick += 156` (snapshot - ours is 156 mod 256 for 323 of 323 static things and creatures = 412
thing_update_all calls at PlayerRec.tick 413); trees `flags |= 0x20000` and `z = terrain height`
(scenery_tree_update_43ba0 does both on every call); switches `flags &= ~1`
(player_commands_process_3a8b0 at 0x3b5d5 clears bit 0 of every class-11 thing); spell pickup
`flags |= 1` when the local player owns the spell (spell_dropped_update_46ae0).

| kind | pairs | result |
|---|---|---|
| trees (2 / 0) | 150 of 150 | **0 of 61 fields differ** (rng, jittered x / y, sprite choice, cell links included) |
| switches (11 / 0, 11 / 2) | 5 of 5 | **0 of 61 fields differ** |
| spell pickup (12 / 0x12) | 1 of 1 | **0 of 61 fields differ** |
| skeletons (5 / 9) | 157 of 157 | 40 fields identical in all pairs: rng, max_health, health, flags, owner, prop_flags, pitch (= the constructor's heading), target_pitch, damage, speed, tick (= per-type serial + 156), filter_cls, speed_base, turn_rate, mana, mana_total, desc, player, ... The 21 that differ are AI-driven: next, cell links, x, y, z, yaw, target_yaw, aux, state, timer_a, sprite, extents, castle_size (31), filter_type (6), speed_cur (6), target (37), damage_slots (1) |
| builders (5 / 0xc) | 2 of 4 | 49 identical; differ: next, rng, aux, yaw, target_yaw, cast_ticks, timer_a, state, x, y, z, target |
| traders (5 / 0xe) | 8 of 10 | 49 identical; differ: next, rng, health (1), yaw, target_yaw, cast_ticks, timer_a, x, y, z, damage_slots (1), target |
| mana balls (10 / 0x27) | 39 of 80 | 41 identical (max_health, flags, prop_flags, filter, state, speed_base, ...); the rest is rolling / merging (not enforced) |

Unexplained differences: **none**. Gone by tick 413 (slot free or reused): 2 builders, 50 townies,
2 traders, 41 mana balls, the 2 volcano stand-ins.

Start positions: `GameState.start_pos` (written by the class-3 type 4..11 constructors) equals the
snapshot's for all 8 players in x, y and z on the generated terrain (player 1: 0xab80, 0x3080, 3919;
on the snapshot terrain z is 5311 because the castle built there raised the ground).

Pass C, constructors that level 38 does not use at level start: for each of the 68 snapshot things
that were created at run time (not agent A's), a fresh thing of the same class / type at the same
position is compared; only fields that the creating / driving code changes may differ (enforced
whitelist per class).

| kind | pairs | fields identical | differ |
|---|---|---|---|
| player 3 / 0 | 1 | 50 | identity (next, rng, cell links, owner, tick), flags, speed_cur, mana*, player |
| player 3 / 1 | 3 | 43 | + yaw, pitch, target_yaw, ext_x / ext_y, sprite (per-player), target, unk94, home |
| castle 3 / 2 | 2 | 40 | max_health (1), flags, aux (level), state, extents, sprite, mana*, mana_owner, home.z |
| 3 / 3 | 2 | 51 | yaw, state, sprite, mana_owner, target |
| archer 5 / 4 | 2 | 49 | health, aux, yaw, pitch, target_yaw, cast_ticks, timer_a |
| projectile 9 / 0xd | 4 | 48 | health, flags, yaw, pitch, damage, filter, sprite, target (set by the caster) |
| effect 10 / 0x12 | 1 | 56 | aux, yaw |
| spells 12 / 0..0x17 (all 24 types) | 38 | 52..54 | identity, flags, caster, mana_cost (zeroed on the local player's spells), castle spell mana / mana_total (3 of 4) |

So all 24 rows of the spell parameter table (state, damage, levels, flags, mana per level, total,
and the cost where it survives: types 0xf, 0x13, 0x17) are confirmed by the engine's own data.
Castle #485 is start position 1 (0xab80, 0x3080) snapped to (0xac00, 0x3000) with home.z = the start
z 3919, exactly what `player_create_type2` computes.

**All 70 levels** load and spawn without a crash; cell lists and states are consistent after each
(level 17 fills the pool: 999 things, 0 free). `thing_dispatch_report` after the 70 spawns:

```
unported create  class 10 type  9  0x38b70 effect_create_volcano_38b70   x3
unported create  class 10 type 11  0x38c40 effect_create_crater_38c40    x22
unported create  class 10 type 45  0x39930 effect_create_wizard_39930    x1
```

All three are agent A's; nothing in this scope is missing.

Not covered by engine data (translated from the disassembly only, unit-checked for sanity): the
creature types absent from level 38 (dragon, vulture, bee, worm, crab, kraken, troll, griffon, emu,
genie, type 15, wyvern), projectiles other than type 0xd, effects other than 0x12 / 0x27, the wind,
class 13.

## Deviations from the original

- A failed segment allocation in the dragon / worm / kraken loops makes the original use a null
  "previous segment" (wild parent index, write to address 0x36). The port stores parent 0 and skips
  the write. Not observable: once an allocation fails, every later one in the loop fails too.
- `creature_stagger_awake` and `crab_update_mana_sprite` guard a division by zero that would fault
  in the original (think_period 0, mana_total < 8). No descriptor has think_period 0.
- `mana_ball_update_sprite`: with an owner whose player number is outside -1..7 the original leaves
  ECX uninitialised; the port uses the unowned base 0x34.
- Field stores are grouped between calls; `thing_set_sprite_small_352d0` is `thing_set_sprite`
  (identical result, see thing.cpp).

## Hooks, TODOs, requests

- Hooks declared: **none**. No call in this address range goes to sound, AI, spells or the player
  code; the only out-of-range callee is mana_ball_update_sprite_25e20 (translated privately; make it
  public when the effect update handlers are ported, effect_mana_ball_update_25980 needs it too).
- Shared-file changes requested: none required. Optional: in `mc_types.h` name the first 17 bytes of
  `GameState.padc` (+0xc) `creature_serial[17]` (the constructors use `padc[type]`).
- Integration: after `terrain_generate_features()` is linked, the plain engine sequence should
  reproduce the snapshot slots directly (the test's placeholders stand in for it). Level-start
  class-10 records of agent A's types (volcano, crater, wizard with DisId 0) are spawned by
  switch_activate, so those constructors must be bound for the indices to line up.

## Corrections for ENGINE.md / notes

- Class 3 Table B types 4..11 ("Flyer1..8") are start-position markers, not things (answers open
  question 2.6). Classes 1, 6, 8 and weather types 0..3 allocate and drop a slot.
- Agent2_B "effects typically `flags &= ~0x20008; flags |= 0x200`" is `|= 0x20000` (`or byte
  [t+0x12], 2`): the effect is recyclable. Flag 0x200 is not involved.
- Thing+0x3f starts as the slot index (thing_alloc) and, for creatures, as a per-type serial taken
  from the byte at `GameState + 0xc + type`; +0x3a (awake gate) starts as `think_period - serial %
  think_period + 4` (dragon, bee, worm, archer, crab, skeleton, type 15), `think_period + 1`
  (vulture) or 0x40 (the rest).
- Projectile states: type == state only up to 0xd; 0xe -> 0xf, 0xf -> 0x10, 0x10..0x13 -> 0x11..0x14.
  Effect states for the types of this file: 0x21 -> 0x23, 0x22 -> 0x24, 0x24 -> 0x26, 0x25 -> 0x27,
  0x26 -> 0x28, 0x27 -> 0x29, 0x28 -> 0x2a, 0x29 -> 0x2b, 0x2a -> 0x2c, 0x2b -> 0x2d, 0x2c -> 0x2e,
  0x34 -> 0x38, 0x35..0x38 -> 0x3a..0x3d; type 0x23 returns the raw allocation; 0x14, 0x15, 0x16,
  0x18 return null.
- Dragon: head sprite 0x28, segments 0x13..0x22 (agent_things has 0x13 / 0x14..); its loop writes
  `mana_total / 32` into the head's mana on every iteration (the worm writes the segment's), so the
  dragon head ends with 140 mana, its first segment keeps 2250.
- Tree: the `health = rng % 5000 + 2500` store is dead (thing_restore_health follows); skeleton
  heading is `rng % 0x832 - 1`, not masked to 0x7ff.
- Briefing: the snapshot's 39 class-12 things are 38 player spells + 1 pickup, not 39 pickups; 412
  thing_update_all calls had run at PlayerRec.tick 413.

## Open questions

- Class 10 type 0x2d ("wizard"): the townie handler walks to it and raises its +0x1a while that is
  below its +0x80, which reads like a house / town population counter. Worth a look when agent A's
  notes are merged.
- What a type-0x23 effect is for (the constructor hands back an unset, class-0 thing).
