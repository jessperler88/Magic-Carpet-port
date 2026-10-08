# Thing pool without the 1000 limit (port round 7, task C)

Files: `src/mcengine/thing.h`, `thing.cpp`, `mc_types.h` (`MC_THING_SLOTS_MAX`), `settings.h` section C, the
pool accesses in `ai_wizard.cpp`, `castle.cpp`, `constructors.cpp`, `creatures2.cpp`, `effects.cpp`,
`hud.cpp`, `input.cpp`, `level_features.cpp`, `player.cpp`, `projectiles.cpp`, `scenery.cpp`, `spatial.cpp`,
`spells.cpp`, `net.cpp`; tests `src/tests/pool_test.cpp` + `.cmake` (new), `src/tests/reference_test.cpp`
(`MC_REF_THING_SLOTS`, a "pool:" line). Build dir `build_C`; scratch `<scratchpad>/round7_C`.

## What was built

**Setting.** `PortSettings::thing_slots` (default 1000 = original, clamped to 1000..32768) and
`PortSettings::thing_cap_villagers` (default true, only matters with more than 1000 slots).

**Storage.** `GameState.things[1000]` stays where it is. Slots 1000..N-1 live in a separately allocated
extension (`g_thing_ext`, a `std::vector<Thing>` in thing.cpp). `thing_at(idx)` picks the array,
`thing_index(t)` maps back (address range test), `thing_pool_slots()` = `g_thing_slots`, `thing_in_pool(t)`
replaces the original's `&things[0] < t < &things[1000]` test.

**Stacks.** The free stack and the recyclable stack carry on outside GameState:

| stack | logical position p | stored top |
|---|---|---|
| free (`free_list`, `free_top`) | p < E: extension; p >= E: `GameState.free_list[p - E]` (E = N - 1000) | `GameState.free_top = logical top - E` |
| recyclable (`active_list`, `active_top`) | p < 1000: `GameState.active_list[p]`; p >= 1000: extension | `GameState.active_top` (the game's own `active_top = -1` writes keep working) |

The extension's free slots sit at the *bottom* of the free stack (highest index lowest), so:
- allocation order is 1, 2, 3, ... as in the original, then 1000, 1001, ... once the original's slots are used;
- while the extension is unused, every byte of GameState (free / recyclable stacks included) is exactly what the
  original has. **The game runs byte for byte as with 1000 slots until the original's pool would have been
  full**; from that tick on the extended pool simply has room;
- a 1000-slot image (movie snapshot, quick save, save game, network join) loaded into an extended pool is a
  valid pool once the extension is emptied (`thing_pool_ext_reset()`): its `free_top` is read relative to the
  new size and nothing has to be rebuilt.

**Index fields stay 16 bits, max 32768 slots, not 65535.** The original sign-extends 16-bit Thing indices at
about 40 places (`movsx` of `Thing.owner`, `P.spell_thing[]`, comparisons like
`(int32_t)b->mana_owner != (int32_t)(int16_t)t->owner`, `(int16_t)thing_free_count()` in
castle_spill_mana_41720 / effect_type36_s38_update_257e0, `sound_request((int16_t)index)`). With indices
below 0x8000 every one of them keeps its value, so no translated line had to change; 65535 would have needed
a rewrite of all of them in extended mode only. 32768 is 33x the original and far above anything measured
(peak 2627, below).

**Out-of-range indices** (`thing_wrap`). The port guards indices the original dereferenced unchecked with
`idx % 1000`. Those are now `thing_wrap(idx)`: a pool slot as it is, anything else `% 1000` as before. In the
faithful pool this is the old expression exactly; in an extended pool a garbage value (a sign-extended index,
`g_projectile_null_hit_index` 0xfcd9 of the reference harness) still lands on the same slot, so it cannot make
the two pool sizes differ. Guards of the form `idx < 1000 ? idx : 0` became `idx < thing_pool_slots()`.

**Level generation keeps 1000 slots.** Level 39's terrain-shaping effects run out of the 1000 slots 680 times
while its map is generated (`level_run_terrain_effects_34fa0`); with more slots the effects carve a different
landscape (found by `reference_gen` with 4000 slots: 7038 cells of level 39 differed). `thing_pool_reset()`
(called by the level load) therefore sets the original's 1000 slots, and `switch_activate(0)` - the level
start, right after the generation - switches the extension on. Every level's map is the original's with any
pool size; the level's own Things (THING_INIT records) already get the extended pool (level 17: 22 records the
original drops at its start because its pool is full at tick 1).

