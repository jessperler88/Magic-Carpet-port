// Software rasteriser of carpet.exe: render target globals (render_set_viewport_78dd5), the
// triangle filler poly_fill_triangle_722e3 with its 27 fill modes, and the clipped line drawer.
// Owner: raster.cpp. Consumers: render_landscape.cpp, sprites, HUD.
#pragma once
#include <cstdint>
#include "mc_types.h"

// Render target (DAT_0009b5f4 / 5f0 / 5fc / 600 / 604) and the current texture (DAT_0009b5f8).
extern uint8_t       *g_rt_dest;      // destination pixels (top-left of the viewport)
extern uint8_t       *g_rt_prev_row;  // dest - pitch (DAT_0009b5f0)
extern int            g_rt_pitch;     // bytes per row (0x140 or 0x280)
extern int            g_rt_width;     // clip width  (pixels)
extern int            g_rt_height;    // clip height (rows)
extern const uint8_t *g_texture_ptr;  // 256-byte row stride texture; texel = tex[(v << 8) | u]

// Fill parameters (DAT_0009e309 / DAT_0009e308).
extern uint8_t g_fill_mode;           // 0..26, see table below
extern uint8_t g_fill_colour;         // constant colour (modes 0, 4, 12..17) or light level (modes 7, 8, 11)

// Per-scanline edge records written by the edge setup and consumed by the span fillers (0x9b608).
// Actual layout as used by the asm: +0 x_left 16.16, +4 x_right 16.16 (SpanRec::count is the integer
// part of x_right, i.e. the exclusive end; pixel count = count - x_left), +8 u, +0xc v, +0x10 shade.
// One record per scanline of the current triangle, so g_rt_height must not exceed MC_MAX_SPAN_ROWS.
constexpr int MC_MAX_SPAN_ROWS = 480;
extern SpanRec g_span_table[MC_MAX_SPAN_ROWS];

// render_set_viewport_78dd5(dest, texture, pitch, width, height): a zero argument keeps the current value.
void render_set_viewport(uint8_t *dest, const uint8_t *texture, int pitch, int width, int height);

// Triangle vertex as the original passes it: three pointers to int[5] {x, y, u, v, shade}.
// x, y are screen pixels relative to the render target; u, v are texture coordinates in 16.16
// (0 .. (B<<16)-1 from g_uv_table); shade is the light level in 16.16 (0 .. 0x20<<16).
struct PolyVertex {
    int32_t x, y, u, v, shade;
};

// poly_fill_triangle_722e3. Fill mode / colour / texture come from the globals above. Vertices may
// start at any of the three but must run CLOCKWISE on screen (y down, (v1-v0) x (v2-v0) > 0);
// counter-clockwise and degenerate triangles draw nothing (this is the engine's back-face cull).
// Partially off-screen triangles are clipped against [0,width) x [0,height). Fill rule: rows
// [top.y, bottom.y), pixels [floor(x_left), floor(x_right)) - half-open, so adjacent triangles
// sharing two vertices neither overlap nor leave gaps. Details at the top of raster.cpp.
//
// mode | pixel  (g = shade gradient integer byte, texel = tex[(v_int << 8) | u_int], both wrap at 256)
//  0   | constant colour
//  1   | colour index = shade gradient (palette ramp)
//  2/3 | texel (3: texel 0 transparent)
//  4   | SHADE[g][colour]
//  5/6 | SHADE[g][texel] (6: keyed)
//  7,11/8 | SHADE[colour][texel] (8: keyed)
//  9,10| dest = SHADE[texel][dest] where texel != 0 (shadow map)
// 12/13| BLEND[texel][colour] / BLEND[colour][texel]
// 14/15| BLEND[colour][dest] / BLEND[dest][colour]
// 16/17| BLEND[SHADE[g][colour]][dest] and swapped
// 18/19| BLEND[texel][dest] and swapped
// 20/21| BLEND[SHADE[g][texel]][dest] and swapped
// 22/23| as 18/19 keyed;  24/25 as 20/21 keyed
// 26   | water: texel < 12 -> BLEND[dest][SHADE[g][texel]], texel >= 12 -> SHADE[g][texel]
void poly_fill_triangle(const PolyVertex *v0, const PolyVertex *v1, const PolyVertex *v2);

// render_draw_line_clipped_78e23: Bresenham line in g_fill_colour clipped to the render target
// (the original takes two int[2] {x, y} pointers and truncates the coordinates to 16 bits; both end
// points inclusive, max(|dx|, |dy|) + 1 pixels when unclipped).
void render_draw_line(int x0, int y0, int x1, int y1);
