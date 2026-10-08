# Projectile port (projectiles.cpp) - task A report, port round 3, 2026-10-06

Files: `src/mcengine/projectiles.h` / `projectiles.cpp`, `src/tests/projectiles_test.cpp` +
`projectiles_test.cmake`. `projectiles_test.exe` (build dir `build_A`, Debug, /W4) -> exit 0, 0 failures,
0 warnings, about 0.6 s. No `tables/projectiles.tables` was needed: the range reads no table of its own
(the one `jmp cs:[..]` table, 0x45eac, is code addresses; the sine table 0x987ec / 0x98fec is
`mc_trig_raw`). No file outside the task's list was touched.

Everything was translated from the disassembly of 0x43f30..0x468d3 (the whole range was read; there
is no code in it that is not listed below). The decompiled C was only used as a cross-check
(44fc0).

## Functions translated

| port | original | notes |
|---|---|---|
| `projectile_steer_to_target(t, target)` | projectile_steer_to_target_43f30 | aim at the middle of the target, turn yaw by <= MoveDesc+2, pitch by <= MoveDesc+6 |
| `thing_turn_toward(a, b)` | thing_turn_toward_43ff0 | the same toward b's base position; only caller is 45360 |
| `projectile_record_hit_stats(t, hit, target)` | projectile_record_hit_stats_440a0 | types 0, 1, 3, 7, 8, 9, 0x13 of a flyer: `P.shots++`; `P.hits++` when hit.owner == target.owner |
| `projectile_fly_and_impact(t)` | projectile_fly_and_impact_44150 | shared flight |
| `projectile_type0_s0_update` | 44510 (state 0, fireball) | |
| `projectile_homing_update` | projectile_homing_update_448b0 (state 1) | |
| `projectile_update_shared` | 44a40 (states 2, 4, 5, 6, 11, 15, 16, 17, 20) | calls 44150 |
| `projectile_type3_s3_update` | 44a50 (state 3) | 44150 + trail effect 10 / 1 with flags 0x10080 |
| `projectile_type7_s7_update` | 44a90 (state 7) | calls 44aa0 |
| `projectile_type8_s8_update` | 44aa0 (state 8) | |
| `projectile_step(t)` | projectile_step_44ea0 | |
| `projectile_lightning_update` | projectile_lightning_update_44fc0 (state 9) incl. fragment 451fb | |
| `projectile_type10_s10_update` | 45360 (state 10, castle seed with a target Thing) | |
| `projectile_castle_seed_update` | projectile_castle_seed_update_45530 | called by 45360 when Thing.target == 0 |
| `projectile_type12_s12_update` | 457a0 (state 12) | |
| `projectile_type13_s13_update` | 45b60 (state 13, arrow) | |
| `projectile_type14_s14_update` | 45c70 (state 14, lightning bolt segment) | |
| `projectile_type18_s18_update` | 45c90 (state 18) | |
| `projectile_type19_s19_update` | 45e60 (state 19) | |
| `projectile_pick_target(t)` | projectile_pick_target_45f00, all 20 entries of the jump table 0x45eac | |
| `projectile_target_score` | projectile_target_score_46470 | |
| `target_aim_score` | target_aim_score_465b0 | |
| `projectile_line_of_fire_clear` | projectile_line_of_fire_clear_466af (real entry 0x466b0) incl. fragment 46857 | dead code, see below |

`projectiles_register_handlers()` binds 0x44510, 0x448b0, 0x44a40, 0x44a50, 0x44a90, 0x44aa0, 0x44fc0,
0x45360, 0x457a0, 0x45b60, 0x45c70, 0x45c90, 0x45e60: all 21 class-9 Table A records (checked in the
test: `thing_update_fn(9, s) != nullptr` for s = 0..20, and the dispatch report of every smoke run
lists no class-9 record).

Private helpers (`namespace {}` in projectiles.cpp) hold code that is repeated instruction for
instruction in the original: `fly` (the part 44150 / 44aa0 / 457a0 share up to their impact tail),
`reflect` (rebound, 4 copies), `ground_or_expire`, `first_tick_aim`, `accelerate`,
`turn_to_target_angles`, `aim_score`. The handlers stay one function per original address.

## What the code does (facts established here)

