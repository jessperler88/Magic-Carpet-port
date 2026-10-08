# Castles, balloons, scenery, switches (castle.cpp, scenery.cpp) - task D report, port round 3, 2026-10-06

Sources: `src/mcengine/castle.h` / `castle.cpp` (class 3 type 2 castle and type 3 balloon, 0x41290..0x42960),
`src/mcengine/scenery.h` / `scenery.cpp` (class 2 scenery 0x43ba0..0x43e80, class 11 switches 0x4a2a0..0x4a940),
test `src/tests/castle_test.cpp` + `castle_test.cmake` (one executable for both files, build dir `build_D`).
Everything was translated from the disassembly (`img.py dis 41290 42a20`, `43ba0 43ea0`, `4a2a0 4a9a0`; jump tables
0x414e0, 0x41978 read with `dwords`), the decompiled C was only used to look at the callees of other tasks.
`castle_test` passes (exit code 0, 0 failures), `/W4` build of the three files has zero warnings.

`src/mcengine/tables/castle.tables` was **not** created: nothing in the range reads a data-segment table (the balloon /
guard counts and the level stats are immediates behind code jump tables).

## Functions translated

### castle.cpp

| port function | original | notes |
|---|---|---|
| `castle_find_free_mana_ball(from, excl_a, excl_b)` | castle_find_free_mana_ball_41290 | nearest (unsigned squared 3D distance) own mana ball (effect 0x27, `mana_owner == (i16)from.owner`) that is neither exclusion |
| `castle_active_update` | player_flyer1_s4_update_413a0 | Table A class 3 state 4: the active castle |
| `castle_build_update` | player_flyer2_s5_update_41500 | state 5: build / upgrade sequencer on `cast_ticks` (jump table 0x414e0, 7 entries) |
| `castle_spawn_build_effect_2a` | castle_spawn_build_effect_2a_41610 | effect 0x2a through `thing_create`, step := 4 |
| `castle_spawn_build_effect_29` | castle_spawn_build_effect_29_41670 | effect 0x29, step := 6 |
| `castle_destroyed_update` | player_respawn_start_416d0 | state 6: castle with health < 0 loses a level |
| `castle_spill_mana` | castle_spill_mana_41720 | up to 32 mana balls for the mana above the capacity |
| `castle_manage_balloons_and_guards` | castle_manage_balloons_and_guards_419a0 | + `castle_level_balloons` / `castle_level_guards` = the jump table at 0x41978 |
| `castle_begin_build_stage` | castle_begin_build_stage_41f00 | level + 1; calls the constructor 0x39a50 directly (`thing_create_fn(10, 0x2a)`) |
| `castle_build_seq_set_done` | castle_build_seq_set_done_42000 | `cast_ticks = 2`; no caller in retail |
| `castle_collapse_level` | castle_collapse_level_42010 | runs the class-10 state 0x35 handler (0x27930, level_features.cpp) on the scratch Thing through `thing_update_fn(10, 0x35)` |
| `castle_take_damage` | castle_take_damage_42460 | 2 dead / 1 hit / 0; consumes slot 0 and the upgrade request in slot 5 |
| `balloon_update` | balloon_update_42530 | Table A class 3 state 9 |
| `castle_update_none` | player_update_shared_42520 (states 7, 8), player_flyer8_s10_update_42760 (state 10), class8_update_shared_42960 (class 8 states 0..5) | each is a single `ret` (42520 and 42760 are the last byte of the function before them) |

Called, not re-translated: 41310 (spatial.cpp), 42170 / 42200 / 42370 / 42770 (player.cpp), 353f0 / 118c0 / 11980 /
27930 (level_features.cpp), 39a50 (constructors.cpp), 25fe0 through `g_hook_thing_drop_mana_ball`.

### scenery.cpp

