# Sprite cache + Thing renderer port (sprite_cache.cpp, render_things.cpp) - agent B report, round 2, 2026-10-06

Files: `src/mcengine/sprites.h`, `sprite_cache.cpp` (463 lines), `render_things.cpp` (694 lines),
`src/mcengine/tables/sprites.tables` -> `gen/sprites_tables.h`, `src/tests/sprites_test.cpp` +
`sprites_test.cmake`. Build dir `build_B`; `sprites_test.exe` -> `sprites_test: OK` (exit 0), zero warnings.
`render_test`, `thing_test` and the integration `engine_test` (all 70 levels with the things drawn, guard
bands intact) still pass with the two shared-file edits listed at the end.

What renders: every linked, visible thing of the level 38 snapshot (443 linked, 59 hidden by flags & 0x21)
with its shadow, in the terrain's painter order, at 320x200 and 640x480, with any camera roll, plus the
reflections of the second-surface pass. Seen in the test frames: the golden mana balls and the large
silver ball over the lake at the demo camera with their reflections, a palm grove (55 things in 11x11
cells) from four sides and rolled by 0x50 / 0x7c0 / 0x180, a company of crossbow men (3-view sprites,
mirrored for the other half of the directions), distance fog on the balls 15..19 cells away.

## Functions translated

| port | original | notes |
|---|---|---|
| `render_cell_things` (+ `render_thing`, `thing_select_sprite`, `thing_project`, `thing_fog`) | render_cell_things_2c600 (5155 B) | both per-thing blocks (shadow, thing); the 2 x 37-case draw-type switches are one helper with a `shadow` flag (the only difference: type 0x15 is not marked upright in the shadow block) |
| `render_cell_things_mirrored` | render_cell_things_mirrored_2e5a0 (2781 B) | same helper, height `-z - cam_z`, anchor 2 |
| `render_sprite_scaled(anchor)` (+ `sprite_blit_upright`, `draw_row_mode`, `row_straight`, `row_rolled`) | render_sprite_scaled_2ad60 (5996 B) | upright path 0x2ad79..0x2b30b (mode table 0x2ace8), rolled path 0x2b30c..0x2c4cb: the 8 octant entries of table 0x2ad10, the two shared clip tails 0x2b50e (even octants) / 0x2bce1 (odd), column table + line loop 0x2b6d5, mode table 0x2ad30. The 2x / 4x unrolled Duff entries are plain loops |
| `tmaps_load` | tmaps_load_4b580 | reads tmaps.dat + tmaps.tab through `mcdata/tmaps.h` |
| `sprite_load_chunk` (static) | tmap_read_chunk_4b5e0 + tmap_find_chunk_4c560 | one heap block per chunk |
| `sprite_group_size / _load / _unload / _unload_if_unlocked` | 4b850 / 4b8b0 / 4b780 / 4b690 | |
| `sprite_cache_evict_lru` | sprite_cache_evict_lru_4b9b0 | 5 smallest stamps, residents only when nothing else is loaded |
| `sprite_ensure_loaded` | sprite_ensure_loaded_4bdd0 | |
| `sprite_cache_init / _shutdown`, `sprites_init / sprites_shutdown` | 4bbf0 / 4bc80 (+ tmaps_close_file_4bcf0) | |
| `sprite_group_priorities_clear`, `sprite_set_group_priority`, `sprite_mark_needed_for_model`, `sprite_groups_reload_by_priority` | 4bec0 / 4bf60 / 4bee0 / 4bfb0 | installed as the three thing.h hooks |
| `texture_mark_resident`, `texture_load_resident`, `texture_mark_needed`, `texture_load_needed` | 4c0a0 / 4c0f0 / 4c130 / 4c1a0 | same cache; the last two serve the demo recorder (state+0x2c) |
| `texture_anim_update` (+ `texture_anim_next_frame`, `sprite_anim_register`, `flic_play_frame`, `flic_decode_ss2`) | texture_anim_update_4be50, texture_anim_next_frame_4c9f0, sprite_cache_register_4c880, flic_set_dest_50de0 / flic_play_chunk_50dfd / flic_decode_frame_50e88 / fli_decode_ss2_50f71 / fli_skip_chunk_50f60 | needed to make the animated sprites move; outside the listed address range but the direct callees |

Not translated: render_cell_things_sirds_2dac0 (out of scope), the pool manager tmap_cache_*
(0x4c280..0x4c790) and the anim-table management (4c7f0, 4c9c0, 4ca70, 4cab0, 4cb10, 4cb50), which the
port replaces (see simplifications), fli_mem_decode_brun (chunk type 15, see below).

