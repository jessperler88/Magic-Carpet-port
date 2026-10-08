// Landscape renderer test: synthetic maps / textures / sky / tables, renders 320x200 frames with
// render_view for several yaws and rolls, writes render_test_<yaw>[_r<roll>].ppm into the working
// directory and checks: no writes outside the frame buffer, sky at the top and terrain at the
// bottom, non-uniform frames, and that the four yaw views differ.
#define _CRT_SECURE_NO_WARNINGS
#include "render.h"
#include "raster.h"
#include "mc_globals.h"
#include "mc_math.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr int W = 320, H = 200, GUARD = 4096;
constexpr int TEX_B = 32;                 // texture block size (block32.dat)
constexpr int TEX_COUNT = 64;             // 8 rows of 8 textures in a 256-wide atlas
constexpr int RAMPS = 12;                 // 12 hue ramps x 16 levels = terrain colours 0..191
constexpr int SKY_BASE = 192;             // sky colours 192..255

static uint8_t g_atlas[TEX_COUNT / 8 * TEX_B * 256];

static void build_palette() {
    for (int c = 0; c < 192; ++c) {
        const int ramp = c / 16, lvl = c % 16;
        const double hue = ramp * 30.0, br = 0.25 + 0.75 * lvl / 15.0;
        const double hh = hue / 60.0;
        const double x = 1.0 - std::fabs(std::fmod(hh, 2.0) - 1.0);
        double r = 0, g = 0, b = 0;
        switch ((int)hh) {
        case 0: r = 1; g = x; break; case 1: r = x; g = 1; break; case 2: g = 1; b = x; break;
        case 3: g = x; b = 1; break; case 4: r = x; b = 1; break; default: r = 1; b = x; break;
        }
        g_palette6[c * 3 + 0] = (uint8_t)(63 * br * (0.3 + 0.7 * r));
        g_palette6[c * 3 + 1] = (uint8_t)(63 * br * (0.3 + 0.7 * g));
        g_palette6[c * 3 + 2] = (uint8_t)(63 * br * (0.3 + 0.7 * b));
    }
    for (int c = SKY_BASE; c < 256; ++c) {              // sky: dark blue -> light
        const int l = c - SKY_BASE;                       // 0..63
        g_palette6[c * 3 + 0] = (uint8_t)(10 + l * 40 / 63);
        g_palette6[c * 3 + 1] = (uint8_t)(20 + l * 40 / 63);
        g_palette6[c * 3 + 2] = (uint8_t)(40 + l * 23 / 63);
    }
}

static void build_textures() {
    for (int t = 0; t < TEX_COUNT; ++t) {
        uint8_t *base = g_atlas + (t / 8) * TEX_B * 256 + (t % 8) * TEX_B;
        const int ramp = t % RAMPS;
        for (int v = 0; v < TEX_B; ++v)
            for (int u = 0; u < TEX_B; ++u) {
                int lvl;
                if (t == 0)      lvl = 6 + ((u / 8 + v / 8) & 1) * 6;             // water: coarse checks
                else if (t % 3 == 0) lvl = ((u / 4 + v / 4) & 1) ? 13 : 5;           // checkerboard
                else if (t % 3 == 1) lvl = u * 15 / (TEX_B - 1);                     // u gradient (dark at u = 0)
                else             lvl = (u < 16 && v < 16) ? 15 : (v < 16 ? 8 : 2);   // marker: bright (0,0) quadrant, mid v<16
                base[v * 256 + u] = (uint8_t)(ramp * 16 + lvl);
            }
        g_texture_table[t] = base;
    }
    for (int t = TEX_COUNT; t < 256; ++t) g_texture_table[t] = g_atlas;
}

static void build_tables() {
    uint8_t *shade = g_shade_table();
    for (int l = 0; l < 64; ++l)
        for (int c = 0; c < 256; ++c) {
            uint8_t out = (uint8_t)c;
            if (c < 192) {                                 // darken the terrain ramps by the level (fog)
                const int ramp = c / 16, lvl = c % 16;
                const int ll = l > 0x20 ? 0x20 : l;
                out = (uint8_t)(ramp * 16 + (lvl * ll) / 0x20);
            }
            shade[(l << 8) | c] = out;
        }
    uint8_t *blend = g_blend_table();
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b) blend[(a << 8) | b] = (uint8_t)a;
    for (int i = 0; i < 65536; ++i) {
        const int u = i & 0xff, v = i >> 8;
        g_sky[i] = (uint8_t)(SKY_BASE + (v >> 3) + ((u >> 4) & 1) * 32);
    }
    // render_set_texture_uv_scale_284f0(32): every non-zero entry := (B<<16)-1
    for (int i = 0; i < 256; ++i) if (g_uv_table_data[i] != 0) g_uv_table[i] = (TEX_B << 16) - 1; else g_uv_table[i] = 0;
}