| port function | original | notes |
|---|---|---|
| `scenery_tree_update` | scenery_tree_update_43ba0 | class 2 state 0 |
| `scenery_tree_s1_update` | scenery_tree_s1_update_43cd0 | state 1, burning |
| `scenery_tree_s2_update` | scenery_tree_s2_update_43d60 | state 2, burnt |
| `scenery_standing_stone_update` | scenery_standing_stone_update_43db0 and scenery_badstone_update_43e60 | states 3 and 9, byte-identical code, one port function bound to both addresses |
| `scenery_dolmen_update` | scenery_dolmen_update_43de0 | state 6 |
| `scenery_update_none` | scenery_update_shared_43dd0 (4, 5), _43e50 (7, 8), _43e80 (10..17), switch_type31_s31_update_4a8a0 | single `ret` |
| `switch_test_player` | switch_test_player_4a8b0 | |
| `switch_test_any_player` | switch_test_any_player_4a940 | |
| `switch_creature_dead_trigger` | switch_creature_dead_trigger_4a660 | |
| `switch_inside_once_update` (static) | switch_hidden_inside_s0_update_4a2a0, switch_update_once_4a460 (s5), switch_obvious_inside_s9_update_4a560 | identical code: `test_player(1)` -> `switch_activate(id, 1)` + delete |
| `switch_outside_once_update` (static) | switch_hidden_outside_s1_update_4a2d0, switch_death_outside_s6_update_4a490, switch_obvious_outside_s10_update_4a590 | same with `test_player(0)` |
| `switch_inside_repeat_update` (static) | switch_hidden_inside_re_s2_update_4a300, switch_death_inside_re_s7_update_4a4c0, switch_type11_s11_update_4a5c0 | `activate(id, 0)`, `aux = 10`, re-arm |
| `switch_outside_repeat_update` (static) | switch_update_repeat_4a350 (s3), switch_type8_s8_update_4a510, switch_type12_s12_update_4a610 | |
| `switch_on_victory_update` (static) | switch_on_victory_s4_update_4a3a0 | |
| `switch_creature_trigger_update<N>` (static template) | the 18 stubs 0x4a780 (creature type 0, state 13) .. 0x4a880 (type 0x10, state 29), 0x4a890 (state 30, type -1) | `push N; push thing; call 4a660` |

All 18 class-2 records, class-3 records 4..10, the 6 class-8 records and all 32 class-11 records are bound by original
address in `castle_register_handlers()` / `scenery_register_handlers()` (the test asserts that none is null).
Dead code seen in the range and not translated: 0x42950 (`xor eax,eax; ret`) and 0x43e90 (`ret`), no callers, no
table record.

## How it works (what the disassembly says)

* **Owner block.** Every castle / balloon function reaches the player block as `things[(i16)t.owner].player`
  (`movsx` +0x18, `* 0xa4`, `+ 0x7503`); the castle's and the balloon's own +0xa0 stay the dummy block (snapshot:
  `player == 0` for all four). `balloon_update` ends in `player_take_damage`, which therefore writes the damage flash
  into the dummy block.
* **State 5 sequencer** (`cast_ticks`): 0 = `castle_crush_wizards`; if `aux != 0` and `(u8)castle_footprint_clear == 0`
  -> step 2 and flag 0x40 cleared (upgrade refused); else once (flag 2) `sprite += P.player_no`, then
  `castle_begin_build_stage`. 2 = state 4, `castle_spell_reset_charge(0)`, step 0. 3 = `reset_charge(1)` + effect 0x2a
  (step 4). 5 = `reset_charge(1)`, z snap, effect 0x29 (step 6). 1 / 4 / 6 = z snap (waiting). > 6 = nothing.
  Step 2 is written by the build effects (26f10, 26b50: task C), step 3 by state 4 when the timer runs out. **No
  writer of step 5 exists** in the export (`grep "0x30) = 5"` / `cast_ticks = 5`): 41670 and the effect 0x29 path look
  dead in retail, like 42000.
* **State 4**: `duration` (+0x32) is a "busy" timer (0x1e from `thing_area_damage_quake`, 5 after a collapse): while it
  is > 1 the castle only counts it down, calls `castle_spell_reset_charge(1)` (the owner's Castle spell stays
  "casting", i.e. unusable) and follows the ground - pending damage waits; at 1 -> state 5 step 3 (the terrain is
  raised again). With the timer at 0: `castle_take_damage` (2 -> state 6; flag 0x40 -> state 5 step 0), z snap,
  `mana_owner = owner` (this is what makes `mana_totals_update` credit the castle to the wizard); on **even
  `Thing.tick` only**: spill, extents, balloons / guards, and one own mana ball touching the castle is taken in when
  `mana < mana_total`.
