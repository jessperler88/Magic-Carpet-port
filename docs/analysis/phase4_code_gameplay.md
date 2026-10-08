# Phase 4 gameplay code analysis: what the engine gives a PvP RTS/RPG mode (2026-10-07)

Read-only survey of `src/mcengine` for the Phase 4 design (PvP RTS/RPG hybrid with wizard bots, towns
that gather resources, summoned monster units with orders, health plates / damage numbers, elite
monsters, new spells). Every claim points at a file:line of the port or at the docs it was taken from
(`docs/ENGINE.md`, `docs/analysis/port_*.md`). Offsets are the original Thing / player-block offsets, which the
port's structs reproduce (`mc_types.h` static_asserts).

Conventions: a "tick" is one `thing_update_all` (the sim runs 1 per frame at substeps 0); the map is 256x256
cells of 256 world units (`x >> 8` = cell); angles are 0..0x7ff (2048 per turn); "P" is the owner's
`PlayerBlock` (PlayerRec+0x44f) reached through `Thing.player`.

---

## 1. Thing system basics

### 1.1 Pool, struct, dispatch

- Pool: 1000 x 0xa4-byte slots in `GameState.things` (slot 0 = scratch sentinel), extendable to 32768
  with `PortSettings::thing_slots` (`thing.h:40-75`, `thing_at()` / `thing_index()`; port_pool.md). All
  inter-Thing links are u16 indices; 0 = none.
- `struct Thing` (`mc_types.h:116-178`). Fields a designer cares about:

| off | field | gameplay meaning |
|---|---|---|
| +0x04 | `rng` | per-Thing LCG seed (determinism: never draw from anything else in handlers) |
| +0x08 / +0x0c | `max_health` / `health` | health; effects use `health` as remaining lifetime; `< 0` = dead |
| +0x10 | `flags` | bit0 hidden, 2 initialised, 4 in cell map, 8 collidable, 0x10 skip player list, 0x20 dead wizard, 0x40 ball being collected, 0x80 sound/shield-sound, 0x100/0x200 cast hand, 0x400 delete, 0x1000 on dolmen, 0x2000 threat-scored, 0x4000 shield, 0x8000 rebound, 0x10000 no area damage, 0x20000 recyclable, 0x40000 sealed spell |
| +0x18 | `owner` | owner Thing index: self for independent things; a wizard's units/projectiles/castle carry the *wizard's Thing index*. Side/team = `owner` equality everywhere (`spatial.cpp:80`, `creature_common.cpp:338`) |
| +0x1a | `aux` | per-type: creature timer / z-vel, castle **level**, wizard-castle **inhabitants**, effect timer |
| +0x1c | `prop_flags` | accepted damage-type mask: bit0 damageable (slot 0), bit1 claimable (castle / wizard castle / mana ball), bit 3/4 steal / pull |
| +0x1e..+0x24 | `yaw, pitch, target_yaw, target_pitch` | heading and steering goal |
| +0x26 / +0x28 | `killer` / `last_attacker` | set by the damage intake (see 6) |
| +0x2a | `caster` | spell Thing -> wizard; build effect -> castle |
| +0x2c | `damage` | 100 at alloc; projectile / melee damage |
| +0x30 / +0x32 | `cast_ticks` / `duration` | spell: ticks left / total; castle: build step / busy timer |
| +0x34 / +0x36 | `parent` / `child` | flock leader / segment chain (dragon, worm, kraken) |
| +0x38 | `speed` | segment spacing |
| +0x3a | `timer_a` | **awake gate**: 0 = asleep (no damage intake, no enemy search) |
| +0x3f | `tick` | ++ after each handler call; `tick % think_period` = think tick |
| +0x40 / +0x41 / +0x46 | `cls` / `type` / `state` | class, model (Table B index), state (Table A index) |
| +0x42 / +0x43 | `filter_cls` / `filter_type` | collision / damage filter (0xff any). **Creatures are built with `filter_cls = 3`**: their shots only hit players/castles (`constructors.cpp:302-782`, `spatial.cpp:15`) |
| +0x44 / +0x45 | `impact_cls` / `impact_type` | effect a projectile leaves |
| +0x48..+0x4c | `x, y, z` | position (a `Pos`) |
| +0x4e..+0x54 | `ext_z0, ext_x, ext_y, ext_h` | bbox half extents (ext_x also = area-damage radius) |
| +0x56 / +0x58 | `sprite` / `frame` | sprite id / animation frame |
| +0x5a | `damage_slots[6]` | {i32 amount, u16 attacker} per damage type: 0 damage, 1 claim, 3 steal mana, 4 pull / tether, 5 castle upgrade request |
| +0x7e / +0x80 / +0x82 | `speed_cur` / `speed_base` / `turn_rate` | speed; `turn_rate` is a *speed* for creatures (follower bonus, villager stroll) |
| +0x84 / +0x88 / +0x8c / +0x90 | `mana_cost` / `mana_total` / `mana` / `mana_owner` | wizard: regen rate / total / carried; creature: `mana` = drop on death; mana ball: amount + owner |
| +0x92 | `target` | target Thing index |
| +0x96 | `home` (Pos) | castle site / projectile destination / teleport return / mana-ball velocity (x, y) |
| +0x9c | `desc` | `MoveDesc` index (30 records, `core_tables.h:708`): speeds, clearances, terrain mask, think period, sight radius, fov |
| +0xa0 | `player` | owner's P block offset (0 = dummy). Only wizards carry a real one; castles / units read it through `things[owner].player` |

- Classes and models (`gen/dispatch_tables.h`, `g_dispatch_classes[14]` at :642; ENGINE.md "Thing (entity) system"):
  1 (10 dummy constructors, no update), 2 scenery (6 models: tree, standing stone, dolmen, bad stone, 2 domes; 18 states = type*3 normal/burning/burnt), 3 player (types 0 human wizard, 1 AI wizard, 2 castle, 3 balloon, 4..11 start markers; states 0 flyer, 1 AI, 2 dying, 3 dead, 4 castle active, 5 castle build, 6 castle destroyed, 7/9 balloon), **4 (no table at all)**, 5 creature (17 models, 121 states = type*6 + {0 idle, 1 main, 2 attack, 3 follow, 4 dying, 5 dead}, 120 = body segment, 102..119 disabled), 6 (2 dummies), 7 weather (wind), 8 (6 dummies), 9 projectile (20 types, 21 states), 10 effect (57 models, 62 states), 11 switch (32), 12 spell (24 spells x 3 phases), 13 (4 dummies, `class13_create` `constructors.cpp:1474`).
