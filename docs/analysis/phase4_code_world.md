# Phase 4 code study: a procedurally generated world larger than 256x256, and an RTS overview camera

Read-only analysis of `src/mcengine` (2026-10-07) for the Phase 4 game mode: a seeded world of 12-16x the
area of a level (768x768 .. 1024x1024 cells or more), generated at match start from a seed and player-tunable
parameters, with a top-down RTS camera. Sources: the port's code (line numbers below are from the current
tree), docs/ENGINE.md 2.3-2.6 / Things 2.1-2.5 / region B 2.1-2.3, docs/analysis/port_terrain.md,
port_render_ext.md, port_pool.md, port_thing.md, port_features.md, port_net.md.

**The short version.** The five maps are plain globals outside GameState and are the *easy* part. The hard
wall is the coordinate type: a world position is `u16 x, u16 y` with the cell in the high byte
(`Thing.x/.y`, `Pos`, `start_pos`, `PosLogEntry`, `Thing.home`), so 65536 units = exactly 256 cells is the
largest world any Thing can be in. Every "bigger world" plan is therefore a 32-bit-coordinate plan, and
the ~110 sign-extending `(int16_t)(b->x - a->x)` torus differences, ~85 `(uint16_t)` position writes and
~75 squared-distance comparisons in int32 are what has to change - not the map arrays. The renderer's
projection has no pitch rotation (pitch is a horizon shift), so a true top-down camera needs a new
orthographic renderer; the existing radar / map screen is already an orthographic 1-cell-per-pixel height
map painter and is the natural seed of an RTS minimap.

---

## 1. Map representation and everything that hard-wires 256

### 1.1 Storage

