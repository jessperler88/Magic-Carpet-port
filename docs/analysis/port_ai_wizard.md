# AI wizard port (ai_wizard.cpp) - task A report, port round 4, 2026-10-07

Files: `src/mcengine/ai_wizard.h` / `ai_wizard.cpp`, `src/mcengine/tables/ai_wizard.tables` ->
`gen/ai_wizard_tables.h` (DAT_000938c4, 24 x u16 cooldown reloads), `src/tests/ai_wizard_test.cpp` +
`ai_wizard_test.cmake` (`${MC_SIM_ALL}`). Build dir `build_A`, `ai_wizard_test.exe` -> exit 0, zero
warnings (/W4). Everything was translated from the disassembly (`img.py dis 0x11de0 0x15590`, jump
tables 0x11da0, 0x141ec, 0x145f4, 0x15434 read with `dwords`); the decompiled C was used only to
cross-check the loop structure of `ai_find_castle_site_12bd0`. No file outside the task's list was
touched.

## Functions translated (all 60 of 0x11de0..0x15590)

| port | original | notes |
|---|---|---|
| `player_type1_s1_update` | player_type1_s1_update_11de0 | Table A class 3 state 1: tick, mode dispatch (jump table 0x11da0), `ai_choose_goal`; mode 0 runs the goal selection **twice**; the return values of the tick and of the mode handler are ignored |
| `player_ai_wizard_tick` | player_ai_wizard_tick_11f20 | housekeeping, hits, movement, regen, spell acquisition, think-tick dodge / heal, z clamp |
| `ai_choose_goal` | ai_choose_goal_12330 | |
| `ai_mode1_upgrade_castle` .. `ai_mode8_attack_wizard` | 12470, 12560, 12600, 12680, 126f0, 12830, 12950, 12a90 (+ thunk 12a80) | modes 2 / 5 = ret_zero_12550, left as empty cases |
| `ai_find_castle_site` | ai_find_castle_site_12bd0 | Thing 0 is the position scratch |
| `ai_goal_repair_castle` | ai_goal_repair_castle_12d70 | dead in retail (no caller) |
| `ai_goal_upgrade_castle`, `ai_goal_collect_mana`, `ai_goal_retreat`, `ai_goal_attack_castle`, `ai_goal_attack_wizard`, `ai_goal_attack_type3`, `ai_goal_hunt_creature`, `ai_goal_default` | 12df0, 12e90, 12f70, 13000, 13210, 13440, 13770, 13a20 | |
| `ai_goal_creature_near_rival` | ai_goal_creature_near_rival_13600 | dead in retail |
| `ai_set_mode(t, mode)` | the 14 setters 13880 (0), 138a0 (1), 138c0 (2), 138e0 (3), 13900 (4), 13920 (5), 13940 (6), 13960 (7), 13980 (0xd), 139a0 (9), 139c0 (8), 139e0 (0xa), 13a00 (0xb) | one function |
| `ai_get_spell_thing` | ai_get_spell_thing_13ac0 | |
| `ai_wizard_move` | ai_wizard_move_13b10 | |
| `ai_find_mana_ball_target` | ai_find_mana_ball_target_13ce0 | |
| `player_find_nearest_by_type`, `player_find_nearest_wizard_excl`, `player_find_nearest_castle_excl` | 13ec0, 13fa0, 14010 | |
| `thing_signature`, `ai_target_valid` | 14080, 140a0 | |
| `ai_approach_target(t, target, near, far)` | ai_approach_target_140d0 | **4** stack arguments (not 5): a null target measures the 2D distance to Thing.home |
| `ai_cast_spell` | ai_cast_spell_14240 | jump table 0x141ec |
| `ai_spell_ready` | ai_spell_ready_14640 | jump table 0x145f4 |
| `ai_castle_spell_ready`, `ai_spell_active`, `ai_can_afford_spell` | 14980, 14aa0, 14ad0 | |
| `ai_spawn_spells` | ai_spawn_spells_14b00 | Table B of class 12 through `thing_create_fn(12, s)` |
| `ai_spell_in_progress` | ai_spell_in_progress_14c40 | |
| `ai_choose_attack_spell`, `ai_choose_castle_attack_spell` | 14c70, 14f00 | the only user of the C runtime `rand()` (crt_rand_5aff8) in the game |
| `ai_record_threat_from_projectiles` | ai_record_threat_from_projectiles_150f0 | installed as `g_hook_ai_record_threat` |
| `ai_find_incoming_projectile`, `ai_set_dodge_steer`, `ai_counter_projectile` | 153b0, 15420, 15460 (jump table 0x15434) | |
| `ai_has_any_attack_spell`, `ai_cache_human_wizard` | 154e0, 15540 | |
| `ai_rand` | crt_rand_5aff8 | extra function (see below) |