- Dispatch (`thing.cpp:~414 thing_update_all`): free flagged things; rebuild the per-tick lists in Config
  (`creature_lists[20]`, `player_list`, `mana_ball_list` (effects 0x27/0x28), `wizard_list` (effect 0x2d =
  villages), `projectile_list`, `mc_types.h:480-484`); call the three hooks `g_hook_creature_wake_tick`,
  `g_hook_ai_record_threat`, `g_hook_mana_totals_update` (`thing.h:~140`); then for every live Thing
  `tableA[cls][state]` -> handler, `tick++`. A state with no record deletes the Thing.
- Binding: handlers are bound to the *original code address* of the record (`thing_register_update(addr, fn)`,
  `thing_register_create(addr, fn)`, `thing.h:24-25`); `thing_update_fn/thing_create_fn(cls, idx)` give raw
  access (`thing.cpp:62-75`). `thing_create(pos, cls, type)` (`thing.cpp:~406`) checks `enabled` and index.
- Registration order: `sim.cpp:33-40` (`sim_register_gameplay`: level_features, constructors, player, then
  hooks), `engine_init` adds effects, projectiles, spells, castle/scenery, creatures 1-3, ai_wizard.

### 1.2 Adding new Thing types without touching the faithful tables

The tables are `inline const` arrays of fixed size, and `rec_a / rec_b` bounds-check against `a_count /
b_count`. Options, cheapest first:

1. **Port-only extension table** (recommended): add `g_ext_dispatch[cls][state]` consulted by
   `thing_update_all` / `thing_create` *only when the faithful record is missing* (`rec_a == null` or
   `!enabled`). Free ids that no level record and no handler ever uses:
   - class **4** (no table: 0 records) and class **13** (4 dummy records) and classes 1/6/8 (allocate-and-drop
     dummies) - whole classes free for "unit", "building", "resource", "order marker";
   - creature states **102..119** (18 disabled `ret` records, `creatures3.cpp` registers `creature_noop_update`);
   - effect states 39, 47, 49, 50 (gaps) and types 0x14..0x16 / 0x18 (constructors return null);
   - projectile types 6, 15, 19 (constructors exist, nothing creates them).
   Gate the extension on `gameplay_rules()` (see 1.4) so the faithful game never sees it. The pool extension
   (slots >= 1000) already proves the pattern of "state outside GameState, byte-identical when unused".
2. **Reuse an existing class with a new `type`** beyond `b_count`: needs the extension table too (same
   mechanism), but keeps `cls == 5` so every "is a creature" test (`creature_lists`, damage filters,
   `cell_kill_things`, HUD radar) works for new unit types for free. Lists are indexed `type < 20`
   (`thing.cpp:~440`): types 17..19 are free creature list slots; beyond that the list code needs a widened array.
3. **Hooks** (`g_hook_*` null-default function pointers, `thing.h`, `spatial.h:~60`, `player.cpp:14-22`,
   `level_features.cpp:14`) are the established way to add behaviour across subsystems. New gameplay hooks
   should follow the same shape: `extern void (*g_hook_damage_event)(...)`, installed by the Phase 4 module.

`Thing.type` and `.state` are **u8** and Table A of class 5 already uses 121 of 256 states; a Phase 4 unit
roster beyond ~20 types with 6 states each exhausts a u8 state space - the extension table should key on
`type` and keep a small per-unit `sub_state` elsewhere (e.g. the unused `unk94` / `unk3e`, or a side array
indexed by Thing index).

### 1.3 Spawn, death, mana drop

- Spawn: `thing_create(pos, cls, type)`; constructors (`constructors.cpp`) fill health, speed, desc, sprite,
  filters and call `thing_link_cell`. Level records: `level_spawn_thing_record` (`thing.cpp:~503`), switches
  `switch_activate(dis_id)`. Creatures spawned at run time by: effect 0x24 skeleton army (`effects.cpp:661`),
  crab eggs (`effects.cpp:1075/1091`), wizard castles sending villagers (`level_features.cpp:563-600`,
  `creature_spawn_random_villager` :524 = 2/12 archer, 2/12 trader, 5/12 townie, 3/12 builder), castle guards
  (type 15, `castle.cpp:130-216`), skeleton conversion of villagers (`skeleton_convert_villager_1c1e0`).
- Death: the intake (`creature_apply_damage` `creature_common.cpp:103`) returns 2 when `health < 0` and sets
  `killer = last_attacker`; the state body goes to `base + 4` -> `creature_die` (:376: segments to dead state,
  **kill credit** `P.kills++` only for a class-3 type-0 killer and a non-villager, non-owned creature) -> `base + 5`
  `creature_dead_drop_mana` (:393): every 8th tick, `g_hook_thing_drop_mana_ball(t)` + death puff (effect 10/1) +
  delete. Players: `player_apply_hits` -> state 2 `player_dying_update` (`player.cpp:765`) -> state 3.
