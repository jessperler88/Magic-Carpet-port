#define _CRT_SECURE_NO_WARNINGS
// Test of the native-resolution compositor (compose.h, round 7 task B).
//  1. layout: HUD rectangles for the scale modes at common display sizes; mouse transform round trip
//     (frame -> display -> frame is the identity, display -> frame clamps, monotonic);
//  2. reference compositor on a synthetic ComposeOutput (view, mask, blits);
//  3. level 38 snapshot: the override renders the view at the display size and point-samples it into
//     the game frame's view window exactly (checked pixel by pixel against the hi-res buffer); the HUD
//     diff (synthetic HUD: an opaque block of the view's own colours, a translucent block, a pixel
//     outside the window) - the second pass finds the same-colour block; no view -> whole frame opaque;
//  4. faithful: without the override a frame is identical before and after install / remove;
//  5. composed frames of level 38 with the real HUD (render_frame_draw) at 1920x1080 and 2560x1080 for
//     640x480 and 320x200, the spell book and the map screen, written as PPM to argv[2] (default: cwd).
// argv[1] = game dir. Exit code 0 = pass.
#include "compose.h"
#include "engine.h"
#include "hud.h"
#include "ui_draw.h"
#include "mc_globals.h"
#include "palette_fx.h"
#include "player.h"
#include "raster.h"
#include "settings.h"
#include "mcfile.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static std::string s_out_dir = ".";

static void write_rgb(const std::string &name, const uint8_t *rgb, int w, int h) {
    const std::string path = s_out_dir + "/" + name;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot write %s\n", path.c_str()); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, (size_t)w * h * 3, f);
    std::fclose(f);
    std::printf("wrote %s\n", path.c_str());
}

// ---------------------------------------------------------------------------------------------
// 1. layout + mouse
// ---------------------------------------------------------------------------------------------
static void test_layout() {
    struct Case { int dw, dh, gw, gh, mode, x, y, w, h; };
    const Case cases[] = {
        {1920, 1080, 640, 480, 0, 320, 60, 1280, 960},   // integer 2x
        {1920, 1080, 640, 480, 1, 240, 0, 1440, 1080},   // fit
        {2560, 1440, 640, 480, 0, 320, 0, 1920, 1440},   // integer 3x = fit
        {3840, 2160, 640, 480, 0, 640, 120, 2560, 1920}, // integer 4x
        {1920, 1080, 320, 200, 0, 320, 40, 1280, 1000},  // 4 x 5 (pixel aspect 0.8 ~ 5/6)
        {1280, 800, 320, 200, 1, 106, 0, 1067, 800},     // fit
        {1280, 800, 320, 200, 0, 106, 0, 1067, 800},     // integer 3 x 4 is 10 % off the aspect: fit
        {3840, 2160, 320, 200, 0, 640, 80, 2560, 2000},  // 8 x 10 (4 % off)
        {640, 480, 640, 480, 0, 0, 0, 640, 480},
        {200, 300, 640, 480, 0, 0, 75, 200, 150},        // too small for 1x, portrait: fit, letterboxed
    };
    for (const Case &c : cases) {
        int x, y, w, h;
        compose_hud_rect(c.dw, c.dh, c.gw, c.gh, c.mode, &x, &y, &w, &h);
        if (x != c.x || y != c.y || w != c.w || h != c.h)
            std::printf("hud rect %dx%d frame %dx%d mode %d: got %d,%d %dx%d want %d,%d %dx%d\n",
                        c.dw, c.dh, c.gw, c.gh, c.mode, x, y, w, h, c.x, c.y, c.w, c.h);
        CHECK(x == c.x && y == c.y && w == c.w && h == c.h);
    }
    // round trip for several layouts
    int bad = 0, nonmono = 0;
    for (const Case &c : cases) {
        ComposeOutput o;
        o.display_w = c.dw; o.display_h = c.dh; o.game_w = c.gw; o.game_h = c.gh;
        compose_hud_rect(c.dw, c.dh, c.gw, c.gh, c.mode, &o.hud_x, &o.hud_y, &o.hud_w, &o.hud_h);
        const bool upscale = o.hud_w >= c.gw && o.hud_h >= c.gh;
        for (int fy = 0; fy < c.gh; fy += 7)
            for (int fx = 0; fx < c.gw; fx += 3) {
                int dx, dy, gx, gy;
                compose_frame_to_display(o, fx, fy, &dx, &dy);
                compose_display_to_frame(o, dx, dy, &gx, &gy);
                if (upscale && (gx != fx || gy != fy)) bad++;
                CHECK(dx >= o.hud_x && dx < o.hud_x + o.hud_w && dy >= o.hud_y && dy < o.hud_y + o.hud_h);
            }
        int prev = -1;
        for (int dx = -10; dx < c.dw + 10; dx++) {
            int gx, gy;
            compose_display_to_frame(o, dx, c.dh / 2, &gx, &gy);
            if (gx < prev || gx < 0 || gx >= c.gw) nonmono++;
            prev = gx;
        }
        int gx, gy;
        compose_display_to_frame(o, -5, -5, &gx, &gy);
        CHECK(gx == 0 && gy == 0);
        compose_display_to_frame(o, c.dw + 5, c.dh + 5, &gx, &gy);
        CHECK(gx == c.gw - 1 && gy == c.gh - 1);
    }
    std::printf("layout: %d round-trip errors, %d non-monotonic steps\n", bad, nonmono);
    CHECK(bad == 0);
    CHECK(nonmono == 0);
}

