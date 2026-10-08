# Level-start features and run-time terrain painting - port report (round 2, agent A, 2026-10-06)

Files: `src/mcengine/level_features.h/.cpp`, `src/mcengine/terrain_paint.h/.cpp`,
`src/mcengine/tables/features.tables` -> `gen/features_tables.h`, `src/tests/features_test.cpp` + `.cmake`.
No shared file was touched. Build: `cmake --build ../build_A --config Debug --target features_test`, zero
warnings; `features_test.exe <game dir>` exits 0 (add any second argument for the per-castle listing).

## Result in one paragraph

With `terrain_generate_features()` linked, level 38 matches the engine's dump on **64088 / 64969 / 64992 /
65137** of 65536 cells (height / light / flags / type); the generator alone gives 56029 / 57884 / 59202 /
59802. Every one of the remaining 1448 height cells lies in a region that play changed during the 413
ticks before the dump, and replaying those events with this port's own handlers brings the maps to
65509 / 65473 / 65342 / 65505: the 27 heights still open all belong to one player castle that is under
attack in the dump. All 17 wizard castles that survive in the snapshot have the same thing index,
position, z, castle_size, extents, home, state, health, flags and rng as the generated ones. This needs
`g_video_mode_flags != 1`; **the port's current default of 1 halves the castle footprints and only
reaches 59741 / 62876 / 61828 / 64421** (see "Requests").

## Functions translated (all from the disassembly)

`terrain_paint.cpp`

| port | original |
|---|---|
| `terrain_slope_orientation` | terrain_slope_orientation_31f90 |
| `terrain_paint_cell` | terrain_paint_cell_32150 (jump table 0x3210c, slope tables 0x94218..0x94337) |
| `terrain_set_quad_texture` | terrain_set_quad_texture_32430 |
| `terrain_retexture_rect`, `terrain_retexture_rect_force` | 324e0, 32760 |
| `terrain_cell_is_nonland` | terrain_cell_is_nonland_3d5f0 |
| `terrain_modify_cell` | terrain_modify_cell_3d620 |
| `terrain_set_cell_height` | terrain_set_cell_height_3d7d0 |
| `terrain_find_cell_spiral` | terrain_find_cell_spiral_3d940 |
| `terrain_modify_cells_spiral` | terrain_modify_cells_spiral_23f20 |
| static `spiral_search_init / _begin / _next` | 101b0, 10080, 10120 (10100 `_end` only releases a slot) |

`level_features.cpp`

| port | original |
|---|---|
| `terrain_generate_features` | terrain_generate_features_34db0 |
| `level_spawn_effect_record` | level_spawn_effect_record_34e00 + level_spawn_effect_direct_34ed7 |
| `level_build_linked_feature` | level_build_linked_feature_34c40 |
| `level_build_wall / _path / _canyon / _ridge` | 342e0, 0x34570, 0x346b0, 0x34760 |
| `level_spawn_terrain_effects`, `level_flag_terrain_effects` | 34d60, 34f40 |
| `math_wrap_diff`, `terrain_set_pos_scratch`, `terrain_rect_height_range` | 34250, 34280, 34820 |
| `terrain_smooth_castle_border`, `terrain_smooth_rect`, `terrain_smooth_cell` | 348b0, 34a00, 34a40 |
| `terrain_height_avg`, `terrain_rect_min_height` | 34b40, 34bb0 |
| `effect_wizard_init` (installed as `g_hook_effect_wizard_init`), `thing_set_castle_extents` | 35090, 353f0 |
| `castle_stamp_footprint` | castle_stamp_footprint_26320 |
| update handlers (bound by address) | 23d30 delete, 23dc0 volcano, 23ec0 dent (type 0xa), 23fc0 crater, 24fc0 / 24eb0 / 250b0 wall piece north / south / east, 251e0 path piece, 25270 canyon digger, 252f0 ridge raiser, 26680 castle build (states 0x30, 0x33), 27710 living wizard castle (0x34), 27930 wizard castle collapse (0x35) |
| static helpers of those | terrain_cell_raise_needed_24e20, creature_spawn_random_villager_27660, thing_apply_pending_damage_27f90, slot-0 path of thing_area_damage_11450 and of thing_area_damage_10d20 (`area_damage_slot0`) |
| constructors (bound by address) | 38b70 volcano, 38bd0 type 0xa, 38c40 crater, 392a0 wall piece (0x1b), 39300 wall, 393c0 path, 39360 path piece (0x1e), 39470 canyon, 39420 canyon digger (0x20), 39930 wizard, 39540 ridge node, 394d0 ridge raiser (0x33) |

