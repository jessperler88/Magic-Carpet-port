# Extended renderer: draw distance, any view size, LOD (task A, port round 7, 2026-10-07)

`render_view_ext(fb, cam)` draws the 3D view (sky, terrain, things, shadows, reflections, the 2x2
smoothing / motion-blur options) into a frame buffer of **any size** (tested 320x200 .. 3840x2160 and
2560x1080) with a **draw distance of up to 127 cells** (the original: 20), in the original's 8-bit look:
same textures, UV table, palette, shade and blend tables, sky, fill-mode formulas, sprite selection,
pixel modes, shadows and the original's fog colour. The faithful renderer is untouched (only the
cell-list walk got a bound) and every pixel reference is still identical.

Performance (Release, i7-14650HX, 24 hardware threads): **1920x1080 at draw distance 80: 1.9 - 2.9 ms per frame
multi-threaded, 5.9 - 7.5 ms single-threaded; 3840x2160 at 127: 4.5 - 5.9 ms multi-threaded.** The target
(1080p, distance >= 80, > 60 fps) is met by a wide margin even on one thread. Recommendation: **no GPU
renderer is needed for draw distance or resolution**; see the end.

## Files

| file | what |
|---|---|
| `src/mcengine/render_ext.cpp` (new) | `render_view_ext`, `render_view_ext_target`: camera, world-aligned terrain traversal, LOD, fog / haze, Things, display list, thread pool, post filters, renderer-only resources (texture mips, average colours, colour cube, mix tables) |
| `src/mcengine/render_ext_raster.cpp` (new) | band rasteriser: triangles (modes 4 / 5 / 7 / 26, with or without haze), affine sprite blitter (pixel modes 0..9), sky, mix-table builder |
| `src/mcengine/render_ext.h` (new) | display-list types, band / sky structs, `RenderExtStats g_render_ext_stats`, test hook `g_render_ext_cell_count` |
| `src/mcengine/render.h` | declarations of `render_view_ext` / `render_view_ext_target` (doc comments) |
| `src/mcengine/render_things.cpp` | bounded cell-list walk + `thing_at()` / `thing_pool_slots()` (both functions) |
| `src/mcengine/settings.h` | section A: `fog_mode`, `render_threads`, `thing_min_px`; `lod` defined |
| `src/tests/render_ext_test.cpp` + `.cmake` | the test (mc_unit_test: `${MC_SIM_ALL}` + engine / tables / renderer / HUD + the two new files) |

`raster.cpp`, `render_landscape.cpp`, `sprite_cache.cpp` are unchanged (the extended path has its own
rasteriser and buffers, so nothing of the faithful path had to be generalised). `src/CMakeLists.txt` needs no
change (mcengine globs `mcengine/*.cpp`; the reference tests do not link the new files and do not need them).

## Design

### Entry points

- `render_view_ext(fb, cam)`: the whole of `fb` (pitch = width). Task B's compositor calls it at the display
  resolution.
- `render_view_ext_target(fb, cam)`: the current render target (`g_rt_dest / pitch / width / height`, i.e.
  the view window `render_set_view_window` chose) - a drop-in `RenderViewFn` for `g_render_view_override`
  when the extended renderer runs at the game frame's own size without the compositor (reduced view sizes
  keep working; the test checks nothing outside the window is touched).
- Neither touches the faithful renderer's globals (`g_rt_*`, `g_fill_mode`, `g_texture_ptr`, `g_work_buf`,
  `g_rcam`, `g_roll*`, the slope low-pass). The test renders a faithful frame, an extended 1080p frame, and
  the faithful frame again: byte-identical.

### Camera

