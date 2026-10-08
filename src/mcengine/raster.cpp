// raster.cpp - the software triangle rasteriser of carpet.exe.
//
// Translated from the disassembly of poly_fill_triangle_722e3 (0x722E3-0x78DD5: vertex sorter,
// 4 cases x 4 edge-setup routines, 25 span fillers for the 27 fill modes), render_set_viewport_78dd5
// and render_draw_line_clipped_78e23. The decompiled C of the big function is unusable; every
// arithmetic step below follows the asm (labels as in ghidra/names/carpet_labels.csv, stack slots
// as [esp+..] in the comments so the code can be checked against the disassembly).
//
// =====================================================================================================
// FILL CONVENTIONS (what the original does, pixel for pixel)
//
// Vertices: three int[5] {x, y, u, v, shade}; x, y integer pixels of the render target, u/v/shade 16.16.
//
// 1. Winding. The sorter (0x722E3-0x72396) orders the vertices by y and accepts exactly one winding:
//    the vertices must run clockwise on screen (y down), i.e. (v1-v0) x (v2-v0) > 0. Counter-clockwise
//    triangles are rejected silently (the "cmp slope; jle exit" at 0x72420 / 0x72D00 and the x-order
//    tests of the flat cases) - this is the engine's back-face cull. Degenerate triangles (three equal
//    y, a flat edge of zero or negative width, equal truncated 16.16 slopes) draw nothing.
//    Four cases: 1 = general, middle vertex right of the long edge (poly_edge_case1_*);
//                2 = general, middle vertex left of the long edge (poly_edge_case2_*);
//                3 = flat bottom (case3), 4 = flat top (case4).
//
// 2. Rows. One SpanRec per scanline for y in [top.y, bottom.y): the top vertex's row is drawn, the
//    bottom vertex's row is not (a flat-bottom triangle does not draw its flat edge, a flat-top one
//    does). Edge x is sampled at integer y with no half-pixel offset: x(y) = X0<<16 + (y-Y0)*dxdy where
//    dxdy = ((X1-X0)<<16) / (Y1-Y0) with idiv (truncation toward zero), accumulated by repeated 32-bit
//    addition. Both edges start exactly at the top vertex's x; in case 1 the right edge is re-seeded
//    to M.x<<16 at the middle row, in case 2 the left edge is.
//
// 3. Pixels. Each span covers x in [floor(x_left), floor(x_right)) - the span fillers read the high
//    16-bit words of the two 16.16 edge values (SpanRec +2 and +6; "count" in mc_types.h is really the
//    integer part of x_right) and the count is x_right_int - x_left_int. Right-exclusive, so two
//    triangles sharing an edge (same two vertices) neither overlap nor leave a gap: a rectangle split
//    along either diagonal fills exactly [x0,x1) x [y0,y1).
//
// 4. Clipping. Vertical: top.y >= height -> nothing; top.y < 0 -> the edge walk is pre-stepped by
//    -top.y rows (gradient * rows, exact); if the bottom / middle vertex is below the target the row
//    counts are clamped so only rows < height are produced ([esp+0x63]/[esp+0x64] flags).
//    Horizontal, per span: x_right is clamped to width; x_left < 0 starts the span at x = 0 with u, v,
//    shade advanced by (-x_left) * gradient (32-bit product); spans with x_right <= 0 or an empty
//    count are skipped. The clipped output is therefore identical to the crop of an unclipped render.
//
// 5. Gradients across a span (per pixel, 16.16, idiv):
//    case 1: du_dx = (M.u - (T.u + (B.u-T.u)*hTM/hTB)) / (w+1), w = M.x - (T.x + (B.x-T.x)*hTM/hTB)
//    case 2: du_dx = ((T.u + (B.u-T.u)*hTM/hTB) - M.u) / (w+1), w = (T.x + (B.x-T.x)*hTM/hTB) - M.x
//            (w = integer width of the row through the middle vertex, products/quotients exact 64-bit
//            imul/idiv truncated toward zero; note the +1). When w == 0 the original leaves the
//            gradients uninitialised (stale stack); the port uses 0.
//    case 3: du_dx = (BR.u - BL.u) / (BR.x - BL.x);  case 4: du_dx = (TR.u - TL.u) / (TR.x - TL.x).
//    Gradients along the edge are always taken on the LEFT edge: case 1 (B.u-T.u)/hTB for all rows;
//    case 2 (M.u-T.u)/hTM for the upper rows then (B.u-M.u)/hMB continuing from the accumulated value
//    (not re-seeded at M); case 3 (BL.u-T.u)/h; case 4 (B.u-TL.u)/h.
//
// 6. Per-pixel arithmetic: u, v, shade are 16.16 but only 24 bits are kept (add dx,lo / adc bl,hi):
//    the integer part is a byte that wraps at 256. texel = TEX[(v_int << 8) | u_int], SHADE index =
//    (level_byte << 8) | colour, BLEND index = (a << 8) | b. The tables are read with a 16-bit index
//    (levels >= 64 read past the 64 shade rows into the blend table, as in the original).
//    Mode 26 (water) is not a counter: texel values < 12 are blended with the destination
//    (BLEND[dest<<8 | SHADE[g<<8|texel]]), texel values >= 12 are drawn opaque (SHADE[g<<8|texel]).
//
// 7. Row count limit: at most g_rt_height records are produced, g_span_table holds MC_MAX_SPAN_ROWS.
// =====================================================================================================
#include "raster.h"
#include "mc_globals.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

