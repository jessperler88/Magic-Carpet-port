# Thing core port (thing.cpp) - integrator notes, 2026-10-06 (round 2)

Files: `src/mcengine/thing.h`, `thing.cpp`, `src/mcengine/tables/thing.tables` -> `gen/thing_tables.h`,
`tools/port/gen_dispatch.py` -> `src/mcengine/gen/dispatch_tables.h`, `src/tests/thing_test.cpp` + `.cmake`.
Shared-header changes: `Pos` struct, `Thing.home` (+0x96), `LevelData.things[1999]` + `player_block`,
`GameState.creature_count` (u32) / `spells_present` (u32[24]) in `mc_types.h`; mutable `g_sprite_desc[285]`
and `g_video_mode_flags` in `mc_globals.*`; `engine_load_level` mirrors level_load_and_init_3d3b0 and
`engine_load_snapshot` loads the demo recorder's state pair.

## Functions translated (from the disassembly)

| port | original |
|---|---|
| `thing_pool_reset`, `models_initialise` | thing_pool_reset_35460, models_initialise_354c0 |
| `thing_alloc`, `thing_free`, `thing_free_all`, `thing_free_count`, `thing_mark_delete` | 35560, 3e3f0, 3e030, 359b0, 3e3e0 |
| `thing_link_cell`, `thing_unlink_cell`, `thing_move_to`, `thing_relink_cell` | 3e250, 3e330, 3e1d0, 3e220 |
| `thing_set_sprite` (also 352d0), `_double`, `_halved`, `thing_set_extents`, `thing_restore_health`, `thing_set_mana_from_health` | 35240, 35340, 35380, 353d0, 35080, 35230 |
| `sprite_table_init_sizes` | sprite_table_init_sizes_4bd10 |
| `thing_create`, `thing_update_all` | thing_create_35690, thing_update_all_3dce0 |
| `level_run_terrain_effects`, `level_spawn_thing_record`, `switch_activate` | 34fa0, 35800, 356e0 |
| `terrain_height_at`, `terrain_height_at_offset`, `terrain_cell_flag_bit`, `terrain_type_mask_at`, `terrain_slope_vector` | 10bc0, 10be0, 103d0, 10480, 3e4b0 |
| `math_atan2`, `math_rotate_offset`, `pos_follow_ground`, `pos_sink_or_follow_ground`, `pos_angle_to`, `pos_pitch_to`, `pos_pitch_from_dz`, `pos_dist_*` (6), `angle_diff`, `angle_turn_dir`, `angle_turn_step`, `math_bbox_overlap`, `thing_collide`, `thing_find_in_sight_of_class`, `thing_set_state`, `thing_anim_advance` | 4cc33, 3e420, 3e560, 3e5f0, 3e6b0, 3e6e0, 3e710, 3e730..3e970, 3e770, 3e7a0, 3e800, 10530, 105c0, 3e9a0, 3ea50, 3ea70 |
| `thing_relink_snapshot` | (port only; replaces demo_relink_state_pointers_3dc10) |

## Dispatch in the port

`gen/dispatch_tables.h` holds every Table A / Table B record of the 13 classes (index, original handler
address, enabled flag, name). Port functions are bound by original address
(`thing_register_update` / `thing_register_create`), each subsystem from its own
`*_register_handlers()` called by `engine_init`. A record without a bound function counts as an empty
handler / failed create; `thing_dispatch_report()` lists what was hit. Calls between subsystems that
are developed separately go through null-default function-pointer hooks (`g_hook_*` in thing.h).

## Verification (`thing_test.exe`, exit 0)

- Pool: allocation order 1,2,3.., defaults, LIFO free stack, exhaustion -> null, recycling through the
  0x20400 "active" stack. Cell lists: head insertion, unlink from the middle, move within / across cells.
- `math_atan2` on the 8 principal directions (0 = -y, 0x200 = +x), angle helpers, rotate offset, bbox.
- Dispatch: bind by address, create through Table B, list building, tick increment, delete on a bad
  state, deferred free.
- **Snapshot** (`movie/gam00000.dat` + `map00000.dat`): after `thing_relink_snapshot` all 449 live
  things have valid descriptor indices / player offsets, live + free = 999, every free-stack entry is a
  class-0 slot, all 443 cell-linked things sit in exactly the cell list of their position with
  consistent prev links, the 4 player things point at their own P block, player 0's descriptor is
  MoveDesc 7, and `thing_set_sprite` reproduces the stored extents and frame count of all 150 scenery
  things (which validates `sprite_table_init_sizes` against tmaps.dat).

## Facts established / corrections for ENGINE.md and carpet_types.txt

- Game functions take **all arguments on the stack** (caller cleans up); the "Thing in EBX" notes in
  ENGINE.md describe the callee-saved register the caller keeps its thing in, not an argument.
- Level file: only **1999** THING_INIT records (0x442..0x90d0); the following 0x6c0 bytes are 8
  per-player blocks of 0xd8 bytes (GameState+0x385d3 + p*0xd8, castle position at +4). FORMATS.md's
  "2095 x 18-byte THING_INIT" counts those blocks as records.
- GameState+0x38c9f is a u32 creature count (level records of class 5 except models 9, 0xc..0xf);
  +0x38ca3 is u32[24] = number of class-12 records per spell.
- thing_set_sprite_small_352d0 is functionally identical to thing_set_sprite_35240.
- The sprite descriptor table in the exe has `half_xy` = 0 and `draw_type` = 0 for every sprite; both
  are filled at load time from the tmap (w * half_z / h, header byte 1). Thing.draw_type =
  DAT_00094344[desc.draw_type] is the last animation frame (thing_anim_advance compares frame < it).
- angle_turn_step_3e800 takes 4 arguments (cur, target, unused MoveDesc+4, max step = MoveDesc.turn_min).
- math_atan2_4cc33(dx, dy): octant lookup in the 0x9b3ec table, 0 = -y, 0x200 = +x, result 0..0x800.
- pos_dist_manhattan sign-extends before subtracting (no wrap); the other distance helpers subtract in
  16 bits first (wrap around the 256-cell torus).
- terrain_type_mask_at_10480 maps the cell's *texture id* (0..0x22) to the MoveDesc.terrain_mask bits
  (jump table at 0x103f4; ids > 0x22 -> 0x800000, ids 13/14 -> 0).
- thing_alloc's recycle path drops all per-tick lists (the victim may be in one).
- movie/gam00000.dat was taken 413 ticks after level start (PlayerRec.tick) by a build whose MoveDesc
  table sits 4 bytes later; demo_relink_state_pointers_3dc10 only rebases descriptors (anchor: player
  0's thing = descriptor 7) and the four player things' P pointers - every other pointer in a saved
  state is only valid when the block is loaded at the same address.

## Deviations

- Table indices are bounds-checked (the original reads past the tables for bad class / state / type).
- level_run_terrain_effects deletes a thing whose handler is not ported instead of looping forever.
- thing_update_all's "bad state" message is not formatted (it only went to a stack buffer).