- **Vertical field of view = the original's at 640x480 for the same zoom**: `focal = height * 800/480 *
  zoom / 256` (= `isqrt(640^2+480^2) * zoom >> 8` at 480 rows). A wider frame sees more sideways; a 4:3 frame
  matches 640x480 exactly. (320x200 is shown at 4:3 on a CRT, so its non-square-pixel view is the same angle
  vertically within 10 %.)
- **Pitch**: the original's horizon offset is `pitch * width >> 8`; the extended path uses the 4:3-equivalent
  width `height * 4/3` instead (task B's request), so looking up / down is the same angle on 16:9 / 21:9.
- Roll, yaw, the camera-space transform and projection are the original's (`x_c = cos*dx - sin*dy`,
  `z_c = cos*dy + sin*dx`, `sx = x_c*f/z_c`, `sy = h*f/z_c + horizon`, then the screen roll), in double
  precision (64-bit safe: `x^2 + z^2` at 127 cells is ~1e9 units^2, the old int32 code would overflow beyond
  ~181 cells and `x * focal` beyond a few thousand pixels).
- The slope low-pass of `render_view_2f6e0` (camera nudge from the height-map slope) is applied with its own
  state, advanced once per game tick (`g_anim_tick` change), so the display frame rate does not change it.

### Terrain

- **World-aligned grid** around the camera cell instead of the 40x21 view-aligned rectangle. A cell = the
  quad between its four corner vertices (cell `(X,Y)` -> corners `(X,Y) (X+1,Y) (X+1,Y+1) (X,Y+1)`), texture
  `g_map_type[(X,Y)]`, UV corners and diagonal exactly as render_landscape_29050 assigns them in view
  quadrant 0 (`uv_sel = (flags>>2 & 0x1c)`, corners uv[0..1] .. uv[6..7], diagonal by `(X+Y)&1`), which is
  the world-consistent assignment; the same world corner order gives clockwise screen triangles in every
  view direction, so the rasteriser's back-face cull works as in the original.
- **Visible set**: every cell whose nearest point lies inside the draw circle (radius = `draw_distance`
  cells) and whose bounding circle touches the horizontal frustum (half-angle from the screen half-diagonal
  + |horizon|, so any roll is covered). Triangles crossing the near plane (z = 32 units) are clipped in
  camera space (Sutherland-Hodgman, attributes interpolated) - the original instead culls vertices with
  z < -0xff and clamps z to >= 0x80, which is invisible at normal flying heights.
- **Painter's order**, as the original: rows along the view quadrant's axis from far to near, inside a row
  columns from the outside in; rows behind the camera row mirrored (a wide view at 45 degrees off-axis sees
  some). Correct for a height field because along any ray from the camera both |row| and |column| offsets
  are non-decreasing. Things of a cell are drawn right after the quad **one row nearer** (the original's
  thing-cell offset in DAT_00093b20: things are drawn after the quad in front of their cell); in the camera
  row the quad hosts its own things and both neighbours'.
- **Map wrap**: the world is a 256x256 torus; the radius is clamped to 127 and every drawn cell lies in the
  window of offsets -128..127 around the camera cell, so no cell can be drawn twice (the test counts quads
  per map cell for 2 levels x 5 sizes x 4 distances x 3 LOD settings: never more than one). The far edge is
  always inside the fog / haze (fully faded at 15/16 resp. 97 % of the radius) - no hard edge is visible;
  seen from high up the faded disc edge curves down at the sides (perspective of a circle), like a
  planet's horizon. Flying on, the terrain beyond simply comes into range: that is the other side of the
  torus, as in the original.
- **Fill modes**: as render_landscape_29050: mode 5 (texture + Gouraud shade); with the second surface on,
  water textures mode 26 over the reflection and flat-shaded textures mode 7 with the four-shade colour.
  The reflection pass (when `cam_z < 0x1000`) is drawn first with the mirrored heights and reflected
  things, texture-0 cells skipped, as the original.
- **Water waves**, light map, the shade formula (`light<<16 + 0x8000 + wave*8`) per vertex as the original.

### Level of detail (`lod`)

Three mechanisms, all world-aligned (no flicker while moving):

1. **Texture mips** (renderer-only): every texture box-filtered to B/2, B/4 .. 1 texels (averaged in RGB,
   quantised to the nearest palette colour through a 6-6-6 cube cache), packed like the original atlas so
   the texel fetch is unchanged. A quad uses mip m once more than 2 texels (lod 1) / 1 texel (lod 2) of the
   full texture fall on a pixel, **never inside the original's 20-cell range** (the near look is
   untouched). The 1-texel level is drawn as a flat-shaded quad (mode 4) with the texture's exact B x B
   average colour (I compute it per texture rather than use 0xCD9B0, whose fixed 32x32 window mixes four
   textures in block16). This removes the shimmer of 32-texel textures at 7 px per cell (1080p, 127 cells).
2. **Merged quads**: beyond `D0 = focal / 3 px` cells (lod 1; `focal / 6` for lod 2) 2x2, 4x4 and 8x8 cell
   quads, flat-shaded with the average colour of the merged cells, heights / light at the coarse corners.
   Regions are nested Chebyshev squares, clipmap style: the area covered by levels <= L is the square of
   level-(L+1) cells within K = ceil(D0/2) of the camera's level-(L+1) cell, so every boundary runs along the
   coarser level's cell edges. With lod 1 this only triggers at low resolutions (320x200: D0 = 55 cells;
   640x480: 133 - never; 1080p: 300 - never), because a cell is still several pixels wide there.
3. **Skirts** on both sides of every level boundary: the fine cells along the boundary hang a flat quad
   from their outer edge down to below the coarse edge through it, the coarse cells along the boundary
   hang one from their inner edge down to the lowest fine vertex along it (both windings). Whichever side
   is higher covers the T-junction gap; nothing sticks up above either surface. No cracks were visible in
   the 320x200 lod 1 / lod 2 frames.

### Fog (`fog_start_pct`, `fog_mode`)

- The original's four constants scaled to the radius R: cull at R, full light below `fog_start_pct` % of R,
  shade 0 at 15/16 R (0x1690000/0x1900000 = 0.9375^2). **With R = 20 and 75 % these are exactly
  0x1900000 / 0xe10000 / 0x1690000 / 0x880000**, so the extended path at distance 20 frames like the original
  (compare `ext_l38_640x480_dd20_lod0.png` with `faithful_l38_640x480.png`). Note: shade level 0 of the shade
  table is not black but the pale blue-white fog colour, so the original's "darkening" already fades into a
  sky-like haze.
- `fog_mode`: 0 = auto (default): **with the textured sky on, terrain and things haze into the sky texel
  behind them** (the sky is an affine function of the screen position, so each pixel knows its sky
  colour): 5 steps 0, 1/4, 1/2, 3/4, 1 from mix tables built from the palette (nearest colour of the RGB
  mix), chosen per pixel with a 4x4 ordered dither between the steps; haze 100 % at 97 % of R, so the horizon
  has no edge and the clouds continue into the distance. With the untextured sky (colour 0xff) the
  original's shade fade is used. 1 = always the original's fade; 2 = always the haze.
- Things use the same fog: the original's DAT_000b5818 light level from the scaled constants (lit / fogged
  pixel-mode tables as the original), or the haze per sprite.

### Things

- Visited per cell through `thing_at()` / `thing_pool_slots()` with a **bounded walk** (at most
  `thing_pool_slots()` links), the original's visibility (`flags & 0x21`, `z_c > 0x40`, distance), sprite
  selection (draw types, 16 directions, the 5- / 3-view remaps, mirroring, upright types), on-screen size
  (`half_z * focal / z`, +1 as the original), shadows (quarter height, upside down, mode 8 with level
  `0x20 + fog/4`, only for shade group 0 and textures without the no-shadow property) and reflections.
  `sprite_ensure_loaded` / LRU stamp / "drawn" bit as the original (so animations keep running); all sprite
  cache access happens on the calling thread while the list is built.
- Things smaller than `thing_min_px` (default 1) are skipped; shadows below 4 px are skipped; 1-2 px
  sprites are drawn by the normal blitter (nearest texel, i.e. a dot of the sprite's colour).
- **Interpolation**: when `g_render_interp.active` and `prev_pos` is set and the slot `< prev_count`, a thing
  is drawn at `prev + (cur - prev) * alpha >> 16` (x / y 16-bit wrap-aware, z plain). Test: alpha 0x10000 ==
  the current tick pixel for pixel; alpha 0 with shifted previous positions moves the sprites.
- **Sprite blitter**: an affine image blit (inverse mapping per pixel row, any roll angle, mirroring,
  upside-down), with the original's 10 pixel modes incl. the upright-path variants of modes 4 / 5 / 8. It is
  not the original's line-walking blitter (which is tied to the 640-entry roll table and g_work_buf); the
  result is the same rotated rectangle with nearest sampling.
- Sprite cache (`sprite_cache.cpp`): no change needed - the port's cache has no pool (chunks are heap
  blocks, nothing is evicted), so any number of visible things just stamps more groups.

### Rasterisation and threads

- The frame is described once as a display list (`ExtTri` / `ExtSprite` in painter's order; 15k - 30k
  triangles at 127 cells) on the calling thread, then drawn by a persistent thread pool (detached workers,
  `render_threads` 0 = hardware threads, max 16) in `3 x threads` horizontal bands; every band draws the sky
  and the whole list clipped to its rows. No shared mutable state, so the output is identical for any thread
  count.
- The triangle filler keeps the original's conventions (rows [top, bottom), pixels [floor(xl), floor(xr)),
  affine u / v / shade, texel `tex[(v>>8 & 0xff00) | (u>>16 & 0xff)]`, `SHADE[g<<8 | t]`), with the set-up in
  double precision (no 16.16 overflow at any size, no 480-row span table), shared edges evaluated identically
  (watertight), and u / v / shade clamped per span to the texture / table (the original can read neighbouring
  atlas texels at quad edges).
- 2x2 smoothing (`opt_smooth`) and motion blur (`opt_motion_blur`, blend with the previous extended frame,
  modes as render_view_2f6e0) run in the same bands (smoothing reads a saved copy of the next band's first row).
  The automatic ghost blend of 320x200 at speed (Config.pentium) is not applied in the extended path.

## Settings (settings.h section A)

| field | default (faithful) | range | meaning |
|---|---|---|---|
| `render_extended` | false | bool | read by the integration (main.cpp / compositor): use render_view_ext |
| `draw_distance` | 20 | 4..127 cells | cull radius (clamped to 127 - LOD cell size so coarse cells stay inside the torus window) |
| `fog_start_pct` | 75 | 0..100 | full light / no haze below this % of the distance (original 15/20) |
| `lod` | 1 | 0, 1, 2 | 0 = full textures everywhere; 1 = mips beyond 2 texels/px (outside 20 cells), merged quads beyond focal/3 cells; 2 = twice as aggressive |
| `fog_mode` | 0 | 0, 1, 2 | 0 auto (haze into the textured sky, else the original fade), 1 original fade, 2 haze |
| `render_threads` | 0 | 0..16 | 0 = auto |
| `thing_min_px` | 1 | 1..16 | things smaller on screen are skipped |

Suggested **play** defaults for task E's config (mcport): `render_extended = 1`, `draw_distance = 96`
(127 is just as fast but 96 keeps a visible fog band at the usual flying height), `fog_start_pct = 60`,
`lod = 1`, `fog_mode = 0`.

## Verification

`render_ext_test` (Release and Debug, zero warnings, exit 0; ~2 s):

- faithful frame (320x200, level 38) byte-identical before / after a 1080p `render_view_ext` call;
- `render_view_ext_target` in a 640x480 frame with view size 0x14: 0 pixels changed outside the window;
- levels 38 and 44 (300 ticks in) at 320x200, 640x480, 1920x1080, 2560x1080, 3840x2160 x distances 20 / 48 /
  80 / 127 x lod 0 / 1 / 2 (lod 0 / 2 at 320 and 1920): guard bands intact, **no map cell drawn twice**;
- far terrain: at every size the distance-127 frame has terrain pixels above the top row of terrain of the
  distance-20 frame (1080p: 151103 pixels on level 38, 242441 on level 44);
- textured sky + haze, second surface + 2x2 smoothing + motion blur, roll 0x60 + pitch 0x18 at 2560x1080;
- interpolation (alpha 1 = current tick exactly, alpha 0 moves things);
- **cell-list cycle** (a thing's `cell_next` pointing at itself): `render_view` and `render_view_ext` both
  return (5 ms).

Faithful gates after the changes (Release, build_A): `render_reference_test` 1396/1396, `render_reference_hud`
2734/2734, `render_reference2_test` 3334/3334, `render_reference_options` 3634/3634 pixel-identical;
`render_test`, `sprites_test`, `reference_test` pass.

### Screenshots (`<scratchpad>\round7_A\`, PPM + PNG; 1080p+ PNGs at half size)

- Before / after: `vista_l38_before_faithful_640x480.png` (original renderer) vs
  `vista_l38_after_1920x1080_dd20.png`, `_dd64.png`, `_dd127.png` (same camera, 12 cells above the player;
  castles and the valley appear far beyond the old fog wall); same for level 44.
- `faithful_l38_640x480.png` vs `ext_l38_640x480_dd20_lod0.png`: the extended path at the original's
  settings frames the same picture.
- `ext_l44_1920x1080_dd127_haze.png` (sea hazing into the cloud sky), `ext_l38_2560x1080_dd127_roll.png`,
  `ext_l*_1920x1080_dd127_options.png` (reflections + smoothing + blur), `perf_l38_1920x1080_dd80.png`
  (mana balls, carpet, shadows), `lod_grid.png` (320x200, lod 0 / 1 / 2, levels 38 / 44).

### Performance (Release; `MC_REXT_PERF=1 render_ext_test`; ms per frame, mean / worst of 32 frames over a full turn; Intel Core i7-14650HX laptop, 24 hardware threads)

Level 44 after 2000 ticks (154 things), player view + 0x100, textured sky, haze:

| size | dd 20 | dd 48 | dd 80 | dd 127 |
|---|---|---|---|---|
| 640x480, threads auto | 0.71 / 0.85 | 0.94 / 1.06 | 1.31 / 1.51 | 2.13 / 2.66 |
| 1920x1080, auto | 0.97 / 1.23 | 1.40 / 1.70 | 1.92 / 2.07 | 2.86 / 4.13 |
| 2560x1440, auto | 1.08 / 1.56 | 1.73 / 2.36 | 2.28 / 2.56 | 3.23 / 3.51 |
| 3840x2160, auto | 1.49 / 1.93 | 2.41 / 2.83 | 3.25 / 3.67 | 4.48 / 5.50 |
| 640x480, 1 thread | 0.87 / 1.03 | 1.47 / 2.17 | 2.19 / 2.54 | 3.37 / 5.49 |
| 1920x1080, 1 thread | 1.97 / 2.24 | 3.47 / 4.22 | 5.94 / 6.89 | 7.57 / 8.83 |
| 2560x1440, 1 thread | 2.97 / 3.52 | 5.06 / 6.08 | 8.47 / 9.74 | 10.74 / 12.38 |
| 3840x2160, 1 thread | 5.69 / 6.67 | 9.56 / 11.60 | 15.41 / 17.40 | 19.08 / 21.49 |

Level 38 after 2000 ticks (390 things), terrain fills the screen, untextured sky, original fade:

| size | dd 20 | dd 48 | dd 80 | dd 127 |
|---|---|---|---|---|
| 640x480, threads auto | 1.46 / 1.66 | 1.74 / 2.19 | 2.29 / 2.54 | 3.68 / 6.28 |
| 1920x1080, auto | 1.72 / 1.87 | 2.18 / 2.76 | 2.92 / 4.25 | 4.45 / 6.06 |
| 2560x1440, auto | 1.99 / 2.22 | 2.45 / 3.63 | 3.47 / 6.06 | 4.90 / 5.77 |
| 3840x2160, auto | 2.71 / 2.88 | 3.33 / 3.63 | 4.20 / 5.08 | 5.93 / 6.76 |
| 640x480, 1 thread | 1.97 / 2.29 | 2.39 / 2.73 | 3.23 / 3.91 | 4.92 / 5.74 |
| 1920x1080, 1 thread | 4.46 / 4.72 | 5.60 / 6.00 | 7.47 / 9.24 | 10.32 / 13.93 |
| 2560x1440, 1 thread | 6.49 / 6.89 | 8.08 / 8.68 | 10.76 / 13.35 | 14.57 / 18.65 |
| 3840x2160, 1 thread | 12.30 / 13.14 | 14.94 / 17.07 | 19.92 / 26.24 | 25.22 / 33.11 |

At 1080p / distance 127 the frame splits into ~1.6 - 3.1 ms of serial list building (traversal of up to 65k
cells, projection, things) and ~6 ms of rasterisation on one thread / ~1 ms on 24, so the multi-threaded
time is dominated by the serial part.

## Deviations / gaps

- Near plane: proper clipping at z = 32 instead of the original's vertex cull (z < -0xff) + z clamp (0x80):
  differs only for terrain within ~1/2 cell of the camera.
- Sprite blitter is a new affine blit (same rectangle, modes and selection; not the original's line walk,
  so sub-pixel placement of rotated sprites can differ by a pixel; the odd-octant edge quirk is absent).
- SIRDS / anaglyph / interlaced stereo are not in the extended path (the faithful path does not port SIRDS
  either). The automatic speed ghost blend of 320x200 is not applied.
- Mip levels and merged quads change the far look by design (lod 0 = original texturing at every distance).
- Texture property tables are indexed with the same 164-entry guard as render_landscape.cpp.
- Mix tables / mips are built from `g_palette6` (rebuilt when it or the atlas changes; ~30 ms once); palette
  effects (fades, flashes) act after the renderer on indices, as for the original.

## Requested shared-file changes

None required. (Optional, integrator: list `render_ext.cpp` / `render_ext_raster.cpp` in the source lists of
any future unit test that wants the extended path; the reference tests do not.)

## For task C

`render_things.cpp` now reads things only through `thing_at()` and bounds both cell walks by
`thing_pool_slots()` (index check and link budget), so a pool > 1000 needs nothing more here. The extended
path does the same.

## mcport integration (main.cpp; for task D / the integrator)

With task B's compositor installed, nothing more: it calls `render_view_ext` at the display size. Without the
compositor (extended renderer at the game frame's own size, e.g. while B's path is off):

```cpp
#include "render.h"
#include "settings.h"
// after config_load, before the game loop (and again when the setting changes):
if (g_settings.render_extended && !g_render_view_override)
    g_render_view_override = render_view_ext_target;     // draws into the view window of the game frame