- **Type is not state.** Types 0..13 run state = type; type 14 -> state 15, 15 -> 16, 16 -> 17,
  17 -> 18, 18 -> 19, 19 -> 20 (constructors.cpp); state 14 is the lightning bolt segment (type 9).
  `projectile_pick_target` and `projectile_record_hit_stats` switch on the **type**, Table A on the
  state. The generated names `projectile_type18_s18_update_45c90` / `type19_s19_45e60` are therefore
  wrong about the type: 45c90 runs type 17, 45e60 runs type 18.
- **Thing fields of a projectile**: health = remaining ticks (max_health = range / speed), flag 2 =
  first tick done, target (+0x92) = Thing homed on, impact_cls / impact_type (+0x44 / +0x45) = effect
  left on impact, damage (+0x2c) and mana (+0x8c, 0x32) copied from the spell, home (+0x96) = the
  castle seed's destination. `aux` (+0x1a, the caster's aim charge) is clamped to 0x10 by 45f00 and
  read by nothing else in the range.
- **Shared flight (44150)**: target set -> steer; else on the first tick `pick_target`: found -> yaw /
  pitch snap to the target direction, not found -> target_yaw / target_pitch := yaw / pitch. speed_cur
  += 2 * sign(speed_base - speed_cur). Move, `thing_find_collision`. A hit Thing with flag 0x8000
  (rebound) whose mana >= projectile mana / 4 reflects impacts 10 / 1 and 10 / 0x11: sound 0x1c, its
  mana -= mana / 4, yaw = (yaw + 0x400) & 0x7ff + rng % 0x2d - 0x16 (not masked), pitch mirrored,
  target = old owner, owner = the hit Thing's owner, health = max_health, position = the Thing's
  position + ext_h. Otherwise the projectile is moved to the middle of the hit Thing and explodes.
  No hit: ground above z -> type 4 or a non-water cell explodes, water (terrain mask == 1) leaves a
  splash (10 / 5, owner copied) and no impact effect; else `--health < 0` explodes. Explode =
  `thing_create(pos, impact_cls, impact_type)`, hit statistics, effect.owner / yaw / pitch / target /
  damage from the projectile (impact type 0x22: effect.health = damage), mark for deletion. **When
  thing_create fails the projectile is not deleted** and tries again every tick.
- **44510 (fireball)** differs: no acceleration; the first-tick aim turns the yaw by at most 0x22
  toward the target (pitch snaps); below ground it first moves back to the position before the step;
  rebound does not test the impact type, spreads by rng % 0x5b - 0x2d, and a rebounding target too
  weak to reflect lets the fireball **pass through** (no explosion, lifetime not decremented); the
  effect gets owner / yaw / pitch only (no target, no damage).
- **448b0 (type 1) / 45c90 (type 17)**: no acceleration, no "copy back" when the aim finds nothing,
  the step is lifted onto the terrain; end on `thing_find_mana_near` (type 1: effects 0x27 / 0x28 / 0x2d
  not owned by the caster) resp. `thing_find_mana_ball_touching` (type 17: any mana ball), ground or
  lifetime. 45c90 creates effect 10 / 0xc and then the impact effect (statistics called twice).
- **44aa0 (types 7, 8)**: the flight of 44150; the impact effect is created only when a class 3 type 0
  / 1 Thing was hit, otherwise just statistics (as a miss) and deletion.
- **457a0 (type 12)**: the flight of 44150; leaves effect 10 / 0x26 which receives the projectile's
  impact_cls / impact_type (spell 18 sets 9 / 9: the effect spawns lightning).
