# Terrain generator port (terrain_build_303f0) - agent report, 2026-10-06

Files: `src/mcengine/terrain_gen.cpp`, `src/mcengine/tables/terrain.tables` -> `gen/terrain_tables.h`,
`src/tests/terrain_test.cpp` + `.cmake`; `terrain.h` gained `extern uint8_t g_corner_tex_table[2401][2];`
(DAT_000b58b0).

## Functions translated (from the disassembly; decompiled C only for control-flow cross-checks)

| port | original |
|---|---|
| `terrain_build(const GenMap&)` | terrain_build_303f0 (0x303f0-0x304fe) |
| `terrain_fractal_fill(seed, off, raise, gnarl)` + static `fractal_square_step`, `fractal_diamond_step`, `fractal_noise` | 71f08, 71f92, 72027 |
| `terrain_generate()` | 313a0 |
| `terrain_carve_rivers(count, min_height)` + static `terrain_trace_river(cell)` | 31430, 314e0 |
| `terrain_flatten_water_quads()` | 31e50 |
| `terrain_classify_flat(thr)` | 309f0 |
| `terrain_mark_lowland(max_h, max_range)` | 31650 |
| `terrain_insert_transitions()` | 30c50 |
| `terrain_mark_interior(max_h, max_range)` | 31800 |
| `terrain_mark_steep(min_range)` | 31ad0 |
| `terrain_flags_fill_holes()` | 308f0 |
| `terrain_smooth_spikes()` | 30500 |
| `terrain_fix_shore_quads()` | 30810 |
| `terrain_assign_textures()` (+ `g_corner_tex_table`) | 30eb0 |
| `terrain_mark_water_anim()` | 30690 |
| `terrain_build_lightmap()` | 31310 |
| `terrain_sample_height(x, y)` | 71e00 |

Argument widths mirror 303f0: everything is a 16-bit load except `river` (32-bit). Inside the passes
`sourc`, `bhlin/bhflt`, `rkste` are compared as bytes, `snflt` as a 32-bit zero-extended word. RNG use:
carve_rivers, assign_textures and build_lightmap use `g_rng16` (DAT_0012dfb0); the fractal uses a private
copy of the LCG seeded from g_rng16 (g_rng16 is not advanced by it); build_lightmap sets g_rng16 := 0
first; `g_state->rng` is written (= seed) but no pass reads it.

Data table: `g_corner_class_tuples` i8[592] at 0x93fc8 (0x94 x 4, ends at the slope tables 0x94218);
closes ENGINE.md region B open question 4.

## Verification (`terrain_test.exe <game dir>`, exit 0)

Level 38 header: seed 0xcc4c, off 0x1403, raise 6157, gnarl 0, river 6, sourc 47, snflt 3, bhlin 86,
bhflt 26, rkste 17. The level has 152 level-start terrain effects (type 0x09 x13, 0x0b x1, 0x1c walls
x43, 0x1d x33, 0x1f x43, 0x2d wizard castles x19) plus 2057 flag-0x80 cells, so the post-start reference
legitimately differs on ~15% of the map. Cells are partitioned into castle footprint (ref flag 0x80),
"effect zone" (within 9 cells of a footprint cell or a terrain-effect THING_INIT) and "pristine"
(41,909 cells):

| map | whole-map match | pristine zone | mismatches on 0x80 / effect zone / pristine |
|---|---|---|---|
| g_map_height | 56029/65536 (85.49%) | **41909/41909 exact** | 853 / 8654 / 0 |
| g_map_light | 57884/65536 (88.32%) | **41909/41909 exact** | 1878 / 5774 / 0 |
| g_map_flags (all bits) | 59202/65536 (90.34%) | 41901/41909 | 2057 / 4269 / 8 |
| g_map_flags (class bits) | 63009/65536 (96.14%) | 41906/41909 | 43 / 2481 / 3 |
| g_map_type | 59802/65536 (91.25%) | 41901/41909 | 1486 / 4240 / 8 |
| g_cell_things | n/a (reference holds 380 thing links placed at level start) |