- Mana drop amount = `Thing.mana` at death: `thing_set_mana_from_health` = `max_health / 2` (`thing.h:~175`) in
  every creature constructor, then per-type tweaks (dragon / worm halve it and spread `mana_total / 32` over
  the 16 segments, which also drop theirs; kraken thirds; crab 500; villagers / guards 0;
  `constructors.cpp:302-782`). `thing_drop_mana_ball` (`effects.cpp:803`) throws **one** ball with all of it,
  `mana_owner` copied (so a wizard's possessed creature drops an owned ball). Mana ball sizes: sprite
  thresholds `g_fx_mana_ball_thresholds` (`gen/effects_tables.h`). There is no per-type "mana table"; the number
  is derived from `max_health`, so an elite's bigger drop is a bigger `mana` field at spawn.

### 1.4 Rule gating already in place

`PortSettings` / `GameplayRules` (`settings.h:14-110`): game logic reads `gameplay_rules()`, movies and network
sessions force the recorded / host rules (`gameplay_force_rules`, `net_agree_rules` `net.h:129`). A Phase 4
mode flag belongs in `GameplayRules` so the faithful references stay byte-identical when it is off.

---

## 2. Creatures

### 2.1 Roster (constructors.cpp, descriptors decoded from `core_tables.h:708`)

| type | name | hp | speed_base / follower bonus | desc (think, sight, fov) | sprite | attack (callback, damage) | notes |
|---|---|---|---|---|---|---|---|
| 0 | dragon | 9000 (+16 segs) | 0x50 / 0x10 | 12 (30, 0x1400, 0xaa) | 0x28, segs 0x13.. | fireball proj 0 (desc 6), 500 | flies; `creature_apply_zvel` |
| 1 | vulture | 2000 | 0x64 / 0x10 | 13 (30, 0xc00, 0x200) | 0x56 | melee within 0x400, `damage` (100) | seeks type-0x28 hoards (`creatures.cpp:111`) |
| 2 | bee | 3000 | 0x46 / 0x1e | 14 (30, 0x1400, 0x2aa) | 3 | melee, dives at 3x speed, backs off (`creatures.cpp:146-165`) | fastest turner (0x71) |
| 3 | worm | 9000 (+16 segs) | 0x40 / 0x10 | 15 (30, 0x1400, 0xaa; ground mask) | 0x58, segs 0x59.. | fireball, 500 | ground-bound |
| 4 | archer | 1000 | 0x1e / 0 | 16 (30, 0x1400) | 0 | arrow proj 0xd, 250 | only "wanted" wizards (P+0x210) and skeletons |
| 5 | crab | 5000 (+5000 per size) | 0x1e / 3 | 17 (30, 0x1400) | 0xb9..0xc0 | volley (1-5 shots, 400/800/8000) | eats mana balls, lays eggs, regenerates |
| 6 | kraken | 9000 (+2 segs) | 0x50 / 0x10 | 18 (35, 0x1400, 0x2aa; **water only**) | 0x31 | 5-shot type-9 bursts 800; grip knock-back | |
| 7 | troll | 4000 (0x55 variant) | 0x14 / 3 | 19 (40, 0xf00) | 0x55/0xc7 | troll fire proj 0xe, 780 | full heal every think tick (bug) |
| 8 | griffon | 10000 | 0x28 / 0x14 | 20 (40, 0x1900) | 0x2f | type-9 shot, 4000; rebound flag while attacking | passive until hurt |
| 9 | skeleton | 1000 | 0x14 / 0 | 21 (25, 0x800) | 0xdc | arrow 400 (600 if owned) | walks to **enemy castles map-wide**; converts villagers |
| 10 | emu | 2000 | 0x3c / 0x14 | 22 (40, 0xc00) | 0xd0 | arrow 250 | plain `creature_ai_step` |
| 11 | genie | 20000 | 0x3c / 0x14 | 23 (10, 0x1e00, 0x7c7) | 0xc8 | bolt proj 8, 3000 | stealth / teleport |
| 12-14 | builder / townie / trader | 1000 | 0x28 / 0x14 | 10 (40, 0xc00) | 0xdd / 0xd9-0xda / 0xdb | none (mana 0) | villagers, see 4.3 |
| 15 | wall guard | 1000 | 0x1e / 0 | 24 (15, 0xf00; castle cells only) | 0 | arrow 0xd | grid walk on castle walls |
| 16 | wyvern | 100000 | 0x3c / 0x14 | 25 (40, 0x1200) | 0xcf | 15-fireball bursts, 3000 | hunts villages |

Base damage for melee creatures is `Thing.damage` = 100 (alloc default) unless a constructor changes it.

### 2.2 Shared state machine (`creature_common.cpp`)

- `+0 idle` `creature_idle_seek_leader` (:215): intake; on think tick `find_leader` (:50, nearest same-type
  creature without a leader, in sight + fov) -> `parent`, state +3.
- `+1 main` `creature_ai_step` (:236): intake (hit by a class-3 thing -> `target = attacker`, state +2); `creature_move_step`
  (:178: ground-follow by desc clearances, step `speed_cur` along `yaw`, three detours +-0x155 / +0x400, boxed in
  -> dies); on think tick `creature_wander_turn` (:131, two RNG draws); **only while awake** the nearest thing of
  `player_list` (wizards, castles, balloons!) in sight + fov -> state +2; else leader -> +3.
- `+2 attack` `creature_attack_target(t, base, cb)` (:280): aim every 4 ticks; target dead -> +1; think tick: out of
  sight (`pos_dist_xyz >= sight_radius`) -> +1; else the callback fires (`creature_attack_fire` :~418 proj 0,
  `_arrow` proj 0xd, `_melee` :457 within 0x400 `thing_add_pending_damage`, `_volley`, `_troll`, `_griffon`).
- `+3 follow` `creature_follow_leader` (:308): follows `parent` at `leader.speed_cur + leader.turn_rate`, separation
  from other-owner same-type creatures within 0x100 (:338-349); switches on `leader.state - base`: 2 -> take the
  leader's target and attack; 3 -> adopt the leader's leader; 4/5 -> drop. A hurt follower makes the **leader**
  attack the attacker (:322-333).
- `+4 dying` `creature_die`, `+5 dead` `creature_dead_drop_mana`.
- Segments (state 0x78, `creature_segment_update` :~141): follow `parent` at distance `speed`; damage to a
  segment lowers the head's health (`creature_apply_damage` :114-121).

Targeting rules: creatures only *look for* things in `player_list` (class 3 alive) - they never attack other
creatures on their own; they retaliate only against class-3 attackers (`:242`, `:285`). Their projectiles carry
`filter_cls = 3` so they cannot hit creatures either. **Unit-vs-unit combat needs (a) an enemy search over
`creature_lists` and (b) `filter_cls = 0xff`** - both are per-Thing fields, so a Phase 4 "unit" handler can set
them without touching the faithful code.

### 2.3 Ownership, Possession, awake gate

- `owner` is set to the wizard's Thing index by whoever spawns for a wizard (skeleton army sets `mana_owner`
  only - `effects.cpp:690`; castle guards get `owner` - `castle.cpp:~200`). `owner == wizard` makes the unit
  immune to that wizard's area damage (`spatial.cpp:80`) and excludes it from his units' searches.
- **Possession (spell 3) does not control creatures.** Projectile type 1 homes on *mana* (balls 0x27, hoards
  0x28, villages 0x2d not owned by the caster, `projectiles.h:36`) and leaves effect 0xc, which writes damage
  slot 1 every tick (`effects.cpp:260-268`); the mana ball / village / hoard handler turns slot 1 into a new
  `mana_owner` (`effects.cpp:709-718`, `level_features.cpp:576-586`, `:784`). Creatures accept slot 1 only if
  `prop_flags & 2`, which no creature has (all `prop_flags = 1`). So "possess a creature" is new code; the
  claim path (slot 1 -> owner change) is the natural hook: give units `prop_flags |= 2` and consume slot 1 in the
  unit handler.
- Awake gate `timer_a` (`creature_proximity_wake_timer_46960`): reloaded to 0x10 only within 0x1800 of the
  **local** player (`port_creatures.md` "Awake gate"). Asleep creatures move and think but take no damage and
  find no enemies. For an RTS with units fighting off-screen this must be bypassed for units (set `timer_a`
  every tick in the unit handler, or a rule in `creature_wake_tick`); it is also **not network-symmetric**
  (local player dependent) - the original only ever ran it with one human's view... in a lockstep game each
  peer's local player differs, so creatures near *any* human are awake only on that peer. This is a latent
  desync source in the existing netcode for creature-heavy maps and must be made symmetric for Phase 4 (wake
  within range of *any* human wizard).