// ---------------------------------------------------------------------------------------------
// 2. reference compositor on a synthetic output
// ---------------------------------------------------------------------------------------------
static void test_synthetic_rgb() {
    uint8_t pal6[768];
    for (int i = 0; i < 256; i++) { pal6[i * 3] = (uint8_t)(i & 63); pal6[i * 3 + 1] = (uint8_t)(i >> 2); pal6[i * 3 + 2] = 1; }
    const int gw = 8, gh = 6, dw = 32, dh = 12;
    std::vector<uint8_t> view((size_t)16 * 6), hud((size_t)gw * gh), mask((size_t)gw * gh, 0);
    for (size_t i = 0; i < view.size(); i++) view[i] = 10;
    for (size_t i = 0; i < hud.size(); i++) hud[i] = 20;
    mask[0] = 1;                                   // top-left frame pixel is HUD
    ComposeOutput o;
    o.display_w = dw; o.display_h = dh; o.game_w = gw; o.game_h = gh;
    o.hud_x = 8; o.hud_y = 0; o.hud_w = 16; o.hud_h = 12;   // 2x, centred
    o.has_view = true; o.view = view.data(); o.view_buf_w = 16; o.view_buf_h = 6;
    o.view_x = 0; o.view_y = 0; o.view_w = dw; o.view_h = dh;
    o.hud = hud.data(); o.mask = mask.data();
    o.blits[0] = ComposeBlit{0, 0, gw, gh, 8, 0, 16, 12}; o.blit_count = 1;
    std::vector<uint8_t> rgb((size_t)dw * dh * 3);
    compose_to_rgb(o, pal6, rgb.data());
    auto px = [&](int x, int y) { return rgb[((size_t)y * dw + x) * 3]; };
    const uint8_t v = (uint8_t)((10 << 2) | (10 >> 4)), h = (uint8_t)((20 << 2) | (20 >> 4));
    CHECK(px(0, 0) == v && px(31, 11) == v);       // view everywhere outside the HUD pixel
    CHECK(px(8, 0) == h && px(9, 1) == h);         // the HUD pixel, 2x2
    CHECK(px(10, 0) == v && px(8, 2) == v);
    // no view: the frame opaque, black bars
    o.has_view = false; o.mask = nullptr;
    compose_to_rgb(o, pal6, rgb.data());
    CHECK(px(0, 0) == 0 && px(7, 5) == 0 && px(8, 0) == h && px(23, 11) == h && px(24, 0) == 0);
}

// ---------------------------------------------------------------------------------------------
// 3. view placement, downsample, HUD diff on the level 38 snapshot
// ---------------------------------------------------------------------------------------------
static Camera s_cam;

