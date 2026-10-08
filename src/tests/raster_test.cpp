// raster_test: exercises poly_fill_triangle / render_draw_line without game data (argv[1] ignored).
//  (a) mode 0: two triangles sharing a rectangle's diagonal cover exactly [x0,x1) x [y0,y1), no gaps,
//      no double writes, for several rectangles (odd sizes, 1-pixel wide/tall), both diagonals and
//      all cyclic vertex orders.
//  (b) clipping: the clipped render equals the crop of an unclipped render of the same triangle on a
//      big canvas, for many random triangles and modes; guard bands around the target stay untouched.
//  (c) mode 2 with a 256x256 texture tex[(v<<8)|u] = (u + 3v) & 0xff maps pixel (x,y) to (u,v) exactly,
//      including the 8-bit wrap.
//  (d) all 27 modes on random triangles with random tables: no crash, no write outside the target.
//  (e) lines: guard bands, pixel counts, horizontal/vertical/diagonal exactness.
//  (f) a 320x200 PPM with modes 0, 4, 5, 7, 14 and a synthetic palette next to the executable.
#define _CRT_SECURE_NO_WARNINGS
#include "mc_globals.h"
#include "raster.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("CHECK FAILED: %s (line %d)\n", #cond, __LINE__); g_failures++; } } while (0)

// ---- deterministic RNG --------------------------------------------------------------------------------
static uint32_t g_seed = 0x12345678u;
static uint32_t rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }
static int rnd_range(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

// ---- a render target with guard bands ----------------------------------------------------------------
struct Canvas {
    int w, h, guard, pitch;
    std::vector<uint8_t> buf;
    Canvas(int w_, int h_, int guard_) : w(w_), h(h_), guard(guard_), pitch(w_ + 2 * guard_),
        buf((size_t)(w_ + 2 * guard_) * (h_ + 2 * guard_), 0) {}
    uint8_t *dest() { return buf.data() + (size_t)guard * pitch + guard; }
    uint8_t &at(int x, int y) { return buf[(size_t)(y + guard) * pitch + (x + guard)]; }
    void fill(uint8_t v) { std::fill(buf.begin(), buf.end(), v); }
    void bind(const uint8_t *tex) { render_set_viewport(dest(), tex, pitch, w, h); }
    // true when nothing outside [0,w) x [0,h) differs from `pristine`
    bool guards_intact(const Canvas &pristine) const {
        for (int y = -guard; y < h + guard; y++)
            for (int x = -guard; x < w + guard; x++) {
                if (x >= 0 && x < w && y >= 0 && y < h) continue;
                size_t i = (size_t)(y + guard) * pitch + (x + guard);
                if (buf[i] != pristine.buf[i]) return false;
            }
        return true;
    }
};

static PolyVertex V(int x, int y, int u = 0, int v = 0, int s = 0) { PolyVertex p{x, y, u, v, s}; return p; }

// (v1-v0) x (v2-v0) in screen coordinates (y down); > 0 = clockwise = the winding the rasteriser accepts
static long long winding(const PolyVertex &a, const PolyVertex &b, const PolyVertex &c) {
    return (long long)(b.x - a.x) * (c.y - a.y) - (long long)(b.y - a.y) * (c.x - a.x);
}
static void tri(const PolyVertex &a, const PolyVertex &b, const PolyVertex &c) { poly_fill_triangle(&a, &b, &c); }

// =======================================================================================================
// (a) rectangle coverage
// =======================================================================================================
static void test_rectangles()
{
    std::printf("[a] rectangle coverage (mode 0)\n");
    const int W = 64, H = 48;
    Canvas c(W, H, 8), pristine(W, H, 8);
    g_fill_mode = 0;
    struct R { int x0, y0, x1, y1; };
    const R rects[] = { {0, 0, 64, 48}, {3, 5, 20, 17}, {10, 10, 11, 30}, {7, 20, 40, 21}, {1, 1, 2, 2},
                        {5, 3, 60, 44}, {17, 9, 38, 40}, {0, 0, 1, 1}, {30, 2, 33, 47}, {2, 30, 63, 33} };
    int cases = 0;
    for (const R &r : rects) {
        const PolyVertex A = V(r.x0, r.y0), B = V(r.x1, r.y0), C = V(r.x1, r.y1), D = V(r.x0, r.y1);
        // diagonal AC: (A,B,C) + (A,C,D); diagonal BD: (A,B,D) + (B,C,D) - all clockwise
        const PolyVertex tris[2][2][3] = { { {A, B, C}, {A, C, D} }, { {A, B, D}, {B, C, D} } };
        for (int diag = 0; diag < 2; diag++) {
            std::vector<uint8_t> ref;
            for (int rot = 0; rot < 3; rot++) {
                c.fill(0);
                c.bind(nullptr);
                for (int t = 0; t < 2; t++) {
                    const PolyVertex *p = tris[diag][t];
                    g_fill_colour = 1;
                    // count writes: render into a scratch canvas and add
                    Canvas s(W, H, 8); s.bind(nullptr);
                    tri(p[rot % 3], p[(rot + 1) % 3], p[(rot + 2) % 3]);
                    CHECK(s.guards_intact(pristine));
                    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) c.at(x, y) = (uint8_t)(c.at(x, y) + s.at(x, y));
                }
                c.bind(nullptr);
                int bad = 0;
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) {
                        const bool inside = x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
                        if (c.at(x, y) != (inside ? 1 : 0)) bad++;
                    }
                if (bad) std::printf("  rect (%d,%d)-(%d,%d) diag %d rot %d: %d bad pixels\n", r.x0, r.y0, r.x1, r.y1, diag, rot, bad);
                CHECK(bad == 0);
                if (rot == 0) ref.assign(c.buf.begin(), c.buf.end());
                else CHECK(ref == c.buf);   // vertex rotation does not change the output
                cases++;
            }
        }
    }
    // counter-clockwise triangles are culled
    c.fill(0); c.bind(nullptr); g_fill_colour = 1;
    tri(V(5, 5), V(5, 30), V(40, 30));
    tri(V(5, 5), V(5, 30), V(40, 5));
    tri(V(10, 3), V(3, 20), V(30, 30));
    int writes = 0;
    for (uint8_t b : c.buf) writes += b != 0;
    CHECK(writes == 0);
    std::printf("  %d rectangle cases checked, CCW triangles draw nothing: %s\n", cases, writes == 0 ? "yes" : "NO");
}

