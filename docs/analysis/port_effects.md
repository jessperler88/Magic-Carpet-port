# Class-10 effects - port report (round 3, task C, 2026-10-06)

Files: `src/mcengine/effects.h/.cpp`, `src/mcengine/tables/effects.tables` -> `gen/effects_tables.h`,
`src/tests/effects_test.cpp` + `.cmake`. No shared file was touched. Build:
`cmake --build ../build_C --config Debug --target effects_test`, zero warnings (/W4);
`effects_test.exe <game dir>` exits 0 (about 60 lines of statistics, quoted below).

## Result in one paragraph

Every Table A record of class 10 now has a port (58 of 58 records bound; 37 registrations here, the
rest in level_features.cpp). Three pieces are checked against data of the original engine: (1) the
**mana ball** handler, merge and sprite: level 38 run from its start for 412 ticks with only the sim
core + effects reproduces the snapshot's balls field by field (38 of 39 comparable balls identical in
position / velocity, 36 in every compared field, the 3 others explained; 7 merged balls with the same
mana; awake timers 39 / 39); (2) the **erupting crater** (effect 0x12, 24810): after 394 ticks of its
life its RNG seed, yaw, aux, tick and position equal the snapshot's thing 503 exactly, and
GameState+0x24 holds the same index; (3) the **castle raise** (26f10): the grown castle has exactly
the heights of `castle_stamp_footprint` (26320, translated independently in round 2) for sizes 1..3
(64 / 441 / 441 cells). Everything else is translated from the disassembly and tested by
construction only (state transitions, timers, RNG draws replayed with the LCG, damage slots, spawned
things); see the table under "Verification" for which is which.

## Functions translated (all from the disassembly, `img.py dis`)

Update handlers, bound by original address in `effects_register_handlers()` (state = Table A index):

| state | original | port (file-local in effects.cpp) | what it is |
|---|---|---|---|
| 0 | effect_explosion_update_238b0 | `effect_explosion_update` | waits while aux & 3; live tick: area damage, scorch / dent, random z speed; then rises and animates |
| 1 | effect_big_explosion_s1_update_23a80 | `effect_big_explosion_update` | explosions on about half the spiral cells of ring aux, 0xc0 apart |
| 2, 3 | 23c00, 23d40 | `effect_lifetime_update` | lifetime only |
| 4, 0x14..0x16, 0x18, 0x3d | 242d0, 24c10, 24ca0, 284c0 | `effect_nop_update` | a lone `ret` |
| 5 | effect_splash_s5_update_23d60 | `effect_splash_update` | animation + sound 0x1b |
| 6 | effect_fire_update_23c20 | `effect_fire_update` | grow 7 ticks, shrink the last 12 with smoke puffs (1/7), dies on water, `thing_area_damage_fire` |
| 0xc | 240b0 | `effect_type12_update` | animated, damage slot 1 every tick |
| 0xd, 0xe | effect_white_smoke_update_24100, effect_black_smoke_update_241f0 | `smoke_update(t, white)` | rise, drift 15 ticks, sprite forth and back |
| 0xf | effect_earthquake_s15_update_242e0 | `effect_earthquake_update` | wandering crack, one 10-tick crater per step |
| 0x10 | 243b0 | `effect_lava_blob_update` | ballistic blob, bounces, lights triple-damage fires / splashes, rolls downhill |
| 0x11 | effect_meteor_s17_update_24630 | `effect_meteor_update` | shock wave: explosions on ring aux (0, 2, .. mod 11), damage / max_health per tick |
| 0x12 | 24810 | `effect_eruption_update` | the crater a volcano leaves: smoke column, one meteor shot, lava blobs for 127 ticks, dormant / restart |
| 0x13 | 24a90 | `effect_volcano_smoke_update` | 4 white clouds per chosen centre cell on odd health, damage every tick |
| 0x17 | 24c20 | `effect_lightning_update` | one burst of damage (slot 0) + sound 0x18, one more tick |
| 0x19 | 24cb0 | `effect_steal_mana_update` | animated, damage slot 3 once |
| 0x1a | 24d10 | `effect_type26_update` | animated, damage slot 4 every tick |
| 0x23 | 25630 | `effect_type33_update` | ages, sets flag 2 |
| 0x24 | effect_teleport_s36_update_253b0 | `effect_teleport_update` | moves every player thing that touches it and looks at it to `home` |
| 0x25 | 25550 | `effect_orbiter_update` | circles its caster (+0x2a) |
| 0x26 | 257e0 | `effect_skeleton_army_update` | up to 8 skeletons (creature 9) in a circle, 64 per owner |
| 0x28 | 25670 | `effect_storm_update` | climbs to ground + 0x400, then two shots of its impact class / type per tick |
| 0x29 | effect_mana_ball_update_25980 | `effect_mana_ball_update` | claim (slot 1), pull (slot 4), collection by a balloon (flag 0x40), flight / bounce / merge / roll, sprite |
| 0x2a | 25f10 | `effect_mana_hoard_update` | hoard marker: a player's claim passes on everything it owns |
| 0x2b | 26b50 | `effect_castle_level_ground_update` | levels the footprint rectangle in 10 steps, waits 10, hands the castle back |
| 0x2c | effect_castle_raise_terrain_s44_26f10 | `effect_castle_raise_terrain_update` | grows the castle of size N in 18 steps, waits 1 / 25, hands the castle back |
| 0x2d | 27d20 | `effect_castle_seed_update` | files the upgrade request (damage slot 5) with the owner's castle or resets the Castle spell |
| 0x2e | 27e90 (record disabled) | `effect_boulder_update` | rolls downhill, damages |
| 0x38, 0x39 | 27ff0, 28050 | `effect_egg_update`, `effect_hatch_update` | crab egg: 600 ticks, then a crab + big explosion |
| 0x3a | 280d0 | `effect_fire_pillar_update` | 15 ticks of fires on rings 0..1, each tick 0x80 higher |
| 0x3b | 28270 | `effect_mana_magnet_update` | pull request (slot 4) on every ball within 0xe00 |
| 0x3c | 28320 | `effect_blast_update` | fuse of aux ticks, then kills scenery / creatures and damages players within 0xa00 |