uint8_t       *g_rt_dest     = nullptr;   // DAT_0009b5f4
uint8_t       *g_rt_prev_row = nullptr;   // DAT_0009b5f0 (= dest - pitch, kept for consumers; never dereferenced here)
int            g_rt_pitch    = 320;       // DAT_0009b5fc
int            g_rt_width    = 320;       // DAT_0009b600
int            g_rt_height   = 200;       // DAT_0009b604
const uint8_t *g_texture_ptr = nullptr;   // DAT_0009b5f8
uint8_t        g_fill_mode   = 0;         // DAT_0009e309
uint8_t        g_fill_colour = 0;         // DAT_0009e308
SpanRec        g_span_table[MC_MAX_SPAN_ROWS];   // 0x9B608

namespace {

// ---- 32-bit two's-complement helpers: add/sub/imul wrap, idiv truncates toward zero -----------------
inline int32_t wadd(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t wsub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t wmul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
inline int32_t shl16(int32_t a)           { return (int32_t)((uint32_t)a << 16); }
// cdq ; idiv r/m32
inline int32_t idiv32(int32_t a, int32_t d) { return (int32_t)((int64_t)a / d); }
// imul r/m32 ; idiv r/m32 (the 64-bit product edx:eax is divided)
inline int32_t muldiv(int32_t a, int32_t b, int32_t d) { return (int32_t)(((int64_t)a * (int64_t)b) / d); }

// What the edge setup leaves for the span filler.
struct EdgeOut {
    int     first_row;              // screen row of g_span_table[0] (= max(top.y, 0)); row ptr [esp]
    int     rows;                   // [esp+0x20] number of records
    int32_t du_dx, dv_dx, ds_dx;    // [esp+0x24] [esp+0x30] [esp+0x3c] per-pixel gradients (16.16)
};

// One record: +0 x_left 16.16, +4 x_right 16.16, +8 u, +0xc v, +0x10 shade. The fillers only read the
// integer words at +2 and +6 (SpanRec::x_left / SpanRec::count).
inline void span_write(SpanRec &r, int32_t xl, int32_t xr, int32_t u, int32_t v, int32_t s) {
    r.unk0   = (uint16_t)((uint32_t)xl);
    r.x_left = (int16_t)((uint32_t)xl >> 16);     // floor(x_left)
    r.unk4   = (uint16_t)((uint32_t)xr);
    r.count  = (uint16_t)((uint32_t)xr >> 16);    // floor(x_right), exclusive
    r.u = u; r.v = v; r.shade = s;
}

// Common prologue of the four cases (0x72396 / 0x72C76 / 0x735BA / 0x73A39): row pointer and the
// negative-top flag [esp+0x62]. Returns false when the top vertex is below the target.
inline bool edge_prologue(int top_y, bool &top_neg, EdgeOut &out) {
    if (top_y < 0) { top_neg = true; out.first_row = 0; return true; }   // [esp] = DAT_0009b5f0
    if (top_y >= g_rt_height) return false;
    top_neg = false; out.first_row = top_y;                              // [esp] = prev_row + y*pitch
    return true;
}

// poly_edge_case1_{texshade,tex,shade,flat} (0x724C5..) with mid_right = true and
// poly_edge_case2_* (0x72DA5..) with mid_right = false. T = top, M = middle, B = bottom (strictly
// increasing y). The four gradient variants only differ in which gradients they compute and store;
// computing all of them is output-equivalent, so one routine serves the 27 modes.
bool edge_setup_general(bool mid_right, const PolyVertex *T, const PolyVertex *M, const PolyVertex *B,
                        EdgeOut &out)
{
    const int height = g_rt_height;
    const int top_y  = T->y;                                   // [esp+0x54]
    bool top_neg;
    if (!edge_prologue(top_y, top_neg, out)) return false;
    bool m_clip = M->y > height;                               // [esp+0x63]
    bool b_clip = B->y > height;                               // [esp+0x64]
    int  hTB   = B->y - top_y;                                 // long edge height
    int  hTM   = M->y - top_y;                                 // upper short edge height
    int  total = hTB;                                          // [esp+0x20]
    const int32_t dxdy_TB = idiv32(shl16(wsub(B->x, T->x)), hTB);
    const int32_t dxdy_TM = idiv32(shl16(wsub(M->x, T->x)), hTM);
    // the middle vertex must lie on the expected side of the long edge, else exit (back-face cull)
    if (mid_right ? (dxdy_TM <= dxdy_TB) : (dxdy_TB <= dxdy_TM)) return false;
    int  hMB = B->y - M->y;                                    // [esp+0x18]
    const int32_t dxdy_MB = idiv32(shl16(wsub(B->x, M->x)), hMB);
    const int32_t mid_x16 = shl16(M->x);                       // [esp+0x1c]

    // --- gradients across the span from the integer width of the row through M ---------------------
    int32_t du_dx = 0, dv_dx = 0, ds_dx = 0;
    {
        int32_t w;
        if (mid_right) w = wadd(wsub(M->x, T->x), muldiv(wsub(T->x, B->x), hTM, hTB));
        else           w = wadd(wsub(T->x, M->x), muldiv(wsub(B->x, T->x), hTM, hTB));
        if (w < 0) return false;                               // jl poly_fill_exit_73f74
        if (w != 0) {                                          // je: gradients left uninitialised
            w = wadd(w, 1);
            if (mid_right) {
                du_dx = idiv32(wsub(wadd(muldiv(wsub(T->u, B->u), hTM, hTB), M->u), T->u), w);
                dv_dx = idiv32(wsub(wadd(muldiv(wsub(T->v, B->v), hTM, hTB), M->v), T->v), w);
                ds_dx = idiv32(wsub(wadd(muldiv(wsub(T->shade, B->shade), hTM, hTB), M->shade), T->shade), w);
            } else {
                du_dx = idiv32(wsub(wadd(muldiv(wsub(B->u, T->u), hTM, hTB), T->u), M->u), w);
                dv_dx = idiv32(wsub(wadd(muldiv(wsub(B->v, T->v), hTM, hTB), T->v), M->v), w);
                ds_dx = idiv32(wsub(wadd(muldiv(wsub(B->shade, T->shade), hTM, hTB), T->shade), M->shade), w);
            }
        }
    }
    // --- gradients along the left edge, upper and lower part --------------------------------------
    int32_t du_up, dv_up, ds_up, du_lo, dv_lo, ds_lo;
    if (mid_right) {                                           // left edge = long edge T->B
        du_up = du_lo = idiv32(wsub(B->u, T->u), hTB);         // [esp+0x28]
        dv_up = dv_lo = idiv32(wsub(B->v, T->v), hTB);         // [esp+0x34]
        ds_up = ds_lo = idiv32(wsub(B->shade, T->shade), hTB); // [esp+0x40]
    } else {                                                   // left edge = T->M then M->B
        du_up = idiv32(wsub(M->u, T->u), hTM);                 // [esp+0x28]
        dv_up = idiv32(wsub(M->v, T->v), hTM);                 // [esp+0x34]
        ds_up = idiv32(wsub(M->shade, T->shade), hTM);         // [esp+0x40]
        du_lo = idiv32(wsub(B->u, M->u), hMB);                 // [esp+0x2c]
        dv_lo = idiv32(wsub(B->v, M->v), hMB);                 // [esp+0x38]
        ds_lo = idiv32(wsub(B->shade, M->shade), hMB);         // [esp+0x44]
    }
    const int32_t dxl_up = mid_right ? dxdy_TB : dxdy_TM;
    const int32_t dxr_up = mid_right ? dxdy_TM : dxdy_TB;
    const int32_t dxl_lo = mid_right ? dxdy_TB : dxdy_MB;
    const int32_t dxr_lo = mid_right ? dxdy_MB : dxdy_TB;

    int32_t xl = shl16(T->x), xr = xl;                         // eax, ebx
    int32_t u = T->u, v = T->v, s = T->shade;                  // ecx, edx, esi
    bool upper = true;
    if (top_neg) {
        const int skip = -top_y;                               // [esp+0x48]
        total -= skip;
        if (total <= 0) return false;
        if (skip < hTM) {
            // part of the upper half is visible: pre-step both edges by `skip` rows
            hTM -= skip;
            xl = wadd(xl, wmul(dxl_up, skip)); xr = wadd(xr, wmul(dxr_up, skip));
            u = wadd(u, wmul(du_up, skip)); v = wadd(v, wmul(dv_up, skip)); s = wadd(s, wmul(ds_up, skip));
            if (b_clip) {
                total = height;
                if (m_clip) hTM = height;
                else { const int lower = height - hTM; m_clip = lower <= 0; hMB = lower; }
            }
        } else {
            // the whole upper half is above the target: walk the long edge down to M's row, re-seed
            // the bent side at M.x, then pre-step both by the remaining rows
            if (mid_right) { xl = wadd(xl, wmul(dxdy_TB, hTM)); }
            else           { xr = wadd(xr, wmul(dxdy_TB, hTM)); }
            u = wadd(u, wmul(du_up, hTM)); v = wadd(v, wmul(dv_up, hTM)); s = wadd(s, wmul(ds_up, hTM));
            if (mid_right) xr = mid_x16; else xl = mid_x16;
            const int rest = skip - hTM;
            hMB -= rest;
            xl = wadd(xl, wmul(dxl_lo, rest)); xr = wadd(xr, wmul(dxr_lo, rest));
            u = wadd(u, wmul(du_lo, rest)); v = wadd(v, wmul(dv_lo, rest)); s = wadd(s, wmul(ds_lo, rest));
            if (b_clip) { hMB = height; total = height; }
            upper = false;
        }
    } else if (b_clip) {
        total = height - top_y;
        if (m_clip) hTM = total;
        else { const int lower = total - hTM; m_clip = lower <= 0; hMB = lower; }
    }

    SpanRec *rec = g_span_table;
    if (upper) {
        do {                                                   // 0x726C7 (dec [esp+0x14]; jne)
            span_write(*rec++, xl, xr, u, v, s);
            xl = wadd(xl, dxl_up); xr = wadd(xr, dxr_up);
            u = wadd(u, du_up); v = wadd(v, dv_up); s = wadd(s, ds_up);
        } while (--hTM != 0);
        if (mid_right) xr = mid_x16; else xl = mid_x16;        // mov ebx/eax, [esp+0x1c]
    }
    if (!m_clip) {
        do {                                                   // 0x7270B (dec [esp+0x18]; jne)
            span_write(*rec++, xl, xr, u, v, s);
            xl = wadd(xl, dxl_lo); xr = wadd(xr, dxr_lo);
            u = wadd(u, du_lo); v = wadd(v, dv_lo); s = wadd(s, ds_lo);
        } while (--hMB != 0);
    }
    out.rows = total; out.du_dx = du_dx; out.dv_dx = dv_dx; out.ds_dx = ds_dx;
    return true;
}

// poly_edge_case3_* (0x736A6.., flat bottom, flat_top = false): A = top, R = bottom-right, L = bottom-left
// poly_edge_case4_* (0x73B25.., flat top,    flat_top = true ): A = top-left, R = top-right, L = bottom
// (register roles esi = A, edi = R, ecx = L). The sorter guarantees R.x > the other flat vertex's x.
bool edge_setup_flat(bool flat_top, const PolyVertex *A, const PolyVertex *R, const PolyVertex *L,
                     EdgeOut &out)
{
    const int height = g_rt_height;
    const int top_y  = A->y;                                   // [esp+0x54]
    bool top_neg;
    if (!edge_prologue(top_y, top_neg, out)) return false;
    bool clip = L->y > height;                                 // [esp+0x63]
    int  h     = L->y - top_y;                                 // [esp+0x10]
    int  total = h;                                            // [esp+0x20]
    const int32_t dxdy_L = idiv32(shl16(wsub(L->x, A->x)), h);                      // [esp+4]
    const int32_t dxdy_R = flat_top ? idiv32(shl16(wsub(L->x, R->x)), h)             // [esp+8]
                                    : idiv32(shl16(wsub(R->x, A->x)), h);
    // across the flat edge: case 3 (R - L) / (R.x - L.x), case 4 (R - A) / (R.x - A.x)
    const PolyVertex *P = flat_top ? A : L;
    const int32_t w = wsub(R->x, P->x);
    const int32_t du_dx = idiv32(wsub(R->u, P->u), w);         // [esp+0x24]
    const int32_t dv_dx = idiv32(wsub(R->v, P->v), w);         // [esp+0x30]
    const int32_t ds_dx = idiv32(wsub(R->shade, P->shade), w); // [esp+0x3c]
    // along the left edge A -> L
    const int32_t du_dy = idiv32(wsub(L->u, A->u), h);         // [esp+0x28]
    const int32_t dv_dy = idiv32(wsub(L->v, A->v), h);         // [esp+0x34]
    const int32_t ds_dy = idiv32(wsub(L->shade, A->shade), h); // [esp+0x40]

    int32_t xl = shl16(A->x);
    int32_t xr = flat_top ? shl16(R->x) : xl;
    int32_t u = A->u, v = A->v, s = A->shade;
    if (top_neg) {
        const int skip = -top_y;                               // [esp+0x48]
        h -= skip; total -= skip;
        if (total <= 0) return false;
        xl = wadd(xl, wmul(dxdy_L, skip)); xr = wadd(xr, wmul(dxdy_R, skip));
        u = wadd(u, wmul(du_dy, skip)); v = wadd(v, wmul(dv_dy, skip)); s = wadd(s, wmul(ds_dy, skip));
        if (clip) total = h = height;
    } else if (clip) {
        total = h = height - top_y;
    }
    SpanRec *rec = g_span_table;
    do {                                                       // 0x73796 (dec [esp+0x10]; jne)
        span_write(*rec++, xl, xr, u, v, s);
        xl = wadd(xl, dxdy_L); xr = wadd(xr, dxdy_R);
        u = wadd(u, du_dy); v = wadd(v, dv_dy); s = wadd(s, ds_dy);
    } while (--h != 0);
    out.rows = total; out.du_dx = du_dx; out.dv_dx = dv_dx; out.ds_dx = ds_dx;
    return true;
}

// ---- span fillers (poly_span_table_73eab) ----------------------------------------------------------
// One loop for all modes; MODE selects the per-pixel operation of span_mXX_*. Every filler walks the
// records, advances the destination row by the pitch, clips the span to [0, width) as described above
// and keeps u / v / shade in 24 bits (16 fraction + one wrapping integer byte). The 16x unrolling and
// the Duff's-device entry of modes 5 / 6 (tables at 0x748D5 / 0x74A05 / 0x74DA0) do not change the
// result. The texel / shade used for a pixel are the values before that pixel's increment.
template <int MODE>
void fill_spans(const EdgeOut &eo)
{
    constexpr bool uses_tex   = !(MODE == 0 || MODE == 1 || MODE == 4 || MODE == 14 || MODE == 15 ||
                                  MODE == 16 || MODE == 17);
    constexpr bool uses_shade = MODE == 1 || MODE == 4 || MODE == 5 || MODE == 6 || MODE == 16 ||
                                MODE == 17 || MODE == 20 || MODE == 21 || MODE == 24 || MODE == 25 ||
                                MODE == 26;
    const int       width  = g_rt_width;                       // DAT_0009b600
    const uint8_t  *shade  = g_shade_table();                  // 0xB99B0
    const uint8_t  *blend  = g_blend_table();                  // 0xBD9B0
    const uint8_t  *tex    = g_texture_ptr;                    // DAT_0009b5f8
    const uint32_t  colour = g_fill_colour;                    // DAT_0009e308
    const uint32_t  du = (uint32_t)eo.du_dx, dv = (uint32_t)eo.dv_dx, ds = (uint32_t)eo.ds_dx;
    (void)shade; (void)blend; (void)tex; (void)colour;
    uint8_t *row = g_rt_dest + (ptrdiff_t)eo.first_row * g_rt_pitch;   // [esp] + pitch
    for (int r = 0; r < eo.rows; ++r, row += g_rt_pitch) {
        const SpanRec &rec = g_span_table[r];
        const int      xl = rec.x_left;                        // mov ax/bx, [esi+2] (signed)
        const unsigned xr = rec.count;                         // movzx ecx, [esi+6]
        uint32_t u = (uint32_t)rec.u, v = (uint32_t)rec.v, s = (uint32_t)rec.shade;
        int      n;
        uint8_t *p;
        if (xl < 0) {
            if ((int16_t)xr <= 0) continue;                    // or cx,cx ; jle
            n = (int)xr; if (n > width) n = width;             // cmp ecx,[width] ; mov ecx,[width]
            const uint32_t pre = (uint16_t)(-xl);              // neg ax ; movzx eax, ax
            u += pre * du; v += pre * dv; s += pre * ds;       // imul + add (32-bit, low 24 used)
            p = row;
        } else {
            n = (int)xr; if (n > width) n = width;
            n = (int16_t)(n - xl);                             // sub cx, ax ; jle
            if (n <= 0) continue;
            p = row + xl;
        }
        if constexpr (MODE == 0) {                             // span_m00_flat_73f17: rep stosb
            std::memset(p, (int)colour, (size_t)n);
            continue;
        } else
        for (int i = 0; i < n; ++i) {
            uint32_t t = 0, g = 0;
            if constexpr (uses_tex)   t = tex[((v >> 8) & 0xff00u) | ((u >> 16) & 0xffu)];
            if constexpr (uses_shade) g = (s >> 16) & 0xffu;
            uint8_t &d = p[i];
            if constexpr (MODE == 1) {                         // span_m01_colour_ramp_73f79
                d = (uint8_t)g;
            } else if constexpr (MODE == 2) {                  // span_m02_textured_74135
                d = (uint8_t)t;
            } else if constexpr (MODE == 3) {                  // span_m03_textured_keyed_743d1
                if (t) d = (uint8_t)t;
            } else if constexpr (MODE == 4) {                  // span_m04_flat_shaded_746b1
                d = shade[(g << 8) | colour];
            } else if constexpr (MODE == 5) {                  // span_m05_textured_gouraud_74915
                d = shade[(g << 8) | t];
            } else if constexpr (MODE == 6) {                  // span_m06_textured_gouraud_keyed_74cb0
                if (t) d = shade[(g << 8) | t];
            } else if constexpr (MODE == 7) {                  // span_m07_textured_flatlit_7508b (7, 11)
                d = shade[(colour << 8) | t];
            } else if constexpr (MODE == 8) {                  // span_m08_textured_flatlit_keyed_75391
                if (t) d = shade[(colour << 8) | t];
            } else if constexpr (MODE == 9) {                  // span_m09_shadow_756d7 (9, 10)
                if (t) d = shade[(t << 8) | d];
            } else if constexpr (MODE == 12) {                 // span_m12_textured_tint_75a4a
                d = blend[(t << 8) | colour];
            } else if constexpr (MODE == 13) {                 // span_m13_textured_tint_swapped_75d4f
                d = blend[(colour << 8) | t];
            } else if constexpr (MODE == 14) {                 // span_m14_flat_translucent_76055
                d = blend[(colour << 8) | d];
            } else if constexpr (MODE == 15) {                 // span_m15_flat_translucent_swapped_761ea
                d = blend[((uint32_t)d << 8) | colour];
            } else if constexpr (MODE == 16) {                 // span_m16_shaded_translucent_7637e
                d = blend[((uint32_t)shade[(g << 8) | colour] << 8) | d];
            } else if constexpr (MODE == 17) {                 // span_m17_shaded_translucent_swapped_76637
                d = blend[((uint32_t)d << 8) | shade[(g << 8) | colour]];
            } else if constexpr (MODE == 18) {                 // span_m18_textured_translucent_768f0
                d = blend[(t << 8) | d];
            } else if constexpr (MODE == 19) {                 // span_m19_textured_translucent_swapped_76c1f
                d = blend[((uint32_t)d << 8) | t];
            } else if constexpr (MODE == 20) {                 // span_m20_gouraud_translucent_76f4e
                d = blend[((uint32_t)shade[(g << 8) | t] << 8) | d];
            } else if constexpr (MODE == 21) {                 // span_m21_gouraud_translucent_swapped_773c7
                d = blend[((uint32_t)d << 8) | shade[(g << 8) | t]];
            } else if constexpr (MODE == 22) {                 // span_m22_textured_translucent_keyed_77840
                if (t) d = blend[(t << 8) | d];
            } else if constexpr (MODE == 23) {                 // span_m23_..._keyed_swapped_77bb3
                if (t) d = blend[((uint32_t)d << 8) | t];
            } else if constexpr (MODE == 24) {                 // span_m24_gouraud_translucent_keyed_77f26
                if (t) d = blend[((uint32_t)shade[(g << 8) | t] << 8) | d];
            } else if constexpr (MODE == 25) {                 // span_m25_..._keyed_swapped_783df
                if (t) d = blend[((uint32_t)d << 8) | shade[(g << 8) | t]];
            } else if constexpr (MODE == 26) {                 // span_m26_water_78898: cmp al,0xc ; jae
                const uint8_t sh = shade[(g << 8) | t];
                d = (t < 0xc) ? blend[((uint32_t)d << 8) | sh] : sh;
            }
            u += du; v += dv; s += ds;
        }
    }
}

// poly_span_table_73eab: fill mode -> filler (27 entries, 25 routines; 10 -> 9, 11 -> 7).
void fill_dispatch(const EdgeOut &eo)
{
    switch (g_fill_mode) {
    case 0:  fill_spans<0>(eo);  break;
    case 1:  fill_spans<1>(eo);  break;
    case 2:  fill_spans<2>(eo);  break;
    case 3:  fill_spans<3>(eo);  break;
    case 4:  fill_spans<4>(eo);  break;
    case 5:  fill_spans<5>(eo);  break;
    case 6:  fill_spans<6>(eo);  break;
    case 7:  fill_spans<7>(eo);  break;
    case 8:  fill_spans<8>(eo);  break;
    case 9:  fill_spans<9>(eo);  break;
    case 10: fill_spans<9>(eo);  break;
    case 11: fill_spans<7>(eo);  break;
    case 12: fill_spans<12>(eo); break;
    case 13: fill_spans<13>(eo); break;
    case 14: fill_spans<14>(eo); break;
    case 15: fill_spans<15>(eo); break;
    case 16: fill_spans<16>(eo); break;
    case 17: fill_spans<17>(eo); break;
    case 18: fill_spans<18>(eo); break;
    case 19: fill_spans<19>(eo); break;
    case 20: fill_spans<20>(eo); break;
    case 21: fill_spans<21>(eo); break;
    case 22: fill_spans<22>(eo); break;
    case 23: fill_spans<23>(eo); break;
    case 24: fill_spans<24>(eo); break;
    case 25: fill_spans<25>(eo); break;
    case 26: fill_spans<26>(eo); break;
    default: break;   // the original would jump through garbage past the table; the port draws nothing
    }
}

} // namespace

// render_set_viewport_78dd5(dest, texture, pitch, width, height): zero keeps the current value.
void render_set_viewport(uint8_t *dest, const uint8_t *texture, int pitch, int width, int height)
{
    if (pitch)   g_rt_pitch = pitch;
    if (dest) {
        g_rt_dest = dest;
        // DAT_0009b5f0 = dest - pitch; formed through uintptr_t so no pointer arithmetic leaves the buffer
        g_rt_prev_row = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(dest) -
                                                    (uintptr_t)(intptr_t)g_rt_pitch);
    }
    if (texture) g_texture_ptr = texture;
    if (height)  g_rt_height = height;
    if (width)   g_rt_width = width;
}

