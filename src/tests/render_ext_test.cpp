#define _CRT_SECURE_NO_WARNINGS
// render_ext_test (round 7, task A): the extended renderer (render_view_ext, render_ext.cpp).
//
//  1. render_view_ext does not disturb the faithful renderer: render_view's frame is byte-identical
//     with and without an extended frame drawn in between (shared state: render target, slope low-pass).
//  2. Levels 38 and 44 (after some ticks, things alive) at 320x200 .. 3840x2160 and draw distances
//     20 / 48 / 80 / 127, lod 0 / 1 / 2, both fog modes: no crash, guard bands intact, no map cell drawn
//     twice, far terrain present above the 20-cell horizon.
//  3. A cycle in a cell's Thing list does not hang either renderer.
//  4. Screenshots (PPM) into argv[2] / MC_REXT_OUT (default: current directory).
//  5. MC_REXT_PERF=1: frame times (ms) for the sizes x distances table on level 44.
// Exit 0 = pass; SKIP when the game data is missing.
#include "engine.h"
#include "sim.h"
#include "mc_globals.h"
#include "render.h"
#include "render_ext.h"
#include "raster.h"
#include "settings.h"
#include "thing.h"
#include "player.h"
#include "mcfile.h"
#include "crash_handler.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static std::string g_out = ".";
static const int GUARD = 4096;

static void write_ppm(const std::string &name, const uint8_t *px, int w, int h) {
    const std::string path = g_out + "/" + name;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t rgb[768];
    mc_palette_to_rgb(g_palette6, rgb);
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row((size_t)w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) std::memcpy(&row[(size_t)x * 3], rgb + px[(size_t)y * w + x] * 3, 3);
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

struct Canvas {
    std::vector<uint8_t> buf;
    int w = 0, h = 0;
    FrameBuffer fb() { return FrameBuffer{buf.data() + GUARD, w, h}; }
    void init(int W, int H) { w = W; h = H; buf.assign((size_t)W * H + 2 * GUARD, 0xAB); }
    bool guards_ok() const {
        for (int i = 0; i < GUARD; i++)
            if (buf[i] != 0xAB || buf[GUARD + (size_t)w * h + i] != 0xAB) return false;
        return true;
    }
    const uint8_t *px() const { return buf.data() + GUARD; }
};

static int count_not(const Canvas &c, uint8_t colour, int row_end) {
    int n = 0;
    for (int y = 0; y < row_end && y < c.h; y++)
        for (int x = 0; x < c.w; x++) n += c.px()[(size_t)y * c.w + x] != colour;
    return n;
}
static int top_row_not(const Canvas &c, uint8_t colour) {
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) if (c.px()[(size_t)y * c.w + x] != colour) return y;
    return c.h;
}

static void set_ext(int dd, int lod, int fog) {
    g_settings.render_extended = true;
    g_settings.draw_distance = dd;
    g_settings.lod = lod;
    g_settings.fog_mode = fog;
}

static bool load_level(int level, int ticks) {
    if (!engine_load_level(level)) return false;
    g_cfg->flags = 0; g_cfg->paused = 0;
    for (int t = 0; t < ticks; t++) engine_tick();
    return true;
}

// A camera above the local player, looking along its view, raised so the far terrain shows.
static Camera vista_camera(int raise) {
    Camera c = player_camera(g_state->local_player & 7);
    c.cam_z += raise;
    if (c.zoom == 0) c.zoom = 0x80;
    return c;
}