// =======================================================================================================
// synthetic tables / textures
// =======================================================================================================
static std::vector<uint8_t> g_tex(65536);

static void random_tables(uint8_t avoid)
{
    uint8_t *shade = g_shade_table(), *blend = g_blend_table();
    for (int i = 0; i < 0x4000; i++) { uint8_t v; do v = (uint8_t)rnd(); while (v == avoid); shade[i] = v; }
    for (int i = 0; i < 0x10000; i++) { uint8_t v; do v = (uint8_t)rnd(); while (v == avoid); blend[i] = v; }
    for (int i = 0; i < 65536; i++) { uint8_t v; do v = (uint8_t)rnd(); while (v == avoid); g_tex[i] = v; }
}

static PolyVertex random_vertex(int lo, int hi) {
    return V(rnd_range(lo, hi), rnd_range(lo, hi), rnd_range(0, (256 << 16) - 1), rnd_range(0, (256 << 16) - 1),
             rnd_range(0, 0x20 << 16));
}

// =======================================================================================================
// (b) clipping == crop of an unclipped render
// =======================================================================================================
static void test_clipping_equivalence()
{
    std::printf("[b] clipped render == crop of unclipped render\n");
    const int W = 160, H = 100, OFF = 300;        // big canvas 760 x 460 (< MC_MAX_SPAN_ROWS rows)
    Canvas small(W, H, 32), big(W + 2 * OFF, H + 2 * OFF, 0), pristine(W, H, 32);
    random_tables(0xa5);
    const int modes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 12, 14, 16, 18, 20, 22, 24, 26 };
    int tested = 0, drawn = 0;
    for (int iter = 0; iter < 600; iter++) {
        g_fill_mode = (uint8_t)modes[iter % (sizeof modes / sizeof modes[0])];
        g_fill_colour = (uint8_t)rnd();
        PolyVertex a = random_vertex(-120, 280), b = random_vertex(-120, 280), c = random_vertex(-120, 280);
        if (winding(a, b, c) < 0) std::swap(b, c);
        // same background in both (the blend modes read the destination)
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) small.at(x, y) = (uint8_t)((x * 7 + y * 13) ^ 0x3c);
        for (int y = -small.guard; y < H + small.guard; y++)
            for (int x = -small.guard; x < W + small.guard; x++)
                if (x < 0 || x >= W || y < 0 || y >= H) small.at(x, y) = 0xa5;
        pristine.buf = small.buf;
        big.fill(0);
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) big.at(x + OFF, y + OFF) = small.at(x, y);
        small.bind(g_tex.data());
        tri(a, b, c);
        big.bind(g_tex.data());
        PolyVertex a2 = a, b2 = b, c2 = c;
        a2.x += OFF; a2.y += OFF; b2.x += OFF; b2.y += OFF; c2.x += OFF; c2.y += OFF;
        tri(a2, b2, c2);
        bool same = true; int changed = 0;
        for (int y = 0; y < H && same; y++)
            for (int x = 0; x < W; x++) {
                if (small.at(x, y) != big.at(x + OFF, y + OFF)) { same = false; break; }
                changed += small.at(x, y) != pristine.at(x, y);
            }
        if (!same) std::printf("  mismatch iter %d mode %d: (%d,%d) (%d,%d) (%d,%d)\n", iter, g_fill_mode, a.x, a.y, b.x, b.y, c.x, c.y);
        CHECK(same);
        CHECK(small.guards_intact(pristine));
        tested++; drawn += changed != 0;
    }
    std::printf("  %d triangles compared, %d touched the target\n", tested, drawn);
}