// poly_fill_triangle_722e3: the vertex sorter (0x722E3-0x72396) then one of the four cases.
// Register roles after the sort: esi / edi / ecx as in the asm (case 1: top / middle / bottom;
// case 2: top / bottom / middle; case 3: top / bottom-right / bottom-left; case 4: top-left /
// top-right / bottom). Every "jle poly_fill_exit" of the sorter is a `return`.
void poly_fill_triangle(const PolyVertex *v0, const PolyVertex *v1, const PolyVertex *v2)
{
    const PolyVertex *esi = v0, *edi = v1, *ecx = v2;
    const int ya = v0->y, yb = v1->y, yc = v2->y;
    int kase;
    if (ya == yb) {                                            // 0x72351
        if (ya == yc) return;
        if (ya < yc) {                                         // 0x7236E flat top: v0 left, v1 right
            if (v1->x <= v0->x) return;
            kase = 4;                                          // esi=v0 edi=v1 ecx=v2
        } else {                                               // 0x7235B flat bottom: v0 right, v1 left
            if (v0->x <= v1->x) return;
            esi = v2; edi = v0; ecx = v1; kase = 3;
        }
    } else if (ya > yb) {                                      // 0x72317
        if (ya == yc) {                                        // 0x735AC flat bottom: v2 right, v0 left
            if (v2->x <= v0->x) return;
            esi = v1; edi = v2; ecx = v0; kase = 3;
        } else if (ya < yc) {                                  // 0x72C72: v1 top, v0 middle, v2 bottom
            esi = v1; edi = v2; ecx = v0; kase = 2;
        } else if (yb == yc) {                                 // 0x73A2B flat top: v1 left, v2 right
            if (v2->x <= v1->x) return;
            esi = v1; edi = v2; ecx = v0; kase = 4;
        } else if (yb < yc) {                                  // 0x72338: v1 top, v2 middle, v0 bottom
            esi = v1; edi = v2; ecx = v0; kase = 1;
        } else {                                               // 0x7232F: v2 top, v1 middle, v0 bottom
            esi = v2; edi = v0; ecx = v1; kase = 2;
        }
    } else {                                                   // ya < yb
        if (ya == yc) {                                        // 0x7233E flat top: v2 left, v0 right
            if (v0->x <= v2->x) return;
            esi = v2; edi = v0; ecx = v1; kase = 4;
        } else if (ya > yc) {                                  // 0x72311: v2 top, v0 middle, v1 bottom
            esi = v2; edi = v0; ecx = v1; kase = 1;
        } else if (yb == yc) {                                 // 0x7237D flat bottom: v1 right, v2 left
            if (v1->x <= v2->x) return;
            kase = 3;                                          // esi=v0 edi=v1 ecx=v2
        } else if (yb > yc) {                                  // 0x72390: v0 top, v2 middle, v1 bottom
            kase = 2;                                          // esi=v0 edi=v1 ecx=v2
        } else {                                               // 0x72396: v0 top, v1 middle, v2 bottom
            kase = 1;
        }
    }
    EdgeOut eo;
    bool ok;
    switch (kase) {
    case 1:  ok = edge_setup_general(true,  esi, edi, ecx, eo); break;   // T, M, B
    case 2:  ok = edge_setup_general(false, esi, ecx, edi, eo); break;   // T, M (ecx), B (edi)
    case 3:  ok = edge_setup_flat(false, esi, edi, ecx, eo); break;      // T, BR, BL
    default: ok = edge_setup_flat(true,  esi, edi, ecx, eo); break;      // TL, TR, B
    }
    if (ok) fill_dispatch(eo);
}