Exported (effects.h):

| port | original |
|---|---|
| `thing_drop_mana_ball` (installed as `g_hook_thing_drop_mana_ball`) | thing_drop_mana_ball_25fe0 |
| `mana_ball_merge` | mana_ball_merge_26120 |
| `terrain_max_drop_around` | terrain_max_drop_around_2376f (entry 0x23780; no caller in the range) |
| `effect_age_tick` | effect_age_tick_25600 (no caller in retail) |
| `terrain_ring_find_height_ne8(cell, start_radius)` | terrain_ring_find_height_ne8_24d70 (dead; jump table 0x24d54) |
| static `mana_ball_update_sprite` | mana_ball_update_sprite_25e20 (second copy, see "Requests") |

Jump tables read: 0x24d54 (4, ring sides), 0x25df8 (9, ball sprite base by player + 1), 0x282f8 (9, blast
by class - 2). Data: `g_fx_mana_ball_thresholds` i32[7] at 0x93910.

## Verification

### Against the original engine (snapshot movie/gam00000.dat, level 38)

The snapshot was taken inside tick 413, i.e. after **412** complete `thing_update_all` (every ball's
`Thing.tick` is index + 412 mod 256). `test_level_start_vs_snapshot` loads level 38, runs
`game_tick_sim()` 412 times (no input) with the sim core + effects and compares thing by thing.

* Mana balls: 80 at level start. 39 start slots hold a ball in both runs:
  position equal **38**, velocity (home.x / home.y / z_vel) equal **38**, every compared field (x, y, z,
  velocity, mana, sprite, flags, mana owner, tick, yaw, state, extents) equal **36**. The three others:
  slot 356 was freed and reused by a later ball in the original run; 385 and 386 have identical position
  and velocity but were claimed by AI wizard 492 there (owner and sprite differ; no AI in the port).
  **7** balls carry merged mana (1024 .. 3584) that is equal in both runs, so the merge partner choice
  and the order of `thing_free` agree. 21 start balls exist only in the port (collected by the AI
  wizards / castles in the original), **0** unowned start balls exist only in the snapshot (the port
  never merged a ball the original kept).