- Existing behaviours closest to RTS orders:
  - **move-to**: `villager_walk_to_wizard` (`creatures.cpp:~600`): `target` + `target_yaw` toward it, arrive within
    0x800; and `ai_approach_target` for flyers. A generic "walk to Pos" is `target_yaw = pos_angle_to(pos, dest)`
    + `creature_move_step` per tick.
  - **attack target**: state +2 with `target` set (`creature_attack_target`) is exactly "attack this Thing" and
    already handles range, aim, dead target.
  - **guard / follow**: state +3 with `parent` = a leader (or a wizard: `parent` may be any Thing; the follow
    body reads `leader.state - base`, so a non-creature leader falls into "drop" - a unit handler would use its
    own follow body).
  - **formation / swarm**: `find_leader` + follow + separation give organic flocks (bees, vultures, griffons);
    the leader's attack propagates (`:356-360`).
- Spawners: level records with `DisId` (switch triggers), eggs, skeleton army effect, villages; there is no
  generic "lair" building - the closest is the wizard castle (effect 0x2d) that emits villagers from `aux`
  (inhabitants) and the skeleton army effect that spawns in a circle (`effects.cpp:661-706`, 8 per cast, 64
  per owner cap).

---

## 3. Computer wizard AI (`ai_wizard.cpp`)

- Driver: class-3 state 1 `player_type1_s1_update` (:986): `player_ai_wizard_tick` (:916: cooldowns, threat
  drift toward 0x601f, regeneration, hits, flight, `ai_spawn_spells`, dodge / counter-spell / heal on think
  ticks), then the mode handler by `P.ai_mode` (+0x19f), then `ai_choose_goal` (:783).
- Goal ladder (:783-797): castle site (no castle + castle spell affordable) > retreat (health < half, has castle)
  > every `0x40 - reaction/4` ticks: upgrade castle (:609) > attack castle (:639) > attack wizard (:668) > attack
  balloon (:701) > collect mana (:619, needs Possession) > hunt creature with mana (:750) > default (:770: hurt
  -> go home, else idle / speed-up).
- Modes (:800-915): 1 upgrade, 3 fly to site, 4 approach, 6 collect (cast possession at the ball), 7 attack
  castle, 8/9/0xd attack wizard / balloon / creature (`ai_choose_attack_spell` :363: skeleton > volcano >
  [lightning roll vs rebound] > meteor > fireball > lightning; conserve flag below 1/4 mana), 0xb return home,
  0xc idle (speed-up). Movement `ai_approach_target` (:394) / `ai_wizard_move` (:413).
- Casting: `ai_spell_ready` (:188) owned + cooldown + mana + aim cone `((255-accuracy)/4+20) * 2048/360`;
  `ai_cast_spell` (:247) arms `spell.cast_ticks = duration` like a human key press and writes the cooldown
  `g_ai_spell_cooldown_reload[24]`; **refuses spell ids > 0x11** (:251) - thunderbolt, magnet, fire wall,
  reverse, smart bomb, mini fireball are never cast by bots.
- Knobs: level player block `+4 aggression, +8 reaction, +0xc accuracy` -> P+0x20a/0x20e/0x20c
  (`player.cpp:359-361`); threat table P+0x1cc (8 x {threat, grudge}); hostility threshold
  `50000 - mana_total/10 * aggression/255` (:35); `ai_want_spell[24]` countdowns (spell acquisition over time).
- State kept: all in P (mode, target + signature `unk94`, burst, conserve, cooldowns, want timers, threat) and
  `Thing.home` = chosen castle site (:571 `ai_find_castle_site`: quadrant corners).
- Weaknesses for an RTS bot: (1) single target, single mode byte, no plan beyond the current goal; (2) every
  target is a `player_list` Thing or a mana ball - no notion of units, buildings, resources; (3) economy = pick
  up balls + upgrade castle when the castle spell is affordable; no spending decisions; (4) spells > 0x11
  unreachable; (5) `rand()` (crt LCG, seed 1) only in the attack ladder - fine for lockstep but global;
  (6) threat only from projectiles fired by class-3 things (`:453`) - units hurting it will not register;
  (7) the think period divides `tick`, so all bots with the same reaction think on the same ticks.
  Extension path: keep this as the "avatar" layer (flight, dodge, spell casting are solid and reference-exact)
  and add a Phase 4 strategic layer that writes `P.ai_mode` / `target` and new order fields - i.e. replace
  `ai_choose_goal` behind a rules flag rather than editing the goals.

---

## 4. Castles, balloons, villages

### 4.1 Player castle (class 3 type 2; `castle.cpp`, `player.cpp:196-235`)

- Levels 0..7 (`aux`). Stats `castle_set_level_stats` (`player.cpp:214`): health {0, 20000, 40000, 40000, 60000,
  60000, 80000, 80000}, capacity {5000, 10000, 20000, 40000, 80000, 160000, 320000, 30000000}; balloons
  {0,1,1,1,2,2,3,3}, guards {0,0,0,4,6,14,18,34} (`castle.cpp:26-33`). Footprints `data/building.tab` entries
  1..7 = 8x8, 21x21, 21x21, 35x35, 35x35, 48x48, 48x48 cells (port_features.md); `thing_set_castle_extents`
  (`level_features.cpp:316`).
- Growth trigger: the **Castle spell** (16): without a castle the seed (proj 0xa) lands 0x1000 ahead and becomes
  the castle (`spells.cpp:661-695`, `projectiles.cpp` 45530); with one it flies to the castle and leaves effect
  0x2b, which files an upgrade request in `damage_slots[5]` (`effects.cpp:1030`); `castle_take_damage`
  (`castle.cpp:57`) turns that into flag 0x40 -> state 5 step 0 (`castle_build_update` :334): `castle_crush_wizards`,
  `castle_footprint_clear` (:416: no castle overlap, no built-on border cell) -> `castle_begin_build_stage` (:233:
  `aux++`, spawn effect 0x2a). Effect 0x2a `effect_castle_raise_terrain_update` (`effects.cpp:912`) grows the
  terrain in 18 steps from the footprint bytes (height = base + (nibble-1)*4, paint kind = high nibble + 0xb;
  `castle_stamp_footprint` `level_features.cpp:452` is the instant version), sets cell flag 0x80 (built-on) and
  hands back `castle.cast_ticks = 2`. Cost = the castle spell's `mana_total`, which `castle_spell_set_capacity`
  (`player.cpp:224`) sets to the *next* capacity - so the castle "costs" its own capacity in castle mana.
- Mana storage: `castle.mana` (<= `mana_total` = capacity); `mana_totals_update` (`player.cpp:154`) credits it to
  the owner's `Thing.mana_total` through `mana_owner` every tick; excess is spilled as balls
  (`castle_spill_mana` :84). Balloons (class 3 type 3, state 9 `balloon_update` :390) fetch own balls within
  reach and unload at the castle (`castle_manage_balloons_and_guards` :130). Spells with `mana_cost != 0` need
  that much in the castle (`spell_can_cast` `spells.cpp:455`).