Data: `g_slope_tex_tables` u8[0x120] at 0x94218 (all seven slope tables of terrain_paint_cell).
The castle "size table" behind the pointer at 0xadfb0 is not in the exe: the resource list at 0x96e48
loads **data/building.tab** into it (69 entries `{u32 offset into building.dat, u8 w, u8 h}`), and the
fix-up list at 0x97468 adds the base of **data/building.dat**. `level_features_load_data(game_dir)`
loads both plus **data/search.dat** (the spiral ring image of 101b0, name string at 0x90004).

## Verification (`features_test`)

Stage A - level 38 generated as `engine_load_level` does, compared with `movie/map00000.dat`:

| | height | light | flags | type |
|---|---|---|---|---|
| terrain_build alone (measured) | 56029 | 57884 | 59202 | 59802 |
| + terrain_generate_features, `g_video_mode_flags` = 1 | 59741 | 62876 | 61828 | 64421 |
| + terrain_generate_features, `g_video_mode_flags` = 8 | **64088** | **64969** | **64992** | **65137** |
| cells still different (mode 8) | 1448 | 567 | 544 | 399 |

Where the 1448 / 567 / 544 / 399 different cells are (each cell goes to the first event whose reach
covers it; nothing is left over):

| play-time event | height | light | flags | type |
|---|---|---|---|---|
| player castle #485 at (172,48), grown to level 3 by its AI owner | 376 | 0 | 80 | 80 |
| player castle #895 at (0,0), level 2, created during play (tick byte: 92 updates old) | 78 | 62 | 46 | 47 |
| wizard castle #841 at (6,213) size 28, placed by a builder creature | 384 | 169 | 114 | 52 |
| wizard castle #906 at (49,98) size 29, placed by a builder creature | 399 | 179 | 146 | 56 |
| wizard castle #3 at (247,133), destroyed during play | 60 | 22 | 23 | 25 |
| wizard castle #10 at (12,137), destroyed during play | 86 | 55 | 48 | 50 |
| volcano records 60 / 61 at (4,147), (2,147): DisId 0, erupt in ticks 1..18 | 64 | 78 | 77 | 79 |
| three impact dents at (254,241), (18,138), (19,137) | 1 | 2 | 10 | 10 |

The dent at (254,241) is the "3x3 class-1 patch" port_terrain.md left open.

Wizard castles (class 10 type 0x2d): 19 generated, 19 in `gam00000.dat`. 17 are still there, at thing
indices 1, 2, 4..9, 11..17, 364, 365 in both (the indices prove the allocation order of the ~350 wall /
canyon / path things created in between), and position (x, y, z), castle_size, all four extents, home,
state 0x34, health, max_health, damage, flags, prop_flags, sprite, owner and rng are equal for all 17.
Generated #3 and #10 were destroyed during play; snapshot #841 and #906 have no level record.

Stage B - replay of those events with this port's handlers (evidence for the handlers; the inputs noted
in brackets are read off the snapshot, they are not derived):