**Villager growth cap** (`thing_cap_villagers`, extended pool only). A full town
(effect_ridge_node_s52_update_27710) sends out a random villager now and then (archer / trader / townie /
builder) and nothing but the pool limits them: with an unlimited pool level 34 grew to 8481 Things in 30000
ticks (townies 2785, archers 2159, builders 2008, traders 1011) and kept growing. With the cap a town sends
nobody while 999 or more slots are in use - exactly when the original's pool would have refused the villager -
and level 34 peaks at 1897. Everything else (mana balls, spells, castles, projectiles, effects) has the room.

**Override.** `thing_pool_force_slots(n)` (0 = none) overrides the setting: a network session uses the host's
size, a movie the size it was recorded with (task E's `demo_open` already uses it: original movies play with
1000). `thing_pool_wanted_slots()` = the override or the setting, clamped. Size changes take effect at the next
level start (`switch_activate(0)`) or snapshot load (`thing_pool_ext_reset()`), never in the middle of a level.

**Statistics** (port only, never read by the game): `g_thing_alloc_failures` (thing_alloc returned null),
`thing_pool_live_count()`. The debug overlay's "Thing 164, Active n" (hud.cpp) shows `slots - free`.

**Network** (net.cpp): `net_state_checksum` hashes the pool size, the extension's Things and stack parts when
the pool is extended (the original's pool hashes exactly as before), so peers with different sizes report a
desync at the first exchange. `net_agree_u32(mine, &differs)` (port only, needs a declaration in net.h, see
below) exchanges one value per peer through `net_exchange_block` and returns the host's: the lobby calls it
with the pool size and forces the result on every peer (negotiation, not refusal: everybody plays with the
host's size, a mismatch is printed).

## Every place that assumed 1000 (original addresses)

Pool core (thing.cpp): thing_pool_reset_35460, models_initialise_354c0 (both stacks over all slots),
thing_alloc_35560 (free / recyclable stack through the mapping), thing_free_3e3f0, thing_free_all_3e030,
thing_free_count_359b0 (logical count), thing_link_cell_3e250 / thing_unlink_cell_3e330 (direct indexing ->
thing_at), thing_update_all_3dce0 (three loops), level_run_terrain_effects_34fa0, thing_find_in_sight_of_class_3e9a0,
switch_activate_356e0 (extension switched on).

Loops over the pool: mana_totals_update_427d0, player_commands_process_3a8b0 command 0x1e arg 1 (switch flags),
crab_target_nearest_mana_ball_1aef0, effect_type40_s42_update_25f10 (mana hoard owner transfer),
effect_type58_s60_update_28320 (delayed blast), ui_draw_radar_blips_42a20 (hud.cpp).

Bounded list walks (`i < 1000`, guard counters): spatial.cpp (cell walk, player list), castle.cpp (mana-ball
list x2), effects.cpp (skeleton / mana lists), input.cpp, level_features.cpp (player / wizard lists),
player.cpp (player / mana lists), projectiles.cpp (8 target searches), scenery.cpp, spells.cpp.

Index guards (`% 1000` -> `thing_wrap`, `< 1000` -> `< thing_pool_slots()`): ~55 sites in castle.cpp,
effects.cpp, hud.cpp (15), input.cpp, level_features.cpp, player.cpp, scenery.cpp, spatial.cpp,
ai_wizard.cpp, constructors.cpp, projectiles.cpp, spells.cpp. hud.cpp's two `spell <= &things[0] || spell >=
&things[1000]` tests -> `thing_in_pool`.

16-bit index fields: all u16 (`Thing.next/cell_next/cell_prev/owner/target/parent/child/...`, `PlayerRec.thing`,
`P.castle/balloons/guards/spell_thing/tether_target`, `g_cell_things`) - fine up to 32768 (sign extension, above).
`Thing.tick = (uint8_t)idx`, `aux = idx % 11 / % 100` and the RNG seed `idx + state rng` only take the index as a
number.

Fixed caps that starve spawning when the pool is full (all behave as the original with 1000 slots and only
see a non-zero free count with more):
- `thing_alloc_35560` recycles only from the recyclable stack, which holds anything only between a
  `models_initialise_354c0` and the next `active_top = -1`: player_spawn_3f360 and player_dying_update_405f0
  (the latter drops one entry unused, `active_top--`). Everything else simply fails when nothing is free.
- castle_destroyed_update (player_respawn_start_416d0): with no free slot a destroyed castle does not
  collapse, it bounces between states 4 and 6 - **an enemy castle cannot be brought down while the pool is full**.
- castle_spill_mana_41720: number of spilled mana balls = min(free count, 32) - mana above the castle's
  capacity is lost with a full pool.
- creature_create_dragon_362d0 / creature_create_kraken_36c80: refuse with fewer than 16 free slots.
- effect_type36_s38_update_257e0 (skeleton army): min(free count, 8) skeletons.
- thing_drop_mana_ball_25fe0 (every creature death): no ball, the mana is gone.
- Per-type lists (`Config.creature_lists[20]`, player / projectile / mana-ball / wizard lists) are linked lists
  through `Thing.next`: no cap. PlayerBlock arrays (`balloons[3]`, `guards[34]`, `spell_slot[24]`) are game
  rules, not pool limits. The sprite cache is task A's.

## Verification

All with Debug builds unless noted; game data from `MagicCarpet/magic`.

**Faithful pool, every reference unchanged**: full `ctest -C Debug` in build_C, 45/45 pass (reference_test,
reference_levels, reference_gen, reference_player, reference_player_test, render_reference_test / _hud /
2 / _options / _fe, thing_test, player_test, castle_test, effects_test, hud_test, sim_test, net_test, ...).

**Extended pool against the original's references** (`MC_REF_THING_SLOTS=4000 reference_test ...`, new): 
identical wherever the original never ran out of slots, and diverging exactly in the tick after it did:

| reference | original's pool first full (port, 1000 slots) | first divergence with 4000 slots |
|---|---|---|
| movie 0 | tick 1481 | dump 1500 (dumps every 10 ticks there) |
| level 49 | tick 2 | tick 3 |
| player p1_castle_spells_l0 | tick 1780 | tick 1781 |
| levels 0, 1, 12, 16, 24, 38, 44; players p2, p3, p4 | never | none (byte identical) |
| generation of all 69 levels (`reference_gen`) | level 39 during generation | none (after the generation fix) |

So the original fills its pool even in the shipped demo movie (a burst of explosions around tick 1481).

**pool_test** (`mc_unit_test` with `${MC_SIM_ALL}`, 6 s Debug, exit 0):
1. 4000 slots: allocations 1..3999 in order, null + failure counted when full, LIFO across the boundary
   (frees of 2500, 7, 1000 come back as 1000, 7, 2500), cell lists through extension slots, 1333 recyclable
   Things on a recyclable stack reaching into the extension come back in order; clamping 500 -> 1000,
   70000 -> 32768; the force override.
2. One pseudo-random script (alloc / free / mark recyclable + models_initialise) on 1000 and 4000 slots:
   4031 steps with the same Thing and byte-identical free / recyclable stacks in GameState, up to the step
   at which the 1000-slot pool has no free slot; the next allocation in the 4000-slot pool is slot 1000.
   A 1000-slot image (600 allocated, 72 freed) loaded into a 4000-slot pool with stale Things in the
   extension: `thing_pool_ext_reset` empties it, the image's allocation order follows, then 1000, 1001.
3. Levels 39 and 49 generated with 1000 and 4000 slots: identical GameState, maps and cell lists (39: 680
   failed allocations during the generation), pool 4000 after the start.
4. Level 49 with 1000 slots: 999 in use at tick 2, 71 failed allocations in 200 ticks; at a tick ending with
   a full pool a killed creature drops no mana ball. With 4000: peak 1133, no failure, the ball appears. The
   4000-slot run twice: identical per-tick `net_state_checksum` sequence (cc4de01a).
   **Winning**: level 2 played by the computer for player 0 (players_init_records with every record a
   computer wizard), the map crowded with standing stones until the 1000-slot pool is full (705 stones):
   with 1000 slots 25 allocations fail and the level is **not won** in 10000 ticks (also not in 30000); with
   4000 slots and the same 705 stones nothing fails (peak 1056) and player 0 **wins at tick 7691**.
   Without stones the same player wins level 2 at tick 5813 with either size.
5. Movie 0 played with 1000 and 4000 slots: the GameState is identical after every tick up to playback tick
   1069 (= reference tick 1481), the first tick in which the 1000-slot pool had no free slot.

**Peak Thing counts** (`pool_test <game> peak 30000 <slots> <levels>`, Release, idle local player, the AI
wizards play, 30000 ticks = 20 minutes at 25 Hz). With 1000 slots, 21 of the 70 levels run out of slots;
with 32768 slots (villager cap on) nothing fails anywhere:

| level | 1000 slots: first full at tick / failed allocations | 32768 slots: peak in use (tick) |
|---|---|---|
| 3 | 15891 / 206 | 1004 (21987) |
| 5 | 12209 / 11134 | 1068 (17143) |
| 13 | 22086 / 38392 | 1047 (29033) |
| 14 | 11964 / 266 | 1170 (15963) |
| 17 | 1 / 74 (22 at the level start) | **2627** (2342) |
| 20 | 9800 / 13816 | 1479 (10599) |
| 23 | 398 / 1272 | 1053 (435) |
| 29 | 7169 / 46 | 1187 (7169) |
| 30 | 5363 / 3218 | 1437 (6388) |
| 31 | 14633 / 8404 | 1258 (15621) |
| 34 | 2561 / 255867 | 1897 (15720); 8481 and rising without the villager cap |
| 37 | 19758 / 2562 | 1254 (29440) |
| 39 | generation only (680) | 206 |
| 40 | 18391 / 890 | 1170 (20141) |
| 43 | 158 / 3886 | 1422 (11270) |
| 48 | 15 / 21 | 1020 (16) |
| 49 | 2 / 1278..2275 | 1534 (18849) |
| 52 | 12 / 64 | 1089 (15) |
| 61 | 66 / 188 | 1144 (102) |
| 62 | 5770 / 75812 | 1713 (23672) |
| 65 | 17150 / 114 | 1080 (23051) |

Levels 40-49 (the briefing's range): 40, 43, 48 and 49 hit the limit; peaks with room 1020..1534. Highest
anywhere: 2627 (level 17). The other 49 levels stay below 1000 (e.g. 41: 678, 44: 193, 45: 908).

**Cost** (Release, us per simulation tick, same runs): level 49: 26 (1000) / 32 (4000) / 43 (8192) /
200 (32768); level 34: 62 / 134 / 145 / 311. The pool loops (three in thing_update_all, mana_totals_update,
others) scale with the slot count; even 32768 slots cost 0.2-0.3 ms of a 40 ms tick.

## Deviations / gaps

- Faithful mode: `thing_at` / `thing_index` / the stack accessors are generalised (one branch each) rather than
  separate functions; with 1000 slots they compute exactly the original's values (the references are the
  proof). An index >= 1000 passed to `thing_at` without a guard now dereferences the (null) extension instead
  of reading past `things[]` - both were undefined; no reference hits it.
- hud.cpp's `things[(int16_t)x % 1000]` (a negative index for x >= 0x8000, i.e. reading GameState before the
  pool) is now `thing_wrap((unsigned)(int16_t)x)`; no reference has such a value (pixel references unchanged).
- In an extended pool a garbage index in [1000, N) (only `g_projectile_null_hit_index` of a reference
  harness can produce one; normal play stores 0) reads an extension slot instead of slot 0.
- `thing_relink_snapshot` stays a converter of 1000-slot images (it works on any GameState image, not only
  g_state); the loaders call `thing_pool_ext_reset()` after it (demo.cpp does; sim.cpp needs it, below).
- `net_agree_u32` is not exercised over a real transport by pool_test (only the no-session path); net_test's
  in-memory LAN would be the place. The checksum change is covered by net_test (unchanged pass).
- Not every original "pool full" symptom disappears with the cap on villagers: towns still stop sending
  villagers at 999 in use, as in the original.

## Settings added (settings.h section C)

| field | default | range | meaning |
|---|---|---|---|
| `thing_slots` | 1000 | 1000..32768 (clamped) | pool size incl. slot 0; read at level start (after the generation) and snapshot load |
| `thing_cap_villagers` | true | bool | extended pool: towns send villagers only while < 999 slots are in use |

Proposed **play** defaults (task E's config): `thing_slots = 8192` (3x the highest peak measured, +17 us/tick,
an extended quick save carries 7192 x 164 bytes = 1.2 MB of extension; 4000 also covers every measured run),
`thing_cap_villagers = true`. `--faithful`: 1000.

## Determinism and compatibility

- The pool size is part of the simulation: two runs with the same size are identical (pool_test 4), a
  different size diverges only from the tick the smaller pool runs out.
- **Network**: sizes are negotiated (host's wins, code below); a remaining mismatch is caught by the desync
  checksum.
- **Movies**: original movies play with 1000 slots (E's `demo_open` forces the movie's size; the reference
  harness can override it with `MC_REF_THING_SLOTS`). A movie recorded with more slots cannot play in the
  original; E marks such recordings (E's report: `MCPX` header, `gax`/`max` snapshot names) - nothing else is
  needed from the pool side than `thing_pool_force_slots(header.thing_slots)` before the level loads.
- **Save games / quick saves**: the original's formats hold `GameState` only, i.e. slots 0..999. A save made
  with an extended pool in the original format loses the Things in slots >= 1000 (and their cell-list links:
  `demo_repair_cell_lists` must run). E's full save (`savestate_*`) stores the pool size plus the extension's
  Things and both stack parts (`thing_pool_ext_count`, `thing_pool_ext_free_stack`,
  `thing_pool_ext_active_stack`) and restores `free_top` / `active_top` after `thing_pool_ext_reset` - that is
  the right sequence (`thing_pool_ext_reset` clamps a `free_top` below -1).

## Requested shared-file changes (exact code)

1. `src/mcengine/sim.cpp`, `sim_load_snapshot`, after `ok = thing_relink_snapshot(g_state);`:
   ```cpp
           ok = thing_relink_snapshot(g_state);
           thing_pool_ext_reset();     // a 1000-slot image: the wanted pool size, empty extension (thing.h)
   ```
2. `src/mcengine/net.h`, after `net_exchange_block`:
   ```cpp
   // Port only (round 7): every peer sends `mine`, all adopt the host's value (*differs: some peer had another).
   // Without a session it returns `mine`. Used for the Thing pool size (port_pool.md).
   uint32_t net_agree_u32(uint32_t mine, bool *differs);
   ```
3. `src/mcengine/frontend.cpp`, the lobby start (~line 1811), after
   `g_cfg->level = (uint16_t)(int16_t)(int8_t)g_state->commands[0].arg;`:
   ```cpp
       {   // port (round 7): every peer plays with the host's Thing pool size (port_pool.md)
           thing_pool_force_slots(0);
           bool differs = false;
           const int slots = (int)net_agree_u32((uint32_t)thing_pool_wanted_slots(), &differs);
           thing_pool_force_slots(slots);
           if (differs) std::fprintf(stderr, "net: Thing pool of %d slots (the host's) for this session\n", slots);
       }
   ```
   and where the session ends / single player is restored (`g_cfg->flags &= 0xffef;`, ~line 1953, and wherever
   a finished network game returns to single player): `thing_pool_force_slots(0);`
   (frontend.cpp needs `#include "thing.h"` if not already there).
4. `src/mcengine/frontend.cpp:1660`: `thing_at(rec.thing % MC_THING_SLOTS)` -> `thing_at(thing_wrap(rec.thing))`.
5. `src/mcengine/game.cpp:26`: `thing_at(local_rec().thing % MC_THING_SLOTS)` -> `thing_at(thing_wrap(local_rec().thing))`;
   `game.cpp:268`: `thing_at(P->castle % MC_THING_SLOTS)` -> `thing_at(thing_wrap(P->castle))` (otherwise a castle
   in slot >= 1000 shows the wrong mana on the result screen).
6. `src/mcengine/sound.cpp:118`: `thing_at(g_state->players[g_state->local_player].thing % MC_THING_SLOTS)` ->
   `thing_at(thing_wrap(g_state->players[g_state->local_player].thing))`.
7. Task A, `src/mcengine/render_landscape.cpp:522`: `g_state->things[pl.thing].speed_cur` ->
   `thing_at(pl.thing)->speed_cur` (motion blur reads the local player's speed; a respawned player can live in
   the extension). render_things.cpp already uses `thing_at` / `thing_pool_slots()`.
8. Task E: nothing beyond what demo.cpp / savegame.cpp already do (checked against this design); keep calling
   `thing_pool_ext_reset()` after every 1000-slot image load.

## mcport integration (src/mcport/main.cpp, task D)

- `main.cpp:691`: `g_state->free_top + 1` -> `thing_free_count()` (with an extended pool `free_top` is relative).
- `main.cpp:892`: `thing_at(r.thing % MC_THING_SLOTS)` -> `thing_at(thing_wrap(r.thing))`.
- The setting itself comes from E's config (`g_settings.thing_slots`, `g_settings.thing_cap_villagers`) before
  the first level loads; nothing else to call. Optional: show `g_thing_alloc_failures` / in-use slots in the
  frame-time overlay.

## Next round

- Network: exercise `net_agree_u32` in net_test's in-memory LAN (3 peers, different sizes -> host's size,
  identical checksums), then wire the lobby call (item 3).
- Decide the play default (8192 proposed) with E's config; consider showing the in-use count in the debug overlay.
- The renderer walks every slot in places (task A); with 32768 slots check those loops' cost.
- Level 17's start spawns 22 records the original drops: worth a look in play (a busier start than the
  original's).
