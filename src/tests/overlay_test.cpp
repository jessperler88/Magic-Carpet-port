#define _CRT_SECURE_NO_WARNINGS
// overlay_test (round 10, task C): the debug overlays and the Thing inspector (debug_overlay.h) over level 38.
//
//  - Flight HUD 640x480 and 320x200 (faithful renderer), the map screen (occupancy on the radar), the extended
//    renderer at the game frame's size and the compositor at 1920x1080: frames with every overlay on, a Thing
//    inspected and a cell picked; the overlays changed pixels, and GameState / Config / the maps / the cell
//    heads / net_state_checksum are byte-identical before and after every drawn frame.
//  - Damage numbers: a health loss between two ticks shows as an event (the overlay's own history).
//  - Registry: names, toggles, "all", unknown names, a registered extra overlay is called.
//  - The inspector's text for a wizard names its AI mode; the follow camera sits behind the Thing.
// PPMs go to argv[2] / MC_OVERLAY_OUT (default: the current directory) - look at them.
// Exit 0 = pass; SKIP without the game data.
#include "engine.h"
#include "sim.h"
#include "mc_globals.h"
#include "render.h"
#include "render_ext.h"
#include "settings.h"
#include "thing.h"
#include "player.h"
#include "hud.h"
#include "ui_draw.h"
#include "compose.h"
#include "net.h"
#include "debug_overlay.h"
#include "crash_handler.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)
static std::string g_out = ".";

static void write_ppm_rgb(const std::string &name, const uint8_t *rgb, int w, int h) {
    const std::string path = g_out + "/" + name;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot write %s\n", path.c_str()); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, (size_t)w * h * 3, f);
    std::fclose(f);
}
static void write_ppm(const std::string &name, const FrameBuffer &fb) {
    uint8_t pal[768];
    mc_palette_to_rgb(g_palette6, pal);
    std::vector<uint8_t> rgb((size_t)fb.width * fb.height * 3);
    for (int i = 0; i < fb.width * fb.height; i++) std::memcpy(&rgb[(size_t)i * 3], pal + fb.pixels[i] * 3, 3);
    write_ppm_rgb(name, rgb.data(), fb.width, fb.height);
}

// Everything the simulation owns that drawing could touch.
struct Snap {
    std::vector<uint8_t> gs, cfg, maps;
    uint32_t sum = 0;
    void take() {
        gs.assign((uint8_t *)g_state, (uint8_t *)g_state + sizeof(GameState));
        cfg.assign((uint8_t *)g_cfg, (uint8_t *)g_cfg + sizeof(Config));
        maps.clear();
        for (const uint8_t *m : {g_map_type, g_map_height, g_map_light, g_map_flags}) maps.insert(maps.end(), m, m + 0x10000);
        maps.insert(maps.end(), (const uint8_t *)g_cell_things, (const uint8_t *)g_cell_things + sizeof(uint16_t) * 0x10000);
        sum = net_state_checksum();
    }
    bool operator==(const Snap &o) const { return gs == o.gs && cfg == o.cfg && maps == o.maps && sum == o.sum; }
};

static int s_extra_calls = 0;
static void extra_overlay(const DebugOverlayCtx &) { s_extra_calls++; }

static bool load_level(int level, int ticks) {
    if (!engine_load_level(level)) return false;
    g_cfg->flags = 0; g_cfg->paused = 0;
    for (int t = 0; t < ticks; t++) engine_tick();
    return true;
}

// One frame as main.cpp draws it: capture bracket, the game's 2D pass, the overlays after the HUD.
static void draw_frame(const FrameBuffer &fb, int local) {
    debug_overlay_frame_begin(fb);
    render_frame_draw(fb, local);
    debug_overlay_draw(fb, local);
    debug_overlay_frame_end();
}

// Draws a frame, checks the state did not change; returns the pixels.
static std::vector<uint8_t> checked_frame(int W, int H, int local, const char *tag) {
    std::vector<uint8_t> px((size_t)W * H, 0);
    FrameBuffer fb{px.data(), W, H};
    Snap a, b;
    a.take();
    draw_frame(fb, local);
    b.take();
    CHECK(a == b, "%s: game state changed while drawing", tag);
    return px;
}

static int diff_count(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) n += a[i] != b[i];
    return n;
}