Tables (`sprites.tables`): `g_sprite_dir_remap_13` (0x93f54, 16 B), `g_sprite_dir_remap_14` (0x93f64, 16 B),
`g_sprite_model_table` (0x9649e, 681 i16). `g_sprite_mode_lit / _fogged` come from `gen/render_tables.h`.

## How the exe draws a thing (established from the disassembly)

- **Visibility**: `thing.flags & 0x21` skips. `dx = (i16)(x - cam_x)`, `dy = (i16)(cam_y - y)`,
  `xc = (cos_yaw*dx - sin_yaw*dy) >> 16`, `zc = (cos_yaw*dy + sin_yaw*dx) >> 16`; drawn when `zc > 0x40` and
  `xc^2 + zc^2 < DAT_000b584c`.
- **Fog** DAT_000b5818: 0x2000 up to DAT_000b5848, 0 from DAT_000b5850, else
  `(((far - d2) << 5) / DAT_000b5844) << 8`. Pixel mode = `DAT_00093f48[shade_group]` when the value is exactly
  0x2000, else `DAT_00093f4e[shade_group]`.
- **Anchor**: `sx = focal*xc/zc`, `sy = horizon + focal*(z - cam_z)/zc`, then the same roll as the vertices:
  `x = cx + (cos_roll*sx - sin_roll*sy >> 16)`, `y = cy - (sx*sin_roll + sy*cos_roll >> 16)`.
- **Size**: `h = (u16)desc.half_z * focal / zc` (64-bit product), `w = sprite_w * h / sprite_h`; the thing and
  the reflection add 1 to both.
- **Sprite number by draw type** (SpriteDesc +0xc), `dir = ((thing.yaw - cam_yaw) >> 7) & 0xf`:
  0, 1 -> base; 2..0x10 -> base + frame; 0x11 -> base + dir (dir < 8) or base + 15 - dir drawn mirrored;
  0x12 -> base + dir (16 views); 0x13 -> base + DAT_00093f54[dir] (5 views `0 1 1 2 2 3 3 4 | 4 3 3 2 2 1 1 0`),
  0x14 -> base + DAT_00093f64[dir] (3 views `0 0 0 1 1 1 2 2 | 2 2 2 1 1 1 0 0`), both mirrored for dir >= 8;
  0x15 -> base, upright; 0x16..0x24 -> base + frame, upright. Mirroring = negative source width DAT_000b582c.
  Every use stamps `DAT_000b7cb0[group] = cfg->tick` and sets bit 3 of the sprite's flag byte.
- **Shadow** (only when `state.opt_shadows`, the cell's vertex +0x2b == 0 and `desc.shade_group == 0`): the
  same sprite projected at `terrain_height_at_71e00(x, y)`, height `>> 2`, drawn with anchor 0 (hanging
  below the ground point, upside down) in mode 8 with level `0x20 + fog/4` (0x28 at full light, 0x20 = no
  effect at the fog limit). A sprite that fails to load in the shadow block skips the thing itself too.
- **render_sprite_scaled_2ad60(anchor)** takes one stack argument: 1 = (x, y) is the bottom centre,
  0 / 2 = top centre and the source rows run bottom-up (shadow, reflection).
  - Rolled path: lines along the rolled x axis through the roll table. Even octants: `cols = w*cos_a >> 16`
    pixels per line, `rows = (h << 16) / cos_a` lines, line start moves by `h*sin_a/rows` (16.16) per line;
    odd octants swap sin_a / cos_a. Per octant the anchor maps to (first line index, 16.16 start entry,
    byte offset of the line origin); clipping at the minor edges uses the step list at work+0xe7e0.
    Mirrored sprites start the column accumulator at `-(cols-1)*xstep`.
  - Upright path (DAT_000b58ae): unrotated box; anchor 1 puts the top-left corner at
    `(x - q - (sin_roll*q >> 16), y - q - (cos_roll*q >> 16))`, anchor 2 at `(x - q + ..., y - q + ...)`,
    `q = (w + h) >> 2`. A negative source width is not handled there (never used with it).
  - Pixel modes (texel t != 0, dest d, L = DAT_000b5818 >> 8): 0 `t`; 1 `SHADE[L][t]`; 2 `BLEND[t][d]`;
    3 `BLEND[d][t]`; 4 `BLEND[k][t]`; 5 `BLEND[t][k]` with k = 4 / 5 in the upright path and 0 in the rolled
    path (the constant is whatever the register held); 6 `SHADE[L][BLEND[t][d]]`; 7 `SHADE[L][BLEND[d][t]]`;
    8 `SHADE[L][d]` (darken, rolled path only - the upright table entry is a no-op, so upright sprites have
    no shadow); 9 the constant byte 2 of DAT_000b5818 (SIRDS depth); > 9 nothing.

## Verification

By construction (all inside `sprites_test`, numbers from the last run):

- **Roll 0 exact**: 600 random blits (plain / mirrored / upside down, anchors 0-2, clipped at all edges,
  rolled path 400 + upright path 200) equal a 10-line reference scaler pixel for pixel. Pixel modes 1..10
  checked against the table formulas above in both paths (19 cases).
- **All 8 octants**: a 4-quadrant test sprite blitted 64x48 at 28 roll angles x {plain, mirrored} x {anchor
  1, 2}: every quadrant's centroid lies where the rectangle rotated like the terrain puts it (worst 3.1 px,
  quadrant spacing 24..32 px; exact at 0 / 0x200 / 0x400 / 0x600), pixel count within 12 % of w*h.