else if (!g_settings.render_extended && g_render_view_override == render_view_ext_target)
    g_render_view_override = nullptr;                      // back to render_view (the original)
```

Task E's config file should carry the section-A keys: `render_extended`, `draw_distance` (4..127),
`fog_start_pct` (0..100), `lod` (0..2), `fog_mode` (0..2), `render_threads` (0..16), `thing_min_px` (1..16).

## Next round

- GPU renderer: **not needed** for draw distance or native resolution - the software path is at 2 - 6 ms per
  frame multi-threaded up to 4K / 127 cells and under 10 ms single-threaded at 1080p / 80 cells. A GPU path is
  only worth it for things software cannot do cheaply: texture filtering / HD texture packs, true
  per-pixel fog / perspective-correct mapping, very high refresh at 4K+ on weak CPUs. If the user wants those,
  round 8 could add it beside this renderer (same display list: triangles + sprites in painter's order map
  directly to a GPU draw list with an 8-bit palette lookup in the shader).
- Cheap improvements if wanted: build the display list in parallel (rows split across threads) to cut the
  ~2 - 3 ms serial part at 127 cells; bin commands per band; dithered mip transitions; a fog colour taken from
  the sky texture's horizon row when the sky is untextured.
- Look at the extended path in motion in mcport (interpolated camera, LOD popping at 320x200, haze bands).
