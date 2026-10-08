# Landscape renderer port (render_landscape.cpp) - agent report, 2026-10-06

Files: `src/mcengine/render_landscape.cpp`, `render.h` (API kept, additions below),
`src/mcengine/tables/render.tables` -> `gen/render_tables.h`, `src/tests/render_test.cpp`,
`render_test_raster_stub.cpp`, `render_test.cmake`. `render_test.exe` -> OK, warning-free.

## Functions translated

| port | original |
|---|---|
| `render_landscape(const Camera&)` | render_landscape_29050 (7320 B): camera globals, quadrant selection, 40x21 vertex grid, rotation/projection/culling/fog, sky or clear, both vertex-pass variants (plain / second surface), roll transform + off-screen flags, mirrored pass, far-to-near terrain pass with modes 5/7/0x1a, Thing hook |
| `render_build_roll_table(int)` | render_build_roll_table_28580: all 8 octants incl. exact-diagonal cases, index list at `g_work_buf+0xe7e0`, the 12 DAT_000b58xx clip/extent globals, the delta pass |
| `render_sky(int)` | render_sky_2f080: per-column byte deltas, yaw scroll, roll rotation; `g_sky[(v<<8)|u]` |
| `render_set_view_window(fb,size)` | render_set_view_window_2f3c0: 320 (`4n + (5n/2)*w`) and 640 (`8n + (12n/2)*w`) variants |
| `render_view(fb, cam)` | render_view_2f6e0 mono path: slope smoothing (DAT_00093f7c/80), auto motion-blur enable, render_landscape, 2x2 smoothing, motion-blur blend with `g_frame2`; anaglyph / SIRDS / interlaced left as comments |

Not translated: SIRDS depth pass inside 29050 (fill mode 1, `(0x1400-z)*0x15e`),
render_cell_things_2c600/2dac0/2e5a0 (hook `g_render_cell_things(first_thing, rec)`; the mirrored
pass draws terrain only).

## Argument order / calling conventions (verified in disassembly)

- render_frame_1fab0 pushes 8 args for render_view_2f6e0: `(fb, cam_x(+0x3655), cam_y(+0x3657),
  yaw(+0x365b), cam_z(+0x3659)+0x80, pitch(+0x365d), roll(+0x365f), zoom(+0x3661))` from the player
  position-history entry. render_view passes the last 7 to render_landscape in the same order.
- render_landscape calls `render_build_roll_table((-roll) & 0x7ff)` (negated) and `render_sky(roll)`
  (yaw scroll comes from DAT_000b58a8 inside render_sky).
- poly_fill_triangle_722e3(v0, v1, v2): stack args; each vertex 5 dwords `{x, y, u, v, shade}`.
- render_set_viewport_78dd5(dest, tex, pitch, w, h): the motion-blur path calls it as
  `(g_frame2,0,0,0,0)` then `(saved_dest,0,0,0,0)`.

## UV corner mapping (exact)

r0 = record (row r, col c), r1 = r0+1, r2 = r0-39, r3 = r0-40; `uv = g_uv_table + uv_sel*8`: r0 gets
(uv[0],uv[1]), r1 (uv[2],uv[3]), r2 (uv[4],uv[5]), r3 (uv[6],uv[7]). Triangles: flags bit0 clear ->
(r0,r1,r2),(r0,r2,r3); set -> (r0,r1,r3),(r3,r1,r2). Mirrored pass: (r0,r2,r1),(r0,r3,r2) /
(r0,r3,r1),(r3,r2,r1). u runs along world x, v along world y.
`uv_sel = ((g_map_flags[texcell] >> 2) & 0x1c) + quadrant`; the exe's table already holds 0x1fffff for B=32.

## Camera parameters (exact meaning)

- cam_x/cam_y: 16-bit world coords. Quadrant `q = ((yaw+0x100)>>9)&3`, in-quadrant angle
  `a' = ((yaw+0x100)&0x1ff)-0x100`. `x_cam = (cos a'*lat>>16) - (sin a'*fwd>>16)`,
  `z_cam = (sin a'*lat>>16) + (cos a'*fwd>>16)`. q0 looks -y, q1 +x, q2 +y, q3 -x.
- cam_z: world height; `h_rel = height*0x20 - cam_z`. `sx = x_cam*focal/z`, `sy = h_rel*focal/z + horizon`,
  z clamped >= 0x80, cull `z < -0xff || x^2+z^2 >= 0x1900000`.