- **Clipping consistency**: 4000 random blits (about 500 per octant): the 320x200 result equals the same
  blit into a view padded by 48 pixels (minor axis both sides, major axis far end) cropped to 320x200.
  Even octants: 0 differences. Odd octants: 61 cases differ only by pixels missing on the one screen
  row / column of the near minor edge - an off-by-one of the exe (below), not of the port.
- **Memory safety**: 60000 random blits (all modes, anchors, octants, upright) + 6000 huge ones (up to
  6000 px) inside a canvas with 64-pixel margins: no margin pixel changed, 0 refused reads / writes; frames
  with 8 KB guard bands; a reduced view window (size 0x14) changes nothing outside the window.
- **Things stand on the terrain**: a thing placed on a terrain vertex is anchored at that vertex's screen
  position as computed by render_landscape: 3193 vertices in 6 views (all 4 quadrants, roll, pitch, zoom,
  both resolutions), worst on-screen difference 1 px (off screen within the 1/256-cell rounding).
- **Cache**: directory invariants (163 groups, group id = first sprite index), resident groups (46 locked
  descriptors -> 100 sprites at init), group load / unload / lock, LRU order and the 5-group limit, priority
  marking incl. the fallback and the unterminated record, reload by priority, the state+0x2c table round
  trip, the three hooks.
