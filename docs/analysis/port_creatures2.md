# Creature port part 2 (creatures2.cpp): crab, kraken, troll, griffon - task B report, port round 4, 2026-10-07

Files: `src/mcengine/creatures2.h`, `src/mcengine/creatures2.cpp`, `src/tests/creatures2_test.cpp` +
`creatures2_test.cmake` (built with **`${MC_SIM_ALL}`** so the creatures meet the real projectiles, effects,
castles). No `.tables` file: the four
types read no exe data table. `creatures2_test.exe` -> **exit 0, zero warnings (/W4)**, built in `build_B` only.

## Functions translated

All handlers are bound by original address in `creatures2_register_handlers()` (the state = Table A index; base =
type * 6 as in port_creatures.md).

| type | state | original | port | what it does |
|---|---|---|---|---|
| crab (5, base 0x1e) | 31 | creature_crab_s31_1a830 | `creature_crab_s31_update` | damage intake; move; every think tick: nearest player-list thing in sight / fov without flag 0x20 -> attack (32) **awake or asleep**; else the chosen mana ball: gone -> forget, within `speed_base << 7` (pos_dist_xy) -> 33 with aux = 15, else aim; no target -> nearest type-0x27 thing of the mana-ball list, and when `mana > mana_total + 500` lay a crab egg (class 10 type 0x34, egg.aux = 100 + 10 * (rng % 10), one draw, -500 mana); tail: regeneration |
| | 32 | creature_crab_s32_update_1ac20 | `creature_crab_s32_update` | creature_attack_target(.., 0x1e, creature_attack_volley), sound 0x20 on a volley; regeneration |
| | 33 | creature_crab_s33_collect_mana_1ac80 | `creature_crab_s33_update` | damage intake; move; every `aux` ticks (tick % aux): ball gone -> 31; within speed_base * 5 -> eat it (mana += ball.mana, ball.mana_owner = 0, ball deleted, `crab_update_mana_sprite`) -> 31; within speed_base * 20 -> aux = 3; aim |
| | 34 / 35 | 1aed0 / 1aee0 | | creature_die / creature_dead_drop_mana |
| | - | crab_target_nearest_mana_ball_1aef0 | `crab_target_nearest_mana_ball` (exported) | pool scan slots 1..999, class 10 type 0x27, pos_dist_xyz < 0x10000 -> target (0 if none). **No caller in the image** |
| kraken (6, base 0x24) | 36 | 1afa0 | `creature_kraken_s36_update` | creature_idle_seek_leader |
| | 37 | 1afb0 | `creature_kraken_s37_update` | creature_ai_step; sound 0x25 on entering 38 |
| | 38 | creature_kraken_s38_update_1b000 | `creature_kraken_s38_update` + `kraken_fire` (exported) | see below |
| | 39 | 1b390 | `creature_kraken_s39_update` | creature_follow_leader; sound 0x25 on entering 38 |
| | 40 / 41 | 1b3e0 / 1b3f0 | | creature_die / creature_dead_drop_mana |
| troll (7, base 0x2a) | 42 | 1b400 | `creature_troll_s42_update` | creature_idle_seek_leader |
| | 43 | 1b410 | `creature_troll_s43_update` | think tick: the "regeneration" (see deviations: a full heal); creature_ai_step; aux = 1 on entering 44 |
| | 44 | 1b470 | `creature_troll_s44_update` | pose countdown (aux; at 1 -> sprite 0x55, speed_base); creature_attack_target(.., creature_attack_fire_troll); a shot with sprite 0x55 -> sprite 0xc6, aux = 30, speed_cur = turn_rate; left the state with 0xc6 -> back to 0x55 |
| | 45 | 1b510 | `creature_troll_s45_update` | creature_follow_leader; aux = 1 on entering 44 |
| | 46 / 47 | 1b530 / 1b540 | | creature_die / creature_dead_drop_mana |
| griffon (8, base 0x30) | 48 | 1b550 | `creature_griffon_s48_update` | creature_idle_seek_leader |
| | 49 | creature_griffon_s49_update_1b560 | `creature_griffon_s49_update` | damage intake (dead -> 52, hit by class 3 -> 50); move; every think tick creature_wander_turn, then **only while awake**: nearest player-list thing in sight / fov (no flag-0x20 test, unlike the crab) -> attacked only when it has type 0 / 1 and P+0x210 != 0 (re-armed to 200); else nearest leaderless griffon in sight / fov -> leader, 51 |
| | 50 | 1b940 | `creature_griffon_s50_update` | aux != 0 -> speed_cur = speed_base; flags \|= 0x8000; creature_attack_target(.., creature_attack_fire_griffon); a shot -> sound 0x26 and the target (type 0 / 1) wanted; every think tick sound 0x26 |
| | 51 | 1ba60 | | creature_follow_leader |
| | 52 | 1ba70 | `creature_griffon_s52_update` | killer of type 0 / 1 -> P+0x210 = 200; creature_die |
| | 53 | 1baf0 | | creature_dead_drop_mana |