// =======================================================================================================
// (c) texture mapping
// =======================================================================================================
static void test_texture_mapping()
{
    std::printf("[c] mode 2 texture mapping\n");
    for (int i = 0; i < 65536; i++) g_tex[i] = (uint8_t)((i & 0xff) + 3 * (i >> 8));   // tex[v<<8|u] = u + 3v
    const int W = 160, H = 160;
    Canvas c(W, H, 8), pristine(W, H, 8);
    g_fill_mode = 2;
    // square [x0,x0+n) x [y0,y0+n) with u = scale*(x-x0), v = scale*(y-y0) at the vertices
    struct Q { int x0, y0, n, scale; };
    const Q quads[] = { {0, 0, 64, 1}, {10, 20, 128, 1}, {5, 5, 128, 2}, {3, 7, 150, 3}, {20, 30, 100, 5} };
    for (const Q &q : quads) {
        c.fill(0xa5); pristine.fill(0xa5);
        c.bind(g_tex.data());
        const int S = q.scale << 16, e = q.n * S;
        const PolyVertex A = V(q.x0, q.y0, 0, 0), B = V(q.x0 + q.n, q.y0, e, 0),
                         C = V(q.x0 + q.n, q.y0 + q.n, e, e), D = V(q.x0, q.y0 + q.n, 0, e);
        tri(A, B, C); tri(A, C, D);
        int bad = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const bool inside = x >= q.x0 && x < q.x0 + q.n && y >= q.y0 && y < q.y0 + q.n;
                uint8_t expect = 0xa5;
                if (inside) {
                    const unsigned u = (unsigned)(q.scale * (x - q.x0)) & 0xff, v = (unsigned)(q.scale * (y - q.y0)) & 0xff;
                    expect = (uint8_t)(u + 3 * v);
                }
                if (c.at(x, y) != expect) bad++;
            }
        if (bad) std::printf("  quad at (%d,%d) size %d scale %d: %d bad texels\n", q.x0, q.y0, q.n, q.scale, bad);
        CHECK(bad == 0);
        CHECK(c.guards_intact(pristine));
    }
    // keyed mode 3 leaves texel 0 untouched: texture = 0 where u < 8
    for (int i = 0; i < 65536; i++) g_tex[i] = (uint8_t)(((i & 0xff) < 8) ? 0 : 7);
    c.fill(0xa5); c.bind(g_tex.data()); g_fill_mode = 3;
    tri(V(0, 0, 0, 0), V(64, 0, 64 << 16, 0), V(64, 64, 64 << 16, 64 << 16));
    tri(V(0, 0, 0, 0), V(64, 64, 64 << 16, 64 << 16), V(0, 64, 0, 64 << 16));
    int bad = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (c.at(x, y) != (x < 8 ? 0xa5 : 7)) bad++;
    CHECK(bad == 0);
    std::printf("  %d quads + keyed check done\n", (int)(sizeof quads / sizeof quads[0]));
}