* The awake gate: with a test-local stand-in for the mana-ball half of `creature_wake_tick_468e0` /
  `creature_proximity_wake_timer_46960` (task E) `timer_a` is equal on **39 / 39**. The test repeats
  the run without the gate (balls updated on every tick): positions equal 34, velocities 1 of 39 (the
  resting hop of z_vel gets out of phase, four balls roll further), so the handler's
  `timer_a == 0 -> return` and the 128 + 17-tick wake rhythm are part of what is confirmed.
  `cast_ticks` (the wake distance) differs because the recorded player flew around during those ticks.
* Erupting crater: the two level volcanoes (effect 9) end at tick 18; in the port the first crater is
  deleted in its first update (its ground no longer has the height it was created at: the second
  volcano, two cells away, ends in the same tick), which fits the single crater of the snapshot. The
  second is thing 503 in the snapshot and 501 in the port (the original had two more things alive). With two placeholder
  allocations the indices line up and **thing 503 is identical: aux 394, rng 0xce9c2f5f, yaw 0x7800,
  tick 129, health 10000, position, flags, owner**; GameState+0x24 = 503 in both. Equal RNG seeds after
  394 ticks mean the same number of draws in the same ticks: the roll on every tick with aux & 0xf != 0
  below 0x80, the blob seeds, the failed roll at aux 0x7f that left it dormant.