- pitch: `horizon = pitch*width >> 8` pixels, screen y = cy - sy.
- roll: `sx' = cx + (cos*sx - sin*sy)>>16`, `sy' = cy - (sy*cos + sx*sin)>>16`; positive = counter-clockwise.
- zoom: `focal = isqrt(w^2+h^2) * zoom >> 8` (0x100 -> 377 px at 320x200; the game uses 0x80).
- shade: `(light<<16) + 0x8000 + wave*8`, wave = `(sin(tick*0x40 + x*0x80)>>8)*(sin(tick*0x40 + y*0x80)>>8)`
  only on flag-bit3 cells (which also lower the vertex by `wave>>10`); fog scales shade by
  `(0x1690000-d2)/0x880000` between 0xe10000 and 0x1690000. Mode 7 colour = `(sum of 4 shades) >> 18`.
- Second surface: `h_mirror = -cam_z - (((wave>>4)+0x8000)*h >> 10)` (reflection in z=0), drawn before
  the terrain when `cam_z < 0x1000`, texture-0 cells skipped, mode forced to 5.

## Additions to render.h

`g_anim_tick`; `MC_GRID_COLS/ROWS/CELLS, kColStart, kRowStart, kCullDist2, kFogFar2, kFogNear2, kFogDiv`;
`RenderCamState g_rcam` (DAT_000b58xx camera globals for the sprite renderer), `g_eye_offset`
(DAT_00093b1c); `RollEntry g_roll_table[641]` (+1 zero guard because the exe reads the dword before
DAT_000b3a10), `g_roll_entries()`, `MC_ROLL_LIST_OFFSET`, `RollState g_roll`.

render.tables: `g_eye_offset_default` (0x93b1c), `g_slope_smooth_default` (0x93f7c/80),
`g_sprite_mode_lit/fogged` (0x93f48/4e: `0,0,2,3,4,5` / `1,0,6,7,4,5`).

## Verification

Synthetic terrain at yaw 0/0x200/0x400/0x600, roll 0x40 variants, one 640x480 frame; guard bands
intact, sky top / terrain bottom, frames differ, roll-table sanity (0x100 diagonal: steps = height;
0x80: 132-133 steps = tan22.5*320; 0x7c0: 63 steps). Images show no seams, correct occlusion, fog.

Bug found: Ghidra's `g_trig_table[]` starts 0x100 entries before the sine table, so
`g_trig_table[a-0x500]` is cos(a); the first roll-table version was off by 0x100.

## Deviations / open questions

- Texture property tables (164 entries) indexed with a bounds guard; the exe reads past them for ids >= 164.
- 32-bit wrap (`mul32`) kept for the roll transform products.
- `DAT_0012edae == 1` replaced by `fb.width == 320`; `DAT_00093f78 = 0x14` (stereo only) not modelled.
- Motion blur blends `g_frame2` from offset 0 while the dest has the view-window offset, as the exe does.

## Corrections for ENGINE.md

- 2.1: render_view_2f6e0 takes 8 args; its slope smoothing nudges **cam_x / cam_y** (low-passed,
  clamped +/-100 height-map slope), not the view angle.
- render_build_roll_table_28580 is called with `(-roll) & 0x7ff`; render_sky_2f080 with roll
  (yaw scroll = yaw*0x8000>>16 texels; 256 texels span the view width; v = 0 at the horizon). Sky pointer
  is DAT_000adf48, plain `[ebx+edx]` addressing (open question 2.10.2 closed); per-pixel u/v deltas are
  applied after sampling.
- 2.5: when opt_second_surface == 0 the vertex pass never sets flags bit7 / flags2 bit4, so everything
  draws with mode 5. The mirrored pass is a reflection in z = 0 drawn before the terrain, not a ceiling.
- 2.5 item 4: bit0 = parity of the vertex's own cell `(cx+cy)&1`.
- 2.7: DAT_000b3a10 entry = {delta to previous offset, byte offset, minor steps so far}.
- 2.4: 2x2 smoothing = `BLEND[BLEND[p11<<8|p01]<<8 | BLEND[p10<<8|p00]]`; motion blur mode 1 =
  `BLEND[dest<<8|prev]`, other values `BLEND[prev<<8|dest]`.