// =======================================================================================================
// (d) all modes, random triangles, guard bands
// =======================================================================================================
static void test_all_modes()
{
    std::printf("[d] all 27 modes on random triangles\n");
    const int W = 200, H = 120;
    Canvas c(W, H, 48), pristine(W, H, 48);
    random_tables(0xa5);
    int touched[27] = {0};
    for (int mode = 0; mode < 27; mode++) {
        g_fill_mode = (uint8_t)mode;
        for (int iter = 0; iter < 120; iter++) {
            g_fill_colour = (uint8_t)rnd();
            if (g_fill_colour == 0xa5) g_fill_colour = 0x11;
            PolyVertex a = random_vertex(-300, 500), b = random_vertex(-300, 500), cc = random_vertex(-300, 500);
            if (iter % 7 == 0) { a = random_vertex(-20, 220); b = random_vertex(-20, 220); cc = random_vertex(-20, 220); }
            if (iter % 11 == 0) { b.y = a.y; }                    // flat edges
            if (iter % 13 == 0) { cc.x = a.x; cc.y = a.y; }       // degenerate
            if (winding(a, b, cc) < 0) std::swap(b, cc);
            c.fill(0xa5);
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) c.at(x, y) = (uint8_t)(rnd() | 1);
            pristine.buf = c.buf;
            c.bind(g_tex.data());
            tri(a, b, cc);
            CHECK(c.guards_intact(pristine));
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) touched[mode] += c.at(x, y) != pristine.at(x, y);
        }
    }
    std::printf("  pixels touched per mode:");
    for (int m = 0; m < 27; m++) std::printf(" %d", touched[m]);
    std::printf("\n");
    for (int m = 0; m < 27; m++) CHECK(touched[m] > 0);
}

// =======================================================================================================
// (e) lines
// =======================================================================================================
static void test_lines()
{
    std::printf("[e] render_draw_line\n");
    const int W = 100, H = 60;
    Canvas c(W, H, 40), pristine(W, H, 40);
    auto count = [&]() { int n = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) n += c.at(x, y) == 9; return n; };
    g_fill_colour = 9;
    // exact shapes
    c.fill(0); pristine.fill(0); c.bind(nullptr);
    render_draw_line(10, 5, 30, 5);  CHECK(count() == 21);
    for (int x = 10; x <= 30; x++) CHECK(c.at(x, 5) == 9);
    c.fill(0); render_draw_line(30, 7, 10, 7); CHECK(count() == 21);
    c.fill(0); render_draw_line(4, 3, 4, 40); CHECK(count() == 38);
    for (int y = 3; y <= 40; y++) CHECK(c.at(4, y) == 9);
    c.fill(0); render_draw_line(2, 2, 22, 22); CHECK(count() == 21);
    for (int i = 0; i <= 20; i++) CHECK(c.at(2 + i, 2 + i) == 9);
    c.fill(0); render_draw_line(22, 2, 2, 22); CHECK(count() == 21);
    for (int i = 0; i <= 20; i++) CHECK(c.at(22 - i, 2 + i) == 9);
    c.fill(0); render_draw_line(5, 10, 50, 20); CHECK(count() == 46);   // x-major: |dx|+1 pixels
    CHECK(c.at(5, 10) == 9 && c.at(50, 20) == 9);
    c.fill(0); render_draw_line(50, 20, 5, 10); CHECK(count() == 46);
    c.fill(0); render_draw_line(5, 10, 15, 50); CHECK(count() == 41);   // y-major: dy+1 pixels
    CHECK(c.at(5, 10) == 9 && c.at(15, 50) == 9);
    // clipped: endpoints on the border, guard bands untouched
    c.fill(0); render_draw_line(20, 30, 150, 30); CHECK(count() == W - 20);
    c.fill(0); render_draw_line(150, 31, 20, 31); CHECK(count() == W - 20);
    // original quirk (0x7917C unsigned x ordering): horizontal lines with a negative x end point are
    // mis-ordered - (-50..150) draws nothing, (-50..50) draws 50..W-1. Kept as in the original.
    c.fill(0); render_draw_line(-50, 30, 150, 30); CHECK(count() == 0);
    c.fill(0); render_draw_line(-50, 30, 50, 30);  CHECK(count() == W - 50 && c.at(50, 30) == 9 && c.at(49, 30) == 0);
    c.fill(0); render_draw_line(20, -100, 20, 300); CHECK(count() == H);
    c.fill(0); render_draw_line(-30, -30, 130, 90); CHECK(count() > 0);
    c.fill(0); render_draw_line(-100, 10, -5, 50); CHECK(count() == 0);
    c.fill(0); render_draw_line(10, 100, 50, 200); CHECK(count() == 0);
    CHECK(c.guards_intact(pristine));
    for (int iter = 0; iter < 3000; iter++) {
        c.fill(0);
        render_draw_line(rnd_range(-400, 500), rnd_range(-400, 500), rnd_range(-400, 500), rnd_range(-400, 500));
        if (!c.guards_intact(pristine)) { std::printf("  guard violation iter %d\n", iter); CHECK(false); break; }
    }
    std::printf("  line checks done\n");
}