Every height and light mismatch is within 9 cells of a castle cell or terrain-effect THING_INIT. Texture
rotation bits, water-anim bits, classes and textures match exactly everywhere else, which validates the
corner-class table orientation (c0=(x,y), c1=(x+1,y), c2=(x+1,y+1), c3=(x,y+1)), the 8 symmetry codes and
the rng16 consumption order.

The only pristine-zone discrepancy is a 3x3 patch at (253..255, 240..242): the reference has class 1
(cliff edge) on 3 cells with unchanged heights and the surrounding 3x3 retextured through the corner
table. The generator can only produce class 1 from class 6, so this is a run-time paint before the dump
(the patch sits inside a cluster of 18 Skeletons). Not a generator issue.

Debug output next to the exe: `terrain_height_{ours,ref}.pgm`, `terrain_type_{ours,ref}.pgm`,
`terrain_{height,type}_diff.ppm`, `terrain_ours.bin`.

## Deviations from the original

1. `terrain_build` clears `g_cell_things` before the fractal (original relies on the per-level reset).
2. The cached heightmap file ("c:/carpet.cd/save/scanned.rmd") is never attempted.
3. `fractal_noise` guards the second modulo against a zero divisor (gnarl 0x7fff/0xffff would #DE in
   the original). The bucket scratch is a static 60,025-byte array instead of the frame buffer.
4. `terrain_trace_river`: when no neighbour qualifies the original's EBX is stale; both exits are
   side-effect free, so the port initialises `best = 0`.

## Open questions

- `gnarl` is 0 on level 38, so the second noise term is unverified; 30 of 70 levels use gnarl 1..128.
- The dump's g_rng16 is 0 while the generator leaves 0x6399; something after level start resets it.
- What painted the class-1 patch at (254,241).

## Corrections for docs/ENGINE.md / carpet_types.txt

- terrain_carve_rivers: the 1000-candidate counter is shared by all rivers; cell = rng16 % 0xffff; the
  source must have height > sourc (strict) and class != 0.
- terrain_generate_313a0 is the normaliser: scale = 0xc40000 / max (idiv), value = (h*scale) >> 16 tested
  as a 16-bit word, clamped 0..0xc4, scratch zeroed. The min is computed but unused.
- terrain_fractal_fill_71f08: args (seed, off, raise, gnarl) all 16-bit; map[off] = raise; 8 levels step
  128..1, per level a full square pass then a full diamond pass starting at `off`;
  value = avg - step*32 + r%(step*64+1) - gnarl + r%(2*gnarl+1) in 16-bit arithmetic, one LCG step per
  midpoint, written only where the scratch is still 0; the diamond step computes two midpoints
  (x+s,y) and (x,y+s) per call. Private LCG copy, not DAT_0012dfb0.
- terrain_fix_shore_quads_30810: only (x+1,y), (x+1,y+1), (x,y+1) are classified; the minimum includes
  (x,y) regardless of its class.
- terrain_mark_steep_31ad0 second pass: with a class-3 neighbour any 2/5/4 neighbour -> class 1; without
  a 3, a 2 or both 5 and 4 -> class 1.
- terrain_assign_textures_30eb0: symmetries (a,b,c,d)->0x00, (b,a,d,c)->0x10, (c,d,a,b)->0x30,
  (d,c,b,a)->0x20, (b,c,d,a)->0x60, (c,b,a,d)->0x70, (d,a,b,c)->0x50, (a,d,c,b)->0x40;
  pick = rng16 % (n+1). Tuple table 0x93fc8..0x94217.
- mark_lowland / mark_interior / mark_steep copy g_map_flags into g_map_type as a scratch never read.
- 0x71e00 is not bilinear: it interpolates over the triangle of the quad, diagonal alternating with the
  parity of x_cell + y_cell.
- carpet_types.txt: add `global 0xb58b0 g_corner_tex_table u8[2401][2]` and
  `0x93fc8 g_corner_class_tuples i8[0x94][4]`.