// A Thing to inspect: a drawn computer wizard if there is one, else a creature, else any drawn Thing.
static int choose_target(int W, int H, int local) {
    debug_inspect_set_cell(5, 5);                    // anything that opens the capture
    checked_frame(W, H, local, "choose");
    debug_inspect_set_cell(-1, -1);
    int n = 0;
    const RenderAnchor *a = render_anchors(&n);
    int best = -1, best_rank = 9, best_depth = 0;
    for (int i = 0; i < n; i++) {
        const Thing *t = thing_at(a[i].slot);
        if (a[i].sx < 40 || a[i].sx >= W - 40 || a[i].sy < 100 || a[i].sy >= H) continue;   // well inside the view
        const int rank = (t->cls == 3 && a[i].slot != g_state->players[local].thing) ? 0 : t->cls == 5 ? 1 : 2;
        if (rank < best_rank || (rank == best_rank && a[i].depth < best_depth)) { best_rank = rank; best = a[i].slot; best_depth = a[i].depth; }
    }
    return best;
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (argc > 2) g_out = argv[2];
    else if (const char *o = std::getenv("MC_OVERLAY_OUT")) g_out = o;
    if (!engine_init(game)) { std::printf("SKIP: engine_init failed (game data missing)\n"); return 0; }
    if (!load_level(38, 300)) { std::printf("SKIP: level 38 missing\n"); return 0; }
    g_video_mode_flags = 8;
    if (!ui_draw_init(game)) { std::printf("SKIP: HUD data missing\n"); return 0; }
    const PortSettings defaults = g_settings;
    const int local = g_state->local_player & 7;
    PlayerRec &rec = g_state->players[local];

    // ---- registry -------------------------------------------------------------------------------------
    CHECK(debug_overlay_count() >= 6, "built-in overlays");
    for (const char *n : {"grid", "labels", "anchors", "occupancy", "damage", "net"}) CHECK(debug_overlay_find(n) >= 0, "overlay %s", n);
    CHECK(!debug_overlay_set("nonsense", true), "unknown overlay accepted");
    CHECK(!debug_overlay_any_on(), "something on at start");
    debug_overlay_register("test_extra", "test", extra_overlay, false);
    CHECK(debug_overlay_toggle("test_extra") && debug_overlay_is_on("test_extra"), "toggle");
    std::printf("%s", debug_overlay_list().c_str());

    // ---- flight 640x480: plain, then everything on -----------------------------------------------------
    rec.input_mode = 0;
    debug_overlay_set("test_extra", false);
    const std::vector<uint8_t> plain = checked_frame(640, 480, local, "plain 640");
    const int target = choose_target(640, 480, local);
    CHECK(target > 0, "no Thing drawn to inspect");
    debug_inspect_set(target);
    {
        std::vector<std::string> L;
        debug_inspect_lines(target, &L);
        std::printf("inspecting slot %d:\n", target);
        for (const std::string &s : L) std::printf("  | %s\n", s.c_str());
        CHECK(L.size() >= 10, "inspector lines");
        if (thing_at((unsigned)target)->cls == 3) {
            bool ai = false;
            for (const std::string &s : L) ai |= s.rfind("ai mode", 0) == 0;
            CHECK(ai, "wizard without its AI mode line");
        }
        Camera fc;
        CHECK(debug_inspect_follow_camera(&fc), "follow camera");
        const Thing *t = thing_at((unsigned)target);
        const int ddx = (int16_t)(uint16_t)(fc.cam_x - t->x), ddy = (int16_t)(uint16_t)(fc.cam_y - t->y);
        CHECK(ddx * ddx + ddy * ddy > 0x200 * 0x200 && fc.cam_z > t->z, "follow camera placement");
    }
    // every computer wizard's panel names its AI mode
    for (int p = 0; p < 8; p++) {
        const PlayerRec &r = g_state->players[p];
        if (!r.active || !r.is_computer) continue;
        std::vector<std::string> L;
        debug_inspect_lines(r.thing, &L);
        bool ai = false;
        for (const std::string &s : L) ai |= s.rfind("ai mode", 0) == 0;
        CHECK(ai, "wizard of player %d without its AI mode line", p);
        if (p == 1 || !ai) for (const std::string &s : L) std::printf("  | %s\n", s.c_str());
    }
    debug_inspect_set_cell((thing_at((unsigned)target)->x >> 8) + 1, (thing_at((unsigned)target)->y >> 8));
    debug_overlay_set("all", true);
    debug_overlay_set("test_extra", true);
    s_extra_calls = 0;
    // damage: a drop in health between two ticks (written by the test, before the tick) becomes a number
    for (int k = 0; k < 6; k++) {
        if (k == 2) thing_at((unsigned)target)->health -= 37;
        engine_tick();
        checked_frame(640, 480, local, "damage ticks");
    }
    const std::vector<uint8_t> full = checked_frame(640, 480, local, "overlays 640");
    CHECK(s_extra_calls > 0, "registered overlay not called");
    const int changed = diff_count(plain, full);
    std::printf("flight 640: overlays changed %d pixels\n", changed);
    CHECK(changed > 2000, "overlays drew (almost) nothing");
    write_ppm("overlay_flight_640.ppm", FrameBuffer{const_cast<uint8_t *>(full.data()), 640, 480});
    debug_overlay_set("test_extra", false);

    // hover box with the pointer in cursor mode
    {
        int n = 0;
        const RenderAnchor *a = render_anchors(&n);
        if (n > 0) {
            debug_overlay_set_pointer(true, a[n - 1].sx, a[n - 1].sy - a[n - 1].h / 2);
            checked_frame(640, 480, local, "hover");
            CHECK(debug_overlay_hover() >= 0, "hover pick");
            debug_overlay_set_pointer(false, 0, 0);
        }
    }

    // ---- 320x200 ---------------------------------------------------------------------------------------
    g_video_mode_flags = 1;
    CHECK(ui_draw_set_video_mode(game), "320 mode");
    {
        const std::vector<uint8_t> px = checked_frame(320, 200, local, "overlays 320");
        write_ppm("overlay_flight_320.ppm", FrameBuffer{const_cast<uint8_t *>(px.data()), 320, 200});
    }
    g_video_mode_flags = 8;
    CHECK(ui_draw_set_video_mode(game), "640 mode");

    // ---- map screen: occupancy on the radar ------------------------------------------------------------
    rec.input_mode = 4;
    {
        debug_overlay_set("none", false);
        const std::vector<uint8_t> a = checked_frame(640, 480, local, "map plain");
        debug_overlay_set("occupancy", true);
        debug_overlay_set("net", true);
        const std::vector<uint8_t> b = checked_frame(640, 480, local, "map occupancy");
        std::printf("map screen: occupancy changed %d pixels\n", diff_count(a, b));
        CHECK(diff_count(a, b) > 200, "occupancy drew nothing");
        write_ppm("overlay_map_640.ppm", FrameBuffer{const_cast<uint8_t *>(b.data()), 640, 480});
    }
    rec.input_mode = 0;
    debug_overlay_set("all", true);

    // ---- extended renderer at the game frame's size ----------------------------------------------------
    g_settings.render_extended = true;
    g_settings.draw_distance = 48;
    {
        const RenderViewFn saved = g_render_view_override;
        g_render_view_override = render_view_ext_target;
        const std::vector<uint8_t> px = checked_frame(640, 480, local, "overlays ext 640");
        g_render_view_override = saved;
        CHECK(render_view_info().kind == RENDER_VIEW_EXT && render_view_info().frame_known, "ext target view info");
        write_ppm("overlay_ext_640.ppm", FrameBuffer{const_cast<uint8_t *>(px.data()), 640, 480});
    }

    // ---- composed 1920x1080 ----------------------------------------------------------------------------
    {
        std::vector<uint8_t> px((size_t)640 * 480, 0);
        FrameBuffer fb{px.data(), 640, 480};
        compose_install();
        Snap a, b;
        a.take();
        debug_overlay_frame_begin(fb);
        compose_draw_frame(1920, 1080, fb, [&] {
            render_frame_draw(fb, local);
            debug_overlay_draw(fb, local);
        });
        debug_overlay_frame_end();
        b.take();
        CHECK(a == b, "composed: game state changed while drawing");
        const ComposeOutput &o = compose_output();
        std::vector<uint8_t> rgb((size_t)o.display_w * o.display_h * 3);
        compose_to_rgb(o, g_palette6, rgb.data());
        write_ppm_rgb("overlay_composed_1920.ppm", rgb.data(), o.display_w, o.display_h);
        CHECK(render_view_info().composed, "composed view info");
        compose_remove();
    }
    g_settings = defaults;

    // ---- off again: nothing drawn, nothing captured ----------------------------------------------------
    debug_overlay_set("none", false);
    debug_inspect_set(-1);
    debug_inspect_set_cell(-1, -1);
    CHECK(!debug_overlay_any_on(), "still on");
    const RenderViewFn before = g_render_view_override;
    const std::vector<uint8_t> again = checked_frame(640, 480, local, "plain again");
    CHECK(g_render_view_override == before && !render_capture_active(), "capture left open");

    ui_draw_shutdown();
    std::printf(g_fail ? "overlay_test: %d FAILURES\n" : "overlay_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