// =======================================================================================================
// (f) PPM scene
// =======================================================================================================
static void write_scene(const char *exe_path)
{
    std::printf("[f] scene PPM\n");
    // synthetic palette: 8 hues x 32 intensities; SHADE[l][c] scales the intensity by l/32,
    // BLEND[a][b] keeps a's hue with the mean intensity
    static uint8_t pal[768];
    const int hues[8][3] = { {255,255,255}, {255,64,32}, {32,200,64}, {48,96,255}, {255,220,40}, {40,220,220}, {220,60,220}, {160,110,60} };
    for (int c = 0; c < 256; c++) { int h = c >> 5, i = c & 31; for (int k = 0; k < 3; k++) pal[c * 3 + k] = (uint8_t)(hues[h][k] * i / 31); }
    uint8_t *shade = g_shade_table(), *blend = g_blend_table();
    for (int l = 0; l < 64; l++) for (int c = 0; c < 256; c++) { int i = (c & 31) * l / 32; if (i > 31) i = 31; shade[(l << 8) | c] = (uint8_t)((c & 0xe0) | i); }
    for (int a = 0; a < 256; a++) for (int b = 0; b < 256; b++) blend[(a << 8) | b] = (uint8_t)((a & 0xe0) | (((a & 31) + (b & 31)) / 2));
    for (int v = 0; v < 256; v++) for (int u = 0; u < 256; u++) {
        int hue = ((u >> 5) + (v >> 5)) & 1 ? 2 : 4;            // checker of green / yellow
        int inten = 8 + ((u & 31) * 23) / 31;
        g_tex[(v << 8) | u] = (uint8_t)((hue << 5) | inten);
    }
    const int W = 320, H = 200;
    std::vector<uint8_t> frame((size_t)W * H, 3 << 5 | 6);   // dim blue background
    render_set_viewport(frame.data(), g_tex.data(), W, W, H);
    g_fill_mode = 0; g_fill_colour = (1 << 5) | 28;            // red flat
    tri(V(20, 20), V(120, 30), V(60, 110));
    g_fill_mode = 4; g_fill_colour = (6 << 5) | 31;            // magenta gouraud (shade 2 .. 32)
    tri(V(140, 15, 0, 0, 2 << 16), V(300, 40, 0, 0, 32 << 16), V(200, 120, 0, 0, 16 << 16));
    g_fill_mode = 5;                                           // textured gouraud
    tri(V(10, 120, 0, 0, 32 << 16), V(150, 130, 255 << 16, 0, 20 << 16), V(100, 195, 255 << 16, 255 << 16, 4 << 16));
    tri(V(10, 120, 0, 0, 32 << 16), V(100, 195, 255 << 16, 255 << 16, 4 << 16), V(5, 190, 0, 255 << 16, 24 << 16));
    g_fill_mode = 7; g_fill_colour = 24;                       // flat-lit texture, level 24
    tri(V(180, 130, 0, 0), V(310, 125, 255 << 16, 0), V(310, 195, 255 << 16, 255 << 16));
    tri(V(180, 130, 0, 0), V(310, 195, 255 << 16, 255 << 16), V(190, 198, 0, 255 << 16));
    g_fill_mode = 14; g_fill_colour = (0 << 5) | 31;           // white translucent over everything
    tri(V(60, 60), V(260, 70), V(160, 170));
    g_fill_mode = 0; g_fill_colour = (4 << 5) | 31;
    // note: a horizontal line starting at x < 0 would vanish (original quirk, see render_draw_line)
    render_draw_line(0, 0, 319, 199); render_draw_line(0, 199, 319, 0); render_draw_line(10, 100, 400, 100);

    std::string path = exe_path ? exe_path : "";
    size_t slash = path.find_last_of("/\\");
    path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "raster_test_scene.ppm";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  (could not write %s)\n", path.c_str()); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < frame.size(); i++) std::fwrite(pal + frame[i] * 3, 1, 3, f);
    std::fclose(f);
    std::printf("  wrote %s\n", path.c_str());
}

int main(int, char **argv)
{
    mc_globals_init();
    test_rectangles();
    test_clipping_equivalence();
    test_texture_mapping();
    test_all_modes();
    test_lines();
    write_scene(argv[0]);
    mc_globals_shutdown();
    std::printf(g_failures ? "raster_test: %d FAILURE(S)\n" : "raster_test: all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