- Damage / destruction: slot 0 via `castle_take_damage`; `health < 0` -> state 6 `castle_destroyed_update` (:375)
  -> `castle_collapse_level` (:262): spill a tenth, run the collapse handler (class 10 state 0x35,
  `level_features.cpp:607`) on the footprint, `aux--`; level 0 -> `P.castle = 0`, deleted. Quake / volcano /
  meteor hits set the busy timer `duration = 0x1e` (`thing_area_damage_quake` `spatial.cpp:187`) during which
  the castle rebuilds its terrain.

### 4.2 Wizard castles = villages / towns (class 10 type 0x2d; `level_features.cpp`)

This is the base game's organic settlement system and the foundation for Phase 4 towns.

- Creation: level records (size = `THING_INIT.Parent + 0x10`, building.tab entries 17..68) through
  `effect_create_wizard` (:941: state 0x33, health 0x1e build ticks, `damage` 100 = 0x64, `aux` 4, `prop_flags
  0x21`, sprite 0xb1) + `effect_wizard_init` (:327: `aux = 2` inhabitants, **`speed_base = w*h >> 4` =
  capacity**, snap to an even cell corner, extents, `z` = average height). Also founded at run time by a
  **builder** (`creature_builder_s72_update` `creatures.cpp:~700`): four attempts E/W/S/N of its home village at
  a random size 0x19..0x20, needs `terrain_rect_is_flat`, no water, no overlap with villages / castles; the
  builder then becomes a townie of the new village.
- Build: state 0x33 `effect_castle_build_update` (:479) raises the footprint over `health` ticks, then state 0x34.
- Living town `effect_wizard_castle_update` (:563): pending damage (`thing_apply_pending_damage` :534); a hit
  **costs one inhabitant, who comes out as an archer** and marks the attacker "wanted" (P+0x210 = 200); claim
  (slot 1, from Possession) -> `mana_owner` + player-coloured sprite (0xb1 + player_no, :580-585); every 40
  ticks **`mana = aux << 8`** (256 mana per inhabitant, credited to the claimer as mana in transit,
  `player.cpp:174-177`); when full (`aux == speed_base`, capacity > 5) a roll
  `rng % cap > cap - cap/16 - 2` sends out a random villager (:588-600).
- Villagers (types 12..14) walk to the nearest village (trader: nearest farther than 0x3c00) and enter it
  (`aux++`) if `speed_base > aux` (`villager_walk_to_wizard`); builders found new villages. So the population
  loop is: full town -> emits villager -> walks to another town (+1 there) or founds a new one.
- Collapse (:607): inhabitants leave as villagers / 3 archers / 1 builder, footprint un-built and smoothed.
- **What is not there**: no building "level" or roof variants per town - the growth you see in the base game
  is *new villages* appearing (builders) and footprints of different sizes; a single village never changes
  size. Sprites: the village Thing has sprite 0xb1(+player) for the radar; the buildings are **terrain**
  (footprint bytes painted with castle textures 6..0x22, `is_castle_texture` :235) and the type-15 guards are
  the only "people on the walls". Town "upgrade" in Phase 4 = re-stamping a larger footprint (the castle
  already does exactly this: `castle_footprint_clear` + raise effect with `castle_size = level`).
- Pool pressure: towns emit villagers without limit; `villager_room` (:558, `thing_cap_villagers`) caps it.

### 4.3 Hooks for resource towns

- Resource "mana" already flows: inhabitants -> `mana` every 40 ticks -> owner's `mana_in_transit` (visible
  on the HUD as total). Food / lumber / ore are new counters; the natural home is a per-player side struct
  (not P - see 7) and per-building `Thing.mana_cost/mana_total` reuse for stock.
- Gatherer behaviour = the villager body: pick target (resource node Thing), walk within 0x800, "enter" (here:
  take), walk back to the town centre, `aux`-style deposit. All existing primitives: `villager_walk_to_wizard`,
  `creature_move_step`, lists in Config.

---

## 5. Spells

### 5.1 Base game table (`constructors.cpp:1439-1464` `kSpells`, `spells.cpp` handlers, AI cooldown `gen/ai_wizard_tables.h`)

`mana_total` = mana charged per cast (taken from the wizard's `mana` via `mana_cost` rate, `spell_charge_mana`
`spells.cpp:471`), `duration` = cast ticks (`cast_ticks` counts down), `cost` = mana the **castle** must hold
(`spell_can_cast` :455), `damage` = copied into the projectile. There are **no spell levels or experience**:
"levels" in the parameter table is the duration. Progression is acquisition only (level pickups class 12 phase
1/2 `spell_dropped_update` :363; campaign `spell_found[24]` P+0x37c; AI `ai_want_spell` timers) plus the castle
level, which raises the Castle spell's `mana_total` (`castle_spell_set_capacity`). Fire rate is governed by
`duration` (fireball 5 ticks, re-armed by holding the key: one shot / 3 ticks) and, for bots, by the cooldown
table.