`ai_wizard_register_handlers()` binds 0x11de0 (1 record) and installs `g_hook_ai_record_threat`.

Private helpers (`namespace {}`): `hover_toward` (the identical "stop and move z toward target z + 0x200"
tail of six mode handlers), `hostility_threshold` (`50000 - (other.mana_total / 10 * aggression) / 255`,
five copies in the original), `set_target` (target + signature), `update_conserve_flag` and
`attack_spell_rung` (the blocks 14c70 and 14f00 repeat per spell), `ListWalk`. Behaviour unchanged.

## What the code does (facts established from the disassembly)

- **Argument list of 140d0** is `(thing, target, near, far)`; mode 3 passes target = 0 and the function
  then measures `pos_dist_xy(self, self.home)` (the 3D call still happens on the null pointer in the
  original; the port skips it). Near / far pairs: mode 1 0x200 / 0x800, mode 3 0x800 / 0x1000, mode 4
  and 0xb 0x100 / 0x800, mode 6 0x400 / 0xc00, mode 7 0x800 / 0xe00, mode 8 0xd00 / 0x1200. Beyond
  `far` with speed-up (spell 2) ready the AI casts it instead of cruising.
- **Spell 2 is speed-up**, not Alliance (class-12 state 6 = `spell_speedup_update_47420`): mode 0xc
  (idle), mode 0xb without a castle and the far branch of 140d0 cast it. agent4_A.md's correction is
  itself wrong on the name.
- **ai_cast_spell refuses spell ids above 0x11** (`cmp dl, 0x11; ja`) *after* `ai_spell_ready` passed
  and after clearing flag 0x100 - so `ai_mode11_return_home`'s `ai_cast_spell(0x13)` (mana magnet, when
  farther than 0x2800 from the castle) can never succeed; the mode falls through to invisible (0xc).
- **ai_spell_ready** rungs (jump table 0x145f4): 2 -> owned + mana only (no cooldown test; the reload of
  32 is written but never read); 4 / 0xc / 0xe -> owned, not casting, cooldown 0, mana; 3 / 7 / 8 / 0x11
  -> the same plus the aim test; 0 / 0xb / 0xd / 0xf -> owned, cooldown 0, mana, aim (may be mid-cast:
  the burst); 0x10 -> without a castle cooldown + mana (Thing.mana), with one also not casting,
  `castle_footprint_clear` and aim; 1 / 5 / 6 / 9 / 0xa and 0x12..0x17 -> owned, cooldown, mana. Aim
  tolerance `((255 - accuracy) / 4 + 20) * 2048 / 360` (accuracy 255: 113 units = 20 degrees).
- **ai_cast_spell** (0x141ec): 0 / 0xf need `ai_burst >= 0`, aim < 0xaa, set the pitch to the target,
  `ai_burst++`, at 8 -> `(reaction - 255) / 8 - 1` (recovery, counted up by the tick); 3 / 7 / 8 / 0xb /
  0xd / 0x11 aim < 0xe3 + pitch; 0x10 with a castle = a normal cast (upgrade), without one
  `thing_create(home, 3, 2)` + owner + `P.castle` (no cooldown, no cast_ticks); the rest start the cast
  and write the cooldown. Every cast writes `cooldown[spell] = DAT_000938c4[spell]` (2, 1, 32, 10, 1, 0,
  0, 4, 400, 0, 1, 0, 1, 0, 1, 1, 40, 600, 0, 1, 1, 2, 3, 4).