* Snapshot invariants (54 balls): sprite = base(owner's player) + size(mana) for 54 / 54; speed_cur in
  0x10..0x3f for all (14 carry a thrown ball's yaw / speed, i.e. came out of `thing_drop_mana_ball`);
  51 on the ground, 3 in flight; the 34 that rest on level ground are fixed points of the handler
  (z_vel alternates 0 / -0x10, as in the snapshot). 600 x `thing_update_all` on the snapshot: mana total
  3,142,756 conserved, cell lists consistent, 449 live + 550 free = 999.
* What the snapshot cannot show: the port's run ends with 18 (17 without the placeholders) of 19 wizard
  castles, the snapshot has 19. In the port a lava-blob fire burns them down (150 per tick from the
  tripled fire damage); the blobs take their constructor seed from their pool index, the original run
  had other things allocated, so its blobs flew elsewhere. Not a contradiction, but not a
  confirmation of the blob / fire chain either.

### Cross-check between two translations

`effect_castle_raise_terrain_update` (26f10) on a free site against `castle_stamp_footprint` (26320,
level_features.cpp) applied for sizes 1..N as `player_spawn` does: heights equal on 64 / 64 (size 1),
441 / 441 (size 2), 441 / 441 (size 3) cells; textures equal on 64 / 64, 434 / 441, 417 / 441 (the raise
paints before the last height step of the same tick, so slope-dependent textures can differ; that
order is the original's). The two functions read the terrace bytes (high nibble 3) with different
formulas; on the shipped building.dat they give the same heights.

### By construction (hand-derived expectations from the disassembly, on level 38 terrain)

| handler | checked |
|---|---|
| explosion | 2 waiting ticks; both RNG draws (dent rnd % 7 on 3 cells = ring 0, z_vel rnd % 0x41 - 0x20); no dent on castle textures; area damage once; life 8 = 10 calls; sound 3 once |
| big explosion | per-cell coin (rnd % 0x9d / 0x4f) and the two position draws replayed for 3 ticks (15 cells in ring 2); owner / flags of the spawned explosions |
| fire | sprite +7 / -7, no draw with flag 0x80, one draw per shrinking tick and a smoke on rnd % 7 == 0 (aux 100, life 15, sprite + 2), z = ground + z_vel, dies on water, damage on the dying tick |
| smoke | speed decay / clamp, z floor, 15 drift steps (450 units), sprite sequence, life + 2 calls (both colours) |
| types 0xc, 0x17, 0x19, 0x1a | damage slot 1 / 0 / 3 / 4, once or every tick, amounts 64000 / 25 / 2000 / 200, sound 0x18 |
| earthquake | yaw draw, 0x100 step, crater with copied extents and life 10, aux up on nibble-0 cells (gone above 8) and down elsewhere |
| lava blob | velocity / z_vel clamps, never below ground, fires with life 0x1e and damage 0x96, splash + delete over water |
| meteor | extents 0xc0 * aux, aux sequence mod 11, 1 + 2 * cells draws per tick, 381 explosions in 10 ticks, 300 damage per tick |
| eruption | first tick (GameState+0x24 / +0x26, smoke, blob with the parent's seed, projectile 9/0 with impact 10/0x11, pitch 0xfe7e, target 0x600 away), take-over by a second crater, every roll of 127 ticks, both endings at 0x7f, dormancy, restart above 2500, death when the ground moved |
| volcano smoke | draws, 4 clouds per chosen cell with yaws k * 0x200 (+ 0x100), damage on the dying tick |
| teleport | needs contact and a view within 0xaa; destination height = ground + MoveDesc.clear_hi; sounds 0x15 / 0x16 / 0x14; palette effect 6; lifetime |
| orbiter, storm, skeleton army | radius / yaw step / height; 16 climbing ticks, 2 shots per tick with opposite yaws, life / 3, impact 10/0x17; 8 skeletons facing outwards, 3 when 61 are owned, mana = 10000 % share |
| mana ball | `thing_drop_mana_ball` (all three RNG uses, z_vel, home vector, owner hand-over, dropper's mana untouched), flight to rest, asleep = untouched, claim, pull + friction, collection by a balloon, 10 `mana_ball_merge` ownership cases, hoard hand-over, merge through the handler, the 8 sprite sizes, magnet range |
| castle levelling (26b50) | 10 equal steps to the surrounding average, bit-3 parking of built-on cells, 10 ticks wait, castle.cast_ticks = 2 and home.z, 22 calls; already-level case |
| castle raise (26f10) | 18 steps, pause while the castle's hit timer runs, every target height reached (64 / 441 / 441 cells), parking, wait of 25, 44 calls |
| castle seed, boulder, crab egg, fire pillar, blast | slot 5 request / spell reset; clamp + move + damage; 601 ticks then crab + big explosion + creature_count; 15 ticks x 15 fires with z_vel aux * 0x80; fuse 32 ticks, kill / damage / spare by class, owner and distance |

### Smoke runs (`game_tick_sim`, no input, dispatch report)

| level | ticks | effects start -> end (peak) | effect types seen (ticks present) |
|---|---|---|---|
| 38 | 2500 | 101 -> 78 (302) | 5 splash 130, 6 fire 238, 9 volcano 19, 0xd smoke 280, 0x10 blob 239, 0x12 crater 2482, 0x13 column 242, 0x27, 0x2d |
| 0 | 2000 | 21 -> 12 (21) | 0x27, 0x2d |
| 5 | 2000 | 27 -> 26 (27) | 0x22 teleport 2000, 0x27, 0x2d |
| 17 | 2000 | 54 -> 25 (54) | 0 explosion 11, 1 big explosion 3, 0x27, 0x34 crab egg 602 |
| 30 | 2000 | 654 -> 654 (654) | 0x27 |

No class-10 handler is reported missing; the pool stays consistent (live + free = 999). The report of
level 38 lists only other tasks' handlers (trees, AI wizard, castle state 5, creatures, projectile 0,
switches, spells).

### Only translated, not exercised by any original data

Everything in the "by construction" table: the expectations were derived by the same reader from the
same disassembly, so they catch slips, not misreadings. In particular the claim / pull / collection
branches of the mana ball, the teleport, 26b50, the storm, the blast and the damage-slot effects have no
independent confirmation.

## Deviations and known gaps

1. `terrain_paint_cell` calls of the explosion (kinds 0x14..0x16) pass the default DL / CL (0xff); the
   original has leftovers of the previous calls there. Only read for a quad that is flat at height 0.
   The raise effect passes the DL / CL it really has (last idiv remainder / the instruction byte).
2. 26f10 keeps its i16 difference table in a local vector instead of the renderer work buffer
   (DAT_000adf68), bounds the index (a sub-footprint that does not fit the top one writes outside the
   buffer in the original) and stops at the end of building.dat.
3. 26f10 centres the sub-footprints with the two table bytes swapped (rows from +4, x offset from +5),
   as the original does; all shipped footprints are square (8, 21, 21, 35, 35, 48, 48), so it cannot
   be observed.
4. Thing indices read from fields (`caster`, `mana_owner`, damage-slot attackers, GameState+0x24 / +0x26)
   are reduced modulo 1000; list walks are bounded; the teleport loop stops at 8 players.
5. `idiv` guards: meteor / fire pillar with max_health 0 deal 0 (the original faults); skeleton share 0.
6. Storm cloud: when the second create fails the original passes a wild thing index to the sound
   request; the port passes 0.
7. `terrain_ring_find_height_ne8` takes the start radius as a parameter (uninitialised stack byte in
   the original; dead code).
8. `mana_ball_update_sprite`: player number above 7 leaves ECX undefined in the original; 0x34 here (as
   in constructors.cpp).
9. `weather_update_nop_4b570` (a lone ret, called by the smoke handlers) is a comment.
10. Not reproduced: `effect_create_type35` hands back a raw allocation (class 0), so state 0x25 (orbiter)
    is never reached through Table B; the handler is ported and tested on a hand-built thing.

## Hooks

* Installed here: `g_hook_thing_drop_mana_ball = thing_drop_mana_ball` (declared in spatial.h).
* Declared here: none. Calls into other subsystems go through thing.h / spatial.h / level_features.h /
  player.h (`player_note_fire_distance`, `player_set_palette_effect`, `castle_spell_reset_charge`,
  `cell_kill_things`, `terrain_smooth_castle_border`, `terrain_height_avg`, `castle_footprint`).
* Needs connecting by others for full behaviour: `g_hook_creature_wake_tick` (task E) drives the mana
  balls' awake gate `timer_a`; without it a ball stays awake (0x80 from its constructor) for ever. The
  projectile the crater fires (class 9 type 0, impact 10/0x11) needs task A's 44510 to fly and produce
  the meteor impact.

## Extra functions outside the task list

* `mana_ball_update_sprite_25e20`: in the range, already ported file-locally in constructors.cpp; a
  second file-local copy lives in effects.cpp (with its own table `g_fx_mana_ball_thresholds`).
* Test only (`effects_test.cpp`): `test_wake_tick`, the mana-ball half of 468e0 / 46960, to reproduce
  the snapshot. It is not part of the engine.

## TODO(port) call sites

None. Sounds go through `sound_request` (ids 3, 4, 0x14..0x18, 0x1b, 0x1e, 0x2b, 0x2c).

## Requested shared-file changes

1. **constructors.h / constructors.cpp**: export `mana_ball_update_sprite` (move it out of the anonymous
   namespace) so effects.cpp can drop its copy and `effects.tables` its duplicate of the threshold table.
2. **CMakeLists.txt / sim.cpp**: add `mcengine/effects.cpp` to `MC_SIM_CORE` and call
   `effects_register_handlers()` from `sim_init`: creature death, player death and the castle drop
   mana through `g_hook_thing_drop_mana_ball`, and nearly every subsystem spawns effects.
3. **mc_types.h**: name the two words inside `GameState.padc`: `+0x24 u16 volcano_thing` (the erupting
   crater, effect 0x12; 0 = none) and `+0x26 u16 volcano_smoke` (its smoke column, effect 0x13).
   effects.cpp reads them by offset (`kStateVolcano`, `kStateVolcanoSmoke`).
4. **gen/dispatch_tables.h / carpet_names**: Table A names of class 10 carry the level editor's model
   names at the *state* index, which is wrong from state 0x17 on: `effect_lightning_s25_update_24cb0` is
   the Steal mana effect (type 0x19), `effect_rain_of_fire_s26_update_24d10` is type 0x1a,
   `effect_type21_s23_update_24c20` is the Lightning strike (type 0x17), `effect_steal_mana_s27_update_24fc0`
   is a wall piece, and the type numbers in `effect_type54_s56` .. `effect_type59_s61` are two too high
   (states 0x38..0x3d belong to types 0x34..0x38; 0x39 is the hatching egg). Suggested names:
   effect_lightning_strike_update_24c20, effect_steal_mana_update_24cb0, effect_lava_blob_update_243b0,
   effect_eruption_update_24810, effect_volcano_smoke_update_24a90, effect_orbiter_update_25550,
   effect_skeleton_army_update_257e0, effect_storm_cloud_update_25670, effect_mana_hoard_update_25f10,
   effect_castle_level_ground_update_26b50, effect_castle_seed_update_27d20, effect_boulder_update_27e90,
   effect_crab_egg_update_27ff0, effect_crab_egg_hatch_28050, effect_fire_pillar_update_280d0,
   effect_mana_magnet_update_28270, effect_blast_update_28320.

## Corrections / additions for docs/ENGINE.md

* Snapshot timing: 412 complete thing updates precede the snapshot (not 413).
* Damage slots by type: 0 damage, 1 claim (new mana owner: mana balls, wizard castles, hoards),
  3 written by the Steal mana effect, 4 pull request of the mana magnet (attacker = magnet),
  5 castle upgrade request (amount 10, attacker = the owner wizard).
* `terrain_cell_flag_bit(pos) & 1` is "flag low nibble == 0": class-0 water *without* the animated-sea
  bit 3. Open sea has nibble 8 and does not match (crater, canyon digger, explosion, earthquake).
* Flag bit 3 of the cell flags doubles as a parking bit: 26b50 and 26f10 move bit 7 (built-on) into
  bit 3 after their last height step and back when their wait ends; 26f10 clears bit 3 on the whole
  footprint one step earlier.
* Castle raise timing: 18 height steps (aux 0x12..1), paint on aux % 7 == 0 and on the last step, then
  1 tick wait, or 25 when +0x3c is set (the constructor of type 0x2a sets it), not "19 ticks". The
  height of a footprint byte >= 0xf is base + (low nibble - 1) * 4 here, without 26320's terrace case.
* Effect types: 0x10 lava blob, 0x12 erupting crater (created by the volcano when it ends; registers in
  GameState+0x24), 0x13 its smoke column, 0x17 lightning strike, 0x19 steal mana, 0x21 -> state 0x23,
  0x23 orbiter (state 0x25, unreachable), 0x24 skeleton army, 0x26 storm cloud, 0x28 mana hoard marker
  (owns balls through +0x90), 0x29 castle ground levelling, 0x2a castle raise, 0x2b castle seed landing,
  0x2c boulder (disabled), 0x34 crab egg (state 0x38, hatches in 0x39), 0x35 fire pillar, 0x36 mana
  magnet, 0x37 delayed blast, 0x38 nothing.
* `thing_drop_mana_ball_25fe0` drops *one* ball with all the mana (the count mana / 2000 it computes is
  unused) and does not clear the dropper's mana, only its mana owner.
* `mana_ball_merge_26120` frees the second ball with `thing_free` at once (not mark_delete), so a slot
  can be reused within the same tick.
* A resting mana ball is not static: z_vel alternates 0 / -0x10 every tick.
* Open question 1 of region B (24d70): confirmed, the start radius byte [esp+8] is never written.
* Level 38: the two volcanoes are live things at level start (not terrain-pass effects); they end at
  tick 18, and (in the port's run) one of the two craters dies in its first update because its ground
  height changed in the same tick; the snapshot has one crater, at the second volcano.
* Mana balls sleep: `timer_a` is 0x80 at creation and is counted down by 468e0 / 46960; from then on a
  ball is only updated for 16 ticks at a time while the local player is within 0x1800.