| what | where | size | inside GameState? |
|---|---|---|---|
| `g_map_type` (texture id per cell, 0xCDFB0) | `mc_globals.h:12`, `mc_globals.cpp:31` | u8[65536] | **no** (static global) |
| `g_map_height` (height byte, world h = byte*0x20, 0xDDFB0) | `mc_globals.h:13` | u8[65536] | no |
| `g_map_light` (0xEDFB0) | `mc_globals.h:14` | u8[65536] | no |
| `g_map_flags` (bits 0-2 class, 3 water anim, 4-6 rotation, 7 built-on, 0xFDFB0) | `mc_globals.h:15` | u8[65536] | no |
| `g_cell_things` (first Thing index per cell; also the generator's i16 scratch, 0x10DFB0) | `mc_globals.h:16` | u16[65536] | no |
| `g_corner_tex_table` (DAT_000b58b0, built by the generator) | `terrain.h:35`, `terrain_gen.cpp:17` | u8[2401][2] | no |

`MC_MAP_SIZE = 256`, `MC_MAP_CELLS = 256*256`, `MC_CELL_UNITS = 256` at `mc_types.h:548-550`. The two canonical
accessors are `mc_cell(x_cell, y_cell) = ((y & 0xff) << 8) | (x & 0xff)` and
`mc_cell_of(x, y) = (y & 0xff00) | (x >> 8)` at `mc_types.h:553-560`. Three files define a private
`cell_xy(uint8_t x, uint8_t y)` clone: `effects.cpp:22`, `level_features.cpp:18`, `terrain_paint.cpp:13`, and
two define `thing_cell_x/y(t) = (uint8_t)(((int16_t)t->x + 0x80) >> 8)` (`effects.cpp:24-25`,
`level_features.cpp:20-21`).

GameState (`mc_types.h:381-424`, 0x38d03 bytes, the block the reference harness compares) holds no map at all:
`things[1000]`, `players[8]`, `commands`, the level file copy (`LevelData`, with 1999 `ThingInit` and the
per-player blocks) and `start_pos[8]`. The maps travel beside it: the demo recorder's `map%05d.dat`
(`demo.cpp:197-217`, 4 x 0x10000 + 0x20000 + corner table), the port's save state `MAPS` chunk
(`savegame.cpp:214, 256-257, 432-437`), `sim_load_snapshot` (`sim.cpp:113-131`) and the reference test's map
compare (`src/tests/reference_test.cpp:209-213`, 0x60000 bytes). The pool extension of round 7 is the
precedent: slots >= 1000 live outside GameState (`port_pool.md`), the faithful image is untouched.

### 1.2 Coordinates and their range

- `Pos {u16 x, u16 y, i16 z}` (`mc_types.h:67-72`); `Thing.x/.y` u16 at +0x48/+0x4a, `Thing.z` i16, `Thing.home`
  Pos at +0x96 (`mc_types.h:155-157, 175`). Cell = `x >> 8`, fraction = `x & 0xff`, 1 cell = 256 units.
  **Maximum representable position: 0xffff = cell 255 + 255/256.** The player is a Thing, so the player
  position has the same range; `GameState.start_pos[8]` is `{u16 x, u16 y, i16 z}` (`mc_types.h:415`); the camera
  log `PosLogEntry.x/.y` is u16 (`mc_types.h:198-200`); `ThingInit.x/.y` are u16 *cell* numbers (`mc_types.h:29-31`,
  spawn position `rec->x * 0x100 + 0x80` at `thing.cpp:500`).
- `Camera.cam_x/cam_y` are `int` (`render.h:11-13`) but both renderers truncate them to 16 bits
  (`render_landscape.cpp:310-312`, `render_ext.cpp:909-910`).
- Height: byte 0..0xc4 -> world 0..6272 units (24.5 cells); `Thing.z` i16 has room (`terrain_generate`,
  `terrain_gen.cpp:124-140`).
- Toroidal wrap, confirmed: cell neighbours are formed by incrementing the two bytes separately
  (`terrain_gen.cpp:5-6` and every `(uint8_t)(x + 1)`), spatial queries mask with `& 0xff`
  (`spatial.cpp:49, 76, 129, 164`), the renderers walk `(uint8_t)` cells (`render_landscape.cpp:363-364, 416-424`,
  `render_ext.cpp:282-284`), and the distance helpers subtract in 16 bits first
  (`pos_dist_chebyshev_xy/sq_xyz/sq_xy`, `thing.cpp:715-733`; `pos_angle_to`, `thing.cpp:692-694`;
  `dist_sq_xy16`, `creatures.cpp:25-29`) so a thing 200 cells east is 56 cells west. `pos_dist_manhattan`
  (`thing.cpp:710-712`) is the one helper that does **not** wrap (original quirk). `mc_wrap_diff`
  (`mc_math.h:35-40`) and `math_wrap_diff` (`level_features.h:37`) default to modulus 256. render_ext states it
  outright: `kWin = 128 // cell offsets -128..127 (one copy of the 256-cell torus)` (`render_ext.cpp:228`).

### 1.3 Inventory of 256-dependent sites

Pattern counts per file (grep over `src/mcengine`; "u8cast" = `(uint8_t)` applied to a cell coordinate,
"and0ff" = `& 0xff` on a coordinate, "shr8" = `>> 8` / `& 0xff00` on a coordinate, "mapsz" = 0x10000 / 0x20000 /
MC_MAP_* / kWin, "wrap16" = `(int16_t)(b.x - a.x)` or wrap_diff, "quad" = `% 0xffff`, `<< 14`, `/ 0x4000`):

| file | mc_cell | cell_xy | u8cast | and0ff | shr8 | mapsz | wrap16 | quad | total | category |
|---|---|---|---|---|---|---|---|---|---|---|
| level_features.cpp | 0 | 32 | 43 | 3 | 7 | 2 | 6 | 0 | 93 | terrain gen (features, castles) |
| terrain_gen.cpp | 9 | 0 | 15 | 1 | 1 | 27 | 0 | 2 | 55 | terrain gen |
| terrain_paint.cpp | 0 | 24 | 25 | 1 | 0 | 0 | 0 | 0 | 50 | terrain paint / spiral |
| render_ext.cpp | 12 | 0 | 11 | 3 | 2 | 9 | 0 | 0 | 37 | renderer (extended) |
| effects.cpp | 0 | 9 | 16 | 1 | 0 | 4 | 0 | 0 | 30 | game logic (terrain effects) |
| render_landscape.cpp | 7 | 0 | 13 | 3 | 3 | 1 | 0 | 0 | 27 | renderer (faithful) |
| thing.cpp | 8 | 0 | 5 | 0 | 2 | 3 | 6 | 0 | 24 | map storage / spatial / math |
| demo.cpp | 3 | 0 | 0 | 0 | 0 | 15 | 0 | 0 | 18 | save state |
| creature_common.cpp | 4 | 0 | 4 | 0 | 2 | 0 | 4 | 0 | 14 | AI |
| spatial.cpp | 4 | 0 | 4 | 4 | 1 | 0 | 0 | 0 | 13 | spatial index |
| hud.cpp | 1 | 0 | 3 | 1 | 1 | 2 | 3 | 0 | 11 | HUD radar / map screen |
| creatures.cpp | 0 | 0 | 3 | 0 | 2 | 0 | 6 | 0 | 11 | AI |
| savegame.cpp | 0 | 0 | 0 | 0 | 0 | 8 | 0 | 0 | 8 | save state |
| mc_types.h / mc_globals.h/.cpp | 3 | 0 | 0 | 1 | 2 | 13 | 0 | 0 | 19 | map storage |
| creatures3.cpp, constructors.cpp, castle.cpp, creatures2.cpp, scenery.cpp, spells.cpp | 0 | 0 | 7 | 0 | 4 | 11 | 4 | 0 | 26 | game logic |
| ai_wizard.cpp | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 5 | 6 | AI (4x4 quadrants of 64 cells) |
| sim.cpp, net.cpp, tables.cpp, fli.cpp, engine.cpp, render.h, render_ext.h, mc_math.h, misc .h | 1 | 0 | 1 | 0 | 0 | 24 | 3 | 0 | 29 | save state / tables / misc |
| **total** | **52** | **65** | **150** | **18** | **27** | **120** | **32** | **7** | **~470** | |

(the regexes are approximate: `and0ff`/`shr8` also catch a few palette-index shifts, `mapsz` counts
0x10000-byte blend tables in tables.cpp / fli.cpp that are *not* map related; the map-related figure is
~430.) Position arithmetic that is not about 256 cells but about **16-bit units** is the larger number:

| pattern | count | files |
|---|---|---|
| `->x` / `.x` / `->y` / `.y` field accesses | 569 | all game logic |
| `(uint16_t)` casts writing a position | 84 | creatures*, effects, level_features, player, constructors, ai_wizard |
| `(int16_t)` casts on x / y (torus difference) | 106 | thing.cpp, creatures*, spatial, hud, level_features, castle |
| `pos_dist_*` calls (int32 squares of int16 differences) | 74 | everywhere |
| `pos_angle_to` / `pos_pitch_to` / `math_atan2` (int16 dx, dy) | 84 | AI, projectiles, HUD |
| `thing_pos(t)` handed to a `Pos *` consumer | 434 | everywhere |
| `g_pos_scratch` (the original's DAT_000adfc4 position scratch) | 318 | everywhere |

By category:

- **Map storage**: `mc_types.h:548-560`, `mc_globals.*`, `terrain.h`; the memsets in `sim.cpp:79-83`,
  `mc_globals.cpp:60-64`, `terrain_build` (`terrain_gen.cpp:486, 489`).
- **Spatial index**: `thing_link_cell/unlink_cell/move_to` (`thing.cpp:259-296`, `mc_cell_of`), `walk_cell` /
  `spiral_find` / `area_cells_slot0` / `thing_exists_near_pos` / `thing_area_damage` (`spatial.cpp:30-38, 43-55,
  66-80, 125-137, 143-170`), `terrain_max_corner_level` (`spatial.cpp:253`), spiral origin and ring limit
  (`terrain_paint.cpp:336-342`), `demo_repair_cell_lists` (`demo.cpp:377-421`).
- **Terrain probes**: `terrain_sample_height` (`terrain_gen.cpp:507-519`: `(uint8_t)(x >> 8)`),
  `terrain_cell_flag_bit`, `terrain_type_mask_at`, `terrain_slope_vector` (`thing.cpp:588-612`),
  `creature_common.cpp:165-169`.
- **Renderer**: faithful grid 40x21 and `(uint8_t)` cell walk (`render_landscape.cpp:363-364, 386-424, 495-501`),
  cull / fog constants `kCullDist2 = 0x1900000` etc. (`render_landscape.cpp:358-361, 393, 407`); extended:
  `kWin = 128`, vertex cache `(2*128+1)^2` (`render_ext.cpp:228-229, 280-284`), `F.cam_x = (uint16_t)`,
  `fx = cam_x & 0xff` (`render_ext.cpp:909-910`), draw distance clamp 4..127 and `R = min(dd, kWin - smax)`
  (`render_ext.cpp:911, 919-921`), `slope_camera` (`render_ext.cpp:859-861`), the mip atlas `256 / Bm`
  (`render_ext.cpp:183` - texture atlas, not map).
- **Terrain gen**: all of `terrain_gen.cpp` (section 2) - `uint8_t x, y` everywhere, `for level = 7..0`
  (`terrain_gen.cpp:101-118`), `% 0xffff` river source pick (`terrain_gen.cpp:187`), `MC_MAP_CELLS` loops.
- **Terrain paint / features**: `terrain_paint.cpp` (cell_xy + `(uint8_t)` in paint / retexture / smooth /
  spiral, lines 111-330), `level_features.cpp` (footprints, walls, canyons, ridges, castle site tests, lines
  181-260, 278-342, 417-508, 612-660, 728-803, 1023-1059, 1111-1112), `effects.cpp` (castle collapse / raise,
  lines 95-127, 857-1022).
- **AI**: wake radius `d2 < 0x2400000` against the local player (`creature_common.cpp:589-594`),
  `dist_sq_xy16` / `sight_sq` (`creatures.cpp:25-33`), `ai_goal_build_castle`'s **4x4 grid of 64-cell quadrants**
  (`t->x / 0x4000`, `(qx & 3) << 14`, `((qx & 3) << 6) + 0x1f) << 8`, `ai_wizard.cpp:578-591`), the squared-range
  constants `0x1900000` (20 cells), `0x3840000`, `0x6400000` (`ai_wizard.cpp:557, 652, 739, 846`), castle growth
  rect `0x3000` chebyshev, villager / wizard castle placement offsets (`creatures.cpp:654-704`,
  `creatures3.cpp:115-117, 488-490`).
- **Network packets**: none carry positions. `CmdPacket` (10 bytes, `mc_types.h:16-25`) is input only; the lobby
  exchanges the level number (`frontend.cpp:1811`) and `NetGameRules` (`frontend.cpp:1815`, `net.h:134`); the
  desync checksum hashes GameState + pool (`net.cpp`, `port_pool.md`). A join does **not** send the map: every
  peer regenerates it from the level number - exactly what a seed + parameters exchange needs.
- **Save state / snapshot**: `demo.cpp:197-217, 282-293` (0x60000-byte map files), `savegame.cpp:214, 256-257,
  432-437`, `sim.cpp:113-131`, `reference_test.cpp:209-213, 598-604` - all hard-wire 0x10000 / 0x20000.
- **HUD**: `ui_draw_radar` cell = `(v >> 8 & 0xff) << 8 | (u >> 8 & 0xff)` (`hud.cpp:126`), `ui_draw_map`
  `(uint8_t)` cells (`hud.cpp:294-295`), radar blips with `(int16_t)(T->x - cam_x)` (`hud.cpp:168-171`), map screen
  scale `0xaa` over `0x17e` pixels = the whole 256-cell map (`hud.cpp:706-707`).
- **Sound**: `sound.cpp:120` cull at `0x9000000` (48 cells), pan / volume from `pos_dist_xy` (`sound.cpp:122-128`).

---

## 2. Terrain generation (terrain_gen.cpp, terrain_paint.cpp, level_features.cpp)

### 2.1 What the original does (terrain_build_303f0 -> `terrain_build`, `terrain_gen.cpp:476-500`)

Inputs: the level header `GenMap` (`mc_types.h:34-48`): `seed, off, raise, gnarl, river, sourc, snflt, bhlin,
bhflt, rkste` (`snlin` unused). `g_rng16 = seed` (the 16-bit LCG `x*0x24a1 + 0x24df`, `mc_math.h:31-33`).

1. **Diamond-square heightfield** (`terrain_fractal_fill`, `terrain_gen.cpp:95-119`; steps 60-93): 16-bit scratch
   in `g_cell_things`; `map[off] = raise`; 8 levels, step 128 .. 1, each level a full pass of square steps
   then a full pass of diamond steps starting at `off` and wrapping on the torus; a midpoint is written only
   while still 0; value = `avg - step*32 + r % (step*64 + 1) - gnarl + r % (2*gnarl + 1)` in **16-bit**
   arithmetic (`fractal_noise`, lines 40-56), one LCG step per midpoint on a private copy of the LCG.
   `uint8_t x, y` and `count = 1 << (7 - level)` (so `count * 2s == 256`) hard-wire 256.
2. **Normalise** (`terrain_generate`, 124-140): scale so the maximum maps to 0xc4, clamp 0..0xc4 into
   `g_map_height`, zero the scratch.
3. **Rivers** (`terrain_carve_rivers` / `terrain_trace_river`, 147-205): class 5 where h != 0 else 0; `river`
   sources from random cells (`rng % 0xffff`) with h > `sourc`; each traced downhill over the 8 neighbours,
   lowering heights monotonically; visited cells -> class 0 (water).
4. **Classification passes** on `g_map_flags & 7` (209-371): flatten water quads; `classify_flat(snflt)`;
   `mark_lowland(bhlin, bhflt)`; `insert_transitions`; `mark_interior(bhlin, bhflt)`; `mark_steep(rkste)`;
   `fill_holes`; `smooth_spikes`; `fix_shore_quads`. All are 3x3 / cross / 2x2-quad local filters over the
   whole map, one or two sweeps each (flatten_water iterates to a fixed point).
5. **Textures** (`terrain_assign_textures`, 390-440): the 0x94-tuple corner-class table expanded into its 8
   symmetries, bucketed by `c0*0x157 + c1*0x31 + c2*7 + c3`; every cell gets a random matching texture
   (`rng16 % (n+1)`) and the rotation code in flag bits 4-6; the compact `g_corner_tex_table` is kept for
   run-time retexturing.
6. **Water animation flag** and **light map** (443-471): light = `0x20 - (h[x+1,y+1] - h[x-1,y-1])` with noise on
   flat cells (rng16 reset to 0 first).

7. **Features** (`terrain_generate_features`, `level_features.cpp:1139-1148`, called from `sim.cpp:92`): every
   class-10 `ThingInit` with `dis_id == 0xffff` is spawned (`level_spawn_effect_record`) and
   `level_run_terrain_effects` (`thing.cpp:459`) runs the resulting effect Things to completion before the
   level starts: volcanoes, dents, craters, wall / path / canyon / ridge chains built from the `parent/child`
   record links (`level_build_linked_feature`, `level_features.cpp:1070-1112`), and **wizard castles**
   (effect 0x2d: `effect_wizard_init` + the span-encoded footprint from `data/building.tab` / `building.dat`,
   size = `ThingInit.parent + 0x10`, `level_features.cpp:82-160, 327-356, 452-520`). Scenery (trees, stones,
   dolmens: class 2) and creatures are plain `ThingInit` records spawned by `switch_activate(0)`
   (`thing.cpp:539`, position `x * 0x100 + 0x80`). **Player castles** come from the per-player level block and
   `castle_level[]` (`player.cpp:308-377`: `start_pos` -> `castle_stamp_footprint` for sizes 0..level-1).
   Villages are not placed: a living wizard castle spawns villagers at run time
   (`creature_spawn_random_villager`, `level_features.cpp:522+`), and builders found new wizard castles
   (`creatures.cpp:654-717`).

**Seed-driven vs level-file-driven**: the height field, rivers, classes, textures and light map are
seed-driven (one 16-bit seed + 9 numbers); *every feature placement* (castles, volcanoes, walls, canyons,
trees, creatures, start positions) is level-file-driven through `ThingInit` cell coordinates. The
reference (`port_terrain.md`) proves the port's generator is byte-exact on all pristine cells of level 38
and all 70 levels generate (`port_features.md`).

### 2.2 Can the existing algorithm run on an NxN map if the stride were parameterised?

Mostly yes, with three real changes:

1. **The fractal**: `uint8_t` coordinates and the 8-level loop assume 256. For N = 2^k the loop needs k
   levels (1024: step 512 .. 1), and the **16-bit noise overflows**: the amplitude term `step*32` is 16384 at
   step 512 and the 4-corner sum of int16 values wraps. Use an int32 scratch (a separate `std::vector<int32_t>`;
   the 16-bit reuse of `g_cell_things` is only an original memory trick) and keep the 16-bit arithmetic
   only in the faithful 256 path - or, cheaper and tunable, seed a coarse lattice first (write every
   (N/256)-th cell from a low-frequency noise of the player's "continent" parameters, then run the unchanged
   8-level fractal from step 128; the "write only where still 0" rule makes seeding free). The second
   variant gives a knob the player can feel (continent count / sea level) without touching the look.
2. **Rivers**: `cell = rng16 % 0xffff` and `tries = 1000` candidates are 256-specific constants; `river` should
   scale with area (level 44 uses 39 rivers on 65k cells).
3. **Everything else** (`flatten_water_quads`, the six class passes, `assign_textures`, `mark_water_anim`,
   `build_lightmap`, `smooth_spikes`, `fix_shore_quads`) is a local filter written as
   `for i in 0..MC_MAP_CELLS` with `x = (uint8_t)i, y = (uint8_t)(i >> 8)` and `(uint8_t)(x + 1)` neighbours:
   it runs on any torus once the cell index / neighbour helpers (`H`, `HR`, `F`, `FR`, `T`, `S`, `cross_range`,
   `block_range`, `terrain_gen.cpp:22-31, 232-250`) take a world descriptor. Cost is linear: 1024^2 is 16x
   level 38's ~20 ms generation, i.e. ~0.3 s - fine at match start. `terrain_assign_textures`' bucket scratch
   is map-size independent.

What a Phase 4 generator additionally needs, none of which exists today:

- a **feature placer** replacing the level file: seeded placement of start positions (spread by player
  count, mutual distance, land class), player castle sites (`castle_site_clear_at_pos`,
  `level_features.cpp:439-449`, works on any Pos), wizard castles / towns (`thing_create(10, 0x2d)` +
  `effect_wizard_init`, verified in `port_features.md` B2), scenery density per class, volcanoes, creature
  nests and mana (class 5 / 10 records). All the constructors take a `Pos`; the only level-file coupling is
  `switch_activate` reading `g_state->level.things[]` and `player_spawn` reading `level.castle_level` /
  `level.player_block` (AI parameters: `PlayerBlock.ai_accuracy/ai_reaction`, `mc_types.h:288-289`). The
  simplest integration is to *synthesise a LevelData* (GenMap + up to 1999 ThingInit + 8 player blocks)
  from the seed and feed the unchanged level-start code - but `ThingInit.x/.y` are u16 cells (fine to
  65535) while `LevelData` lives inside GameState with 1999 records: a 1024^2 world wants far more than 1999
  scenery records, so the placer should also spawn directly (`thing_create`) after `switch_activate(0)`.
- the generation currently runs with the **1000-slot pool** (`thing_pool_reset` before
  `terrain_generate_features`, `sim.cpp:91-96`; the extension is switched on in `switch_activate(0)`,
  `thing.cpp:165`) because more slots change the shaped landscape of level 39 (`port_pool.md`). A bigger world
  with hundreds of simultaneous terrain effects must run the generation with the extended pool from the
  start (it is a new, non-faithful path, so that is allowed).
- parameters the player can tune map directly onto the existing knobs: `raise` / `gnarl` (roughness),
  `sourc` + `river` (rivers), `snflt` / `bhlin` / `bhflt` / `rkste` (how much flat land vs cliffs), plus new ones:
  sea level (a bias before normalisation), continent lattice, feature densities.

---

## 3. Spatial index (spatial.cpp, thing.cpp, terrain_paint.cpp)

- **Cell lists**: `g_cell_things[cell]` is the head, Things are doubly linked through `cell_next/cell_prev`
  (`thing_link_cell`, `thing.cpp:259-271`; `thing_move_to` relinks only when `x >> 8` or `y >> 8` changed,
  `thing.cpp:287-296`). Lists are per cell, so their length depends on *density*, not map size.
- **Spiral search**: `data/search.dat` is a 32x32 image of ring numbers converted to a list sorted by ring
  (`terrain_paint.cpp:15-52`); rings 0..15 are populated, 16..31 empty, so the largest query radius is 15
  cells (`spiral_search_begin` clamps `end` to 32). The four collision / mana searches use rings
  `0 .. (ext_x + 0xff) / 256` around `(x + 0x80) >> 8` (`spatial.cpp:43-55`); the area damage functions walk
  a square of radius `ext_x / 256` (`spatial.cpp:66-80`). All are relative to the Thing, so they are
  unaffected by world size once the cell helper wraps on N.
- **Sleep / wake**: `creature_wake_tick` (`creature_common.cpp:561-575`) walks the 20 creature lists and the
  mana-ball list every tick; `creature_proximity_wake_timer` (578-602) counts `timer_a` down and, when it
  and `timer_b` hit 0, re-arms `timer_a = 0x10` only if `dx^2 + dy^2 < 0x2400000` from **the local player**
  (6144 units = **24 cells**). `timer_a != 0` gates targeting, wandering and damage application
  (`creature_common.cpp:103-121, 252-256`); `creature_move_step` runs every tick regardless (252). The
  renderer culls at 20 cells (`kCullDist2 = 0x1900000`, `render_landscape.cpp:393`; the extended renderer up
  to 127). In **network games the wake tick is skipped entirely** (`thing.cpp:431`: `!(Config.flags & 0x10)`;
  `port_net.md:176-178`) because it reads the local player - creatures never re-arm `timer_a` in lockstep
  play. Phase 4 needs a deterministic replacement anyway: wake within R cells of *any* player (or any
  player's units), evaluated identically on every peer - a cheap per-player distance test over the creature
  lists, or a per-cell "interest" bitmap refreshed each tick from the 8 player positions.
- **Cost model for 8 players on 1024^2**: the update loop is over pool slots, not cells
  (`thing_update_all`, `thing.cpp:376`; 32768 slots cost 0.2-0.3 ms/tick, `port_pool.md`), so the map size
  itself costs nothing per tick. What scales is the Thing count. Level 38 has ~450 Things on 65k cells; at the
  same density a 1024^2 world starts with ~7000 and the measured per-level peaks (up to 2627 on level 17)
  scale to ~40k - **above the 32768-slot ceiling**, which is a hard one: indices are u16 fields in
  Thing / `g_cell_things` and the original sign-extends them at ~40 places (`port_pool.md`). Either keep
  scenery density well below the campaign's (a battle royale wants open space anyway), spawn scenery lazily
  near players, or accept a larger refactor of the index width. The 8 awake discs of 24 cells cover
  ~14.5k cells = 1.4 % of a 1024^2 world, so AI cost stays bounded by what is near players.

---

## 4. Renderer and the RTS camera

### 4.1 Projection and camera limits

Both renderers use the original's transform: rotate (dx, dy) by yaw into (x_c, z_c), then
`sx = x_c * focal / z_c`, `sy = h_rel * focal / z_c + horizon`, `horizon = pitch * width >> 8`, then a screen
roll (`render_landscape.cpp:362, 398-416`; `render_ext.cpp:266, 901`; ENGINE.md 2.4). **Pitch is a horizon
shift, not a rotation**: looking "down" moves the horizon line up the screen; the terrain never tilts
towards the viewer. A camera straight above the ground sees everything squashed to the horizon line. So a
top-down or steeply pitched RTS view cannot come out of the existing projection, no matter the altitude.

Altitude: the flyer's `MoveDesc 7` (`gen/core_tables.h:725-726`) has `clear_lo` (cruise / maximum height above
ground) = 0x400 = **4 cells** and `clear_hi` (minimum) = 0x80; `player_flyer_move` limits the climb by the
ratio to that cruise height (`player.cpp:468-483`). The terrain tops out at 6272 units (24.5 cells), so the
carpet never gets high enough for an overview either - and the camera is the player Thing's log entry
(`PosLogEntry`, `hud.cpp:703-705`).

Draw distance: faithful 40x21 vertex grid, 20-cell cull (`render_landscape.cpp`, MC_GRID_*); extended:
world-aligned traversal in rows along the view-quadrant axis, far to near, columns outside-in
(`render_ext.cpp:720-840`), radius <= 127 so that one torus copy is never drawn twice, vertex cache of
257^2 entries (`render_ext.cpp:228-229, 280-300`). On a bigger world `kWin` becomes `min(N/2, something)`,
the cache grows with the square of the radius, and nothing else changes - the traversal never looks at
cells outside the window.

### 4.2 The existing top-down painter: radar and map screen

`ui_draw_radar` (`hud.cpp:91-135`) **is** an orthographic terrain renderer: for every radar pixel it steps
(u, v) in world units rotated by yaw, reads `g_map_light[cell]` and the texture's average colour
(`g_tex_avg_colour`, the 0xCD9B0 table) and shades it through the shade table; `scale 0x100` = 1 cell per
pixel. The flight radar is 128x128 px (64 cells each way, `radar_zoom_pct` 50..200, `hud.cpp:764-767`); the
map screen (`map_screen`, `hud.cpp:697-708`, view mode 4) draws it 0x17e px wide at scale 0xaa, i.e. the
whole 256-cell map, over a small 3D view, plus `ui_draw_radar_blips` (`hud.cpp:144-250`: wizards, the dotted
line to the castle, creatures as dots) and the player list. `ui_draw_map` (`hud.cpp:290-319`) is an unused
2 px/cell variant. All of it indexes cells with `& 0xff`.

This is the right seed for an **RTS minimap** (1 px/cell is 1024 px for the whole world - a zoomable
minimap window, not the whole map) but not for the main RTS view: it has no sprites, no castle footprints as
geometry, no height relief beyond the light map, and it is 8-bit palette + blend table.

### 4.3 What a true RTS view needs

A new renderer `render_ortho` (or "overview") beside render_ext, not a camera mode of it:

- **Projection**: orthographic or mildly oblique: `sx = (dx*cos - dy*sin) * s`, `sy = (dx*sin + dy*cos) * s * k
  - h_rel * s * m` with a per-pixel scale `s`, an elevation factor `k` (1 = pure top-down, ~0.5 = 60 degree
  tilt) and a height factor `m`. Terrain cells become parallelograms (two triangles) with the same UV
  table / rotation codes / shade formula as render_ext; painter's order rows far-to-near is still correct
  for a height field under an oblique view.
- **Reuse**: `render_ext_raster.cpp`'s `ext_fill_triangle` (modes 4 / 5 / 7 / 26, affine u/v/shade, haze) and
  `ext_draw_sprite` (affine blit, 10 pixel modes) and the band thread pool take an `ExtTri` / `ExtSprite`
  list (`render_ext.h:12-57`); render_ext's `vertex()` cache, mip atlas, average colours and the
  `thing_pos` interpolation / sleeping-segment layout can be shared. Only the list builder is new
  (~600-900 lines). Sprites: the 16-direction sprite selection works for an oblique view; a pure top-down
  view would want top-down sprites the game does not have, so the design should be oblique (classic RTS
  3/4 view), which also keeps castles recognisable from their footprint relief.
- **Pixel budget**: at 1080p and 8 px per cell the view covers 240 x 135 cells = 32k quads, the same order as
  render_ext at distance 127 (15-30k triangles, 2-6 ms multi-threaded, `port_render_ext.md`). An RTS view
  wants a far zoom-out (2-4 px per cell): use the existing merged-quad LOD (2x2 / 4x4 / 8x8 cells flat-shaded
  with average colours, `render_ext.cpp` LOD section) - at 2 px/cell the whole view is level-2 quads.
- **GPU**: there is no GPU renderer and no GPU heightfield sketch. The platform is SDL_Renderer with a
  `direct3d11` hint (`platform_sdl.cpp:97-98`) uploading an RGBA conversion of the 8-bit frame
  (`platform_sdl.cpp:115, 163-168`); `compose.*` is a CPU compositor (`compose.h:1-25`), and the only GPU idea
  on record is a palette-lookup shader (`port_compose.md:107, 298-299`); `port_render_ext.md` concluded a
  GPU path is unnecessary for distance / resolution. A heightfield on the GPU would be a natural fit for an
  RTS zoom-out over 1M cells, but the CPU design above gets there first and keeps the 8-bit look; decide
  after the first playable RTS view.
- **Input / selection**: the RTS view needs picking (screen -> cell / Thing): invert the ortho transform,
  then the cell list. Nothing exists today; the mouse is only read for the spell book and lobby.

---

## 5. Fixed point and overflow when coordinates grow 4x

- **Units**: 256 units per cell, 8 units per height step (byte * 0x20), speeds in units per tick (flyer
  `target_speed` up to 0x50 = 0.31 cell/tick = 7.8 cells/s at 25 Hz: crossing a 1024-cell world takes
  ~130 s; the original's 256 map takes ~33 s). The user's "fog at ~10 m" is the 20-cell cull; keep "1 cell =
  half a metre" as the working scale and the 24-cell awake radius is 12 m, a 1024-cell world ~500 m across.
- **Angles**: 11-bit, 0..0x7ff (`MC_ANGLE_MASK`, `mc_math.h:7-9`), 16.16 trig; unaffected.
- **Overflow risks with int32 coordinates** (all exist because the code was written for |d| <= 32768):
  - `pos_dist_sq_xy / sq_xyz` (`thing.cpp:721-731`) and `dist_sq_xy16` (`creatures.cpp:25-29`): int16 differences
    squared into int32 - at most 2^31 today (already at the signed limit at exactly 128 cells). With 1024-cell
    differences (262144 units) the square is 6.9e10: **every squared-distance comparison must become int64
    or clamp the difference first** (74 `pos_dist_*` calls, ~40 `dist_sq` / `best_d` nearest searches, the AI
    constants `0x1900000 / 0x3840000 / 0x6400000`, the sound cull `0x9000000`).
  - `math_atan2(dx, dy)` sign-extends both arguments to int16 (`thing.cpp:618-619`): `pos_angle_to` to a target
    more than 128 cells away returns the angle to its torus alias - wrong on a 1024 world (AI wizards flying
    to a far castle, the radar's castle line, projectile aiming). Needs an int32 octant reduction (the
    table lookup `(num << 8) / den` is fine once num/den are 32-bit; `num << 8` overflows above 2^23 units,
    so divide first).
  - `mc_isqrt` is uint32 (`mc_math.h:16-26`): fine for distances (not squares) up to 2^32.
  - render_landscape's `d2 = x_cam^2 + z_cam^2` is local (<= 20 cells); render_ext computes in double
    (`port_render_ext.md`: "the old int32 code would overflow beyond ~181 cells").
  - the fractal's 16-bit noise sum (section 2.2).
  - `Thing.z` i16 and `cam_z` are fine (terrain <= 6272).
  - `PosLogEntry` / `start_pos` / `Thing.home` / `ThingInit` carry positions and must widen with `Pos`.
- **Torus semantics**: with N not equal to 256 the 16-bit wrap `(int16_t)(b - a)` no longer means "shortest
  way round"; a `world_diff(a, b)` with modulus `N * 256` replaces all 106 casts. For N = 256 and
  `N * 256 = 65536` it is bit-identical to the cast, which is the faithful-mode proof.

---

## 6. Coexisting with the reference harness

What the harness needs: `GameState` (0x38d03 bytes) field-identical to the DOSBox dumps (`reference_test.cpp`
compares through a field table, `reference_test.cpp:150-152, 481`), the five maps identical, and pixel
references identical, all with default `PortSettings`.

Proposal, mirroring the pool extension:

1. **World descriptor** `g_world {int shift_x, shift_y; uint32 mask_x, mask_y; uint32 units_x, units_y;
   uint8 *type, *height, *light, *flags; uint16 *cell_things;}` set at level start. Faithful: shifts 8/8, the
   pointers are the existing static arrays (so no behaviour or layout change, and the memsets / snapshot
   loaders keep working). Extended: heap allocations of N*N (1024^2: 4 MB + 2 MB), the snapshot / save code
   writes a `WRLD` chunk with the size and the arrays (`savegame.cpp`, `demo.cpp` `max%05d.dat` already is the
   port's own extended format).
2. **Accessors** replacing `mc_cell`, `mc_cell_of`, the three `cell_xy` clones and the `(uint8_t)` neighbour
   arithmetic: `world_cell(cx, cy)`, `world_cell_of(x, y)`, `world_cell_step(cell, dx, dy)`, `cell_x(cell)`,
   `cell_y(cell)`; the generator's `H/HR/F/FR/T/S` go through them. With shifts 8/8 these compile to the
   present expressions (the reference tests prove it).
3. **32-bit positions**: `Pos {int32 x, y; int16 z}` and `Thing.x/.y` int32. This changes the Thing record
   (0xa4 -> 0xa8) and hence GameState's in-memory layout; the harness, movies, saves and the net checksum all
   treat GameState as a byte image, so the port needs an explicit **serialise / deserialise to the original
   layout** (the `thing_relink_snapshot` pointer->index conversion is the precedent; the field table of
   reference_test.cpp is already offset-driven). Alternative that avoids the layout change: a side array
   `g_thing_hi[slot] {int16 x_hi, y_hi}` with Thing.x/.y the low 16 bits - but then every one of the 84
   position writes must carry into the high word and every one of the 106 differences must combine both,
   with no type system help; the int32 `Pos` makes the compiler find every site. Recommend the int32 Pos.
4. **Functions needing a parameterised stride / mask / wrap** (estimate): map accessors and their callers
   ~52 `mc_cell` + 65 `cell_xy` + ~150 `(uint8_t)` neighbour sites in 14 files; 106 torus differences; 74
   distance calls; 84 angle calls; the 11 generator passes; render_landscape (3 places) and render_ext
   (kWin, vertex cache, cam truncation: ~8 places); hud radar / map (4); demo / savegame / sim / reference map
   I/O (6 blocks); ai_wizard quadrants (1 function); sound (1). Roughly **120 functions** touched, of which
   ~45 are mechanical accessor swaps.

---

## 7. Recommendation

**Approach A - parameterise stride / mask everywhere, keep u16 positions, 256 default.** Does not reach the
goal: a Thing cannot stand beyond cell 255. It only buys non-square or *smaller* maps. Reject for >256.

**Approach B - world module with 32-bit positions (recommended).** One code path, world size a run-time
parameter, faithful = 256 (bit-identical through the accessors), maps and per-slot data allocated at level
start outside GameState like the pool extension, GameState serialised to the original layout for movies /
saves / references / net checksum. Steps and effort (one engineer, days):

| step | work | days | risk |
|---|---|---|---|
| B1 | world descriptor + accessors; replace mc_cell / cell_xy / `(uint8_t)` neighbours in 14 files; map I/O chunks | 3-4 | low (mechanical; references catch every slip) |
| B2 | `Pos`/`Thing` int32 x/y; `world_diff`; int64 squared distances; int32 atan2; `PosLogEntry`, `start_pos`, `home`, `ThingInit` | 5-7 | **medium-high**: 569 field sites, 106 wraps, 84 casts; the layout change hits demo.cpp / savegame.cpp / net checksum / reference_test field table |
| B3 | GameState serialise / deserialise (original layout <-> in-memory) and re-validating all 49 ctest targets | 3-4 | medium (the harness is the safety net; `thing_relink_snapshot` shows the pattern) |
| B4 | generator on N: int32 scratch or seeded lattice, k levels, river scaling, pass helpers; `terrain_build(const GenMap&, const WorldParams&)` | 3-4 | low-medium (tuning the look at 1024 is the real work) |
| B5 | seeded feature placer: start positions, castles, towns, scenery, mana / creature nests; generation with the extended pool; params struct exchanged in the lobby like `NetGameRules` | 6-9 | medium (game design decisions; the constructors are verified) |
| B6 | AI / spatial: deterministic wake-near-any-player, quadrant AI generalised, sound / AI range constants reviewed | 2-3 | low |
| B7 | render_ext window `kWin = min(N/2, 256)`, vertex cache sizing; render_landscape untouched (faithful only); radar / map screen scale for N | 2-3 | low |
| B8 | RTS oblique renderer on the render_ext_raster display list, LOD at far zoom, picking; minimap from the radar painter | 8-12 | medium (new code, but the rasteriser and sprite blit exist) |
| B9 | tests: world_test (512 / 1024: seam wrap, spiral queries across the seam, generator determinism across two peers, snapshot round trip), ortho pixel tests | 3-4 | low |
| | **total** | **35-50** | |

**Approach C - tile streaming / chunked world.** Not needed: a 1024^2 world is 6 MB of maps, render_ext
already visits only the window around the camera, and the update loop is per Thing, not per cell. Streaming
would add seams to a code base whose every cell access assumes a flat torus. Reject; revisit only beyond
~4096^2.

**Pool ceiling**: keep 32768 as the hard limit (16-bit indices) and design densities for it; measure peaks
with the placer before deciding on an index-width refactor.

**Verification strategy**:

1. After B1 and again after B2/B3, every existing gate must be identical: `reference_test` (movie 0 +
   8 levels), `reference_levels`, `reference_gen` (all 69 levels), `reference_player*`, the four
   `render_reference*` pixel suites, `pool_test`, `net_test` (full `ctest`, 49 targets). Faithful mode is
   N = 256 with default settings; nothing may be branched on "extended" in the sim - the accessors must be
   the identity there.
2. New `world_test`: (a) 256 world through the new code == old arrays byte for byte after `terrain_build`
   for all 69 levels (same as reference_gen, through the descriptor); (b) 512 and 1024: a Thing walking
   across the seam relinks to the right cell, `spiral_find` / `thing_area_damage` hit things across the
   seam, `world_diff` shortest-path property; (c) determinism: two engine instances with the same seed /
   params produce identical maps, Thing pools and `net_state_checksum` for 2000 ticks; (d) save state round
   trip of a 1024 world; (e) overflow probes: distances / angles between Things 500 cells apart.
3. `render_ext_test` extended with a 1024 world at distances 127 and (new) 255 ("no cell drawn twice" counts
   on N^2 cells); ortho renderer pixel tests against a hand-checked reference frame.
4. A `MC_REF_WORLD=1024` style override (like `MC_REF_THING_SLOTS`) to run the campaign references on a
   bigger torus and confirm they diverge **only** where a thing crosses the old 256 boundary - the same
   argument port_pool.md made for the pool.