static void test_view_and_diff() {
    const int gw = 640, gh = 480, dw = 1920, dh = 1080;
    std::vector<uint8_t> buf((size_t)gw * gh, 0);
    FrameBuffer fb{buf.data(), gw, gh};
    g_settings.hud_scale_mode = 0;
    compose_install();
    CHECK(compose_installed());
    // draw: the view (full window) + synthetic HUD
    //   block A (100..139, 300..309): opaque copy of the view's own pixels - invisible to a single diff
    //   block B (200..239, 300..309): translucent BLEND[0x40][dest]
    const uint8_t *blend = g_blend_table();
    auto draw = [&] {
        render_set_view_window(fb, 0x28);
        render_view_frame(fb, s_cam);
        static std::vector<uint8_t> first;           // the view of pass 1 (block A copies it)
        if (first.empty()) first.assign(buf.begin(), buf.end());
        for (int y = 300; y < 310; y++)
            for (int x = 100; x < 140; x++) buf[(size_t)y * gw + x] = first[(size_t)y * gw + x];
        for (int y = 300; y < 310; y++)
            for (int x = 200; x < 240; x++) { uint8_t &p = buf[(size_t)y * gw + x]; p = blend[(0x40u << 8) | p]; }
    };
    compose_draw_frame(dw, dh, fb, draw);
    const ComposeOutput &o = compose_output();
    CHECK(o.has_view && o.mask && o.view);
    CHECK(o.view_x == 0 && o.view_y == 0 && o.view_w == dw && o.view_h == dh);
    CHECK(o.view_buf_w == dw && o.view_buf_h == dh);
    CHECK(o.hud_x == 320 && o.hud_y == 60 && o.hud_w == 1280 && o.hud_h == 960);
    // downsample: frame pixel = view buffer at the frame pixel's display centre (outside the blocks)
    int ds_bad = 0;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            if (y >= 300 && y < 310 && x >= 100 && x < 240) continue;
            int dx, dy;
            compose_frame_to_display(o, x, y, &dx, &dy);
            if (buf[(size_t)y * gw + x] != o.view[(size_t)dy * o.view_buf_w + dx]) ds_bad++;
        }
    std::printf("downsample: %d pixels differ from the hi-res view at their centres\n", ds_bad);
    CHECK(ds_bad == 0);
    // mask: block A found by the second pass (all of it unless the 0x80 flip were an identity), B
    // where the blend changed the pixel, nothing else
    int a = 0, b = 0, bchg = 0, other = 0;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            const bool m = o.mask[(size_t)y * gw + x] != 0;
            if (y >= 300 && y < 310 && x >= 100 && x < 140) a += m;
            else if (y >= 300 && y < 310 && x >= 200 && x < 240) b += m;
            else other += m;
        }
    // how many pixels of B the blend really changed (in pass 1)
    for (int y = 300; y < 310; y++)
        for (int x = 200; x < 240; x++) {
            int dx, dy;
            compose_frame_to_display(o, x, y, &dx, &dy);
            const uint8_t v = o.view[(size_t)dy * o.view_buf_w + dx];
            bchg += blend[(0x40u << 8) | v] != v || blend[(0x40u << 8) | (uint8_t)(v ^ 0x80)] != (uint8_t)(v ^ 0x80);
        }
    std::printf("mask: same-colour block %d/400, translucent block %d/400 (%d changed), elsewhere %d\n", a, b, bchg, other);
    CHECK(a == 400);
    CHECK(b == bchg && b > 300);
    CHECK(other == 0);
    // the frame is pass 1's (the second pass's altered view is not left behind)
    {
        int dx, dy;
        compose_frame_to_display(o, 10, 470, &dx, &dy);
        CHECK(buf[(size_t)470 * gw + 10] == o.view[(size_t)dy * o.view_buf_w + dx]);
    }
    CHECK(o.hud && o.hud[(size_t)305 * gw + 205] == buf[(size_t)305 * gw + 205]);

    // reduced view window (view size 0x20): the view rect is the window's display rectangle, the
    // rest of the frame is HUD (opaque)
    std::memset(buf.data(), 0, buf.size());
    compose_draw_frame(dw, dh, fb, [&] { render_set_view_window(fb, 0x20); render_view_frame(fb, s_cam); });
    {
        const ComposeOutput &r = compose_output();
        // window 0x20: 512x384 at (64, 48)
        CHECK(r.has_view && r.view_x == 320 + 128 && r.view_y == 60 + 96 && r.view_w == 1024 && r.view_h == 768);
        int outside = 0, inside = 0;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                const bool in = x >= 64 && x < 576 && y >= 48 && y < 432;
                (in ? inside : outside) += r.mask[(size_t)y * gw + x] != 0;
            }
        CHECK(inside == 0 && outside == gw * gh - 512 * 384);
        CHECK(r.blit_count == 1);                    // no corner anchoring without a full-display view
    }
    // view_width / view_height: a lower render resolution
    g_settings.view_width = 960; g_settings.view_height = 540;
    compose_draw_frame(dw, dh, fb, [&] { render_set_view_window(fb, 0x28); render_view_frame(fb, s_cam); });
    CHECK(compose_output().view_buf_w == 960 && compose_output().view_buf_h == 540 && compose_output().view_w == dw);
    g_settings.view_width = g_settings.view_height = 0;
    // no view: the whole frame opaque, one blit
    compose_draw_frame(dw, dh, fb, [&] { std::memset(buf.data(), 7, buf.size()); });
    CHECK(!compose_output().has_view && compose_output().mask == nullptr && compose_output().blit_count == 1);
    // compose_frame_for_present: a fresh frame, then the same again (fade), then a changed frame buffer
    // (front end drawn outside compose_draw_frame) as a plain frame
    compose_draw_frame(dw, dh, fb, [&] { render_set_view_window(fb, 0x28); render_view_frame(fb, s_cam); });
    CHECK(compose_frame_for_present(dw, dh, fb).has_view);
    CHECK(compose_frame_for_present(dw, dh, fb).has_view);
    buf[1234] ^= 0x55;
    {
        const ComposeOutput &p = compose_frame_for_present(dw, dh, fb);
        CHECK(!p.has_view && p.mask == nullptr && p.hud[1234] == buf[1234]);
    }
    compose_remove();
    CHECK(!compose_installed() && g_render_view_override == nullptr);
}