// render_draw_line_clipped_78e23(int *p0, int *p1): the coordinates are truncated to 16 bits and
// clipped parametrically to [0,width-1] x [0,height-1] (16-bit imul/idiv, truncation toward zero),
// then drawn in g_fill_colour: horizontal -> run, vertical -> pitch loop, else Bresenham with the
// start pixel plus `major` steps (|dx| or dy, whichever is larger; ties are x-major).
// Quirks kept (0x79167 horizontal path): the two x values are ordered with an UNSIGNED compare
// (cmp cx,di / jae), so a horizontal line with one end at x < 0 is mis-ordered: (-50..150) draws
// nothing (the "min" 150 is beyond width-1) and (-50..50) draws x = 50..width-1; a line entering
// from x < 0 that ends exactly at x = 0 draws nothing (or cx,cx / jle).
void render_draw_line(int ax0, int ay0, int ax1, int ay1)
{
    int16_t x0 = (int16_t)ax0, y0 = (int16_t)ay0, x1 = (int16_t)ax1, y1 = (int16_t)ay1;
    const int16_t W = (int16_t)g_rt_width, H = (int16_t)g_rt_height;
    const int pitch = g_rt_pitch;
    const uint8_t col = g_fill_colour;
    uint8_t *const dest = g_rt_dest;
    // 16-bit imul ; idiv (32-bit product, quotient truncated to 16 bits)
    auto mdiv = [](int a, int b, int c) -> int16_t { return (int16_t)((a * b) / c); };
    auto swap16 = [](int16_t &a, int16_t &b) { int16_t t = a; a = b; b = t; };
    auto hrun = [&](int xs, int len, int y) {                 // 0x791D5: len+1 pixels from (xs, y)
        std::memset(dest + (ptrdiff_t)y * pitch + xs, col, (size_t)(uint16_t)(len + 1));
    };

    // ---- y clipping ---------------------------------------------------------------------------------
    bool clip_bottom = false;
    if (y0 >= 0 && y1 < 0) {                                   // 0x78ED2: make the negative end y0
        swap16(x0, x1); swap16(y0, y1);
    }
    if (y0 < 0) {                                              // 0x78E58
        if (y1 < 0) return;
        x0 = (int16_t)(x0 + mdiv(x1 - x0, -y0, y1 - y0));
        y0 = 0;
        clip_bottom = y1 >= H;
    } else if (y0 == y1) {                                     // 0x79167 horizontal, unclipped path
        if (y0 >= H) return;
        int16_t lo = x0, hi = x1;
        if ((uint16_t)hi < (uint16_t)lo) swap16(lo, hi);
        const int16_t maxx = (int16_t)(W - 1);
        if (lo < 0) {
            if (hi <= 0) return;                               // quirk: x = 0 end point not drawn
            lo = 0;
            if ((uint16_t)hi > (uint16_t)maxx) hi = maxx;
        } else {
            if (lo > maxx) return;
            if ((uint16_t)hi > (uint16_t)maxx) hi = maxx;
        }
        hrun(lo, hi - lo, y0);
        return;
    } else if (y0 < y1) {                                      // 0x78F3A
        if (y0 >= H) return;
        clip_bottom = y1 >= H;
    } else {                                                   // 0x78F06: swap so that y0 < y1
        if (y1 >= H) return;
        swap16(x0, x1); swap16(y0, y1);
        clip_bottom = y1 >= H;
    }
    if (clip_bottom) {                                         // 0x78E97
        x1 = (int16_t)(mdiv(x1 - x0, H - y0, y1 - y0) + x0);
        y1 = (int16_t)(H - 1);
    }
    // ---- x clipping (0x78F54) ------------------------------------------------------------------------
    int dir;
    if (x0 < 0) {
        if (x1 < 0) return;
        dir = 1;
        y0 = (int16_t)(y0 + mdiv(y1 - y0, -x0, x1 - x0));
        x0 = 0;
        if (x1 >= W) {
            y1 = (int16_t)(mdiv(y1 - y0, W - x0, x1) + y0);     // divides by x1 (x0 is 0 here)
            x1 = (int16_t)(W - 1);
        }
    } else if (x0 >= W) {                                      // 0x78FD8
        if (x1 >= W) return;
        dir = -1;
        y0 = (int16_t)(y0 + mdiv(y1 - y0, x0 - W, x0 - x1));
        x0 = (int16_t)(W - 1);
        if (x1 < 0) {
            y1 = (int16_t)(y1 - mdiv(y1 - y0, -x1, -x1 + x0));
            x1 = 0;
        }
    } else if (x1 < 0) {                                       // 0x79055
        dir = -1;
        y1 = (int16_t)(mdiv(y1 - y0, x0, x0 - x1) + y0);
        x1 = 0;
    } else if (x1 >= W) {                                      // 0x79086
        dir = 1;
        y1 = (int16_t)(mdiv(y1 - y0, W - x0, x1 - x0) + y0);
        x1 = (int16_t)(W - 1);
    } else {                                                   // 0x790BE
        dir = 1;
        if (x0 == x1) {                                        // 0x791FA vertical
            int16_t ys = y0, len = (int16_t)(y1 - y0);
            if ((uint16_t)y1 < (uint16_t)y0) { len = (int16_t)(y0 - y1); ys = y1; }
            uint8_t *p = dest + (ptrdiff_t)ys * pitch + x0;
            for (unsigned k = (uint16_t)(len + 1); k != 0; --k) { *p = col; p += pitch; }
            return;
        }
        if (x0 > x1) dir = -1;
    }
    // ---- Bresenham (0x790D7) -----------------------------------------------------------------------
    uint8_t *p = dest + (ptrdiff_t)y0 * pitch + x0;
    const int16_t adx = (int16_t)((x1 - x0) * dir);            // imul ax, si
    const int16_t dy  = (int16_t)(y1 - y0);
    if (dy == 0) {                                             // 0x791BD horizontal after clipping
        int xs = x0, len = x1 - x0;
        if ((uint16_t)x1 < (uint16_t)x0) { len = x0 - x1; xs = x1; }
        hrun(xs, len, y0);
        return;
    }
    int16_t major, minor; ptrdiff_t step_major, step_minor;
    if ((uint16_t)dy > (uint16_t)adx) { major = dy; minor = adx; step_major = pitch; step_minor = dir; }
    else                              { major = adx; minor = dy; step_major = dir; step_minor = pitch; }
    const int16_t two_minor = (int16_t)(minor + minor);
    int16_t err = (int16_t)(two_minor - major);                // bx
    const int16_t two_diff = (int16_t)((minor - major) * 2);   // cx
    *p = col;
    for (unsigned k = (uint16_t)major; k != 0; --k) {
        p += step_major;
        if (err < 0) { err = (int16_t)(err + two_minor); }
        else         { p += step_minor; err = (int16_t)(err + two_diff); }
        *p = col;
    }
}