| replay | result |
|---|---|
| B1 volcano records 60 / 61 through `level_spawn_thing_record` + 64 `thing_update_all` [thing indices 52 / 53: #53 is the owner of the type-0x12 effect the volcano left] | all 64 / 78 / 77 / 79 cells reproduced, flags included, so `g_rng16` is still in sync after 18 ticks of play |
| B2 `thing_create(10, 0x2d)` + `effect_wizard_init` + 31 ticks [position and size of #841, #906] | all 783 heights, 348 lights, 108 textures reproduced; z 6400 / 6272 and extents equal; 119 flag cells differ in the random rotation bits only |
| B3 `castle_stamp_footprint` for sizes 0..aux on thing 0 [home of #485, #895] | #485 reproduced completely (376 / 80 / 80); #895: 27 heights, 62 lights, 46 flags, 31 textures stay open |
| B4 a type-0xa effect with its `rng % 7` depth forced [cell and depth of the three dents] | all cells reproduced (class, texture, light) |
| B5 collapse handler 27930 on #3 and #10 [inhabitant count unknown: 0..40 tried, 0..4 fit] | 0 of 121 and 0 of 143 heights differ; 1 light cell and 29 rotation-only flag cells stay open |

After the replay: 65509 / 65473 / 65342 / 65505 equal. Of the 194 flag cells 148 differ only in the
rotation bits of a texture < 8; the other 46, and every open height, are at player castle #895, which
is damaged in the dump (health 12800 of 20000) and needs the castle code that is not in this scope.

Other checks in the test: `castle_stamp_footprint` and the 30-step build agree on the interior of a
9x9 wizard castle (49 of 49 heights and textures); all 70 levels of levels.dat generate, terminate and
leave only finished wizard castles (665 of 665 records).

What level 38 does not verify: ridges (type 0x32 / 0x33, 300 records on 12 other levels: they run, no
reference), paint kinds 0, 8, 9, 0xa..0xf and 0x14..0x16 (the level's castles use 1, 0x10..0x13), the
damage and claim branches of the living wizard castle (27710), `terrain_set_cell_height`,
`terrain_rect_height_range`, `terrain_rect_min_height`, `level_spawn_terrain_effects` /
`level_flag_terrain_effects`, the state-0x30 exit of the castle build, the area damage.

## Deviations from the original

1. Data files are loaded by `level_features_load_data(game_dir)`; if nobody called it, the first user
   loads from `MC_DEFAULT_GAME_DIR`.
2. Spiral search: the 100 cursor slots at 0xac160 are replaced by a cursor on the caller's stack. Rings
   16..31 are empty in search.dat; the original then reads its heap behind the list, the port reads zeros.
3. `terrain_slope_orientation` leaves DL / CL unwritten when no corner (resp. no second corner) is above
   0. The port takes them as parameters: the castle code passes the DL it really has, kind 0x10 the
   cell's texture, everything else 0xff (result 0 for an all-zero quad). Unverified, sea-level quads only.
4. `level_build_wall` tests `thing_create` for null (the original writes through the null pointer).
5. `level_build_linked_feature` bounds both chain walks by the record count and stops on a record
   number outside 1..1999 (the original loops forever on a cyclic chain).
6. `level_spawn_effect_record` goes through `thing_create_fn`, so a model above 56 creates nothing
   instead of reading past Table B (no level has one; the three type-0x31 records hit an empty record).
7. Footprint walks stop at the end of building.dat; thing indices read from `caster`, `last_attacker`
   and the damage slot are reduced modulo 1000; list walks are bounded.
8. `terrain_smooth_cell` reads the texture map at linear offsets -0x101, -0x100, -1; below cell 0x101
   that is the end of the tables.dat image in the original and `g_tables_image` here (all zero in the
   shipped tables.dat).
9. Wall pieces keep the original's 16-bit loop counters, so a negative length walks 65535 rows as in
   the exe; no level triggers it.

Not ported, marked `// TODO(port)` or left to `thing_create` (null until the owner is linked):
`sound_request_49720` (4 call sites), constructors class 10 type 0x12 (0x39050, the volcano's residue;
7 failed creates on level 38) and class 5 types 4, 0xc, 0xd, 0xe (villagers leaving a wizard castle),
the non-zero-slot branch of thing_area_damage_10d20.

## Hooks and interfaces

- Installed: `g_hook_effect_wizard_init = effect_wizard_init`.
- Declared in `level_features.h`, null by default: `g_hook_player_note_ridge_distance(Thing *)` =
  player_note_ridge_distance_3f2c0, called at the end of the living wizard castle handler.
- For other subsystems: `castle_stamp_footprint(Thing *)` (player_spawn_3f360 calls it on thing 0 for
  sizes 0..level-1), `thing_set_castle_extents`, `castle_footprint(size)`, `terrain_paint_cell`,
  `terrain_retexture_rect(_force)`, `terrain_set_quad_texture`, `terrain_modify_cell`,
  `terrain_find_cell_spiral`, `terrain_smooth_castle_border`, `terrain_smooth_cell`.
- The spiral search (10080 / 10100 / 10120) is `static` in terrain_paint.cpp as instructed; an agent
  that needs the raw iterator has to ask for it to be exported.
- `area_damage_slot0` in level_features.cpp is a private copy of the slot-0 path of 11450 / 10d20; it
  should give way to the real `thing_area_damage` once someone owns it.

## Requests for shared files

1. `mc_globals.cpp`: `g_video_mode_flags` defaults to 1, which halves every castle footprint and extent.
   The recording needs any value other than 1. Decision for the integrator: default it to 8, or split a
   game-logic flag off the render mode.
2. `engine.cpp`: call `level_features_load_data(game_dir)` in `engine_init`, so the game dir does not
   come from the compile-time default.
3. `terrain.h`: the comment "runtime terrain helpers belong in terrain.cpp later" now means terrain_paint.h.

## Corrections for ENGINE.md / carpet_types.txt / mc_types.h

- Castle size table = data/building.tab + building.dat (see above), not exe data. Entries 1..7 are the
  player castle levels (8x8, 21x21, 21x21, 35x35, 35x35, 48x48, 48x48), 8..16 are 1x1, 17..68 the
  wizard buildings; a wizard castle's size is THING_INIT.Parent + 0x10.
- Footprint bytes (26320, 26680): for a byte >= 0xf with high nibble != 3 the height is
  base + (low nibble - 1) * 4, skipped when the low nibble is 0 (ENGINE.md 2.3 says (nib - 1) * 4); the
  paint kind is high nibble + 0xb. High nibble 3: low % 3 = 1 -> base + 0xc, 2 -> base + 0x10, 0 -> no
  height; kind 10 + low / 3. 26320 writes the class with `& 0xf8`, 26680 with `& 0xf0`.
- `DAT_0012edae == 1` halving: the recording was made with a value other than 1.
- search.dat: 32x32 ring numbers, ring 0 = a 2x2 quad whose first cell is the centre. spiral_search_next
  returns 2 together with the last cell of the last ring and every caller tests `== 1`, so that cell is
  never processed (ring 0 alone = an L of 3 cells).
- Class 10 names: state 0x34 (27710) is the living wizard castle, 0x35 (27930) its collapse, not "ridge
  node / type51". Type 0x1b is a wall piece with states 0x1b / 0x1c / 0x1d = north / south / east
  (table names "steal_mana_s27", "type26_s28", "type27_s29" are wrong). Type 0x1e = path piece, 0x20 =
  canyon digger, 0x33 = ridge raiser, 0xa = dent. Types 0x1c, 0x1d, 0x1f, 0x32 are markers whose handler
  23d30 deletes them; the work is done by level_build_linked_feature when SwiId != 0.
- level_spawn_effect_record places things at the cell corner (x << 8), not the centre.
- terrain_modify_cell_3d620 / 3d7d0: new height != 0 -> class 1; height 0 -> class nibble cleared only
  when all 8 neighbours are non-land, otherwise the flags stay (agent4_B: "otherwise class := 1").
- terrain_set_quad_texture_32430 clamps light below 0x20 to 0x20; the retexture functions use
  (v & 3) + 0x1c below 0x1c. Neither adds the generator's noise for flat cells.
- Wizard castle fields: aux (+0x1a) = inhabitants, speed_base (+0x80) = capacity, mana = aux << 8 every
  40 ticks, damage_slots[1].attacker = claiming player, mana_owner (+0x90) = current claimer, duration
  (+0x32) = castle hit timer set by 11450. The retail exe computes the capacity as w * h >> 4 (0x350d9);
  all 17 snapshot castles hold w * h >> 2, one more sign that the recording build differs.
- Wizard castles without a level record come from creature_genie_s72_place_castle_1d540 (a builder).
- level_run_terrain_effects does not increment Thing.tick, so a crater's `tick % 3` test is on its thing
  index for the whole generation (confirmed by the canyon cells matching).

## Open questions

- Is DAT_0012edae really 1 in a normal 320x200 game? If so, castles are half size there and a recording
  made in the other mode cannot be replayed faithfully in it.
- What spawns the type-0xa dents (three in the dump, far from any castle fight in one case).
- Player castle #895: which of the castle states (26b50, 26f10, collapse 42010) leaves the 27 heights.
- The single light cell and the rotation bits at the two collapsed wizard castles (rotation needs the
  `g_rng16` value at the time of the collapse; the light cell is unexplained).
