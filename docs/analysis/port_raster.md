# Rasteriser port (raster.cpp) - agent report, 2026-10-06

Files: `src/mcengine/raster.cpp`, `raster.h` (comments only, API unchanged), `src/tests/raster_test.cpp`,
`raster_test.cmake`. No `raster.tables` needed (the only exe tables are code-address jump tables).
`raster_test.exe` -> all checks passed; writes `raster_test_scene.ppm`.

## What was translated (label -> code)

| Original | Port |
|---|---|
| `poly_fill_triangle_722e3` sorter 0x722E3-0x72396 (+ case entries 0x72C72, 0x735AC, 0x73A2B) | `poly_fill_triangle()` - 15 branches annotated with asm addresses; picks case 1-4 and the vertex roles |
| `poly_edge_case1_*` (0x724C5/72744/72963/72B26), `poly_edge_case2_*` (0x72DA5/73051/7328E/73460) | `edge_setup_general(mid_right, T, M, B)` - the four gradient variants only differ in which gradients they store |
| `poly_edge_case3_*` (0x736A6..) flat bottom, `poly_edge_case4_*` (0x73B25..) flat top | `edge_setup_flat(flat_top, A, R, L)` |
| `poly_span_table_73eab` + 25 fillers `span_m00_flat_73f17` .. `span_m26_water_78898` | `fill_spans<MODE>()` template + `fill_dispatch()` (10->9, 11->7); the 16x unrolling / Duff's device entries (offset table 0x748D5, jump tables 0x74A05/0x74DA0) are folded into a plain loop |
| `render_set_viewport_78dd5` | `render_set_viewport()` (pitch first, then dest/prev_row, texture, height, width; zero keeps) |
| `render_draw_line_clipped_78e23` | `render_draw_line(x0,y0,x1,y1)`, 16-bit semantics kept |

## Fill conventions (also at the top of raster.cpp)

1. **Winding / back-face cull**: only clockwise triangles (screen y down, `(v1-v0)x(v2-v0) > 0`) are
   drawn; CCW and degenerate triangles are rejected silently (0x72420, 0x72D00). Any cyclic rotation of
   the vertices gives identical output.
2. **Rows**: `[top.y, bottom.y)`. Edge x sampled at integer y, no half-pixel offset:
   `x = X0<<16 + k*dxdy`, `dxdy = ((X1-X0)<<16)/dy` (idiv), accumulated by 32-bit adds. The bent side is
   re-seeded to `M.x<<16` at the middle row.
3. **Pixels**: `[floor(x_left), floor(x_right))` - half-open; two triangles sharing an edge never
   overlap nor leave a gap (60 rectangle cases verified).
4. **Clipping**: top.y >= height -> nothing; top.y < 0 -> edge walk pre-stepped by -top.y rows;
   bottom/middle below height -> row counts clamped. Per span: x_right clamped to width; x_left < 0 ->
   start at 0 with u/v/shade advanced by `(-x_left)*gradient`. Clipped render == crop of unclipped
   render for 600 random triangles over 17 modes.
5. **Gradients across the span**: case 1 `du_dx = (M.u - (T.u + (B.u-T.u)*hTM/hTB)) / (w+1)`, case 2
   mirrored (w = integer width of the row through M, 64-bit imul/idiv, note the +1); flat cases
   `(R.u - L.u)/(R.x - L.x)`. Along the edge always the LEFT edge; in case 2 the per-row step switches
   from (M-T)/hTM to (B-M)/hMB at the middle row continuing from the accumulated value.
6. **Per-pixel**: u, v, shade kept in 24 bits (16 fraction + wrapping integer byte); texel =
   `TEX[(v_int<<8)|u_int]`; the texel/shade of a pixel are the values before that pixel's increment.
   SHADE/BLEND are indexed with 16-bit indices (level bytes >= 64 read into the blend table, as the
   original; still inside `g_tables_image`).

## Tests

(a) rectangles / CCW draws nothing; (b) clip == crop, guard bands; (c) mode 2 with `tex = u+3v` at
scales 2/3/5 maps every pixel exactly, mode 3 key; (d) 27 modes x 120 random triangles with random
tables, no OOB; (e) lines: exact pixel counts, clipping, 3000 random lines; (f) 320x200 PPM.

## Deviations / open questions

- w == 0 in cases 1/2 (slivers): the original uses stale stack gradients; the port uses 0.
- Fill mode > 26: original jumps through garbage; port draws nothing.
- Line drawer quirks reproduced: the horizontal path (0x7917C) orders x0/x1 with an unsigned compare,
  so a horizontal line with a negative x end point is mis-ordered ((-50..150) draws nothing,
  (-50..50) draws x = 50..width-1); a horizontal line from x < 0 ending exactly at x = 0 draws nothing.
- Targets taller than 480 rows are not supported (MC_MAX_SPAN_ROWS).

## Corrections to ENGINE.md / mc_types.h

1. Vertices are **int[5] {x, y, u, v, shade}**, not int[3].
2. `SpanRec` +6 is the **integer part of x_right (exclusive end)**, not a pixel count; +0/+4 hold the
   full 16.16 x_left / x_right (only the high words are read).
3. Mode 26 (water): `cmp al,0xc` tests the **texel value**: texel < 12 ->
   `BLEND[dest<<8 | SHADE[g<<8|texel]]`, else opaque `SHADE[g<<8|texel]`.
4. Only CW triangles are drawn; half-open fill rule; a negative top row is pre-stepped, not rejected.
5. Mode 1: pixel = shade integer byte (the `mov al, colour` is dead code).
6. Inner jump tables 0x74A05/0x74DA0 are Duff's-device entries indexed by `count & 15`, paired with the
   dword offset table at 0x748D5.