| id | spell | mana | dur | castle | dmg | AI cd | implementation |
|---|---|---|---|---|---|---|---|
| 0 | Fireball | 200 | 5 | - | 125 | 2 | `spell_cast_burst` -> proj 0 (0x180 speed, range 0x2000, desc 5), impact effect 0 explosion (`effects.cpp:141`: area damage once, scorch, dent) |
| 1 | Heal | 1000/tick | 21 | - | - | 1 | +5 % max health per tick (`spells.cpp:508`) |
| 2 | Speed-up | 1000 | 251 | - | - | 32 | `spell_cast_speed(+1)` (:324): 3x/2x speed, smoke puffs |
| 3 | Possession | 50 | 3 | - | - | 10 | proj 1 homing on mana -> effect 0xc claim (slot 1) |
| 4 | Shield | 2000 | 251 | - | - | 1 | caster flag 0x4000: next hit quartered, paid in mana (`player.cpp:659-664`) |
| 5 | Beyond Sight | 3000 | 101 | - | - | 0 | mana only; the renderer reads `cast_ticks` |
| 6 | Earthquake | 6000 | 51 | 120000 | 6000 | 0 | proj 2 -> effect 0xf wandering crack with 10-tick craters (`effects.cpp:312`); charges on the launch tick only |
| 7 | Meteor | 10000 | 11 | 100000 | 10000 | 4 | proj 3 (trail) -> effect 0x11 (:381) expanding explosion rings, `damage / max_health` per tick |
| 8 | Volcano | 30000 | 65 | 180000 | 1000 | 400 | proj 4 -> effect 9 raises a cone, then crater 0x12 erupts lava blobs 0x10 (:419) |
| 9 | Crater | 12000 | 31 | 100000 | 6000 | 0 | proj 5 -> effect 0xb digs (`level_features.cpp:704`) |
| 10 | Teleport | 5000 | 51 | 10000 | - | 1 | castle <-> return point, else random 0x4000 jump (:571) |
| 11 | Rubber band | 2500 | 17 | 16000 | - | 0 | proj 7 -> effect 0x1a slot 4 tether (`player.cpp:636-646`) |
| 12 | Invisible | 5000 | 251 | 50000 | - | 1 | caster flag 0x20 (:614) |
| 13 | Steal mana | 500 | 11 | 20000 | 2000 | 0 | proj 8 -> effect 0x19 slot 3 once (:530) |
| 14 | Rebound | 1000 | 101 | 8000 | - | 1 | caster flag 0x8000: projectiles reflect (`projectiles.cpp` `reflect`) |
| 15 | Lightning | 1000 | 2 | 25000 | 500 | 1 | proj 9 instant ray + segments (`projectile_lightning_update`), impact 0x17 (:513 one area burst) |
| 16 | Castle | 1000+ | 101 | - | 10000 | 40 | seed proj 0xa (see 4.1) |
| 17 | Skeleton army | 13000 | 13 | 150000 | - | 600 | proj 0xb -> effect 0x24 (:661) 8 skeletons, `mana_owner` = caster |
| 18 | Thunderbolt | 20000 | 33 | 90000 | 2000 | 0 | proj 0xc -> **storm cloud 0x26** (:617): climbs to ground+0x400, then 2 lightning bolts (class 9 type 9) per tick in random opposite directions for `health` ticks |
| 19 | Mana magnet | 4000 | 17 | 10000 | - | 1 | proj 0x11 ground-hugging -> effect 0x36 (:1139) pull requests (slot 4) to balls within 0xe00 |
| 20 | Wall of fire | 5000 | 51 | 12000 | 90000 | 1 | proj 0x10 -> effect 0x35 **fire pillar** (:1107): 15 ticks of fires (effect 6) on rings 0..1, rising |
| 21 | Reverse accel | 1000 | 251 | - | - | 2 | `spell_cast_speed(-1)` |
| 22 | Smart bomb (global death) | 75000 | 101 | 200000 | 7000 | 3 | proj 0x12 -> effect 0x37 **delayed blast** (:1160): fuse, then kills every creature / scenery and damages players within 0xa00 |
| 23 | Mini fireball | 600 | 3 | 50000 | 50 | 4 | same body as fireball |

Not in MC1 (the brief listed them): gravity well, alliance, undead army, path / rapid fire - the nearest
existing pieces are the kraken grip (knock-back pull), the mana magnet pull, skeleton army, and `burst`
(`Thing.burst` multi-shot in `spell_cast_burst`, always 0 in retail).

Cast flow a new spell plugs into: `player_cast_spell` (`player.cpp:560`) arms `cast_ticks = duration` on the
spell Thing in the chosen hand -> Table A phase-0 handler per tick -> `spell_can_cast` / `spell_charge_mana`
-> `thing_create(pos, 9, type)` + `spell_projectile_origin` (:482) -> projectile flight -> impact effect.
Adding spells 24+ means widening the 24-wide P arrays (`spell_slot[24]`, `spell_thing[24]`, cooldowns, book
UI `g_spell_slot_order`) - **all hard-coded to 24**; a Phase 4 spell book is better kept in a side struct and
mapped onto the 24 slots per match, or the rules flag can repurpose the ids with the extension table.

### 5.2 Proposed new spells (reuse first)

1. **Thunderstorm** - a storm cloud (effect 0x26 body `effects.cpp:617`) with a longer life and a target list:
   every N ticks pick a random enemy Thing within 0x1000 (walk `player_list` + `creature_lists`, the search of
   `creature_ai_step` :253) and spawn a lightning projectile (class 9 type 9, `launch_lightning` pattern
   `spells.cpp:240`) aimed down at it; keep the 0x17 impact. Add a rain visual by spawning white smoke (0xd).
2. **Chain lightning** - lightning bolt whose impact effect, on hit, re-fires `projectile_lightning_update`
   from the victim toward the nearest other enemy within 0x800 up to K jumps; store the jump count in
   `effect.aux`, halve `damage` per jump. Everything is `thing_create(9, 9)` + `target`.
3. **Meteor shower** - a marker effect that for `health` ticks creates a meteor projectile (type 3, impact 0x11)
   at a random offset above the target cell every 4 ticks; scale `effect_meteor_update`'s `aux` rings down (0xa0
   spacing) for small craters.
4. **Firestorm** - fire pillar (0x35 :1107) with `spiral_search_begin(&s, 0, 3)` rings and the lava blob (0x10
   :336) thrown every tick: blobs roll downhill and light fires (`thing_area_damage_fire`) - a spreading fire.
5. **Blizzard / Frost** - storm cloud spawning a new "frost" effect reusing 0x1a's "slot 4 every tick"
   (`effects.cpp:545`) with a new slot semantics for units: units read slot 4 as "slow" (halve `speed_cur` for N
   ticks). For wizards slot 4 is the tether; use a different slot (2 is unused by everything).
6. **Summon swarm** - skeleton army body (:661) parameterised: `thing_create(5, type)` in a circle, `owner` =
   caster (not only `mana_owner`), `filter_cls = 0xff`, `timer_a` forced awake; bees for a swarm, vultures for
   scouts. The 64-per-owner cap and `thing_free_count` guard are already there.
7. **Earth wall** - `level_build_wall` (`level_features.cpp:959`: staircase of wall pieces 0x1b, states
   north/south/east `:756-790` raising cells by `terrain_cell_raise_needed`) from the caster toward the aim point
   for 10 cells; wall cells block walkers (terrain mask) and are destroyed by craters. Pair with "Raise land":
   `castle_stamp_footprint` with a synthetic footprint (`CastleFootprint` + `footprint_walk` :103).
8. **Sinkhole / Terraform down** - crater effect 0xb (`effect_crater_update` :704) with a bigger `aux` depth
   and `terrain_modify_cells_spiral`; the inverse (raise) is the ridge raiser 0x33 (:828).
9. **Tornado** - a moving effect (yaw wander like 0xf earthquake :312) that each tick writes the kraken grip
   into every player P within 0x600 (`knock_yaw/knock_pitch/knock_speed`, `creatures2` kraken 38) and `home.x/y`
   velocity pulls into mana balls (slot 4 as the magnet :1139); lifts creatures by `z_vel`.