static void build_maps() {
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const uint16_t cell = mc_cell(x, y);
            const int h = 32 + (((mc_sin(x * 0x40) + mc_sin(y * 0x60 + 0x100)) * 5) >> 16);   // 22..42, water below 26
            g_map_height[cell] = (uint8_t)h;
            uint8_t tex = (uint8_t)(1 + (x * 7 + y * 3) % 40);
            uint8_t flags = 0;
            if (h < 26) { tex = 0; flags |= 8; }           // low ground: animated water
            if (((x >> 2) + (y >> 3)) % 5 == 0) flags |= (uint8_t)(((x + y) & 7) << 4);   // rotation bits
            g_map_type[cell] = tex;
            g_map_flags[cell] = flags;
            g_map_light[cell] = 0x20;
            g_cell_things[cell] = 0;
        }
}

static void write_ppm(const char *name, const uint8_t *pix) {
    FILE *f = std::fopen(name, "wb");
    if (!f) { std::printf("cannot write %s\n", name); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        const uint8_t c = pix[i];
        const uint8_t rgb[3] = {(uint8_t)(g_palette6[c * 3] * 4), (uint8_t)(g_palette6[c * 3 + 1] * 4), (uint8_t)(g_palette6[c * 3 + 2] * 4)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

int main(int, char **) {
    mc_globals_init();
    build_palette();
    build_textures();
    build_tables();
    build_maps();
    g_state->opt_smooth = 0;
    g_state->opt_motion_blur = 0;
    g_frame2 = nullptr;
    g_anim_tick = 7;

    std::vector<uint8_t> buf(W * H + 2 * GUARD);
    uint8_t *pix = buf.data() + GUARD;
    FrameBuffer fb{pix, W, H};

    const int cam_cell_x = 100, cam_cell_y = 100;
    const int cam_x = (cam_cell_x << 8) + 0x80, cam_y = (cam_cell_y << 8) + 0x80;
    const int terrain_z = g_map_height[mc_cell(cam_cell_x, cam_cell_y)] * 0x20;

    struct View { int yaw, roll; };
    const View views[] = {{0, 0}, {0x200, 0}, {0x400, 0}, {0x600, 0}, {0, 0x40}, {0x300, 0x40}};
    std::vector<std::vector<uint8_t>> frames;
    for (const View &vw : views) {
        std::memset(buf.data(), 0xAB, buf.size());
        std::memset(pix, 0, W * H);
        render_set_view_window(fb, 0x28);
        CHECK(g_rt_dest == pix && g_rt_pitch == W && g_rt_width == W && g_rt_height == H,
              "viewport %d %d %d", g_rt_pitch, g_rt_width, g_rt_height);
        Camera cam{cam_x, cam_y, vw.yaw, terrain_z + 0x300, 0, vw.roll, 0x100};
        render_view(fb, cam);
        if (vw.yaw == 0 && vw.roll == 0 && std::getenv("RENDER_TEST_DEBUG")) {
            const VertexRec *V = g_work_vertices();
            for (int r = 0; r <= 3; ++r) for (int c = 18; c <= 21; ++c) {
                const VertexRec &v = V[r * MC_GRID_COLS + c];
                std::printf("r%d c%d: xcam %d zcam %d hrel %d sx %d sy %d shade %x flags %02x/%02x tex %d sel %d\n", r, c,
                            v.x_cam, v.z_cam, v.h_rel, v.sx, v.sy, v.shade, v.flags, v.flags2, v.texture, v.uv_sel);
            }
        }

        // guard bands
        bool guard_ok = true;
        for (int i = 0; i < GUARD; ++i)
            if (buf[i] != 0xAB || buf[GUARD + W * H + i] != 0xAB) { guard_ok = false; break; }
        CHECK(guard_ok, "out-of-bounds write (yaw %x roll %x)", vw.yaw, vw.roll);

        // sky on top, terrain at the bottom
        int sky_top = 0, terrain_bottom = 0;
        for (int x = 0; x < W; ++x) {
            if (pix[x] >= SKY_BASE) sky_top++;
            if (pix[(H - 1) * W + x] < SKY_BASE) terrain_bottom++;
        }
        CHECK(sky_top == W, "yaw %x roll %x: top row has %d sky pixels", vw.yaw, vw.roll, sky_top);
        CHECK(terrain_bottom == W, "yaw %x roll %x: bottom row has %d terrain pixels", vw.yaw, vw.roll, terrain_bottom);

        // both terrain and sky present (the far edge sits below the horizon: 20-cell draw distance)
        int terrain_pixels = 0, sky_pixels = 0;
        for (int i = 0; i < W * H; ++i) (pix[i] >= SKY_BASE ? sky_pixels : terrain_pixels)++;
        CHECK(terrain_pixels > W * H / 10 && sky_pixels > W * H / 3, "yaw %x: %d terrain / %d sky pixels", vw.yaw, terrain_pixels, sky_pixels);

        // not uniform
        int hist[256] = {0};
        for (int i = 0; i < W * H; ++i) hist[pix[i]]++;
        int distinct = 0;
        for (int c = 0; c < 256; ++c) if (hist[c]) distinct++;
        CHECK(distinct > 20, "yaw %x: only %d distinct colours", vw.yaw, distinct);

        char name[64];
        if (vw.roll) std::snprintf(name, sizeof name, "render_test_%x_r%x.ppm", vw.yaw, vw.roll);
        else         std::snprintf(name, sizeof name, "render_test_%x.ppm", vw.yaw);
        write_ppm(name, pix);
        frames.emplace_back(pix, pix + W * H);
        std::printf("%s: %d terrain / %d sky pixels, %d colours, roll table octant %d steps %d\n", name,
                    terrain_pixels, sky_pixels, distinct, g_roll.octant, g_roll.steps);
    }
    // One 640x480 frame (DAT_0012ed70 == 0x280 path of render_set_view_window) for bounds checking.
    {
        constexpr int W2 = 640, H2 = 480;
        std::vector<uint8_t> buf2(W2 * H2 + 2 * GUARD, 0xAB);
        uint8_t *pix2 = buf2.data() + GUARD;
        std::memset(pix2, 0, W2 * H2);
        FrameBuffer fb2{pix2, W2, H2};
        render_set_view_window(fb2, 0x28);
        CHECK(g_rt_dest == pix2 && g_rt_pitch == W2 && g_rt_width == W2 && g_rt_height == H2,
              "640 viewport %d %d %d", g_rt_pitch, g_rt_width, g_rt_height);
        Camera cam{cam_x, cam_y, 0x100, terrain_z + 0x300, 0x10, 0x20, 0x100};
        render_view(fb2, cam);
        bool guard_ok = true;
        for (int i = 0; i < GUARD; ++i)
            if (buf2[i] != 0xAB || buf2[GUARD + W2 * H2 + i] != 0xAB) { guard_ok = false; break; }
        CHECK(guard_ok, "640x480: out-of-bounds write");
        int terrain_pixels = 0;
        for (int i = 0; i < W2 * H2; ++i) if (pix2[i] < SKY_BASE) terrain_pixels++;
        CHECK(terrain_pixels > W2 * H2 / 10, "640x480: %d terrain pixels", terrain_pixels);
        FILE *f = std::fopen("render_test_640.ppm", "wb");
        if (f) {
            std::fprintf(f, "P6\n%d %d\n255\n", W2, H2);
            for (int i = 0; i < W2 * H2; ++i) {
                const uint8_t c = pix2[i];
                const uint8_t rgb[3] = {(uint8_t)(g_palette6[c * 3] * 4), (uint8_t)(g_palette6[c * 3 + 1] * 4), (uint8_t)(g_palette6[c * 3 + 2] * 4)};
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
        }
        std::printf("render_test_640.ppm: %d terrain pixels, roll table octant %d steps %d\n", terrain_pixels, g_roll.octant, g_roll.steps);
    }
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) {
            int diff = 0;
            for (int k = 0; k < W * H; ++k) if (frames[i][k] != frames[j][k]) diff++;
            CHECK(diff > W * H / 10, "views %d and %d differ in only %d pixels", i, j, diff);
        }

    // Roll table sanity: octant 0 at angle 0 has no minor steps, 45 degrees steps every pixel.
    render_set_view_window(fb, 0x28);       // back to the 320x200 target
    render_build_roll_table(0);
    CHECK(g_roll.octant == 0 && g_roll.steps == 0 && g_roll.extent_major == W, "roll 0: steps %d", g_roll.steps);
    render_build_roll_table(0x100);
    CHECK(g_roll.octant == 1 && g_roll.steps == H && g_roll.step == 0x10000, "roll 0x100: steps %d", g_roll.steps);
    render_set_view_window(fb, 0x28);
    render_build_roll_table(0x80);          // tan(22.5 deg) * 320 = 132.5 minor steps
    CHECK(g_roll.octant == 0 && g_roll.steps >= 132 && g_roll.steps <= 133 && g_roll_entries()[W - 1].offset > 0,
          "roll 0x80: steps %d", g_roll.steps);
    CHECK(g_roll_entries()[0].delta == 0 && g_roll_entries()[1].delta >= 1, "roll 0x80: deltas %d %d",
          g_roll_entries()[0].delta, g_roll_entries()[1].delta);
    render_build_roll_table(0x7c0);         // -0x40: octant 7, tan(11.25 deg) * 320 = 63.6
    CHECK(g_roll.octant == 7 && g_roll.steps >= 63 && g_roll.steps <= 64, "roll 0x7c0: steps %d", g_roll.steps);
    render_build_roll_table(0x300);         // exact diagonal of octant 3: one minor step per entry
    CHECK(g_roll.octant == 3 && g_roll.steps == W && g_roll.step == 0x10000, "roll 0x300: steps %d", g_roll.steps);
    render_build_roll_table(0x500 + 0x20);  // octant 5: major axis = height
    CHECK(g_roll.octant == 5 && g_roll.extent_major == H && g_roll.steps > 0 && g_roll.steps < H, "roll 0x520: steps %d", g_roll.steps);

    std::printf(g_failures ? "render_test: %d FAILURES\n" : "render_test: OK\n", g_failures);
    return g_failures ? 1 : 0;
}
