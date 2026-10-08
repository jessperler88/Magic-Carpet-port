# Port round 3 - integrator core (spatial queries, damage, castle sites, sim glue)

Written before the parallel round-3 tasks so they share one query / damage layer. Briefing:
`docs/port/BRIEFING_round3.md`.

## Translated

`src/mcengine/spatial.cpp` (declared in `spatial.h`):

| Port function | Original |
|---|---|
| `thing_find_collision` | thing_find_collision_105f0 |
| `thing_find_mana_near` | thing_find_mana_near_10730 |
| `thing_find_mana_ball_touching` | thing_find_mana_ball_touching_10870 |
| `thing_find_collision_other_owner` | thing_find_collision_other_owner_10980 |
| `thing_exists_near_pos` | thing_exists_near_pos_10ac0 |
| `thing_area_damage` | thing_area_damage_10d20 (both branches: slot 0 and slots 1..5) |
| `thing_area_damage_fire` | thing_area_damage_11160 |
| `thing_area_damage_quake` | thing_area_damage_11450 |
| `thing_try_damage` | thing_try_damage_116e0 (dead code in retail) |
| `thing_add_pending_damage` | thing_add_pending_damage_117c0 |
| `creature_check_terrain` | creature_check_terrain_102b0 |
| `terrain_max_corner_level` | terrain_max_corner_level_10c30 (dead code) |
| `terrain_minmax_along_path` | terrain_minmax_along_path_10cb0 (dead code) |
| `cell_kill_things` | cell_kill_things_3da30 (jump table 0x3da04: class 2 -> delete, class 5 -> kill) |
| `thing_z_add_half_height` / `_sub_` / `thing_aim_at` | 43ea0 / 43ec0 / 43ee0 |
| `castle_spell_reset_charge` | castle_spell_reset_charge_41310 |

`src/mcengine/level_features.cpp` (castle site tests, next to the footprint code they use):
`castle_near_thing` (11820, dead code), `castle_crush_wizards` (118c0), `castle_footprint_clear`
(11980), `castle_site_clear_at_pos` (11be0). Not translated: `dead_castle_site_search_11cef`
(unreachable fragment behind `stub_return_zero_11ce0`).

Glue: `sim.h` / `sim.cpp` (renderer-free init, level load, snapshot load, split out of engine.cpp),
`sound_request` / `sound_fade` wrappers and their hooks in thing.h (moved from player.h), the spiral
walk exported from terrain_paint.h, `${MC_SIM_CORE}` in CMakeLists.txt.

## Facts established from the disassembly

- The four spiral searches all walk rings `0 .. (ext_x + 0xff) / 256` (idiv) around the cell of
  `(x + 0x80) >> 8`, `(y + 0x80) >> 8` and call `thing_collide(searcher, candidate)`.
- The slot-0 area walks use a different centre: `(x - 0x80) / 256` (idiv, truncating toward zero), a
  square of `+-(ext_x + 0xff) / 256` cells; the non-zero-slot branch of 10d20 uses `(x + 0x80) >> 8`.
  Check order differs too: slot 0 = owner, overlap, `prop_flags & 1`, flag 8, not a castle, filter;
  other slots = owner, class != 0, flag 8, `prop_flags & (1 << slot)`, filter, overlap.
- Castles (class 3 type 2) are damaged only through `g_cfg->player_list`, never through the cell
  walk (their box is larger than the cells the walk visits). 11450 sets the hit timer `+0x32 = 0x1e`
  on every castle the box touches, the dealer's own included, before the owner test.
- Slot write, area functions: `attacker != 0 ? amount += x : amount = x`. `thing_add_pending_damage_117c0`
  and `thing_try_damage_116e0` do the opposite (`attacker == 0 ? amount += x : amount = x`). Both as
  in the code; the second looks like a bug in the original but it is what the retail game does.
- `test byte [ebx+0x12], 1` at 0x10d46 (flag 0x10000, "no area damage" in ENGINE.md) is dead: nothing
  branches on it. The flag has no effect inside 10d20.
- `creature_check_terrain_102b0(t, pos, flags)`: flag 2 compares `pos.z` with ground + `desc+0xc`
  (lower bound) and ground + `desc+0xa` (upper bound), so MoveDesc `clear_hi` (+0xc) is the *minimum*
  and `clear_lo` (+0xa) the *maximum* height above ground (the names in mc_types.h are swapped);
  flag 1 returns the offending type-mask bits; flag 4 limits the pitch from the thing to `pos` by
  `desc+0x12` (upwards) / `desc+0x10` (downwards) - `MoveDesc.unk12` / `unk10` are the pitch limits.
- `castle_footprint_clear_11980` tests four border strips of the grown footprint for built-on cells
  (flags bit 7). Strip 3 and 4 use `dy` rows (not the remaining height) and strip 4 restarts each
  row at the left edge instead of its own start column - kept as in the original.
- `cell_kill_things_3da30` compares the sign-extended owner with the zero-extended argument.

## Verification

`tests/spatial_test.cpp` (in ctest as `spatial_test`), on a generated level 38 and on the snapshot:
471 + 443 probe boxes placed on live things - a collidable thing under the probe is always found,
no hit without an overlap, class filters respected, mana balls found exactly where one overlaps
(80 / 79); damage accumulation, owner exclusion, damage-type masks, the reversed 117c0 write and
`cell_kill_things` by construction; 218 of 221 creatures of level 38 stand on terrain their own
MoveDesc mask accepts; castle site test rejects a block containing a built-on cell and accepts
217 / 256 grid positions; `castle_footprint_clear` leaves the extents of the snapshot's two castles
untouched (both may grow). The area functions replaced the private copy in level_features.cpp;
`features_test` (cell-by-cell comparison with the engine's map dump) still passes unchanged.

Not verified against the original: the exact set of cells a spiral ring covers relative to the box
(taken from the search.dat walk of round 2), and the non-zero damage slots (no user ported yet).

## Corrections for other files

- `mc_types.h` MoveDesc: `clear_lo` / `clear_hi` meaning as above; `unk10` / `unk12` = max pitch down / up.
- ENGINE.md Thing flags: 0x10000 is tested but ignored in thing_area_damage_10d20.