10. **Mana beacon** - effect 0x28 hoard marker (:784) + magnet pull (:1139) at a fixed point for N ticks: balls
    roll to it, merge (`mana_ball_merge` :825) and get `mana_owner` = caster; balloons then fetch one big ball.
11. **Gravity well** - the reverse: pull *projectiles* - walk `projectile_list`, turn each `yaw/pitch` toward
    the well by `angle_turn_step`; the lightning ray ignores it (instant) which is a nice counter-play.
12. **Mass heal / Regeneration aura** - heal body (:508) applied to every own unit within 0x800 (units'
    `health += max_health/20`), charged `mana_total` per tick; or a persistent effect that adds the troll's
    regeneration (`crab_regenerate`, `max_health >> 7` per tick) to units in range.
13. **Mind control** (possession of creatures) - possession projectile with `filter_cls = 5` and an impact
    effect writing slot 1; unit handler consumes slot 1 -> `owner = claimer` (and `mana_owner`), state reset to
    idle. Reuses the claim path end to end.
14. **Raise dead** - on cast, every dead-state creature (state base+5) within 0x800 is replaced by a skeleton
    (`skeleton_convert_villager` pattern, `creatures.cpp`) owned by the caster.

Each sketch is one Table A handler (effect or projectile) in the Phase 4 extension table plus a `kSpells`-style
parameter row; the spell Thing itself can be a class-12 Thing with the extension `type`.

---

## 6. Damage model

- Dealers never change health. They write a damage slot: `thing_area_damage(t, slot, amount)` (`spatial.cpp:142`:
  slot 0 = every other-owner, collidable (`flags & 8`), `prop_flags & 1`, filter-passing Thing whose bbox
  overlaps `t` within `ext_x` cells, plus enemy castles from `player_list`; other slots: `prop_flags & (1 <<
  slot)`), `thing_area_damage_fire` (:175, trees take 1/10), `thing_area_damage_quake` (:187, castles get the
  busy timer), `thing_add_pending_damage(attacker, victim, slot, amount)` (:199, unconditional write: melee,
  arrows, blast). `slot_hit` accumulates while an attacker is recorded (:22). The attacker field is the dealer's
  **owner** index, so kill credit flows to the wizard.
- Victims consume slot 0 in their own update: creatures `creature_apply_damage` (`creature_common.cpp:103`,
  **only while awake**), wizards `player_apply_hits` (`player.cpp:631`: tether slot 4, steal slot 3, shield
  quartering, knock-back `knock_speed = amount/10` capped 0x50, palette flash, regen pause; inside the own castle
  the hit is forwarded to the castle and the wizard is invulnerable :706-724), castles / balloons
  `castle_take_damage` (`castle.cpp:57`) / `player_take_damage` (:689), villages `thing_apply_pending_damage`
  (`level_features.cpp:534`), trees `scenery_tree_update`, segments (`creature_segment_update`).
- Armour: none. Mitigation = shield flag (one hit, quarter through, mana cost), rebound flag (projectile
  reflected if `target.mana >= proj.mana/4`), invulnerability timer P+0x14b (100 at spawn, 2 in castle),
  troll / crab / genie regeneration, castle busy timer (damage waits). Max health per type: constructors
  (section 2.1; wizard 10000, castle by level, village 0x1e build ticks then `damage` 100).
- What hits what: creature shots `filter_cls 3` -> players / castles only; wizard projectiles `filter 0xff`
  -> anything collidable of another owner; area damage skips same owner; `cell_kill_things` (:220) kills by cell.
- **Hook point for a damage event** (victim, amount, attacker, position) without touching faithful behaviour:
  the slot write is the single funnel - `slot_hit` (`spatial.cpp:22`) and `thing_add_pending_damage` (:199) are
  the only two writers of `damage_slots[0]` outside the castle forward (`player.cpp:716-720`) and the segment
  intake. But the *applied* amount is only known at intake (shield, invulnerability, asleep, castle forward).
  Recommended: a port-only `g_hook_damage_applied(victim, amount, attacker_idx)` called right after each
  `health -= ...` in `creature_apply_damage` (:108), `player_apply_hits` (:665), `player_take_damage` (:693),
  `castle_take_damage`, `thing_apply_pending_damage` (:539), `creature_segment_update`. These are six call
  sites, all port code, null-default pointer = zero behaviour change. Floating numbers read `thing_pos(victim)`
  and `victim->ext_h`; health plates read `health / max_health` per frame (the HUD already does this for the
  castle: `ui_fill_bar` `hud.cpp:439, 486`). Render-side: the extended renderer's per-Thing emit
  (`render_ext.cpp:670 emit_cell_things`, `push_sprite` :561) is where a plate quad above `z + ext_h` goes; the
  faithful renderer `render_thing` (`render_things.cpp:596`) must stay untouched.

---

## 7. Player / wizard

- Wizard Thing: class 3 type 0 (human) / 1 (AI), 10000 hp, `mana_total` 1000 at spawn (`player_spawn`
  `player.cpp:308-428`), sprite 0x2c for player 0 and 0x111..0x117 for players 1..7 (:353, **8 colour sprites**),
  desc 7 (cruise 0x400, clearance 0x80, sight 0x2000), start position `GameState.start_pos[8]`.
- Mana: `Thing.mana` carried (regenerates `mana_total/2000` per tick, `/200` in the castle, min 100 / 1000;
  `player_type0_s0_update`), `Thing.mana_total` = P.mana (1000) + everything with `mana_owner` = wizard
  (`mana_totals_update` :154: creatures, castle, balloons, balls, villages) - this is the HUD's "total mana";
  castle mana is a separate pool (`castle.mana`) that gates castle-cost spells. Claiming: Possession on balls /
  hoards / villages (slot 1), balloons ferry owned balls to the castle, the castle swallows a touching own ball
  (`castle.cpp:320-332`), a wizard flying through a ball picks it up (`player_flyer_move`). Level won:
  `game_check_level_won` (:894) castle mana vs `win_percent` of `Config.total_mana`.
- Death: `player_apply_hits` -> state 2 `player_dying_update` (:765): fall, drop all 24 spells as pickups (phase
  1, life 200..289), convert owned balls to a hoard marker (effect 0x28), `flags |= 0x20`; state 3
  `player_type3_s3_update` (:840): AI respawns at its castle after `((255-reaction)/8)*32+32` ticks, a human
  waits for command 0xf (respawn request; **no castle = level lost**, `rec.status |= 0xc`, :1068). Respawn =
  `player_spawn` at the castle position if any (:323), else the start position, invulnerable 100 ticks.