* **castle_take_damage**: slot 0 as usual (kill: `killer = attacker`, attacker cleared, amount kept; hit: slot
  cleared, `P+0x187 = 4`; between the two the original pushes amount + thing for a call that was compiled out). Then
  `damage_slots[5].attacker == (i16)owner` (written by the castle spell's effect 27d20 together with amount 10) sets
  flag 0x40 when `aux < 7` and clears the attacker; the amount 10 stays in the slot forever.
* **castle_spill_mana**: `excess = mana - mana_total` when `mana + P.mana_in_transit > mana_total` (so transit mana
  only arms the test), all of `mana` at level 0; `n = clamp(excess / 1000, 1, 32)` limited by the free slots (pool
  empty: `models_initialise`, `active_top = -1`, n = 8); each ball gets `excess / n` (the last ones the remainder
  rule `if (excess < per) per = excess`), `mana_owner = castle.owner`, `home.x = home.y = 0`, `speed_cur = ball.rng %
  0x30 + 0x10` (one step of the *ball's* RNG), `z_vel = (0x400 - height above ground) / 8`, and is thrown
  `castle.rng % 0x1400 + 0xf00` units in direction `castle.rng & 0x7ff` (two steps of the *castle's* RNG, distance
  first). Nothing else steps a castle's RNG.
* **Balloons / guards by level** 0..7: 0,1,1,1,2,2,3,3 and 0,0,0,4,6,14,18,34. Per balloon slot: empty -> new balloon
  (class 3 type 3, `owner`, `mana_owner`, `sprite += player_no`, state 9); dead (`health < 0`) -> `thing_drop_mana_ball`
  + delete + slot 0; else target := castle when the castle is full (`mana + transit >= mana_total`), otherwise - only
  when `castle.tick % balloons == 0` and the balloon is in state 9 - target := castle and, with room in the hold, the
  nearest own ball **measured from the balloon** that is not the target of the other two slots. Slots above the
  allowance are released (cargo dropped). `P+0x126` is cleared at the top of *every* loop iteration and then gets the
  cargo, so it ends as the last balloon's cargo; `P+0x122 += balloon.mana_total` is **never cleared**. Guards: `z_vel`
  (+0x2e) is a cooldown of 0x10 managed updates; a slot whose Thing is no longer class 5 / type 0xf or is in state 0x5f
  is cleared (and restarts the cooldown); a free slot gets a new creature 5/0xf at castle + (0x80, 0x280), yaw 0x200.
* **castle_collapse_level** (level > 0): capacity lowered by `mana_total * 10 / 100` around one `castle_spill_mana`,
  sound 0x1e, scratch `things[0]` = {pos = home, castle_size = level, owner, type 0, aux 0, caster = castle} run
  through 0x27930, level - 1, extents, `castle_set_level_stats`, `reset_charge(1)`. Level 0 afterwards:
  `reset_charge(0)`, `P.castle = 0`, delete. `castle_destroyed_update` then sets state 4, spills again (everything at
  level 0), manages balloons (level 0 releases them), `cast_ticks = 0`, `duration = 5`. With an empty pool it only
  returns to state 4 (and comes back, since health is still < 0).
* **Balloon** (state 9; the constructor leaves state 7 = empty handler until a castle adopts it): no target -> only
  `player_take_damage`. Target of class 10 (no type test) with `mana_owner == owner`: within 0x400 the ball gets flag
  0x40 and `target = balloon` (the ball's own handler flies it in), on overlap the balloon takes the mana, `target =
  0`, `health = max_health`, ball deleted; beyond one step (`speed_cur` 0x30) it flies, else it sits on the ball.
  Target of class 3: beyond `speed_cur * castle level` it flies; inside, when not higher than `ground + desc+0xc`
  and the castle level > 0, it snaps to the castle, unloads, `mana_owner = owner`, `health = max_health` - so a
  balloon parked at its castle is fully repaired on every update *before* the damage is applied. Then
  `pos_follow_ground(desc+0xc, +0xa, +0xe)` = (0x200, 0x600, -16) and `thing_move_to`.
* **Trees**: flag 0x20000 (recyclable) is set on every update; slot 0 damage; at `health < 0` a fire (effect 6) is
  created with `owner = things[attacker].owner`, `z_vel = ext_h * 3 / 4`, both get `health = rng % 0x3c + 0x82`, the
  tree loses flag 8, goes to state 1 and is relinked; the attacker (not the amount) is cleared. State 1: `health--`,
  below 0x3c -> state 2 and sprite 0x53 -> 0xe2 / 0x54 -> 0xe3. All three states: z snap, and on a water cell
  (`terrain_type_mask_at == 1`) a splash (effect 5, tree's owner) + delete. A burnt tree never grows back (there is no
  regrowth code in the range).
* **Switches**: `switch_test_player` works on every 8th update of the switch only, looks at human wizards (class 3
  type 0) of the player list, plays sound 0x29 at the wizard that matches and snaps the switch to the ground only
  when nobody matched. "Once" kinds fire `switch_activate(SwiId, clear = 1)` and delete themselves; "repeat" kinds
  fire with `clear = 0`, set `aux = 10` and count it down only on updates where `switch_test_any_player` (all player
  Things, any type, dead or alive) is false. The SwiId is pushed sign-extended (`movsx`), so an id >= 0x8000 can never
  match. Creature triggers: when list `Config.creature_lists[type]` is empty (-1: lists 0..0xb and 0x10, i.e. not the
  villagers / guards 0xc..0xf): `aux` 0 -> 0x10, then down to 1, then sound + `activate(id, 1)` + delete = 17 updates
  after the last creature is gone; a creature reappearing only pauses the countdown. The victory switch reads
  **player record 0** (`state+0x3415`, not the local player): castle != 0 and status bit 2 ("won") -> `activate(id, 0)`,
  delete, the won bit is taken back, sound 0x29.

## Verification (castle_test, numbers from the run)

### 1. The engine's snapshot (movie/gam00000.dat, 413 ticks into level 38)

It holds 2 castles, 2 balloons, 150 trees, 5 switches. Checked as "must be producible by the translated code":

* castle 485 (player 1, level 2) and castle 895 (player 2, level 1): state 4, `cast_ticks == duration == 0`,
  `max_health` / `mana_total` = level table (40000 / 20000, 20000 / 10000), flag 2 and `sprite == 0xb1 + player`,
  `mana_owner == owner`, `z == terrain_height_at(pos)`, `P.castle == index`, `P.castle_level == aux`, `player == 0`,
  extents == `thing_set_castle_extents(level)` (whole Thing compared), balloon slots == `castle_level_balloons(level)`
  (1 each), no guards; balloons in state 9 with `sprite == 0xa9 + player`.
* `P.balloon_total` (+0x122) = 1,850,000 / 1,530,000 / 620,000 for players 1..3: multiples of the balloon capacity
  10000 that only make sense if the field is never cleared (185 / 153 / 62 managed updates with a balloon).
* From `Thing.tick` (starts at index & 0xff, +1 per update): **both** castles launched their balloon at their 22nd
  update, on an even castle tick (250 and 148) - the even-tick gate, and a prediction for the integration with task
  C's 26f10: in a fresh level 38 the three balloons must appear at castle update 22.
* balloon 521 hovers exactly on its castle at `ground + 0x200` with cargo 0 and target = castle; balloon 856 is
  1167 units from its target, an own mana ball that is not flagged 0x40 yet (> 0x400), and its yaw equals the
  direction to the ball (0x135).
* The mana balls of the two destroyed castles: player 3 owns exactly **5 balls of 1000** (the 5000 a level-1 AI castle
  starts with, spilled at level 0: n = 5), player 2 exactly **8 balls of 1009** (8072..8079 mana: n = 8, 8072 / 8);
  all `speed_cur` values lie in 0x10..0x3f (22..56, 16..61). Player 3 has `P.castle == 0` but `P.castle_level == 1`
  (41f00 is the only writer of +0x1a0).
* **Fixed points**: two `thing_update_all()` on the snapshot (one odd, one even castle tick) leave castle 485, its
  balloon 521 and castle 895 byte-identical apart from `tick`; `P.balloon_total` grows by exactly 10000 for players 1
  and 2; balloon 856 keeps its target and gets 94 units closer (two steps of 0x30); all 155 scenery / switch Things
  are unchanged apart from the tick.

### 2. Fresh level 38 against the snapshot

* Tick 1: the three AI castles (footer levels 1, 1, 1) are in state 5 / step 4 at level 1 with 20000 health, capacity
  10000, mana 5000, colour applied, `P.castle` / `P.castle_level` set, and each has its effect 0x2a (state 0x2c,
  `castle_size` 1, `caster` = castle, `spell_flags` 0, flag 0x10000, at `home`). Without a handler for the effect the
  castle waits in step 4.
* After 412 updates (the snapshot's age) **150 of 150 trees are byte-identical to the snapshot** (164 bytes each,
  `next` / cell links masked): position, z (= ground), flags 0x2000c, health, tick. The 5 switches are identical apart
  from flag bit 0, which the recording player's "all spells" cheat clears (player.cpp cheat 1; the recording wizard
  carries 31 x 100000 cheat mana) - no class-11 handler touches the flags.
* Castle 485 (same site in both): tick, state, flags, prop_flags, sprite, owner, mana_owner, cast_ticks, duration,
  z_vel, home, x / y and the level-independent extents equal the snapshot; its RNG is exactly **2 LCG steps** behind
  the snapshot's = one spilled ball in the original run (the AI had overfilled the castle once), which is what "two
  castle draws per ball, nothing else" predicts.
* After an upgrade by construction (slot 5 request -> state 5 -> level 2 -> state 4) castle 485 is **byte-identical
  to the snapshot's level-2 castle in all 164 bytes except mana, tick and rng** (0 differing bytes): max_health,
  health, capacity, extents, the 10 left in `damage_slots[5].amount`, and `z` = 5440 on both sides (the terrain the
  footprints raise).

### 3. By construction (level 38 after those 412 updates)

* `castle_take_damage`: no hit, hit (health, slot cleared, P+0x187), killing hit (killer, attacker cleared, amount
  kept, no flash), dead castle, upgrade request below level 7 / at level 7 / foreign index in slot 5.
* `castle_spill_mana`: excess 7500 -> 7 x 1071 (+3 left), 999 -> 1 x 999, 100000 -> 32 x 3125, 8079 -> 8 x 1009 (+7;
  the snapshot case); every ball at the position predicted by replaying the castle RNG (distance 0xf00..0x22ff),
  `speed_cur == ball.rng % 0x30 + 0x10`, `z_vel`, owner, zero home; castle RNG advanced by exactly 2 per ball; no
  spill at / below capacity or when only the transit mana exceeds it; level 0 spills everything.
* `castle_find_free_mana_ball`: nearest, both exclusions, foreign ball ignored, distance taken from the first argument.
* Balloon ferry (ball of 700 mana 0x1400 from a level-1 castle): sent after 1 update, ball called (flag 0x40, target)
  at update 87 at <= 0x400, swallowed at 88, unloaded at 175; castle + balloon + ball mana constant on every update,
  distance never grows, never below `ground + 0x200`.
* Upgrade refused: a built-on cell (flag 0x80) in the corner of the grown box, and castle 2 of level 38 (castle 3
  stands inside its grown box): state 5 -> step 2 -> state 4, level unchanged, no effect, flag 0x40 dropped. Level 2 ->
  3 has the same footprint size (no border strips).
* Quake timer: `duration = 0x1e` counts 0x1d..1 with pending damage untouched, then state 5 step 3 -> effect 0x2a
  with `spell_flags == 1` (constructor value) -> state 4 -> the waiting hit lands. Steps 5, 6, 7 and 42000 called
  directly.
* Guards: a level-3 castle creates its 4 guards at managed updates 0, 16, 32, 48 at castle + (0x80, 0x280), yaw 0x200;
  a guard in state 0x5f frees its slot and is replaced 16 managed updates later; 3 balloons at level 6, surplus
  released (2 `thing_drop_mana_ball` calls) at level 2.
* Balloon death: a 2500 hit is repaired on the next update while parked; a hit above max_health kills; the castle
  frees the slot on its next even tick (1 drop call) and launches a new balloon two updates later.
* Destruction: player 3's level-1 castle with 5000 mana -> `P.castle == 0`, `P.castle_level == 1`, balloon released,
  **5 balls of 1000** - exactly what the snapshot holds for that player. A level-2 castle with 19500 mana and health
  -301: 1 ball of 1500 (the tenth), level 1, health 20000 - 301, capacity 10000, 8 balls of 1000, `duration` 5 -> 4
  ... 1 -> state 5 step 3 -> state 4; empty pool -> state 4 only.
* Scenery: tree survives 100 damage, burns at the next 1000 (fire owner = owner of the attacking balloon, life = `rng
  % 0x3c + 0x82`, one RNG step, flag 8 cleared), `life - 0x3b` burning updates, sprites 0xe3 / 0xe2, burnt tree is a
  fixed point; tree on water -> splash + delete; stone / bad stone; dolmen sets 0x1000 on the overlapping living
  wizard only, and the flyer of player.cpp turns it into `mana_cost` 1000 instead of 100.
* Switches: the 8-tick gate, type 0 only, z snap only on a miss, `any_player` with an AI wizard; switch 1 of level 38
  (inside, once): 99 records -> 99 Things, records cleared, switch deleted; an "outside, once" kind; switch 6 (inside,
  repeating): fires, keeps its record, stays at `aux` 10 while the wizard is inside, counts 9..0 after he left,
  fires again; dragon trigger (no dragons) fires on the 17th update, skeleton and "all" triggers stay quiet while
  skeletons live, "all" ignores list 0xc but not list 0x10; victory switch (no castle / not won / won).

### 4. Smoke runs (with the two stand-ins below)

* Snapshot + the recorded movie, 2500 ticks (tick 413 -> 2912): no crash, cell lists consistent, castle count /
  levels and the 150 trees unchanged; balloon 856 fetched all 9 balls its owner had lying around (**9 pickups, 9096
  mana** = 8 x 1009 + 1024 unloaded into castle 895: castle mana 13704 -> 22800), the 8 balls of the castle-less
  players stay. `thing_dispatch_report` lists only handlers of other tasks (AI wizard, creatures, spells, 0x45b60,
  0x24810).
* Levels 0, 5, 12, 24, 38, 44, 2000 ticks each without input: no crash, lists consistent, every scenery / switch /
  castle / balloon state that occurs has a handler (trees 419 / 313 / 52 / 4 / 150 / 0, other scenery 13 / 20 / 0 /
  20 / 0 / 8, switches 0 / 5 / 5 / 18 / 5 / 3); AI castles get their balloons (1, 1, 3, 3), level 44's level-3
  castle its 4 guards.

### Not verified against the original (translated only, or by construction only)

* No per-tick reference: balloon flight is checked by fixed points, the 2-update distance and invariants, not
  position by position. Balloon pick-up needs the mana ball handler (25980, task C) to fly the called ball in; the
  test uses a stand-in.
* The build / rebuild sequence needs 26f10 / 26b50 (task C); the test's stand-in stamps the footprints at once
  (which happens to reproduce the snapshot's castle z exactly) and reports step 2. Timing of a real build (22
  updates, see above) is therefore untested here.
* `castle_spill_mana` with an exhausted pool (`models_initialise` branch) is translated, not exercised.
* Guards and balloons above level 2, switch states other than 0 and 2, standing stone / dolmen / bad stone: by
  construction and smoke only (level 38 has none; the snapshot has no castle above level 2).
* Sound requests go to `sound_request()`; nothing checks ids beyond the disassembly (0xa build, 0x1e collapse, 0x29
  switch).

## Deviations from the original

* Raw thing indices that the original multiplies by 0xa4 unchecked are reduced `% 1000` (owner, balloon / guard
  slots, targets, damage attackers); list walks stop at index 0 or >= 1000.
* `castle_spill_mana`: a zero ball count returns instead of dividing by zero (unreachable: the count is >= 1 after the
  clamps unless the free count is negative).
* `switch_creature_dead_trigger` bounds the creature type to the 20 list heads; `switch_on_victory_update` masks the
  player number with 7.
* The three empty handlers of castle.cpp and the four of scenery.cpp are one function each.
* Descriptive names for the misnamed table handlers (original names in the comments and the tables): see
  "Corrections".

## Hooks

None declared. Used: `g_hook_thing_drop_mana_ball` (spatial.h, to be installed by the effects owner) at the two call
sites of 25fe0 in `castle_manage_balloons_and_guards` - until it is installed a dead or surplus balloon loses its
cargo silently.

What the castle needs from other tasks to run end to end (all reached through the tables, nothing to connect):
0x26f10 / 0x26b50 must write `things[effect.caster].cast_ticks = 2` and delete themselves; 0x25980 must fly a ball
with flag 0x40 to `things[ball.target]`; 0x27d20 writes `damage_slots[5] = {10, owner}` on the owner's castle;
0x45530 creates the class 3 type 2 Thing; creature type 0xf (states 0x5b..0x5f) are the guards.

## Extra functions outside the range

None. (Test-local stand-ins only: `standin_build_effect` bound to 0x26f10 / 0x26b50 and `standin_mana_ball` bound to
0x25980 inside castle_test.exe; they do not exist in the engine.)

## TODO(port) call sites

None: the only non-game callee in the range is the sound request (`sound_request()`).

## Requested shared-file changes

* **`engine.cpp`: `engine_init` calls `castle_register_handlers()` but not `scenery_register_handlers()`** (no
  `#include "scenery.h"` either). Worked around locally: `castle_register_handlers()` ends with a call to
  `scenery_register_handlers()` (so castle.cpp now needs scenery.cpp at link time). Please add the call to
  `engine_init` (or `sim_init`, see below) and delete the forwarding line in castle.cpp.
* `mc_types.h`: `PlayerBlock.castle_level` (+0x1a0) is written as a 16-bit word (`mov [P+0x1a0], dx` in 41f00; castle.cpp
  stores it with a 2-byte memcpy) -> `int16_t castle_level; uint8_t pad1a2[...]`. Comments: `balloon_total` (+0x122)
  "sum of balloon.mana_total over every managed castle update, never cleared", `balloon_mana` (+0x126) "cargo of the
  last managed balloon"; Thing: `z_vel` castle = guard cooldown, `cast_ticks` castle = build step, `duration` castle =
  busy / rebuild timer, `damage_slots[5]` castle = upgrade request.
* `player.h`: the comment "castle helpers player_spawn needs (the castle handlers themselves are not ported)" is
  outdated. `castle_owner_spell` in player.cpp could move next to `castle_spell_reset_charge` if anybody else needs it.
* CMake / sim: castle.cpp and scenery.cpp depend only on the sim core (thing, spatial, level_features, player); they
  could join `MC_SIM_CORE` with their register calls in `sim_init`, so every other test gets living castles, trees
  and switches.
* Names (`ghidra/names`, `gen/dispatch_tables.h` after regeneration): `player_flyer1_s4_update_413a0` ->
  `castle_active_update_413a0`, `player_flyer2_s5_update_41500` -> `castle_build_update_41500`,
  `player_respawn_start_416d0` -> `castle_destroyed_update_416d0`, `player_update_shared_42520` /
  `player_flyer8_s10_update_42760` / `class8_update_shared_42960` -> `thing_update_none_*`, the switch states 5 / 8 /
  11 / 12 after their twins (inside once / outside repeat / inside repeat / outside repeat).

## Corrections for ENGINE.md / agent notes

* Region C "Castle state machine": state 6 is the destroyed castle, not a respawn; "4 -> wait" also holds for steps 1
  and 6; step 5 has no writer; add the busy timer +0x32 and the even-tick gate of state 4 as described above.
* agent2_C 419a0: P+0x122 / P+0x126 are not "recomputed each castle tick" (see above); the free-ball search is
  measured from the balloon, and `castle_find_free_mana_ball_41290`'s first argument is the balloon.
* agent2_C 41f00: the effect gets flag **0x10000** (`or byte [e+0x12], 1`), not 0x100, and `spell_flags = 0` (the
  short variant of 26f10); P+0x1a0 gets the level as a word.
* agent4_C "Scenery handlers": trees set the recyclable flag 0x20000 on **every** update of state 0, not only once
  they burn (all 150 living trees of the snapshot carry 0x2000c; a fresh tree has 0xc). Burning clears flag 8.
* Thing +0x2e on a castle = guard cooldown; +0x7c = `damage_slots[5].attacker`; a balloon is created in state 7 and
  only flies once a castle sets state 9.
* Footprint sizes of castle levels 2 and 3 are equal (extents 3328), so `castle_footprint_clear` for 2 -> 3 only tests
  for other castles.
* Possible mana duplication in the original: neither the castle nor the balloon tests the delete flag 0x400 of a ball,
  so a ball that both touch in the same update is credited twice (seen in an early version of the ferry test when the
  ball was called inside the castle's box).
* Switch notes: victory switch uses player record 0; "all creatures" = lists 0..0xb and 0x10; trigger delay 17
  updates; repeat switches re-arm after 10 updates without a player Thing in the triggering position.
