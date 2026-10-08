# Creature port, part 3: emu, genie, type 15, wyvern (creatures3.cpp) - task C report, port round 4, 2026-10-07

Files: `src/mcengine/creatures3.h`, `creatures3.cpp`, `tables/creatures3.tables` ->
`gen/creatures3_tables.h`, `src/tests/creatures3_test.cpp` + `creatures3_test.cmake`.
`creatures3_test.exe` -> exit 0, zero warnings (/W4), built in `build_C` only. Full run (construction,
replay, movie, levels 24 / 44, genie scan of 70 levels, level 16) takes about 3 s.

**Test build:** a unit build, `${MC_SIM_CORE}` + effects, projectiles, spells, castle, scenery,
creature_common, creatures, creatures3, input (not `${MC_SIM_ALL}`). While I worked, `${MC_SIM_ALL}`
did not compile because the other round-4 files (sound.cpp / sndbank.h, ai_wizard) were half-written.
`register_gameplay()` in the test mirrors `sim_register_gameplay()` without creatures2 / ai_wizard /
sound. The creatures still meet real projectiles, effects, castles and spells. Once the round is
integrated, `${MC_SIM_ALL}` can replace the list unchanged.

## Functions translated

All handlers are bound by original address in `creatures3_register_handlers()`.

| port | original | state |
|---|---|---|
| `creature_emu_s61_update` | creature_emu_s61_update_1c8f0 | 61: `creature_ai_step(t, 0x3c)` |
| `creature_emu_s62_update` | creature_emu_s62_update_1c900 | 62: `creature_attack_target(t, 0x3c, creature_attack_arrow)`, result unused (no sound) |
| `creature_emu_s63..65_update` | 1c920 / 1c930 / 1c940 | follow / die / dead |
| `creature_genie_s66_update` | creature_genie_s66_update_1c950 | 66 (+0, constructor state): vanish / appear transition |
| `creature_genie_s67_update` | creature_genie_s67_update_1caf0 | 67: hidden wander, regeneration, enemy / mana search |
| `creature_genie_s68_update` | creature_genie_s68_update_1ce80 | 68: visible attack (class-9 type-8 shots) |
| `creature_genie_s69..71_update` | 1d1f0 / 1d200 / 1d210 | follow / die / dead |
| `genie_vanish` (exported) | 1d220 | target = 0, aux = 0, state 66, sound 0xb |
| `genie_appear_at_target` (exported) | 1d270 | moves the genie to target pos + (speed_cur << 6) along the target's yaw, state 66 |
| `genie_steal_mana` (exported) | 1d310 | nearest type-0x27 mana ball in sight while mana < mana_total |
| `creature_type15_s91_update` | creature_type15_s91_update_1ea60 | 91: grid walk, enemy search (other owner only) |
| `creature_type15_s92_update` | creature_type15_s92_update_1ecd0 | 92: stand and shoot arrows (class 9 type 0xd) |
| `creature_type15_s93..95_update` | 1eee0 / 1eef0 / 1ef00 | follow / die / dead |
| `type15_set_attack_sprite` (exported) | 1ef10 | one rng draw, speed 0, sprite 1 (rng % 20 > 10) or 0xce |
| `type15_set_move_sprite` (exported) | 1ef50 | speed_base, sprite 0 |
| `type15_move` (exported) | 1ef80 | the grid walk; weights at 0x1ea40, jump table 0x1ef6c |
| `creature_wyvern_s96_update` | 1f200 (named `creature_type15_s96_update`) | 96 (+0): `creature_idle_seek_leader(t, 0x60)` |
| `creature_wyvern_s97_update` | creature_wyvern_s97_update_1f210 | 97: ai_step + nearest village (wizard_list) every think_period + 1 ticks |
| `creature_wyvern_s98_update` | creature_wyvern_s98_update_1f2e0 | 98: attack, 15-fireball bursts |
| `creature_wyvern_s99..101_update` | 1f660 / 1f670 / 1f680 | follow / die / dead |
| `creature_noop_update` | creature_update_shared_1f690 / 1f6a0 / 1f6b0 | 102..119 (`ret`; the records are disabled) |