- Records: `PlayerRec[8]` x 0x801 at GameState+0x340b (`mc_types.h:339-365`): status/win, `thing`, tick,
  8 message slots (one per sender), camera log, name[0x40], input mode, and the P block (`PlayerBlock`
  `mc_types.h:220-307`): steering, knock-back, `kills_of_player[8]`, `castle`, `balloons[3]`, `guards[34]`,
  mana / transit / tether, invuln, regen, stats (shots, hits, kills, pct_*), AI fields (burst, conserve, mode,
  `threat[8]`, aggression / accuracy / reaction, wanted timer P+0x210), `spell_slot[24]`, want / thing /
  cooldown / hotkey / allowed / flash / found / sealed [24], hand slots. Everything is per-player, nothing
  per-team.
- Team / alliance needs: a `team[8]` (or per-player side id) and an `is_enemy(owner_a, owner_b)` helper
  replacing the `owner != owner` tests in `spatial.cpp:80,101`, `creature_common.cpp:338`, `creature_ai_step`'s
  search (:253), `ai_goal_*`, `projectile_pick_target`, `mana_ball_merge` ownership, HUD colours. There are
  ~25 such comparisons; a rules-gated `same_side()` inline keeps the faithful path (`a == b`) intact. Mana ownership
  (`mana_owner`) is per wizard, so shared team mana is a new layer, not a reinterpretation.
- Network: 10-byte `CmdPacket` per player per tick (`mc_types.h:16-25`: cmd, arg, pad2, steer x/y, key bits,
  **4 unused bytes**), exchanged by `net_exchange_frame` (`net.h:24`) in lockstep, checksummed side channel
  (`net.h:155-184`). RTS orders need more payload per tick (selection + target Pos + order type ~8 bytes): a
  second packet class or a bigger packet behind `NetGameRules` - movies record the same packets
  (`demo_record_playback_step`), so the demo format follows.

---

## 8. Feature -> reuse / new, and the hard-coded traps

| feature | existing mechanism to reuse | new code |
|---|---|---|
| Castle building | castle spell, seed, footprint raise, levels / balloons / guards (4.1) | nothing for parity; a "build menu" path that arms the castle spell without aiming |
| Town centre / lumber yard / mine | wizard castle Thing (4.2): footprint build, inhabitants, capacity, periodic mana, villager emission, claim by player, collapse | new building kinds (class 10 extension types or class 4), resource counters, a per-building footprint in a new .tab, builder order to found at a chosen site (`creature_builder_s72_update` logic with a given Pos) |
| Villagers gathering | villager walk / enter body, `creature_lists[12..14]`, `wizard_list` | resource node Things, carry state, deposit; gatherer owned by a player (`owner`) so enemies can kill them |
| Organic town growth | builders founding villages; castle upgrade re-stamp | per-town level with growing footprints (re-stamp like the castle), roof sprite variants are terrain paint kinds (`terrain_paint_cell`) |
| Lair / summoning | skeleton army effect (circle spawn, owner cap), creature constructors | lair building Thing, queue + cost, spawn with `owner`, `filter_cls 0xff`, awake |
| RTS orders (move/attack/guard) | `creature_attack_target` (+2), follow (+3), villager walk-to, `target` / `parent` / `target_yaw` | unit handler with an order field, group selection, pathing (there is none: `creature_move_step` detours only), network order packets |
| Unit vs unit combat | damage slots, area damage, `filter_pass`, projectiles' `target` | enemy search over `creature_lists`, `filter 0xff`, symmetric awake gate, side test |
| Wizard bots | `ai_wizard.cpp` avatar layer (flight, dodge, casting, threat) | strategic layer: economy, build, army, orders; spells > 0x11 |
| Health plates / damage numbers | `health/max_health`, `ui_fill_bar`, extended renderer per-Thing emit | `g_hook_damage_applied` at the 6 intake sites; overlay drawing in `render_view_ext` / compose 2D pass |
| Elite monsters | constructors (health, `mana` drop), `thing_set_sprite_double` (bigger sprite), regeneration bodies, name strings in `PlayerMsg`-style text | elite affix table, name labels, bigger `mana`, spell-upgrade drop (new pickup type reusing spell pickup phase 1) |
| Spell upgrades | none in MC1 (acquisition only) | per-spell level in a side struct scaling `damage` / `mana_total` / `duration` at launch time |
| New spells | section 5.2 bodies | extension table rows + handlers |
| Teams | - | `same_side()` + team array (7) |

Hard-coded things that will bite:

- **8 players**: `PlayerRec[8]`, `CmdPacket[8]`, `start_pos[8]`, `kills_of_player[8]`, `threat[8]`,
  `messages[8]`, `& 7` masks everywhere (`player.cpp:243,256,355,414`, `hud.cpp`), `net.cpp` NCB[8], level
  player blocks [8], 8 wizard sprites (0x2c, 0x111..0x117) and `g_hud_player_colours[16]` = 8 colour pairs
  (`gen/hud_tables.h:8`); balloon / castle / village sprites are `base + player_no` (`castle.cpp:146, 346`,
  `level_features.cpp:585`) - a 9th player would index into unrelated sprites. More than 8 wizards is a deep
  change; 8 players with teams is cheap.
- **u8 ids**: `cls`, `type`, `state`, 20 creature list heads indexed by `type`, 24 spells (P arrays, book order
  table, `spells_present[24]`), 6 damage slots (`prop_flags` bits), `castle_size` u8, footprint table of 69
  entries (`building.tab`), `g_sprite_desc[285]` and 0x211 sprites in tmaps - new unit art needs the sprite
  pipeline extended (`sprite_cache.cpp`).
- **Awake gate is local-player relative** (2.3) - must be symmetric for any multi-human simulation.
- **Pool**: 1000 slots faithful; dragons / worms need 16 free slots each; the port's extension (<= 32768) is in,
  but indices are sign-extended i16 in places (`thing_wrap`), so stay below 32768.
- **No pathfinding**: `creature_move_step` is steer-and-detour; the terrain mask per descriptor is the only
  "navigation". Castles' footprints block via texture mask (`fff080fe` = no castle cells for walkers except the
  wall guards' `0x20000`).
- **Determinism**: per-Thing LCG only; `rand()` in the AI ladder; `thing_free_count()` drives spawn counts
  (skeleton army) so pool size changes gameplay - the rules struct must carry the pool size (it does).
- **Mana is the only resource**, and totals are recomputed every tick by a full pool scan (`mana_totals_update`
  :161) - add resources to that pass, don't add more full scans.
- `Thing.player` is only valid on wizards; every unit reaches P through `things[owner].player`
  (`castle.cpp:18-21`). A unit whose owner wizard dies keeps `owner` = the dead wizard's slot (reused later!) -
  the hoard marker trick for balls (`player_dying_update` :818-823) shows how ownership is re-homed on death;
  units need the same.
- `Config.flags` bits and `Config.paused` are global; `substeps` (1/4/16 updates per tick) changes every timer
  - Phase 4 should fix substeps = 0 (1 update per tick).