- **Animation**: all 213 animated chunks: 1108 frames decode inside their chunk, every loop ends exactly at
  the chunk end and reproduces the stored image (so the stored pixels are the loop's last frame).

By eye only (no reference screenshot exists): which sprite a draw type / direction selects and the mirror
rule (crossbow men face plausibly and consistently; types 0x11-0x14 have 145 instances in the snapshot),
shadow look (flipped quarter-height darkening under palms and creatures), reflection placement, fog
shading. Never exercised by real things: pixel modes 2..7 (every thing in the snapshot has shade group 0;
only formula-tested), draw types 2..0x10 and 0x16..0x24 (no instance; 0x15 = the mana balls is seen).
The 1..2 px placement differences between octants come from the exe's integer conventions and cannot be
confirmed without a DOSBox capture.

## Simplifications / deviations

- **No pool**: chunks are heap blocks, `tmap_free_space()` = 0x7fffffff. `sprite_ensure_loaded` therefore
  never evicts and `sprite_groups_reload_by_priority` loads every wanted group. Kept: group ids,
  `g_sprite_ptr` (DAT_000b8d3c), LRU stamps with `g_cfg->tick` (DAT_000b7cb0), lock table (DAT_000b9580),
  priority table (DAT_000b9791), the "drawn" bit, `cfg+0x95 = 5` on a demand load. `sprite_cache_evict_lru`
  is translated and tested but nothing calls it (mem_check_lowmem_59760 would). DAT_000b84f8 is dropped.
- `sprites_init` also runs `sprite_cache_init` (resident groups load at engine start, in the exe at the
  first level start); `sprite_cache_init` is idempotent.
- Animation records are one per sprite instead of a searched 0x211-slot table. FLIC chunk type 15 is
  skipped: the exe would decode it with 65536 lines (flic_set_dest passes y = 0) and tmaps.dat has none
  (1093 x type 7, 8 x type 16 which the exe skips too). A frame that does not decode stops the animation.
- Port-only guards (the exe has none): thing index >= 1000 ends the list (2c600 does not check; 2e5a0
  checks but then follows a stale pointer), sprite number >= 0x211, draw type > 0x24 (the exe would reuse
  the previous sprite pointer), sprite height 0, reads outside the sprite image count as transparent and
  writes outside the render target are refused (`g_sprite_oob_reads / _writes`, 0 in every test), triple /
  column / roll indices are kept inside `g_work_buf` / `g_roll_table`.
- The blitter keeps the exe's work-buffer layout (`g_work_buf + 0x9060` columns, `+0xb360` triples) and its
  delta-chain source addressing, so the overflow behaviour for sprites wider than the table (stale column
  records) is the same.
- `render_cell_things(first_thing, cell)` takes the first index from the hook argument (the exe re-reads
  vertex +0x24); `ret_stub_1fa80(thing)` (a `ret`) is not called.

## Hooks

Installed by `render_things_install()`: `g_render_cell_things`, `g_render_cell_things_mirrored`,
`g_hook_sprite_group_priorities_clear`, `g_hook_sprite_mark_needed_for_model` (= `(cls, model, -1)` as
switch_activate pushes it), `g_hook_sprite_groups_reload_by_priority`.
Declared: `g_sprite_blit_probe` (port-only debug hook, null).
**For the integrator**: `texture_anim_update()` must be called once per tick from the game loop
(game_tick_update_32e80, not while paused) - it clears the "drawn" bits and animates 213 sprites;
`g_cfg->tick` must advance for the LRU stamps.

## Shared-file changes (Edit tool, minimal)

- `render.h`: `extern CellThingsFn g_render_cell_things_mirrored;` + 2 comment lines.
- `render_landscape.cpp`: definition of that pointer; `draw_quad_mirrored` calls it when
  `first_thing != 0` (replaces the "not ported" comment). As in the exe (0x29ae6 / 0x29aef) the call is
  made for texture-0 cells too.

Requested: `Config` has no field at +0x95 (it is the last byte of `session[0x21]`); the exe writes 5 there
after a demand load (disk-activity countdown?) - make `session` 0x20 bytes + `uint8_t disk_timer`.

## Corrections / additions for ENGINE.md and carpet_types.txt

- 2.6: sprite header byte 0 = flags (bit0 = animated: FLIC frames follow the pixels, bit3 = drawn since
  the last texture_anim_update), byte 1 = draw type. Modes: 8 is "darken dest" (shadow), 9 the solid
  colour; 4 / 5 are not a tint with a chosen colour (see above). DAT_000b58ae = "upright" flag.
- tmaps.tab has 530 records (529 sprites + sentinel {0, file size, 0}); the group id is the index of the
  group's first sprite and DAT_000b7cb0 / b8d3c / b9580 / b9791 are all indexed by sprite number.
- sprite_groups_reload_by_priority_4bfb0: the "pool nearly full" early-out is `cmp ebp(=0x400), 0x4c550`
  - a comparison with the address of tmap_free_space, never true; texture_load_needed_4c1a0 has the
  intended `tmap_free_space() < 0x400`.
- sprite_mark_needed_for_model_4bee0: records are 17 words {class, model, 15 SpriteDesc indices}; the list
  ends at the first negative word, the last matching record wins, and record (5, 4) has no terminator, so
  it also marks the key and list of (5, 5).
- state+0x2c (texture_needed): 0 not loaded, 1 loaded, 2 loaded and resident; 4c130 clears 0x214 bytes.
- DAT_000987e8 (resident groups enabled) is set to 1 by mem_init_pools_59500; residents = SpriteDesc
  records with load_priority 0xff.
- Animation record (0x1c bytes): +0 active, +4 handle, +8 offset of the next frame (relative to the
  pixels), +0xe first frame offset (w*h + 6), +0x10 frame count (u16 at pixels + w*h), +0x12 w, +0x14 h,
  +0x16 next frame number (1-based), +0x1a sprite id. The extra data is {u16 frames, u32 size} + FLC 0xF1FA
  frames. fli_decode_ss2_50f71: a fill count of 0 means 256 words; the "last byte" opcode (0x80xx) reuses
  the same word as the packet count (broken, unused).
- render_sprite_scaled quirks: (a) odd octants number the lines along the minor axis one too high: the
  first line kept at the far edge is invisible and the last visible line at the near edge is dropped (a
  sprite crossing that edge loses one pixel row / column: octant 1 x = 0, 3 y = 0, 5 x = w-1, 7 y = h-1);
  (b) the clip code indexes the step list below its start for fully hidden lines and reads the tail of the
  triple table (zero in practice); (c) the column table holds width + height entries and the code writes
  one more (at 640x480 that is the first triple); (d) upright path: column record 0 gets the constant
  delta 0x16.

## Open questions

- Meaning of cfg+0x95 (set to 5 on every demand load).
- Whether the exe relies on the stale-column behaviour for very large near sprites (visible glitch?).
- SIRDS thing pass (2dac0) and its mode 9 depth value are untranslated.