- **Goals**: `ai_choose_goal` = castle site (needs no castle, castle spell owned and affordable by
  `mana_total`) > retreat (health < max / 2 and a castle) > [tick % (0x40 - reaction / 4) == 0] upgrade
  (castle in state 4 with duration 0, `ai_castle_spell_ready`) > attack castle > attack wizard > attack
  type 3 > collect mana (needs possession, and with the castle spell owned only while `mana_total <=`
  its cost) > hunt creature > default (hurt + castle -> 0xb, else 0xc).
  - attack castle (13000): refused when no castle but the castle spell is owned; a castle is a
    candidate when hostile (threat toward its owner > threshold with the *owner's* mana_total),
    its **owner** more than 0x1e00 away from it and not touching it (the distance is owner -> castle, not
    this wizard -> castle; 0x130e2..0x130ec), **or** when `(255 - aggression) * 0x280 +
    castle.mana < own castle.mana` (the own castle is *richer*; without a castle Thing 0's mana = 0);
    nearest wins, and only within the descriptor's sight radius.
  - attack wizard (13210): grudge == 1 toward a wizard -> immediate target; else hostile ones, or a
    wizard that has no castle, owns the castle spell and has less than `own mana - (255 - aggr) * 32`
    mana; wizards casting invisible are skipped; range sight radius + 10.
  - attack type 3 (13440): hostile, mana > (0x113 - aggr) * 10, not touching its own castle.
  - collect mana (13ce0): unowned balls by distance from the wizard; balls of hostile owners by
    distance from the **own castle**; balls of friends only when the nearest other wizard is more than
    20 cells away (or shares the owner) and no castle touches the ball.
  - hunt creature (13770): nearest creature with mana > 0 not owned by the wizard, measured from the
    own castle when there is one.
- **Threat drift** (11f20): toward 0x601f, up by `aggression + 1` per tick, down by `0x100 - aggression`
  unless the grudge word is set; both clamp at 0x601f. **Threat recorder** (150f0): per projectile with
  a class-3 shooter and a target, once (flag 0x2000): target castle -> its owner's threat toward the
  shooter's player number += 5000 (types 3, 4, 0xb) / 1000 and grudge = 1 when the value passes the
  threshold computed with the **castle's** player block (the dummy block, aggression 0 -> 50000);
  other player Things -> += 3000 / 500; projectile type 0xa (castle seed) adds **nothing** (the `jbe`
  at 0x1519f / 0x152aa) but is still marked and still runs the grudge test; a possession shot (type 1) at a player-owned mana ball ->
  += ball mana / 4. All clamp 0..0xffff.
- **Regeneration** (11f20): in the own castle (bbox overlap) or with flag 0x1000 (dolmen):
  `mana_cost = max(mana_total / 200, 1000)`, `health_regen = max_health / 200`, flag cleared; else
  `max(mana_total / 2000, 100)` and `/ 500`. `invuln_timer = 2` in the castle (damage slots zeroed,
  `player_apply_hits` skipped). The tick adds `mana_cost` *before* recomputing it.
- **Flight** (13b10): `pos_follow_ground(desc +0xc, +0xa, +0xe)`, forward by `speed_cur`, sideways
  (yaw + 0x200) by `P.strafe_speed` which decays by 4 toward 0 (0x50 when dodging), `speed_cur` steps
  by 16 toward `P.target_speed` (0 or `speed_base`), yaw turns by `angle_diff / ((255 - reaction) / 16
  + 8)` clamped to the descriptor's [+4, +2] and snaps when the step crosses the target. The z is
  clamped to ground + [+0xc, +0xa] at the end of the tick.
- **Spell acquisition** (14b00): for every *human* flyer in the player list (so twice per tick with two
  humans) the AI's 24 `ai_want_spell` countdowns run; at 0 the spell constructor is called at the
  wizard's position, the Thing gets flag 1 and `caster`, and the first free `spell_slot`.
- **Attack spell choice** (14c70): conserve flag below `mana_total / 4`, cleared above quarter + 6000 (or
  above half); ladder skeleton (0x11) > volcano (8) > [only when the target **is** casting rebound
  (0x14dc0 `je 0x14e33` when it is not), and `rand() % 255 < accuracy`: lightning or nothing] > meteor (7) > fireball (0) > lightning (0xf); a rung
  that is owned, affordable and off cooldown but not ready (aim) returns 0xff (wait). The castle
  variant (14f00) has no random branch. The `rand()` is the Watcom ANSI LCG from seed 1 (DAT_0009e5c8,
  never re-seeded), so the sequence is 16838, 5758, 10113, ...
- **Counter-spells** (15460): within 4 cells, fireball / meteor -> rebound else shield; types 4 and 9
  -> shield. The dodge (15420) only sets `strafe_speed = 0x50`.
- Signature (14080) = `(int8)cls * 0x80 + (int8)type + owner` in 16 bits; a target is valid while the
  Thing in the slot still has it.

## Verification (ai_wizard_test, numbers from the final run)

`ai_wizard_test.exe "C:/Magic Carpet/MagicCarpet/magic"`: exit 0, 0 failures, zero warnings, about
5 minutes in Debug (most of it is the movie and the two replays).

**1. By construction** (values worked out from the disassembly, on real AI wizards of the snapshot):
- the extracted cooldown table (fireball 2, speed-up 32, possession 10, volcano 400, castle 40, skeleton
  600, ...), aim tolerance (accuracy 255 / 128 / 0 -> 113 / 290 / 472 units), think period, signature
  incl. sign-extended class / type, `rand()` from seed 1 = 16838, 5758, 10113;
- threat drift: below neutral +aggr+1, clamping at 0x601f from both sides, decay `0x100 - aggr`, held
  by a grudge, neutral untouched; cooldowns count down to 0, burst recovery counts up; regen values
  outside the castle;
- spell acquisition: want timers of 3 create the 7 spells on the third call, in free book slots, with
  flag 1, caster and the wizard's position; the timer of an owned spell does not run;
- readiness / casting: heal (cooldown, mana), speed-up (ignores the cooldown), shield (refused
  mid-cast), fireball aim limits (tolerance and the 0xaa cast limit), 8-shot burst -> recovery value
  `(reaction - 255) / 8 - 1`, pitch to the target, flag 0x100 cleared; castle spell with castle (site
  clear, not casting, mana = spell mana_total 20000) and without (founds class 3 type 2 at Thing.home
  with the owner, no cooldown, no cast_ticks); spells above 0x11 ready but never cast; conserve flag
  set / kept / cleared; the attack ladder picks 0x11 for wizard 1 with full mana;
- threat recorder: castle +1000, scored once (flag 0x2000), meteor +5000 and grudge above 50000,
  saturation 0xffff, wizard +500 / +3000 (type 0xb), possession at an owned mana ball + mana / 4, other
  types on balls and creature shots ignored (not marked);
- dodge / counter: incoming projectile found within 20 cells (not at 21), strafe 0x50, rebound for a
  fireball, shield for lightning, nothing for type 5, nothing beyond 4 cells;
- approach: stop within near, cruise between, beyond far cast speed-up (no cruise) or cruise without
  it, nothing while it runs, 2D distance to Thing.home for a null target;
- finders against brute force over the pool, type 1 / 4 rejected; `ai_find_castle_site` (wizard 1
  without its castle: site (49152, 0), corner of the quadrant two to the east; the own quadrant's corner
  is taken iff no castle within 0x3000);
- flight: one step from rest (yaw step clamped to the descriptor min 5 / max 256, speed +16, strafe
  0x50 moves +0x50 in x and decays by 4), snap at the target in both directions, speed and strafe
  oscillation around their targets.

**2. The snapshot's AI players** (tick 413): player 1 mode 6 (collect mana, target ball 377), player 2
mode 8 (attacking AI wizard 492), player 3 mode 3 (flying to site (16384, 0), castle destroyed). All
three target signatures match the Thing in the slot; every cooldown / want timer is 0, all threat
entries 0x601f / no grudge (nothing was fired at an AI before the recording), burst 0 / 3 / 0, conserve
0, target speed = speed_base, accelerating 1, regen = max / 500. Every value is producible by the
translated code (checked).

**3. Replay of the 412 ticks before the snapshot** (numbers *before* the two per-tick fixes below;
after them: 401 of 449 slots, 101 of 102 AI fields - see "Per-tick reference") (copy of `sim_test::test_replay_to_snapshot`, with
a no-op handler on 0x11de0 vs. the port):

| class | AI off | AI on | snapshot |
|---|---|---|---|
| scenery | 150 | 150 | 150 |
| player | 0 | 1 | 8 |
| creature | 128 | 131 | 169 |
| projectile | 0 | 0 | 4 |
| effect | 51 | 55 | 74 |
| switch | 5 | 5 | 5 |
| spell | 13 | 15 | 39 |
| **all** | **347** | **357** | 449 |

Census with the AI: 452 things (players 8 as in the snapshot - the AI founds and keeps its castles /
balloons; without the AI only 6), creatures 170 vs 169, projectiles 5 vs 4, effects 75 vs 74, spells
39. `thing_dispatch_report` is empty. PlayerBlock / Thing fields of players 1..3 against the snapshot:
**78 of 102 equal with the AI, 43 without**. Equal for all three: ai_mode, target_speed, accelerating,
strafe_speed, countdown15f, ai_conserve, castle_level, P.mana, kills, shots / hits, the whole threat
and grudge table, all 24 cooldowns, all 24 want timers, spell_thing[24], target + signature, Thing
mana / mana_total / health / flags / state; player 2 and 3 also invuln_timer, health_regen, mana_cost,
speed_cur; player 3 aim_charge and pitch. Different for all three: position, z, yaw, target_yaw (the
paths diverge: e.g. player 1 at cell 168,43 vs 146,45, player 3 at 49,1 vs 47,1); player 2's castle
index (another slot was taken), its burst counter and home; player 1's invuln / regen / mana_cost /
speed_cur (it is inside its castle in the port, outside in the snapshot) and aim_charge. The same modes
and targets in all three wizards after 412 ticks of independent simulation are good evidence that the
goal logic is right; positions cannot match while the creatures / effects (102 differing slots before,
92 after) still diverge.

**4. Movie 0 from the snapshot with the AI** (first version; after the fixes see "Per-tick reference") (8551 ticks, 34201 / 34201 packets, pool consistent at
every census, dispatch report empty):

| | AI 1 | AI 2 | AI 3 |
|---|---|---|---|
| ticks moving | 3380 | 6535 | 7367 |
| path (cells) | 1112 | 2434 | 2771 |
| casts started | 401 | 275 | 129 |
| ticks casting | 2930 | 2214 | 901 |
| ticks dodging | 57 | 270 | 128 |
| castles founded | 0 | 0 | 7 |
| modes (ticks) | 1:461 6:831 7:17 8:6864 0xb:88 0xd:290 | 1:1504 6:3285 8:3464 0xd:298 | 1:3446 3:89 6:2424 8:2115 0xd:477 |

2180 projectiles were launched by the AI wizards. At the end the human flyer is dead (state 3,
health -210), all three AIs alive in mode 8 (attack wizard); player 3 holds 3,159,098 mana - it
claimed the recording player's 3.1 M cheat mana ball with possession. Census at the end: 332 things
(players 12, creatures 108, spells 15: the human's 24 cheat spells were dropped on his death and
expired). Player 3 founded 7 castles: its castle is destroyed and refounded repeatedly. No deaths of
AI wizards (none of them took fatal damage in this run).

**What is verified how**: the decision logic (goals, modes, spell choice, threat) by construction and
by the replay's mode / target / threat / cooldown agreement with the original's snapshot; the flight
model by construction only (no per-tick reference: positions diverge with the world); the movie run
proves robustness and plausible behaviour, not equality with the original. `ai_goal_repair_castle` and
`ai_goal_creature_near_rival` (dead code) are translated, not tested. `rand()` agreement with the
original is assumed (seed 1, never re-seeded, only caller) but untested against original data.

## Deviations from the original

- Indices read from P fields / Thing fields that the original multiplies by 0xa4 unchecked are bounded
  (`thing_ref`: slot 0 for an index outside the pool; `thing_or_null` for the "pointer <= &things[0]"
  tests). `ai_get_spell_thing` returns null for a spell id >= 24 (the original indexes P+0x2a4 out of
  range; no caller does it). `ai_can_afford_spell` returns 0 when the spell is not owned (the original
  reads through a null pointer; every caller tests ownership first).
- `ai_approach_target` with a null target skips the 3D distance call that the original makes on the
  null pointer (its result is unused).
- A think period of 0 (reaction >= 256, impossible from the level data) would divide by zero in the
  original; the port treats it as "think every tick". The yaw turn divisor is likewise guarded.
- The 14 mode setters are one function; the six identical hover tails, five threshold computations and
  the two attack-spell ladders share private helpers.

## Hooks

Installed: `g_hook_ai_record_threat` (thing.h). Declared: none - everything the range calls is in the
sim core (`thing.h`: finders / angles / move_to / terrain, `player.h`: `player_apply_hits`,
`player_rebuild_spell_index`, `player_block`, `player_threat`; `level_features.h`:
`castle_footprint_clear`) or reached through the tables (`thing_create`, `thing_create_fn`).

## Extra functions outside the range

`ai_rand` = crt_rand_5aff8 (ANSI `rand()`: `seed = seed * 0x41c64e6d + 0x3039; return (seed >> 16) &
0x7fff`), with `g_ai_rand_seed` = DAT_0009e5c8 (initial 1). It is called from nowhere else in the
image (`mc.py callers`); `srand` (5b01a) has no caller at all.

## TODO(port) call sites

None.

## Requested shared-file changes

- `src/CMakeLists.txt`: `mcdata/sndbank.c` (task F) is not in the `mcdata` library yet, so every
  `${MC_SIM_ALL}` test fails to link (`mc_sndbank_*` unresolved from sound.cpp). Worked around in my own
  `tests/ai_wizard_test.cmake` by listing `mcdata/sndbank.c`; remove that once the library has it.
- `sim_test`: raise the floor - with the AI registered the replay gives 401 of 449 (creatures 163,
  effects 63, spells 15, players 5).
- `reference_test` / level_features: tick 713 diverges on `g_wizard_castle_capacity_shift` (original 2, port default 4).

- `mc_types.h` comments (no layout change): `PlayerBlock.ai_burst` (+0x194) "fireball / lightning burst
  counter, negative = recovering `(reaction - 255) / 8 - 1` .. 0"; `ai_conserve` (+0x196) "1 while mana
  < mana_total / 4"; `Thing.unk94` (+0x94) on a wizard = `thing_signature` of `Thing.target` (AI); Thing
  flag 0x2000 on a projectile = "scored by the AI threat recorder"; `Thing.home` on an AI wizard = the
  castle site chosen by ai_find_castle_site; `PlayerThreat.grudge` is written 0 / 1 only.
- `thing.h`: the comment on `g_hook_ai_record_threat` says "called for every player"; it is called once
  per `thing_update_all` and walks the projectile list itself.
- `docs/analysis/agent4_A.md` / ENGINE.md: see corrections.

## Corrections to docs/ENGINE.md, agent notes, carpet_types.txt, dispatch names

- ENGINE.md 2.1 / agent2_A: `ai_approach_target_140d0` has 4 arguments (thing, target, near, far), no
  "use3d" flag - a null target selects the 2D distance to Thing.home. The hostility threshold uses the
  other player's **mana_total**, not health (`[owner + 0x88]`). `ai_mode11_return_home`: "farther than
  0x2800 -> try casting spell" is spell 0x13 and always fails (see above). `ai_goal_attack_castle`:
  "farther than 0x1e00" is the castle owner's distance from his own castle (an enemy away from home),
  not the AI's distance to the castle; "castle richer than (255 - aggr) * 0x280 + own mana" is reversed: the *own castle's* mana must exceed
  the target castle's mana + (255 - aggr) * 0x280. `ai_goal_attack_wizard`: the mana comparison is
  `(255 - aggr) * 32 + their mana < own mana` and applies to wizards *without* a castle that own the
  castle spell. `ai_find_mana_ball_target`: hostile owners' balls are measured from the own castle.
  `ai_goal_hunt_creature`: distance from the own castle when one exists. `ai_choose_attack_spell`: the
  accuracy roll is made only while the **target is casting rebound** and selects *lightning only* (not
  "meteor skipped"); the order is 0x11, 8, [target casting rebound: rand() % 255 < accuracy -> 0xf or
  0xff], 7, 0, 0xf.
  `ai_spawn_spells_14b00`: the countdown loop runs once per **human** flyer in the player list.
  `player_ai_wizard_tick`: the memset of the damage slots happens whenever `invuln_timer != 0`, not
  only in the castle; the dolmen flag 0x1000 also gives the fast regeneration.
- agent4_A.md: spell 2 is speed-up (`spell_speedup_update_47420`), not Alliance; `ai_mode12_idle_12680`
  casts speed-up.
- `ai_record_threat_from_projectiles_150f0`: the grudge threshold is computed from the *castle's*
  player block (dummy, aggression 0), so it is a constant 50000; "set grudge when above the hostility
  threshold" of agent2_A is right only in that sense.
- `ai_cast_spell_14240` case 0x10 does not write a cooldown or cast_ticks when it founds a castle;
  `thing_create(pos = Thing.home, 3, 2)` is the whole creation (the constructor does the rest).
- Table names (`gen/dispatch_tables.h`): the record of 0x11de0 is `player_type1_s1_update_11de0`; a
  descriptive name would be `ai_wizard_update_11de0`. `ret_zero_12550` serves modes 2 and 5.
- carpet_types.txt / mc_types.h: `PlayerBlock.accelerating` (+0xe) is set to 1 by every AI speed
  decision (stop and cruise alike) and cleared at the start of `ai_approach_target`; for the AI it is a
  "speed decided this tick" flag.

## Per-tick reference (task E dumps, movie 0, ticks 413..1013)

`reference_test` (integrator's, build_E) replays the movie in the port and diffs every tick against the
original's dumps `extracted/reference/movie0/tick%05d.gam`.

- **First divergence was tick 553** (AI wizard 486 / its fireball 487): the original re-armed the
  fireball every 2 ticks, the port did not. Cause: in `ai_choose_attack_spell_14c70` I had inverted the
  test at 0x14d87..0x14dc2 (`call ai_spell_in_progress(things[target], 0xe); test eax,eax; je 0x14e33`).
  The accuracy roll and its `rand()` draw happen only while the **target casts rebound**; otherwise the
  ladder goes straight on to meteor / fireball. The port rolled on every call and often returned
  "lightning or nothing" (0xff). Fixed. No `rand()` draw happened in the reference window after the fix,
  so the missing global rand seed in the dumps does not matter there.
- **Next AI divergence was tick 867** (player 3's record, P+0x1e4 = its threat toward itself):
  the port added 1000 when player 3's castle seed (projectile 0xa, launched by its own castle spell
  during an upgrade, target = own castle) was scored. In `ai_record_threat_from_projectiles_150f0`
  type 0xa takes the `jbe` past both additions (0x1519f castle branch, 0x152aa wizard branch). Fixed.
- **Now:** every AI wizard Thing, its spells and the PlayerRecs of players 1..3 are identical to the
  original for all 601 compared ticks (global RNG and live counts identical throughout). The first and
  only diverging slot is **tick 713, slot 395, effect 0x2d state 51 (wizard castle), speed_base 20 vs
  5**: the capacity `w * h >> g_wizard_castle_capacity_shift` - the original exe used shift 2 here, the
  port's default is 4. Not AI: the integrator should run the reference replay with shift 2 (and probably
  question whether retail really uses 4). Player 0's record differs from tick 442 at P+0x34c
  (`spell_flash`, the human's quick-select keys - input / player, not AI).

Effect of the two fixes on `ai_wizard_test`:
- replay of the 412 ticks before the snapshot: **401 of 449** slots identical (was 357; creatures 163 /
  169, effects 63 / 74, players 5 / 8, spells 15 / 39, scenery / switches all); census 448 vs 449;
  **101 of 102** AI fields equal - position, z, yaw, target, mode, mana, cooldowns, threat of all three
  wizards are exact after 412 independently simulated ticks; the only difference is player 2's castle
  *slot index* (pool allocation order).
- movie: 8551 ticks clean; AI 1 / 2 / 3 path 2452 / 2681 / 2636 cells, 374 / 149 / 189 casts, 2 / 0 / 3
  deaths and respawns, 4861 projectiles launched by the AI.
- `sim_test`'s floor can be raised to 401 of 449 with the AI registered.
- New regression cases: castle seed scored without adding threat; no `rand()` draw while the target
  does not cast rebound.

### Per-tick reference, round 2 (dense dumps `extracted/reference/movie0_dense`, ticks 413..2497)

After the integrator's castle-footprint fix (320x200 doubling, no capacity shift) everything matched
through tick 1254. Then:

- **Tick 1255, AI wizard 480 (player 1), z**: the original sank 1 per tick (pos_follow_ground only),
  the port climbed 3 (pos_follow_ground -1, plus +4 from the hover tail). The wizard was in mode 8 with
  the burst counter recovering. In `ai_mode8_attack_wizard_12a90` the test `cmp word [P+0x194], 0;
  jl 0x12bc6` (0x12b15) jumps to the `mov eax, 1` **behind** the hover block, so a recovering wizard
  returns without touching z. The port hovered. The same shape is in `ai_mode7_attack_castle_12950`:
  on a non-think tick `jne 0x12a70` (0x129f8) also skips the hover. Both fixed. The other mode handlers
  (1, 3, 6) were re-checked: there the approach-not-finished path already skips the hover, and the
  hover is reached only after a failed cast, as the port does it.
- **Now: every Thing and the records of players 1..3 are identical to the original for all 2085
  compared ticks (413..2497)**, global RNG and live counts included, and the dispatch report is empty.
  The only difference left is **player 0's record from tick 1485 at offset 0x60** =
  `PlayerRec.messages[1].text` (the HUD message line that player 1's action produced for the local
  player). That is message text written by the player / HUD code (port_player.md lists the
  language-file lines as not copied), not AI state.
- New regression cases in `ai_wizard_test` (`test_movement`): mode 8 with `ai_burst < 0` leaves z
  unchanged and returns 1; with burst 0 and nothing castable it hovers by `z_step`; mode 7 leaves z
  unchanged on a non-think tick and hovers on a think tick.
- The replay of the 412 ticks before the snapshot is unchanged by this fix (401 of 449 slots, 101 of
  102 AI fields); the movie runs 8551 ticks clean.

### Per-tick reference, round 3 (full dumps `extracted/reference/movie0_full`, ticks 413..8600)

With the round-2 fixes and the integrator's (HUD tick state, null-hit index from the reference pool
base, French notices) everything matched through tick 8497.

- **Tick 8498, AI wizard 486 (player 2)**: original mode 7 (attack castle, target 59 = the human's
  castle, signature 865 = 3 * 128 + 2 + 479), port mode 6 (collect mana). In `ai_goal_attack_castle_13000`
  the hostile branch measures `pos_dist_sq_xy(things[castle.owner], castle) > 0x3840000`: edi =
  `(i16)castle.owner * 0xa4 + &things[0]`, `push castle + 0x48; push edi + 0x48` at 0x130e2..0x130ec.
  So a hostile castle qualifies when **its owner is more than 0x1e00 from it** (and not touching it).
  The port measured this wizard's distance to the castle instead. Fixed. `rand()` played no part (the
  decision chain 12330 -> 13000 draws none).
- **Now: no divergence at all** - every Thing, every PlayerRec and the global RNG are byte-identical
  to the original for all 8152 compared ticks (413 .. end of the dumps).
- Regression case in `ai_wizard_test` (`test_movement`): with the enemy hostile and the own castle
  empty, the castle is not a target while its owner is 0x1000 from it and becomes the target (with
  its signature) at 0x2000, independent of the AI's own position; not hostile -> no target.
- I re-checked the other threat goals for the same pattern: 13210 (wizards: distance self -> wizard),
  13440 (type 3: collide with the type-3 owner's castle, distance self -> Thing) and 13ce0 (balls)
  measure what the port measures.