Kraken state 38 (0x1b000) in detail: damage intake (dead -> 40; hit by a class-3 thing -> target := attacker,
**aux = -10**, return without moving; hit by anything else -> return); move; `(tick & 3) == 0` -> aim; target
health < 0 or flag 0x400 -> 37. Then the grip: `aux++` (pre-value > 40 -> aux = -90); while aux > 0 the
**target's player block** gets knock_yaw = (angle kraken->target + 0x400) & 0x7ff, knock_pitch = 0x100,
knock_speed = 0x50 (the wizard is pulled toward the kraken) and sound 0x2a. So one gripping cycle is 41 ticks
of pull followed by 90 ticks of rest. Every think tick: pos_dist_xyz >= sight radius -> 37, else sound 0x25 and
`castle_size = 5` (burst counter). While castle_size != 0: castle_size-- and one shot per tick (`kraken_fire`:
class 9 type 9, impact 10 / 0x17, owner, yaw / pitch at the target, z + ext_h, desc 0x96ad0, target, damage
0x320, collision filter = the **target's** filter_cls / filter_type).

Helpers private to creatures2.cpp: `nearest_player_thing` (crab / griffon copies of the enemy search, with the
flag-0x20 difference), `nearest_leader` (griffon's inlined leader search), `blame_wizard` (the villager
"wanted" store at 0x1b7d7 / 0x1b9d7 / 0x1baa4), `crab_regenerate` (tail 0x1abfc / 0x1ac64: health <
max_health -> health += max_health >> 7).

## Verification (numbers from `creatures2_test`)

1. **Construction tests**, hand-computed from the disassembly, on a fresh level 38 after one tick with
   creatures created by `thing_create` next to the local player (`g_hook_sound_request` replaced by a
   recorder):
   - crab: 30 -> 31; regeneration +max_health >> 7 (5000 -> +39) in 31 and 32, none at max; mana ball picked
     from the list on a think tick, aimed at on the next, state 33 with aux 15 inside speed_base * 128;
     non-think ticks change nothing; state 33: aux period, aim, aux = 3 within 20 x speed, eat within 5 x
     speed (mana 500 + 512, ball deleted, mana_owner 0, sprite 0xb9 + level and +5000 max health when the
     level rises); a vanished / non-mana target -> 31; the egg (exactly one LCG draw, aux = 100 + 10 *
     (rng % 10), -500 mana, nothing at exactly mana_total + 500); an enemy in sight / fov is attacked while
     asleep; state 32 makes the volley (1 projectile with 500 mana) and sound 0x20 once; hit by a class-3
     thing in 31 / 33 -> 32; lethal hit -> 34 -> 35 -> deleted with death effect; `crab_target_nearest_mana_ball`
     (nearer of two, the other, none).
   - kraken (placed in the nearest water area, the creature dies on land through creature_move_step): 36, 37
     -> 38 with sound 0x25; grip from aux 0: aux 1, knock speed 0x50 / pitch 0x100 / yaw angle + 0x400, sound
     0x2a; 40 -> 41 still grips, 41 -> -90; no grip while negative; aim every 4th tick; a think tick in range:
     sound 0x25, burst 5 with the first shot at once (castle_size 4), the next tick another shot (3); every
     field of the shot (type, impact, damage 800, owner, target, desc 0x96ad0, filter from the target, yaw,
     pitch, z raised by ext_h over the constructor's z); out of sight -> 37; target dead -> 37; class-3 hit ->
     aux -10, retarget, no move; 39 without leader -> 37; death with both segments -> 41.
   - troll: health top-up only on think ticks, health 0 stays 0, a hit of max_health + 1 in one think period
     kills; entering 44 sets aux 1; a shot (type 0xe, damage 780) puts sprite 0x55 into pose 0xc6 for 30
     ticks at turn_rate; the last pose tick restores 0x55 / speed_base; losing the target ends the pose; the
     0xc7 variant keeps its sprite; 45 without a leader -> 43; 46 -> 47 -> deleted.
   - griffon: 48; the think-tick turn draws exactly two LCG steps even asleep and nothing else happens asleep;
     awake + wanted wizard -> 50 and P+0x210 = 200; innocent wizard ignored; a leaderless griffon ahead ->
     51 / parent; 51 aims at the leader; 50: flag 0x8000, speed_base while aux != 0, one shot (type 9,
     damage 4000), sound 0x26 twice (shot + think tick), wizard wanted; not on a non-think tick; 52 blames
     the killer -> 53 -> deleted.
2. **Snapshot / replay.** The level-38 snapshot holds **no** crab, kraken, troll or griffon (census 0), and the
   412-tick replay of sim_test spawns none, so the replay result is the same with my handlers unbound and
   bound: **357 / 449 slots identical (creatures 131 / 169)** in both runs, 0 unported handlers dispatched. My
   code neither helps nor harms that integration check; there is no ground truth for these four types yet
   (a per-tick reference from task E on a level with them would be the next step).
3. **Movie from the snapshot**: 8551 ticks, 34201 / 34201 packets, pool clean every 1000 ticks, 0 unported
   handlers. **No kraken appears in the whole movie** (max 0 of my types at any tick), contrary to the
   briefing's "kraken on the movie level" (corrections below).
4. **Smoke runs**, 3000 ticks each, no input, census + `check_pool` (state belongs to the type, cell list
   membership, every cell chain consistent) every 500 ticks, `thing_dispatch_report` empty in all of them:

   | level | start (my types) | tick 3000 (state:count) | states reached | eggs laid |
   |---|---|---|---|---|
   | 12 | 10 griffons (49) | 49:4 51:6 | 49 51 | - |
   | 24 | 17 trolls (43) | 43:5 45:12 | 43 45 | - |
   | 44 | 11 krakens (37) + 22 segments | 31:2 32:2 37:5 39:6 (crabs from 1000 on) | 31 32 33 37 39 | 4 |
   | 25 | 10 crabs, 3 krakens, 37 trolls, 26 griffons | 31:11 33:2 37:1 39:2 43:9 45:28 49:8 51:18 | 31 33 37 38 39 43 45 49 51 | 3 |
   | 49 | 40 crabs, 68 trolls, 58 griffons | 31:10 32:10 33:10 43:24 45:25 49:23 51:34 (136 left) | 31..35 43..47 49 51 52 53 | 0 |

   Intermediate censuses: level 44 tick 1000 `32:2 33:2 37:5 39:6`, 2000 `31:2 32:1 33:1 37:5 39:6`; level 49
   tick 1000 `31:18 32:14 33:4 43:24 45:25 49:23 51:35`, 2000 `31:8 32:19 33:7 ...`; level 25 tick 1000 `31:7
   33:3 37:1 39:2 43:11 45:26 49:8 51:18`. On level 49 crabs fight trolls (crab volleys / troll fireballs
   between creatures of different owners), so the dying / dead states of crab and troll are exercised.
   Not reached in any smoke run (construction-tested only): 36, 40, 41, 42, 48, 50. Griffons are passive
   until a wizard hurts them (or a wizard is already "wanted"); in runs without input nobody shoots a
   griffon, so 50 is not reached (integrator's note, see the griffon correction below); kraken deaths need
   a wizard.
   Level scan (`creatures2_test <dir> scan`, level-start spawns): crabs on 18, 22, 25, 29, 31, 36, 41, 49, 63;
   krakens on 7, 16, 23, 25, 28, 30, 31, 33, 42..46, 56, 61; trolls on 7, 8, 15, 21, 22, 24, 25, 29, 41, 49,
   52, 63; griffons on 12, 16, 20..23, 25, 30, 40, 42, 43, 46, 48, 49, 61, 62. (Crabs on 44 hatch from eggs /
   switch spawns later; there are none at the start.)

What is only translated / weakly verified: everything beyond the construction tests has no reference values;
kraken 38 was reached once in smoke (level 25), the grip / burst only by construction; griffon 50 only by
construction.

## Deviations and gaps

- `tick % think_period` and crab 33's `tick % aux` fault on a zero divisor in the original; the port treats 0
  as "never" (no descriptor of these types has a zero period; crab aux is 15 or 3 in state 33).
- Troll state 43 (0x1b410) is translated as the CPU does it: `health += ((max_health >> 6) > max_health)`
  (always 0 for a positive max) and then `health = max_health` whenever the sum is non-zero, i.e. **a full
  heal on every think tick** (every 40 ticks) unless the health is exactly 0. Probably an intended `+=
  max_health >> 6` gone wrong in the original; it makes trolls very hard to kill in state 43 (damage is applied
  after the top-up, so only a single hit above max_health or several hits within one think period kill).
- Crab 31 contains a dead test at 0x1ab10 (`(tick % period) * 2 != 0 -> return` right after the passed
  `tick % period == 0`); griffon 49 a doubled `je` at 0x1b7d3. Not reproduced.
- The kraken grip writes the knock-back into the target's player block; for a target without one
  (Thing.player = 0) that is the dummy block, as in the original.

## Hooks

None declared, none installed. Everything called is ported: the shared creature bodies and attack callbacks
(creatures.h), `crab_update_mana_sprite` (constructors.h), `thing_create` for the egg (class 10 type 0x34) and
the kraken shot (class 9 type 9), `sound_request` (thing.h).

## Extra functions

`crab_target_nearest_mana_ball_1aef0` (inside my range, no caller; exported for completeness). No function
outside the range.

## TODO(port) call sites

None.

## Requested shared-file changes

1. (Done by the integrator during the round: `mcdata/sndbank.c` added to the mcdata library; my temporary
   listing of it in `creatures2_test.cmake` was removed. Rebuilt and re-run afterwards: exit 0, no warnings.)
2. `gen/dispatch_tables.h` / `tools/port/gen_dispatch.py` names, off by one type exactly as port_creatures.md
   said for the others: state 30 `creature_archer_s30_update_1a820` is the **crab's** base + 0; 36
   `creature_crab_s36_update_1afa0` is the **kraken's** idle; 42 `creature_kraken_s42_update_1b400` the
   **troll's** idle; 48 `creature_troll_s48_update_1b550` the **griffon's** idle (and 54
   `creature_griffon_s54_update_1bb00` the skeleton's rising state, already noted). Names to use:
   `creature_crab_s30`, `creature_kraken_s36`, `creature_troll_s42`, `creature_griffon_s48`. The +1..+5 names of
   31..53 are right.
3. Ghidra name `creature_target_nearest_mana_ball_1aef0` -> `crab_target_nearest_mana_ball_1aef0` would match
   the port; its comment ("placed between the crab s35 and s36 handlers") is right, and s36 is the kraken's.

## Corrections to ENGINE.md / briefing / agent notes

- **The movie level has no kraken.** Neither the snapshot nor any of the 8551 movie ticks contain a crab,
  kraken, troll or griffon. The briefing's "kraken on the movie level" probably came from sim_test's
  dispatch report, which sums the movie and its level 44 run.
- Crab (type 5): wanders and eats mana balls (`mana_ball_list`, type 0x27) and grows with them
  (sprite 0xb9..0xc0, +5000 max health per size step); lays a crab egg (class 10 type 0x34) for 500 mana when
  it carries more than mana_total + 500; regenerates max_health / 128 per tick in states 31 / 32; attacks
  the nearest player-list thing in sight / fov **whether or not it is awake**; its volley is the only attack.
- Kraken (type 6): grip cycle of 41 ticks of pull (the target wizard's P+0x16 / 0x18 / 0x1a knock-back =
  0x50 / toward the kraken / 0x100) and 90 ticks of rest; bursts of 5 shots (Thing+0x47 counts them) of a
  type-9 projectile with impact effect 0x17 and 800 damage. `Thing.castle_size` (+0x47) = kraken burst counter.
- Troll (type 7): firing pose sprite 0xc6 for 30 ticks at turn_rate speed (only for the 0x55 variant);
  the state-43 full heal above.
- Griffon (type 8): passive by default. It attacks (a) any class-3 thing that damages it - the damage
  intake of state 49 targets the attacker and enters 50 - and (b) on its own, only wizards that are
  already "wanted" (P+0x210 != 0, like the archer). It makes wizards wanted when it shoots at them or is
  killed by them. Its enemy search has no flag-0x20 exclusion. State 50 sets Thing.flags bit 0x8000 every
  tick: that is the rebound flag (projectiles.cpp `reflect`), so an attacking griffon throws projectiles
  back at their owner. (Integrator's correction 2026-10-07, prompted by the user's description of the
  game; `creatures2_test` now checks the hurt -> attack transition.)