- **Lightning (44fc0)**: speed_cur := speed_base, unlink from the cell, `projectile_step` until the
  Thing is marked for deletion (hit: position := the hit Thing's base position; ground; lifetime),
  yaw / pitch restored to their values after the first step. Then count = steps * 8 and for count ..
  0 a segment Thing (class 9 type 9 state 14, sprite 0xd8, owner, max_health = 0 when its slot is
  behind the bolt's else -1) is linked every speed / 8 along the ray, displaced sideways (yaw + 0x200)
  and vertically by the *same* amount 12 * off_b, off_b a +-1 walk kept within min(8, count / 2). A
  second walk ([esp+0x20]) is computed with its own RNG draws and never used. Finally
  `thing_find_collision` at the end position and the impact effect at the position one segment step
  past the last segment; its damage is quartered when the Thing hit is class 3 with flag 0x8000 and
  mana >= projectile mana / 4 (rebound, not shield as agent_things.md says). The bolt is not marked
  again (44ea0 did it). 45c70: `if (health-- < 0) delete` - every segment is gone on the next tick.
- **Castle seed**: 45360 with a target Thing (spell 16 sets the caster's castle and impact 10 / 0x2b):
  `thing_turn_toward`, accelerate, move; arrived when its box touches the target's (moved onto its base
  position), on ground contact or lifetime. An impact of class 3 is dropped while the owner has a
  castle (P+0x32 != 0). When thing_create fails `castle_spell_reset_charge(owner, 0)` (the seed stays).
  Without a target (impact 3 / 2, home = caster + 0x1000 on the ground) 45530: the first tick only tests
  `castle_site_clear_at_pos` at the launch position (blocked -> release the Castle spell, delete);
  afterwards it turns toward `home` by the descriptor limits, accelerates, moves, and ends on ground
  contact, lifetime, or as soon as the site under it is not clear - then it steps back once (yaw +
  0x400) and builds there. The impact Thing gets the owner. (The collision test of 45530 is against
  Thing 0, the scratch slot, and raises that Thing's z by its ext_z0 without taking it back.)
- **Arrow (45b60)**: first tick one RNG draw, sound 0x21 + (rng & 3). The collision search runs at the
  position *before* the move. Ground above the next position, `health-- == 0` or a hit end it: moved
  to the middle of the hit Thing if any, `thing_area_damage(t, 0, damage)`, delete; no effect. With
  the castle-only filter the skeletons give their arrows (3 / 2) the castle is found either by that
  search (when the arrow is in the castle's own cell ring) or by the area damage's player list.
- **45e60 (type 18)**: becomes its impact effect (owner, yaw, pitch, damage) on the first update.
- **Target selection (45f00)** by type, best = smallest score, strict `<` in list order (players, then
  the 20 creature lists): see the table in `projectiles.h`. Players / castles / balloons are taken
  from `cfg->player_list` (other owner, flag 0x20 clear, within the **owner's** `MoveDesc.sight_radius`
  of the projectile; lightning: within max_health * speed_base), creatures need `timer_a != 0`
  (awake). Castles (type 2) are scored with 465b0 (base position) in the type 0 / 9 groups; the
  type 7 / 8 / 0xb / 0xc group has the same branch but calls 46470 on both sides. Choosing a human
  flyer calls `player_set_combat_music_timer` (not for lightning, not for the mana seekers).
- **Scores (46470 / 465b0)**: -1 when |yaw difference| > max_yaw, |pitch difference| > max_pitch
  (both compared unsigned 16-bit, "above" = reject) or the xy distance > 0x1400; else with d = xy
  distance `(d cos dy >> 16)^2 + (4 d sin dy >> 16)^2 + (d cos dp >> 16)^2 + (4 d sin dp >> 16)^2`.
  46470 brackets the computation with z += ext_z0 / z -= ext_z0 on the target (every exit).
- **No code outside 0x43f30..0x468e0 calls anything in the range except through Table A** (raw scan
  of the image for E8 / E9 rel32 and for dword references to 43f30, 43ff0, 440a0, 44ea0, 45530,
  45f00, 46470, 465b0, 466b0). In particular creature attacks and the AI do **not** call the target
  selection, and `projectile_line_of_fire_clear_466b0` is dead code. It also contains a bug: the probe
  copy of the shooter is tested with `thing_collide(probe, shooter)`, i.e. against the shooter itself,
  so it returns 1 exactly when the shooter is wider than half a 0x180 step.

## Verification

`projectiles_test.exe "C:\Magic Carpet\MagicCarpet\magic"`; numbers from the final run.

1. **Real in-flight objects (snapshot, strongest evidence).** The 4 projectiles of `gam00000.dat` are
   arrows (type 13, sprite 0xcb, damage 400, filter 3 / 2, target = castle #895) fired by skeletons
   #292, #294, #295, #296 (state 56). For each: every constructor field (state, max_health 13,
   speeds, mana, desc, impact, prop_flags) equals a freshly constructed type 13; sprite / extents equal
   `thing_set_sprite_double(0xcb)`; damage / filter / target are what skeleton_attack_fire_19550 writes;
   flags == constructor | 2, target angles untouched, **health == max_health - (tick - (index &
   0xff))** (1, 3, 4, 5 updates). And the flight itself: from the owner's present position (+ ext_h),
   with yaw / pitch = `pos_angle_to` / `pos_pitch_to` owner -> castle, `updates` steps of
   `math_rotate_offset(yaw, pitch, 0x180)` give the arrow's snapshot position **bit-exactly for 4 of
   4** (e.g. #348 after 5 steps: face fd33 5742). Stepped on with `thing_update_all`: every arrow
   advances by exactly one step and one health per tick, they end at ticks 2, 4, 6, 7 (castle found by
   the collision search), and castle #895's pending damage slot 0 goes 0 -> 1600 = 4 x 400, attacker
   recorded; no class-9 Thing is left, pool / free stack / cell lists consistent.
2. **Automatic aim against brute force.** 4 sweeps x 640 launches (4 wizards x 32 headings x types
   0, 7, 9, 3, 12) compared with an independent search over the whole pool (list membership rules of
   thing_update_all + the per-type rules): 0 mismatches in 2560 launches, 333 of them with a target
   (snapshot as it is: 4, only 2 of 169 creatures are awake; every creature awake: 4; wizards moved
   to the castles: 114 = 60 creatures + 54 castles; wizards in a ring: 211 = 31 creatures + 180
   players, 36 combat-music locks on the human flyer). target_yaw / target_pitch == `thing_aim_at`,
   aux clamp, no aim for types 2, 5, 6, 10, 13, 14, 15, asleep creature never chosen.
3. **By construction, values worked out from the disassembly** (inside the snapshot world):
   - scores: dead ahead 2 d^2 (0x200000 at d = 0x400), limit 0x1400 inclusive, a quarter turn =
     17 d^2 inside a 0x200 cone and rejected at 0x1ff, off-axis value from the sine table, z bracket
     restored on all four exits, castle (type 2) not raised;
   - steering: +0x16 per call with descriptor 1 (0x96a30), both directions, across the 0 / 0x7ff
     wrap, pitch reached at once when nearer than the limit; descriptor limits read back (decimal,
     yaw / pitch per tick): type 0 -> descriptor 5 (5 / 22), type 8 -> 4 (11 / 22), type 1 -> 2
     (113 / 113), sight radius 4096 each;
   - hit statistics for all 20 types x 5 situations (miss, hit the target, hit the target owner's
     castle, hit without a target, hit somebody else's; AI owner counts nothing);
   - straight flight of types 2, 4, 5, 6, 11, 14, 15, 16, 19, 3, 0, 8, 12 in empty sky: one exact
     step per tick, health counts down, ends after max_health + 1 updates at x = start + (life + 1) *
     speed, one impact effect with owner / yaw / pitch / damage / target (type 0: without the last
     two; type 8: none; type 12: effect 0x26 carrying the impact pair; type 3: life + 1 trail
     effects with flags 0x10080), teleport lifetime = damage;
   - acceleration cases (0x100 -> 0x102 -> 0x104, 0x200 -> 0x1fe, the 0x17f / 0x181 oscillation) and
     none for types 0, 1, 17, 13;
   - ground / water for types 2, 4, 0, 12, 8, 1 (12 cases): effect type, splash on water except type 4,
     44510 stepping back, the ground hugger lifted onto the terrain;
   - collision with a wizard for types 2, 3, 8, 7, 12, 0 (stops at z + ext_z0, effect.target = the
     wizard, the wizard's z restored), shots / hits, 44aa0 on a creature (no effect);
   - rebound, 8 cases: reflection values (yaw from the projectile's own RNG, pitch mirror, owner /
     target swap, health reset, position, mana - 12, one sound request (thing, -1, 0x1c)), homing
     on the former owner on the next tick, "wrong impact type" and "target too weak" exploding,
     44510 reflecting any impact type and passing through a weak target;
   - lightning: 10 steps, 81 segments + 1 effect allocated, each segment matched to its place on the
     ray with a wander that is a multiple of 12, changes by exactly 12 per segment, starts at 0 and
     is squeezed back at the far end (widest seen: 108 = 9 x 12), effect one step past the last
     segment, segment lifetime on both sides of the bolt's slot; a wizard in the ray: stop at its
     base position after 2 free steps, 25 segments, damage full / quartered (rebound) / full (rebound
     too weak), shot and hit counted;
   - castle seed: first-tick site test, flight to `home` (turn <= 0x16 per tick, lands 78 units from
     the destination after 11 ticks, castle Thing class 3 type 2 with the owner on the constructor's
     cell corner), launch on a taken site (Castle spell cast_ticks -> 0, deleted), flying into a taken
     site (one step back, a clear site again, 512 units short of the enemy castle), homing on a
     castle Thing (effect 10 / 0x2b at its base position after 20 ticks), "owner already has a castle";
   - arrow: 13 moves + the ending update, one RNG draw and the sound id, hit on the tick after it
     moved onto a creature, damage slot 0 = 400 with the attacker;
   - mana seekers type 1 / 17 with a foreign / own mana ball (aim rules, ground hugging, end on the
     ball's middle, effects 0xc resp. 0xc + 0x36); one case was stopped early by a real mana ball of
     the level (#386) lying in the way, which the test identifies; type 18 -> effect at once;
   - `projectile_line_of_fire_clear` as translated (range, both cones, self-hit, terrain).
4. **Smoke runs** (`game_tick_sim`, projectiles of 16 types launched from every live wizard the way
   spell_fireball_update_47130 sets them up; effects whose handlers are not linked into this test
   are removed by the test after 12 ticks so the pool does not fill):

   | run | ticks | launched | ended | lightning segments | max projectiles | max Things | failed allocs |
   |---|---|---|---|---|---|---|---|
   | movie from the snapshot | 2500 | 3336 | 3314 | 16080 | 26 | 601 | 0 |
   | generated level 38 | 2000 | 1600 | 1592 | 5980 | 14 | 614 | 0 |
   | generated level 0 | 600 | 120 | 120 | 223 | 4 | 671 | 0 |
   | generated level 12 | 600 | 360 | 350 | 1582 | 14 | 235 | 0 |

   The difference launched - ended is what was still in flight at the end (<= 3 per type). Mean
   lifetimes (movie): about 18..19 ticks for the 0x2000-range kinds (max 22), 31.3 for type 14 (max 33),
   13.2 arrows (max 14), 10.9 ground huggers (max 11), 5.9 type 12 (max 6), 1.0 lightning and type 18.
   No projectile below z = -0x400, pool / free stack / cell lists consistent every 500 ticks,
   `thing_dispatch_report` lists no class-9 record. Impact effects created in the movie run by type:
   0:382 1:2572 5:61 9:199 b:200 c:416 f:193 11:195 12:203 17:208 19:1 24:203 26:207 35:203 36:208
   37:208.

**Verified against the original's own data:** state 13 (arrows) only - position, health, flags of 4
real flights, to the bit. **Verified by construction / cross-check only:** everything else (flight
kinds, rebound, lightning, castle seed, target selection, scores, statistics). The expected values in
those tests come from my own reading of the disassembly, so they catch slips in the C++ but not a
misreading shared by both; there is no reference from the original for them. **Not exercised at all:**
the interplay with the real impact effects, spells, creatures and the AI (none of them is linked into
this test: spells never fire, effects never deal their damage, nothing dies), and
`ai_record_threat_from_projectiles_150f0`, which walks `cfg->projectile_list` (AI, not ported).

Known gaps / things the integrator should expect:

- A full Thing pool keeps projectiles alive: when `thing_create` fails for the impact effect the
  projectile is not deleted and flies on under the ground, retrying every tick. This is the
  original's behaviour (seen in an early smoke run while the test did not yet remove its leftover
  effects: pool at 999, projectiles far below the terrain).
- No creator was found for types 6, 15 and 19 (scan for `push type; push 9; call thing_create`); their
  handlers are bound anyway.
- The reflected yaw and the fireball's first-tick yaw are stored unmasked (can be negative or above
  0x7ff as a u16), as in the original; `math_rotate_offset` masks, direct readers must too.

## Deviations from the original

1. **Null-hit index.** 44150, 44fc0 and 457a0 store `(hit - things) / 0xa4` into the impact effect's
   `target` even when `hit == NULL`: the original writes the low 16 bits of `-(&things[0]) / 0xa4`, an
   address-dependent value (for the run that made the snapshot: pool at 0x45aa3 -> 0xf935; taken as a
   signed index that points at address 0x97, below the pool, so "pointer > &things[0]" tests see no
   Thing). The port writes `g_projectile_null_hit_index`, default 0 = no Thing. Consumers of
   effect.target (effects task) should treat any index outside 1..999 as "none".
2. `&things[index]` with an index outside the pool (cannot come from this code with the default
   above) reads as Thing 0 instead of foreign memory.
3. 440a0 tests the owner's P pointer for NULL before `hits++`; the pointer is never NULL (thing_alloc
   points it at the dummy block), the port increments unconditionally.
4. Repeated code is factored into private helpers (see above); behaviour is unchanged.

## Hooks declared

None. Everything the range calls is in the sim core (`thing.h`, `spatial.h`,
`level_features.h::castle_site_clear_at_pos`, `player.h::player_set_combat_music_timer` /
`player_block`) or reached through `thing_create`. Sound requests go through `sound_request()`.

For the hooks task E declares in creatures.h "for target selection / line of fire": by the scan above
no creature or AI function calls 45f00, 46470, 465b0 or 466b0, so those hooks stand for nothing in
this range; if a creature attack needs a projectile it calls `thing_create(pos, 9, type)` and fills the
fields itself (193f0.. / 1ee5a), and the projectile's own first tick does the aiming.

## Extra functions outside the range

None. (The test re-creates the launch sequence of spell_fireball_update_47130 for its smoke runs;
that is test scaffolding, not a port of the spell.)

## TODO(port) call sites

None.

## Requested shared-file changes

None required. Suggestions:

- `mc_types.h` `MoveDesc`: `turn_min` (+2) is the **yaw** turn limit per tick and `turn_max` (+6) the
  **pitch** turn limit (43f30, 43ff0, 45530); `unk4` (+4) and `unk8` (+8) are the values pushed as
  the unused third argument of angle_turn_step. Names like `turn_yaw` / `turn_pitch` would be clearer.
- `gen/dispatch_tables.h` names (from gennames.py): `projectile_type18_s18_update_45c90` is the
  type 17 handler, `projectile_type19_s19_update_45e60` the type 18 handler, `..._s15 / s16 / s17 /
  s20` shared records belong to types 14, 15, 16, 19. The port functions keep the generated names.
- Integration: `engine_init` already calls `projectiles_register_handlers()`; `sim_init` does not (by
  design), tests call it themselves.
- All five agents were given the same scratchpad directory; `dis.txt` there was overwritten by
  another agent while I was reading it. A per-task subdirectory avoids that.

## Corrections to docs/ENGINE.md, agent files, carpet_types.txt

- ENGINE.md 2.3 / agent_things.md "9 Projectile ... type == state (0..20)": wrong for types 14..19
  (state = type + 1), and state 14 belongs to type 9.
- agent_things.md `projectile_lightning_update_44fc0`: "step until cell change" -> steps until the
  Thing is marked for deletion (byte +0x11 bit 4 is flag 0x400, not the cell-changed result);
  "quartered vs shielded player" -> vs a rebound-flagged (0x8000) class 3 Thing with mana >=
  projectile mana / 4.
- agent2_C.md `projectile_castle_seed_update_45530`: it does not need a free site at the end: it
  creates the impact Thing on ground contact / lifetime regardless, and when the site under it is taken
  it steps back one step and creates there. 45360 is the state handler; 45530 only runs without a
  target Thing.
- agent2_C.md `projectile_line_of_fire_clear_466af`: "hits a thing -> 1" is a collision test against
  the shooter itself (bug); confirmed dead (no call, jmp or pointer reference).
- agent2_C.md `projectile_target_score_46470`: the sine terms are (4 d sin)^2, i.e. weight 16, not 4;
  the distance limit applies to the xy distance.
- agent2_C.md `projectile_fly_and_impact_44150`: the rebound condition is target mana >= projectile
  mana / 4 (signed division); 44510 has its own copy with different rules (see above).
- ENGINE.md "projectile +0x1a = aim byte": only clamped (<= 0x10) by 45f00, never read by class-9
  code.
- Region C states "projectile_pick_target_45f00 selects a class 3 type 0 (human) player ->
  player_set_under_attack_timer": true for the type groups {0, 3, 4, 0x10, 0x12, 0x13} and {7, 8, 0xb,
  0xc} only.
- Snapshot fact for the creatures task: the four arrows match `skeleton_attack_fire_19550` exactly
  (damage 0x190 because the skeleton's +0x90 is 0, sprite 0xcb doubled, filter and target copied
  from the skeleton, yaw / pitch from the skeleton's base position to the target's base position,
  launch z = skeleton z + ext_h).
