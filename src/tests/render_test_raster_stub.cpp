// Stand-in implementation of raster.h for render_test: stores the render-target globals, fills
// triangles with a plain scanline rasteriser that honours the fill modes the landscape uses
// (0/4 flat, 5/0x1a shaded texture, 7 flat-lit texture) and clips to the viewport. It exists only
// so the landscape renderer can be linked and tested before the real raster.cpp is finished.
#include "raster.h"
#include "mc_globals.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

uint8_t       *g_rt_dest = nullptr;
uint8_t       *g_rt_prev_row = nullptr;
int            g_rt_pitch = 0;
int            g_rt_width = 0;
int            g_rt_height = 0;
const uint8_t *g_texture_ptr = nullptr;
uint8_t        g_fill_mode = 0;
uint8_t        g_fill_colour = 0;
SpanRec        g_span_table[MC_MAX_SPAN_ROWS];

void render_set_viewport(uint8_t *dest, const uint8_t *texture, int pitch, int width, int height) {
    if (pitch) g_rt_pitch = pitch;
    if (dest) { g_rt_dest = dest; g_rt_prev_row = dest - g_rt_pitch; }
    if (texture) g_texture_ptr = texture;
    if (height) g_rt_height = height;
    if (width) g_rt_width = width;
}

static inline uint8_t shade_pixel(int64_t u, int64_t v, int64_t s) {
    const uint8_t *shade = g_shade_table();
    int lvl = (int)(s >> 16);
    if (lvl < 0) lvl = 0;
    if (lvl > 0x3f) lvl = 0x3f;
    uint8_t texel = 0;
    if (g_texture_ptr) texel = g_texture_ptr[(((unsigned)(v >> 16) & 0xff) << 8) | ((unsigned)(u >> 16) & 0xff)];
    switch (g_fill_mode) {
    case 0:  return g_fill_colour;
    case 4:  return shade[(lvl << 8) | g_fill_colour];
    case 7: case 8: case 11: return shade[((unsigned)g_fill_colour << 8) | texel];
    case 2: case 3: return texel;
    case 5: case 6: case 0x1a: return shade[(lvl << 8) | texel];
    default: return shade[(lvl << 8) | texel];
    }
}

// Linear interpolation helper along an edge in 16.16 (64-bit intermediates).
struct Edge {
    int64_t x, u, v, s;        // current values (16.16)
    int64_t dx, du, dv, ds;    // per scanline
};
static Edge make_edge(const PolyVertex *a, const PolyVertex *b) {
    Edge e;
    const int64_t dy = (int64_t)b->y - a->y;
    e.x = (int64_t)a->x << 16; e.u = a->u; e.v = a->v; e.s = a->shade;
    if (dy > 0) {
        e.dx = (((int64_t)b->x - a->x) << 16) / dy;
        e.du = ((int64_t)b->u - a->u) / dy;
        e.dv = ((int64_t)b->v - a->v) / dy;
        e.ds = ((int64_t)b->shade - a->shade) / dy;
    } else {
        e.dx = e.du = e.dv = e.ds = 0;
    }
    return e;
}

static void fill_span(int y, const Edge &l, const Edge &r) {
    if (y < 0 || y >= g_rt_height) return;
    int64_t xl = l.x, xr = r.x, ul = l.u, ur = r.u, vl = l.v, vr = r.v, sl = l.s, sr = r.s;
    if (xl > xr) { std::swap(xl, xr); std::swap(ul, ur); std::swap(vl, vr); std::swap(sl, sr); }
    int x0 = (int)(xl >> 16), x1 = (int)(xr >> 16);
    if (x1 <= x0) return;
    const int64_t n = x1 - x0;
    const int64_t du = (ur - ul) / n, dv = (vr - vl) / n, ds = (sr - sl) / n;
    int64_t u = ul, v = vl, s = sl;
    if (x0 < 0) { u += du * -x0; v += dv * -x0; s += ds * -x0; x0 = 0; }
    if (x1 > g_rt_width) x1 = g_rt_width;
    uint8_t *row = g_rt_dest + (size_t)y * g_rt_pitch;
    for (int x = x0; x < x1; ++x) {
        row[x] = shade_pixel(u, v, s);
        u += du; v += dv; s += ds;
    }
}

void poly_fill_triangle(const PolyVertex *v0, const PolyVertex *v1, const PolyVertex *v2) {
    const PolyVertex *p[3] = {v0, v1, v2};
    if (p[0]->y > p[1]->y) std::swap(p[0], p[1]);
    if (p[1]->y > p[2]->y) std::swap(p[1], p[2]);
    if (p[0]->y > p[1]->y) std::swap(p[0], p[1]);
    const int y0 = p[0]->y, y1 = p[1]->y, y2 = p[2]->y;
    if (y0 == y2 || y0 >= g_rt_height || y2 < 0) return;
    Edge longe = make_edge(p[0], p[2]);
    Edge top = make_edge(p[0], p[1]);
    Edge bot = make_edge(p[1], p[2]);
    for (int y = y0; y < y2; ++y) {
        if (y == y1) bot = make_edge(p[1], p[2]);
        const Edge &shorte = y < y1 ? top : bot;
        fill_span(y, longe, shorte);
        longe.x += longe.dx; longe.u += longe.du; longe.v += longe.dv; longe.s += longe.ds;
        if (y < y1) { top.x += top.dx; top.u += top.du; top.v += top.dv; top.s += top.ds; }
        else        { bot.x += bot.dx; bot.u += bot.du; bot.v += bot.dv; bot.s += bot.ds; }
    }
}

void render_draw_line(int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < g_rt_width && y0 >= 0 && y0 < g_rt_height)
            g_rt_dest[(size_t)y0 * g_rt_pitch + x0] = g_fill_colour;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