// ---------------------------------------------------------------------------------------------
// 4. faithful path unchanged
// ---------------------------------------------------------------------------------------------
static void test_faithful(int local) {
    const int gw = 640, gh = 480;
    std::vector<uint8_t> a((size_t)gw * gh, 0), b((size_t)gw * gh, 0);
    int32_t sx, sy;
    render_get_slope_state(&sx, &sy);
    render_frame_draw(FrameBuffer{a.data(), gw, gh}, local);
    compose_install();
    compose_remove();
    render_set_slope_state(sx, sy);
    render_frame_draw(FrameBuffer{b.data(), gw, gh}, local);
    CHECK(a == b);
    // the override installed but no frame open (e.g. a test calling render_frame directly): the original path
    compose_install();
    std::vector<uint8_t> c((size_t)gw * gh, 0);
    render_set_slope_state(sx, sy);
    render_frame_draw(FrameBuffer{c.data(), gw, gh}, local);
    compose_remove();
    CHECK(a == c);
    std::printf("faithful: frames without the compositor identical: %s\n", a == b && a == c ? "yes" : "NO");
}

// ---------------------------------------------------------------------------------------------
// 5. composed frames with the real HUD
// ---------------------------------------------------------------------------------------------
static void composed_frame(const char *game, int flags, int dw, int dh, int input_mode, const char *name, int local) {
    g_video_mode_flags = (uint16_t)flags;
    CHECK(ui_draw_set_video_mode(game));
    const int gw = flags == 1 ? 320 : 640, gh = flags == 1 ? 200 : 480;
    std::vector<uint8_t> buf((size_t)gw * gh, 0);
    FrameBuffer fb{buf.data(), gw, gh};
    PlayerRec &rec = g_state->players[local];
    const uint8_t saved_mode = rec.input_mode;
    rec.input_mode = (uint8_t)input_mode;
    compose_install();
    const auto t0 = std::chrono::steady_clock::now();
    compose_draw_frame(dw, dh, fb, [&] { render_frame_draw(fb, local); });
    const auto t1 = std::chrono::steady_clock::now();
    const ComposeOutput &o = compose_output();
    std::vector<uint8_t> rgb((size_t)dw * dh * 3);
    compose_to_rgb(o, g_palette6, rgb.data());
    const auto t2 = std::chrono::steady_clock::now();
    int hud = 0;
    if (o.mask) for (int i = 0; i < gw * gh; i++) hud += o.mask[i] != 0;
    std::printf("%s: frame %dx%d on %dx%d: view %s %dx%d at %d,%d, HUD rect %d,%d %dx%d, %d HUD pixels, %d blits%s; "
                "draw %.1f ms, cpu compose %.1f ms\n",
                name, gw, gh, dw, dh, o.has_view ? "yes" : "no", o.view_w, o.view_h, o.view_x, o.view_y,
                o.hud_x, o.hud_y, o.hud_w, o.hud_h, hud, o.blit_count, o.corners_anchored ? " (corners anchored)" : "",
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(t2 - t1).count());
    CHECK(o.has_view);
    {   // the blits cover the frame exactly once
        std::vector<int> cover((size_t)gw * gh, 0);
        for (int i = 0; i < o.blit_count; i++)
            for (int y = o.blits[i].sy; y < o.blits[i].sy + o.blits[i].sh; y++)
                for (int x = o.blits[i].sx; x < o.blits[i].sx + o.blits[i].sw; x++) cover[(size_t)y * gw + x]++;
        int bad = 0;
        for (int c : cover) bad += c != 1;
        CHECK(bad == 0);
        if (dw * 3 > dh * 4 && input_mode == 0 && g_settings.hud_corners) CHECK(o.corners_anchored);
    }
    write_rgb(std::string("compose_") + name + ".ppm", rgb.data(), dw, dh);
    // the game frame itself, for comparison
    {
        std::vector<uint8_t> frgb((size_t)gw * gh * 3);
        uint8_t pal[768];
        mc_palette_to_rgb(g_palette6, pal);
        for (int i = 0; i < gw * gh; i++) std::memcpy(&frgb[(size_t)i * 3], pal + buf[(size_t)i] * 3, 3);
        write_rgb(std::string("compose_") + name + "_frame.ppm", frgb.data(), gw, gh);
    }
    compose_remove();
    rec.input_mode = saved_mode;
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (argc > 2) s_out_dir = argv[2];
    test_layout();
    test_synthetic_rgb();
    if (!engine_init(game)) { std::printf("engine_init failed (no game data): SKIP the frame tests\n"); return g_fail ? 1 : 0; }
    CHECK(engine_load_snapshot("movie/gam00000.dat", "movie/map00000.dat"));
    g_video_mode_flags = 8;
    CHECK(ui_draw_init(game));
    const int local = g_state->local_player & 7;
    PlayerRec &rec = g_state->players[local];
    rec.input_mode = 0;
    g_state->opt_hud_a = 1;
    g_state->opt_hud_b = 1;
    // hands: the first two owned spells (labels drawn); a message line
    {
        int found = 0;
        for (int i = 0; i < 24 && found < 2; i++)
            if (rec.blk.spell_slot[i] > 0 && rec.blk.spell_slot[i] < 1000) {
                (found ? rec.blk.slot_right : rec.blk.slot_left) = (int16_t)i;
                found++;
            }
        std::snprintf(rec.messages[1].text, sizeof rec.messages[1].text, "%s", "has died.");
        rec.messages[1].arg = 0;
        rec.messages[1].ticks = 50;
    }
    s_cam = player_camera(local);
    test_view_and_diff();
    test_faithful(local);
    composed_frame(game, 8, 1920, 1080, 0, "640_1920x1080", local);
    composed_frame(game, 8, 2560, 1080, 0, "640_2560x1080", local);
    composed_frame(game, 1, 1920, 1080, 0, "320_1920x1080", local);
    composed_frame(game, 1, 2560, 1080, 0, "320_2560x1080", local);
    composed_frame(game, 8, 2560, 1440, 2, "640_book_2560x1440", local);
    composed_frame(game, 8, 2560, 1080, 4, "640_map_2560x1080", local);
    g_settings.hud_scale_mode = 1;
    composed_frame(game, 8, 2560, 1080, 0, "640_fit_2560x1080", local);
    g_settings.hud_scale_mode = 0;
    g_video_mode_flags = 8;
    ui_draw_set_video_mode(game);
    ui_draw_shutdown();
    engine_shutdown();
    std::printf(g_fail ? "compose_test: %d FAILURES\n" : "compose_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