Table: `g_type15_dir_weights` (4 x u16 at 0x1ea40 = 7000, 7000, 10, 7000).

Behaviour summary (from the disassembly):

- **Emu**: the archer's arrows, but the plain `creature_ai_step`. It has no "wanted" test, so it
  attacks any player-list thing in sight.
- **Genie**:
  - State 66, first tick (aux 0): 12 class-10 type-1 puffs on a 3 x 4 grid of 0x28 starting at the
    genie (genie's owner, flags |= 0x10000), then aux = 1 and `flags ^= 1`.
  - State 66, second tick: sound 0x15. If now hidden (bit 0 set), it forgets the target and the
    slot-0 attacker, teleports by `0x3200 + (rng % 60) * 0x100` in x and y (two draws) and goes to
    state 67. If visible, it goes to state 68.
  - State 67 (hidden): on each think tick (desc 0x96cf0: period 10, sight 0x1e00) it regenerates
    max_health / 64, clamped to -1..max. While awake and above 1/4 health, an enemy in sight / fov
    makes it appear in front of that enemy; otherwise it steals a mana ball. Then it makes the random
    turn. Above 3/4 health, the first player-list thing that holds mana becomes the target and it
    appears there. A hit by a class-3 thing also makes it appear in front of the attacker.
  - State 68 (visible): below 1/2 health it vanishes. If the target is dead, deleted or gone, it
    steals mana and vanishes. Otherwise it aims every 8 ticks. On each think tick: out of sight ->
    vanish; else aux++, sound 0xb every 8th think tick, and a class-9 type-8 projectile (impact
    10/0x19, damage 3000, aux 0x14, raised by ext_h, aimed) followed by sound 9. The vanish does not
    end the tick, so a genie that vanishes on a think tick still fires that tick (as the original does).
- **Type 15** (desc 0x96d10: terrain mask 0x20000 = castle cells only, period 15, sight 0xf00): the
  guards that walk the castle walls (they appear when castles are built).
  - Every 8 ticks: if the cell under it is not castle, it goes straight to state 94. Otherwise each of
    yaw, +0x200, +0x400, +0x600 gets one draw. A step of 0x100 that passes
    `creature_check_terrain(.., 1)` scores `rng % weight + 2`; the strictly best score (16-bit compare,
    starting at 1) sets yaw directly.
  - Every 16 ticks: the coordinate across the heading snaps to the cell centre (+0x80).
  - It turns away from a same-type creature of another owner within 0x100 in x and y.
  - While target_yaw != yaw, an extra draw skips the step when rng % 20 > 10.
  - It steps speed_cur along yaw with `pos_follow_ground`.
  - States 91 / 92 work like the archer's, but only against things of another owner. In state 92 a
    hit does not retarget.
- **Wyvern** (desc 0x96d30: period 40):
  - State 97: `creature_ai_step`, plus the nearest village (cfg wizard_list, i.e. effect 0x2d, xy
    distance within sight) every 41 ticks.
  - State 98: it aims every 8 ticks (a non-player target only from 0x200 away). On each think tick: a
    target outside sight (xy squared) -> back to 97; sound 0x27 every 2nd think tick; facing the
    target within 0xe3 arms aux = 15.
  - While aux > 0 it fires one class-9 type-0 fireball per tick (desc 0x96a50, impact 10/0, damage
    3000, mana 60000, raised by 4 * ext_h, the wyvern's filter).

## Verification (numbers from `creatures3_test`)

1. **Construction tests** (hand-computed from the disassembly), all passing:
   - `type15_set_attack_sprite` against concrete LCG values: seed 0 -> 0x24df -> sprite 1; then
     0x546b11e -> 0xce; seed 3 -> 10 = boundary -> 0xce; seed 7 -> 1.
   - `type15_set_move_sprite`; `genie_vanish`; `genie_appear_at_target`: no target -> no-op;
     speed 0x3c along yaw 0x200 -> +0xf00 in x and relinked in the new cell.
   - Genie state 66: 12 puffs exactly on the grid with owner and flag, no draw on the first tick;
     teleport offsets from two draws; hidden -> 67, visible -> 68, countdown otherwise.
   - Genie state 67: regeneration +312, the clamp, the two wander draws, the mana-holder target above
     3/4 health, a class-3 hit -> appear, a lethal hit -> 70 with killer.
   - Genie state 68: a shot with all fields; vanish below half health (still shooting); out of sight
     -> vanish without a shot; a hit retargets without moving.
   - `genie_steal_mana`: mana added, ball deleted, `mana_owner` cleared, type-0 effect at the ball,
     nothing when full.
   - `type15_move`: no draw on plain ticks; the turning draw (seed 3 moves, seed 0 does not); a
     forbidden cell -> 94 without a draw; heading choice and draw count checked against an
     independent simulation of the scoring; the snap on tick 16.
   - Type 15 states 91 / 92: enemy in sight -> 92 with attack sprite and speed 0; same owner
     ignored; a hit from another owner -> 92; arrow fields; aim every 4 ticks; out of sight or a dead
     target -> 91 with the move sprite; lethal -> 94.
   - Wyvern 97 / 98: the search only on tick % 41 and the nearest village; the burst (aux 15 -> 15
     fireballs, one per tick, all fields); no burst when not facing; aim, gone, out of sight -> 97;
     lethal -> 100; class-3 hit retargets.
   - Emu 62: an arrow with damage 250; a dead target -> 61.
2. **Snapshot**: none of the four types is in `gam00000.dat` (creatures by type: archer 2, skeleton
   157, builder 2, trader 8), so no field-level ground truth exists. The sim_test-style replay with my
   handlers registered gives creatures 128 / 169, effects 51 / 74, projectiles 0 / 4, spells 13 / 39.
   That is unchanged from round 3: none of my types exists in those 412 ticks.
3. **Movie from the snapshot** (8551 ticks, 34201 / 34201 packets):
   - Wyverns appear at tick 4628 and type-15 guards at tick 7044.
   - States seen (ticks present / peak count): 91: 673 / 4, 94: 7 / 1, 95: 37 / 4, 97: 3916 / 3,
     98: 2149 / 3. Peak class-9 counts: fireballs (type 0) 26, arrows (0xd) 8.
   - Pool / cell checks are clean every 1000 ticks; per-tick invariants have 0 violations.
   - There are 22 guard-ticks off castle cells (the castle under them changed). Each guard died
     within 8 ticks, as `type15_move` prescribes: states 94 / 95 were reached.
   - Census over time (my types): 0 up to tick 4000; 97:3 at 5000 / 6000; 97:2 98:1 at 7000;
     97:3 at 8000; 97:1 98:2 at 8551.
4. **Levels, 3000 ticks each, no input**:
   - Level 24: 5 wyverns. 97:5 at start; 97:4 98:1 at 1000; 97:2 98:3 at 2000 and 3000. Over the run:
     97 in 3000 ticks, 98 in 2610 ticks, peak 4 attacking.
   - Level 44: emu 18 + 1 wyvern at start; type 15 from tick 23.
     - Census: 61:10 63:7 91:4 97:1 at 1000; 61:9 63:6 91:4 97:1 at 3000.
     - Every emu state is reached (61 / 62 peak 8 / 63 / 64 / 65).
   - Genie scan over `sim_load_level(0..69)`: genies alive at level start on levels 16, 20 (3), 22,
     34, 42, 43, 46 (2), 49 (5). Another 30 levels hold genie records that wait for a switch (e.g.
     25: 13, 49: 11, 23: 9).
   - Level 16 (first genie level): states 66 (7 ticks), 67 (19 ticks), 68 (2974 ticks), peak 1 genie
     shot in flight. The genie appears in front of the idle local player and keeps shooting, because
     the hovering player never leaves its sight.
   - On every run, `thing_dispatch_report` lists none of my states. What it does list belongs to
     other tasks: AI wizard 0x11de0, crab 0x1a830, kraken 0x1afb0, troll 0x1b410, griffon 0x1b560.
     `check_pool` is clean.

What is only translated or smoke-tested: everything at the behaviour level. The construction tests
pin the arithmetic and the RNG draw order, but there is no per-tick reference for these types yet.
Task E's per-tick dump of movie 0 would cover the wyverns (from tick 4628) and the type-15 guards
(from tick 7044).

## Deviations and gaps

- As in creature_common: `tick % period` with a zero period would fault in the original; the port
  treats it as "never". All four descriptors have non-zero periods.
- I did not register the emu's state 60 or type 15's state 90 again (creatures.cpp has them). I did
  register the wyvern's +0 state 96 (0x1f200), which no one had.

## Hooks

None declared, none installed. Everything I call is in creatures.h, thing.h or spatial.h
(`creature_check_terrain`), or goes through `thing_create`.

## Extra functions outside the range

None. 0x1d220 / 0x1d270 / 0x1d310 and 0x1ef10 / 0x1ef50 / 0x1ef80 are inside the briefed range and are
exported from creatures3.h.

## TODO(port) call sites

None. The sounds go through `sound_request` (0x15, 0xb, 9, 0x27).

## Requested shared-file changes

1. `tests/creatures3_test.cmake` could switch to `${MC_SIM_ALL}` + `sim_register_gameplay()` once every
   round-4 file compiles (see the note at the top).
2. `mc_types.h` `Thing.flags`: bit 0 is toggled by the genie (set while hidden in state 67, clear in
   68). It is also set on the local player's flyer (player.cpp:383), so it probably means "do not draw"
   for the renderer; this is not verified. Bit 0x10000 is set on the genie's puff / theft effects
   (`or byte [eax+0x12], 1`); its meaning is unknown.
3. `gen/dispatch_tables.h` names, off by one type (binding is by address, so this only matters for
   reading):
   - `creature_skeleton_s60_update_1c8e0` is the emu's +0 state.
   - `creature_genie_s72_place_castle_1d540` is the builder's (as port_creatures.md said).
   - `creature_trader_s90_update_1ea50` is type 15's +0 state.
   - `creature_type15_s96_update_1f200` is the **wyvern's** +0 state (`push 0x60`).
   - 61..71 and 91..95, 97..101 are named correctly.
   - `creature_update_shared_1f690/1f6a0/1f6b0` (102..119) are bare `ret`s with enabled = 0.

## Corrections to ENGINE.md / agent notes

- **Type 15** is a castle-wall guard archer: terrain mask 0x20000 (castle textures 0x15 / 0x16 /
  0x18), so it dies when the castle cell under it goes away. Its movement is grid-based (`type15_move`
  above), not `creature_move_step`. It only attacks things of another owner.
- **Genie** is a stealth type: hidden (Thing.flags bit 0) while wandering and regenerating. It
  teleports next to whoever hits it, whoever is in sight, or (above 3/4 health) the first player
  thing that holds mana, and shoots class-9 type-8 bolts. It vanishes again (teleport by
  0x3200..0x6f00 in x and y) below half health or when the target leaves sight. It also eats
  type-0x27 mana balls up to `mana_total` (= 2 * mana at creation).
- **Wyvern** hunts villages (class 10 type 0x2d) on its own every 41 ticks, besides
  `creature_ai_step`'s player-list targets. It fires bursts of 15 homing-descriptor fireballs
  (desc 0x96a50) once it faces the target within 0xe3.
- **Emu** = archer arrows (`creature_attack_arrow_194a0`) with the generic `creature_ai_step` (no
  "wanted" test).
- The weights at 0x1ea40 are {7000, 7000, 10, 7000}: turning around (+0x400) is almost never
  chosen.