static std::vector<uint8_t> render_faithful_once(Canvas &cv, const Camera &cam) {
    int32_t sx, sy;
    render_get_slope_state(&sx, &sy);
    cv.init(320, 200);
    FrameBuffer fb = cv.fb();
    render_set_view_window(fb, 0x28);
    render_view(fb, cam);
    render_set_slope_state(sx, sy);
    return std::vector<uint8_t>(cv.px(), cv.px() + 320 * 200);
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (argc > 2) g_out = argv[2];
    else if (const char *o = std::getenv("MC_REXT_OUT")) g_out = o;
    if (!engine_init(game_dir)) { std::printf("SKIP: engine_init failed (game data missing)\n"); return 0; }
    const PortSettings defaults = g_settings;
    const bool perf = std::getenv("MC_REXT_PERF") != nullptr;

    // ---- 1. faithful renderer untouched ------------------------------------------------------------
    if (!load_level(38, 50)) { std::printf("SKIP: level 38 missing\n"); return 0; }
    {
        Canvas a, b, big;
        const Camera cam = player_camera(g_state->local_player & 7);
        const std::vector<uint8_t> f1 = render_faithful_once(a, cam);
        set_ext(80, 1, 0);
        big.init(1920, 1080);
        render_view_ext(big.fb(), cam);
        CHECK(big.guards_ok(), "1920x1080 guard band");
        g_settings = defaults;
        const std::vector<uint8_t> f2 = render_faithful_once(b, cam);
        CHECK(f1 == f2, "render_view's frame changed after a render_view_ext call");
        std::printf("faithful frame unchanged by render_view_ext: %s\n", f1 == f2 ? "yes" : "NO");
        write_ppm("faithful_l38_320.ppm", f1.data(), 320, 200);
        // drop-in override at the game frame's size with a reduced view window: nothing outside the window
        Canvas w;
        w.init(640, 480);
        FrameBuffer wfb = w.fb();
        std::memset(wfb.pixels, 0x11, 640 * 480);
        set_ext(127, 1, 1);
        render_set_view_window(wfb, 0x14);
        const RenderViewFn saved = g_render_view_override;
        g_render_view_override = render_view_ext_target;
        render_view_frame(wfb, cam);
        g_render_view_override = saved;
        int outside = 0, inside = 0;
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 640; x++) {
                const bool in = x >= 160 && x < 480 && y >= 120 && y < 360;   // size 0x14 of 0x28, centred
                const bool changed = wfb.pixels[y * 640 + x] != 0x11;
                outside += !in && changed; inside += in && changed;
            }
        CHECK(w.guards_ok() && outside == 0 && inside > 320 * 240 / 2, "view window: %d pixels outside, %d inside", outside, inside);
        write_ppm("ext_target_window_640.ppm", wfb.pixels, 640, 480);
        g_settings = defaults;
    }

    // ---- 2. sizes x distances on levels 38 and 44 ----------------------------------------------------
    static uint8_t counts[65536];
    g_render_ext_cell_count = counts;
    const int sizes[][2] = { {320, 200}, {640, 480}, {1920, 1080}, {2560, 1080}, {3840, 2160} };
    const int dists[] = { 20, 48, 80, 127 };
    for (int level : { 38, 44 }) {
        if (!load_level(level, 300)) { std::printf("FAIL: level %d\n", level); g_fail++; continue; }
        const Camera cam = vista_camera(0x300);
        const uint8_t saved_sky = g_state->opt_textured_sky;
        for (const auto &sz : sizes) {
            for (int dd : dists) {
                for (int lod = 0; lod <= 2; lod++) {
                    if (lod != 1 && !(sz[0] == 320 || sz[0] == 1920)) continue;
                    Canvas c;
                    c.init(sz[0], sz[1]);
                    set_ext(dd, lod, 1);
                    g_state->opt_textured_sky = 0;
                    std::memset(counts, 0, sizeof counts);
                    render_view_ext(c.fb(), cam);
                    const RenderExtStats st = g_render_ext_stats;
                    int twice = 0;
                    for (int i = 0; i < 65536; i++) twice += counts[i] > 1;
                    CHECK(c.guards_ok(), "level %d %dx%d dd %d lod %d: guard band", level, sz[0], sz[1], dd, lod);
                    CHECK(twice == 0, "level %d %dx%d dd %d lod %d: %d cells drawn twice", level, sz[0], sz[1], dd, lod, twice);
                    if (lod == 1 && (dd == 20 || dd == 127))
                        std::printf("level %d %4dx%-4d dd %3d lod %d: %6d tris %5d quads %4d skirts %4d sprites "
                                    "(things %d/%d) levels %d radius %.0f\n",
                                    level, sz[0], sz[1], dd, lod, st.triangles, st.quads, st.skirts, st.sprites,
                                    st.things_drawn, st.things_seen, st.levels, st.draw_radius);
                    if ((dd == 20 || dd == 127) && lod == 1 && (sz[0] == 1920 || sz[0] == 640)) {
                        char name[96];
                        std::snprintf(name, sizeof name, "ext_l%d_%dx%d_dd%d.ppm", level, sz[0], sz[1], dd);
                        write_ppm(name, c.px(), c.w, c.h);
                    }
                    if (sz[0] == 320 && dd == 127) {
                        char name[96];
                        std::snprintf(name, sizeof name, "ext_l%d_320_dd127_lod%d.ppm", level, lod);
                        write_ppm(name, c.px(), c.w, c.h);
                    }
                }
            }
            // far terrain: with the untextured sky (0xff) the dd 127 frame has terrain above the dd 20 frame's top
            Canvas n20, n127;
            n20.init(sz[0], sz[1]); n127.init(sz[0], sz[1]);
            g_state->opt_textured_sky = 0;
            set_ext(20, 1, 1); render_view_ext(n20.fb(), cam);
            set_ext(127, 1, 1); render_view_ext(n127.fb(), cam);
            const int top20 = top_row_not(n20, 0xff);
            const int above = count_not(n127, 0xff, top20);
            CHECK(above > sz[0] / 4, "level %d %dx%d: only %d terrain pixels above the 20-cell horizon (row %d)", level,
                  sz[0], sz[1], above, top20);
            if (sz[0] == 1920) std::printf("level %d %dx%d: dd 20 terrain starts at row %d, dd 127 has %d pixels above it\n",
                                           level, sz[0], sz[1], top20, above);
        }
        // haze into the textured sky, with the other options on
        {
            g_state->opt_textured_sky = 1;
            const uint8_t s2 = g_state->opt_second_surface, sm = g_state->opt_smooth, mb = g_state->opt_motion_blur;
            Canvas c;
            c.init(1920, 1080);
            set_ext(127, 1, 0);
            render_view_ext(c.fb(), cam);
            char name[96];
            std::snprintf(name, sizeof name, "ext_l%d_1920x1080_dd127_haze.ppm", level);
            write_ppm(name, c.px(), c.w, c.h);
            g_state->opt_second_surface = 1; g_state->opt_smooth = 1; g_state->opt_motion_blur = 1;
            for (int k = 0; k < 2; k++) render_view_ext(c.fb(), cam);
            CHECK(c.guards_ok(), "options: guard band");
            std::snprintf(name, sizeof name, "ext_l%d_1920x1080_dd127_options.ppm", level);
            write_ppm(name, c.px(), c.w, c.h);
            g_state->opt_second_surface = s2; g_state->opt_smooth = sm; g_state->opt_motion_blur = mb;
            // rolled and pitched view
            Camera rc = cam;
            rc.roll = 0x60; rc.pitch = 0x18;
            Canvas r;
            r.init(2560, 1080);
            render_view_ext(r.fb(), rc);
            CHECK(r.guards_ok(), "rolled: guard band");
            std::snprintf(name, sizeof name, "ext_l%d_2560x1080_dd127_roll.ppm", level);
            write_ppm(name, r.px(), r.w, r.h);
            // faithful-equivalent: dd 20 at 640x480, the original's size
            Canvas f;
            f.init(640, 480);
            set_ext(20, 0, 1);
            render_view_ext(f.fb(), cam);
            std::snprintf(name, sizeof name, "ext_l%d_640x480_dd20_lod0.ppm", level);
            write_ppm(name, f.px(), f.w, f.h);
            Canvas o;
            o.init(640, 480);
            g_settings = defaults;
            int32_t sx, sy;
            render_get_slope_state(&sx, &sy);
            render_set_view_window(o.fb(), 0x28);
            render_view(o.fb(), cam);
            render_set_slope_state(sx, sy);
            std::snprintf(name, sizeof name, "faithful_l%d_640x480.ppm", level);
            write_ppm(name, o.px(), o.w, o.h);
        }
        // before / after vista: high above the player, looking slightly down
        {
            Camera vc = vista_camera(0xc00);
            vc.pitch = 0x18;
            char name[96];
            Canvas o; o.init(640, 480);
            g_settings = defaults;
            g_state->opt_textured_sky = 1;
            int32_t sx, sy;
            render_get_slope_state(&sx, &sy);
            render_set_view_window(o.fb(), 0x28);
            render_view(o.fb(), vc);
            render_set_slope_state(sx, sy);
            std::snprintf(name, sizeof name, "vista_l%d_before_faithful_640x480.ppm", level);
            write_ppm(name, o.px(), o.w, o.h);
            for (int dd : { 20, 64, 127 }) {
                Canvas c; c.init(1920, 1080);
                set_ext(dd, 1, 0);
                render_view_ext(c.fb(), vc);
                std::snprintf(name, sizeof name, "vista_l%d_after_1920x1080_dd%d.ppm", level, dd);
                write_ppm(name, c.px(), c.w, c.h);
            }
            g_settings = defaults;
        }
        g_state->opt_textured_sky = saved_sky;
    }
    g_render_ext_cell_count = nullptr;

    // ---- interpolation: alpha 0x10000 = the current tick; alpha 0 with prev = shifted moves the sprites
    {
        const Camera cam = vista_camera(0x100);
        const int n = thing_pool_slots();
        std::vector<int16_t> prev((size_t)n * 3);
        for (int i = 0; i < n; i++) {
            const Thing *t = thing_at((unsigned)i);
            prev[(size_t)i * 3] = (int16_t)(t->x - 0x180); prev[(size_t)i * 3 + 1] = (int16_t)t->y; prev[(size_t)i * 3 + 2] = t->z;
        }
        set_ext(48, 1, 1);
        Canvas a, b, c;
        a.init(640, 480); b.init(640, 480); c.init(640, 480);
        render_view_ext(a.fb(), cam);
        g_render_interp.active = true;
        g_render_interp.prev_pos = reinterpret_cast<const int16_t (*)[3]>(prev.data());
        g_render_interp.prev_count = n;
        g_render_interp.alpha = 0x10000;
        render_view_ext(b.fb(), cam);
        g_render_interp.alpha = 0;
        render_view_ext(c.fb(), cam);
        g_render_interp = RenderInterp{};
        g_settings = defaults;
        const bool same = std::memcmp(a.px(), b.px(), 640 * 480) == 0;
        const bool moved = std::memcmp(a.px(), c.px(), 640 * 480) != 0 || g_render_ext_stats.things_drawn == 0;
        CHECK(same, "interpolation alpha 0x10000 differs from the current tick");
        CHECK(moved, "interpolation alpha 0 did not move anything");
        std::printf("interpolation: alpha 1 = current tick %s, alpha 0 moved the things %s\n", same ? "yes" : "NO", moved ? "yes" : "NO");
    }

    // ---- 3. a cycle in a cell list ---------------------------------------------------------------------
    {
        const Camera cam = vista_camera(0);
        // the first linked thing near the camera: make its list point back at itself
        int cyc = 0;
        unsigned cell = 0;
        for (int dy = -3; dy <= 3 && !cyc; dy++)
            for (int dx = -3; dx <= 3 && !cyc; dx++) {
                cell = mc_cell((uint8_t)((cam.cam_x >> 8) + dx), (uint8_t)((cam.cam_y >> 8) + dy));
                if (g_cell_things[cell]) cyc = g_cell_things[cell];
            }
        if (!cyc) for (cell = 0; cell < 65536 && !cyc; cell++) if (g_cell_things[cell]) cyc = g_cell_things[cell];
        if (cyc) {
            Thing *t = thing_at((unsigned)cyc);
            const uint16_t saved = t->cell_next;
            t->cell_next = (uint16_t)cyc;
            const auto t0 = std::chrono::steady_clock::now();
            Canvas a; a.init(640, 480);
            render_set_view_window(a.fb(), 0x28);
            render_view(a.fb(), cam);
            set_ext(127, 1, 1);
            Canvas b; b.init(1920, 1080);
            render_view_ext(b.fb(), cam);
            g_settings = defaults;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            t->cell_next = saved;
            std::printf("cell-list cycle at thing %d: both renderers returned (%.1f ms)\n", cyc, ms);
        } else {
            std::printf("FAIL: no linked thing for the cycle test\n");
            g_fail++;
        }
    }

    // ---- 5. performance -----------------------------------------------------------------------------
    if (perf) {
        const int perf_ticks = std::getenv("MC_REXT_PERF_TICKS") ? std::atoi(std::getenv("MC_REXT_PERF_TICKS")) : 2000;
        struct Scene { int level; int raise; bool textured; const char *what; };
        const Scene scenes[] = { { 44, 0x100, true, "player view + 0x100, textured sky, haze" },
                                 { 38, 0x100, false, "player view + 0x100, untextured sky, darken fog" } };
        for (const Scene &sc : scenes) {
            load_level(sc.level, perf_ticks);
            int live = 0;
            for (int i = 1; i < thing_pool_slots(); i++) live += thing_at((unsigned)i)->cls != 0;
            g_state->opt_textured_sky = sc.textured ? 1 : 0;
            const Camera cam = vista_camera(sc.raise);
            std::printf("\nframe time (ms; mean / worst of 32 frames, one full turn), level %d after %d ticks (%d things), %s\n",
                        sc.level, perf_ticks, live, sc.what);
            std::printf("%-10s", "size");
            for (int dd : dists) std::printf("      dd %3d", dd);
            std::printf("   (threads: %s)\n", std::getenv("MC_REXT_THREADS") ? std::getenv("MC_REXT_THREADS") : "auto");
            for (const auto &sz : { std::array<int,2>{640, 480}, std::array<int,2>{1920, 1080}, std::array<int,2>{2560, 1440}, std::array<int,2>{3840, 2160} }) {
                std::printf("%4dx%-5d", sz[0], sz[1]);
                for (int dd : dists) {
                    Canvas c; c.init(sz[0], sz[1]);
                    set_ext(dd, 1, 0);
                    if (const char *t = std::getenv("MC_REXT_THREADS")) g_settings.render_threads = std::atoi(t);
                    Camera cm = cam;
                    render_view_ext(c.fb(), cm);
                    const int frames = 32;
                    double sum = 0, worst = 0;
                    for (int k = 0; k < frames; k++) {
                        cm.yaw = (cam.yaw + k * 0x40) & 0x7ff;
                        const auto t0 = std::chrono::steady_clock::now();
                        render_view_ext(c.fb(), cm);
                        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                        sum += ms; worst = (std::max)(worst, ms);
                    }
                    std::printf(" %5.2f/%5.2f", sum / frames, worst);
                    if (sz[0] == 1920 && dd == 127) std::printf(" [build %.2f raster %.2f]", g_render_ext_stats.build_ms, g_render_ext_stats.raster_ms);
                    if (sz[0] == 1920 && dd == 80) {
                        char name[96];
                        std::snprintf(name, sizeof name, "perf_l%d_1920x1080_dd80.ppm", sc.level);
                        write_ppm(name, c.px(), c.w, c.h);
                    }
                }
                std::printf("\n");
            }
        }
        g_settings = defaults;
    }

    if (g_fail) { std::printf("render_ext_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("render_ext_test: OK\n");
    return 0;
}
