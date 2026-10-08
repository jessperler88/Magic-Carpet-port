#define _CRT_SECURE_NO_WARNINGS
// project_test (round 10, task C): the projection export of render.h.
//
//  1. Faithful renderer (320x200, 640x480; level 38 after 50 ticks; plain camera, rolled camera, reduced view
//     window): every Thing blit the sprite probe (g_sprite_blit_probe) sees is exactly where
//     render_project_world puts the Thing; the anchor list equals the probe's list; the frame drawn inside a
//     capture is byte-identical to the frame drawn without one and the override is restored.
//  2. Extended renderer (320x200, 640x480, 1920x1080): every anchor re-projects exactly onto itself, and its
//     world position is the Thing's (no interpolation in the test).
//  3. Ground pick round trip on both renderers and all sizes: the centre of every visible cell within 8 cells
//     of the camera projected and picked back gives the same cell (cells hidden behind nearer terrain are
//     counted apart); render_pick_thing at an anchor's box centre returns a Thing whose box contains it.
//  4. Composed (compositor 1920x1080 over a 640x480 game frame): view -> game frame -> display agrees with
//     the compositor's own view -> display mapping, and compose_display_to_view inverts it.
// Exit 0 = pass; SKIP without the game data.
#include "engine.h"
#include "sim.h"
#include "mc_globals.h"
#include "render.h"
#include "render_ext.h"
#include "raster.h"
#include "settings.h"
#include "sprites.h"
#include "terrain.h"
#include "thing.h"
#include "player.h"
#include "compose.h"
#include "crash_handler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

struct Canvas {
    std::vector<uint8_t> buf;
    int w = 0, h = 0;
    void init(int W, int H) { w = W; h = H; buf.assign((size_t)W * H, 0); }
    FrameBuffer fb() { return FrameBuffer{buf.data(), w, h}; }
};

struct Blit { unsigned slot; int x, y; };
static std::vector<Blit> s_blits;
static void probe(unsigned anchor) {
    const SpriteBlit &b = g_sprite_blit;
    if (anchor == 1 && b.thing) s_blits.push_back(Blit{thing_index(b.thing), b.x, b.y});
}

static bool load_level(int level, int ticks) {
    if (!engine_load_level(level)) return false;
    g_cfg->flags = 0; g_cfg->paused = 0;
    for (int t = 0; t < ticks; t++) engine_tick();
    return true;
}

struct PickStats { int tried = 0, ok = 0, hidden = 0, wrong = 0, miss = 0; };

static PickStats pick_round_trip(int radius) {
    PickStats st;
    const RenderViewInfo &v = render_view_info();
    const int ccx = (v.cam_x >> 8) & 0xff, ccy = (v.cam_y >> 8) & 0xff;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            const int cx = (ccx + dx) & 0xff, cy = (ccy + dy) & 0xff;
            const int32_t wx = cx * 256 + 128, wy = cy * 256 + 128;
            const int32_t wz = terrain_sample_height((uint16_t)wx, (uint16_t)wy);
            int sx, sy, depth;
            if (!render_project_world(wx, wy, wz, &sx, &sy, &depth)) continue;
            if (sx < 0 || sy < 0 || sx >= v.view_w || sy >= v.view_h) continue;
            st.tried++;
            int px, py;
            int32_t hx, hy, hz;
            if (!render_pick_ground(sx, sy, &px, &py, &hx, &hy, &hz)) { st.miss++; continue; }
            if (px == cx && py == cy) { st.ok++; continue; }
            int s2, t2, d2;
            if (render_project_world(hx, hy, hz, &s2, &t2, &d2) && d2 < depth - 192) st.hidden++;   // nearer terrain in the way
            else {
                st.wrong++;
                if (st.wrong <= 3)
                    std::printf("    pick (%d,%d) depth %d -> cell %d,%d (want %d,%d) hit depth %d\n", sx, sy, depth, px, py, cx, cy, d2);
            }
        }
    return st;
}

// render_pick_thing at the centre of each anchor's box returns an anchor that contains the point, in front.
static int check_thing_picks() {
    int n = 0, bad = 0;
    const RenderAnchor *a = render_anchors(&n);
    for (int i = 0; i < n; i++) {
        if (a[i].w < 2 || a[i].h < 2) continue;
        const int px = a[i].sx, py = a[i].sy - a[i].h / 2;
        const int slot = render_pick_thing(px, py, 0);
        bool ok = false;
        for (int k = 0; k < n && !ok; k++) {
            if ((int)a[k].slot != slot) continue;
            const bool inside = px >= a[k].sx - a[k].w * 0.5 && px <= a[k].sx + a[k].w * 0.5 && py >= a[k].sy - a[k].h && py <= a[k].sy;
            ok = inside && a[k].depth <= a[i].depth;
        }
        bad += !ok;
    }
    return bad;
}

static void faithful_case(int W, int H, const Camera &cam, int view_size, const char *tag) {
    g_video_mode_flags = W == 320 ? 1 : 8;
    Canvas plain, cap;
    plain.init(W, H); cap.init(W, H);
    int32_t slx, sly;
    render_get_slope_state(&slx, &sly);
    // without a capture
    FrameBuffer pfb = plain.fb();
    render_set_view_window(pfb, view_size);
    render_view_frame(pfb, cam);
    render_set_slope_state(slx, sly);
    // with a capture (anchors on) and the probe
    s_blits.clear();
    g_sprite_blit_probe = probe;
    FrameBuffer cfb = cap.fb();
    const RenderViewFn before = g_render_view_override;
    render_capture_begin(cfb, true);
    render_set_view_window(cfb, view_size);
    render_view_frame(cfb, cam);
    render_capture_end();
    g_sprite_blit_probe = nullptr;
    CHECK(g_render_view_override == before, "%s: override not restored", tag);
    CHECK(plain.buf == cap.buf, "%s: the captured frame differs from the plain one", tag);
    const RenderViewInfo &v = render_view_info();
    CHECK(v.kind == RENDER_VIEW_FAITHFUL && v.frame_known, "%s: view info kind %d known %d", tag, v.kind, v.frame_known);
    int na = 0;
    const RenderAnchor *a = render_anchors(&na);
    CHECK(na == (int)s_blits.size(), "%s: %d anchors, %d blits", tag, na, (int)s_blits.size());
    int exact = 0, anchors_same = 0;
    for (size_t i = 0; i < s_blits.size(); i++) {
        const Blit &b = s_blits[i];
        const Thing *t = thing_at(b.slot);
        int sx, sy, depth;
        const bool in_front = render_project_world(t->x, t->y, t->z, &sx, &sy, &depth);
        if (in_front && sx == b.x && sy == b.y) exact++;
        else if (exact + 3 > (int)i) std::printf("    %s: slot %u blit (%d,%d) projected (%d,%d) depth %d\n", tag, b.slot, b.x, b.y, sx, sy, depth);
        if ((int)i < na && a[i].slot == b.slot && a[i].sx == b.x && a[i].sy == b.y) anchors_same++;
    }
    CHECK(exact == (int)s_blits.size(), "%s: %d of %d blits at the projected point", tag, exact, (int)s_blits.size());
    CHECK(anchors_same == (int)s_blits.size(), "%s: %d of %d anchors equal the blits", tag, anchors_same, (int)s_blits.size());
    // view -> frame: the window offset of render_set_view_window
    const intptr_t off = g_rt_dest - cfb.pixels;
    double fx, fy;
    CHECK(render_view_to_frame(0, 0, &fx, &fy) && fx == (double)(off % W) && fy == (double)(off / W), "%s: frame mapping", tag);
    const PickStats ps = pick_round_trip(8);
    const int bad = check_thing_picks();
    std::printf("faithful %-14s: %3d blits = projection = anchors; ground pick %d/%d (hidden %d, wrong %d, miss %d); thing picks bad %d\n",
                tag, exact, ps.ok, ps.tried, ps.hidden, ps.wrong, ps.miss, bad);
    CHECK(ps.tried > 20 && ps.wrong == 0 && ps.miss == 0, "%s: ground pick round trip", tag);
    CHECK(bad == 0, "%s: %d thing picks", tag, bad);
    const PickStats wide = pick_round_trip(18);   // information: a pixel spans more than a cell far away
    std::printf("    up to 18 cells: %d/%d (hidden %d, wrong %d, miss %d)\n", wide.ok, wide.tried, wide.hidden, wide.wrong, wide.miss);
}

static void ext_case(int W, int H, const Camera &cam, const char *tag) {
    Canvas c;
    c.init(W, H);
    render_capture_begin(c.fb(), true);
    render_view_ext(c.fb(), cam);
    render_capture_end();
    const RenderViewInfo &v = render_view_info();
    CHECK(v.kind == RENDER_VIEW_EXT && v.view_w == W && v.view_h == H, "%s: view info", tag);
    int na = 0;
    const RenderAnchor *a = render_anchors(&na);
    int exact = 0, pos_ok = 0;
    for (int i = 0; i < na; i++) {
        int sx, sy, depth;
        if (render_project_world(a[i].x, a[i].y, a[i].z, &sx, &sy, &depth) && sx == a[i].sx && sy == a[i].sy) exact++;
        const Thing *t = thing_at(a[i].slot);
        const bool segment = t->cls == 5 && t->state == 0x78;
        if (segment || (a[i].x == t->x && a[i].y == t->y && a[i].z == t->z)) pos_ok++;
    }
    CHECK(na > 0 && exact == na, "%s: %d of %d anchors re-project exactly", tag, exact, na);
    CHECK(pos_ok == na, "%s: %d of %d anchors at the Thing's position", tag, pos_ok, na);
    const PickStats ps = pick_round_trip(8);
    const int bad = check_thing_picks();
    std::printf("extended %-14s: %3d anchors re-project exactly; ground pick %d/%d (hidden %d, wrong %d, miss %d); thing picks bad %d\n",
                tag, exact, ps.ok, ps.tried, ps.hidden, ps.wrong, ps.miss, bad);
    CHECK(ps.tried > 20 && ps.wrong == 0 && ps.miss == 0, "%s: ground pick round trip", tag);
    CHECK(bad == 0, "%s: %d thing picks", tag, bad);
    const PickStats wide = pick_round_trip(40);   // information: a pixel spans more than a cell far away
    std::printf("    up to 40 cells: %d/%d (hidden %d, wrong %d, miss %d)\n", wide.ok, wide.tried, wide.hidden, wide.wrong, wide.miss);
}

static void composed_case(const Camera &cam) {
    Canvas g;
    g.init(640, 480);
    g_video_mode_flags = 8;
    FrameBuffer fb = g.fb();
    compose_install();
    render_capture_begin(fb, true);
    compose_begin_frame(1920, 1080, fb);
    render_set_view_window(fb, 0x28);
    render_view_frame(fb, cam);
    if (compose_need_second_pass()) {
        compose_begin_second_pass(fb);
        render_set_view_window(fb, 0x28);
        render_view_frame(fb, cam);                  // places the altered view, renders nothing: info / anchors stay
    }
    compose_end_frame(fb);
    render_capture_end();
    compose_remove();
    const ComposeOutput &o = compose_output();
    (void)o;
    const RenderViewInfo &v = render_view_info();
    CHECK(v.kind == RENDER_VIEW_EXT && v.composed && v.frame_known && v.view_w == 1920 && v.view_h == 1080, "composed: view info");
    // the compositor's output was reset by compose_remove: rebuild the mapping it had from the record
    ComposeOutput m;
    m.display_w = 1920; m.display_h = 1080; m.game_w = 640; m.game_h = 480;
    compose_hud_rect(1920, 1080, 640, 480, g_settings.hud_scale_mode, &m.hud_x, &m.hud_y, &m.hud_w, &m.hud_h);
    m.has_view = true; m.view_buf_w = 1920; m.view_buf_h = 1080; m.view_x = 0; m.view_y = 0; m.view_w = 1920; m.view_h = 1080;
    int na = 0;
    const RenderAnchor *a = render_anchors(&na);
    int ok = 0, inv = 0, inside = 0;
    for (int i = 0; i < na; i++) {
        double fx, fy, dx1, dy1, dx2, dy2;
        if (!render_view_to_frame(a[i].sx + 0.5, a[i].sy + 0.5, &fx, &fy)) continue;
        dx1 = m.hud_x + fx * m.hud_w / m.game_w; dy1 = m.hud_y + fy * m.hud_h / m.game_h;
        compose_view_to_display(m, a[i].sx, a[i].sy, &dx2, &dy2);
        if (std::fabs(dx1 - dx2) < 1e-6 && std::fabs(dy1 - dy2) < 1e-6) ok++;
        int vx, vy;
        if (a[i].sx < 0 || a[i].sy < 0 || a[i].sx >= 1920 || a[i].sy >= 1080) continue;   // anchor off the view
        inside++;
        if (compose_display_to_view(m, (int)std::floor(dx2), (int)std::floor(dy2), &vx, &vy) && vx == a[i].sx && vy == a[i].sy) inv++;
    }
    std::printf("composed 1920x1080 over 640x480: %d anchors, view->frame->display = compositor %d, display->view %d of %d\n", na, ok, inv, inside);
    CHECK(na > 0 && ok == na && inv == inside, "composed mapping");
    const PickStats ps = pick_round_trip(8);
    std::printf("composed ground pick %d/%d (hidden %d, wrong %d, miss %d)\n", ps.ok, ps.tried, ps.hidden, ps.wrong, ps.miss);
    CHECK(ps.tried > 20 && ps.wrong == 0 && ps.miss == 0, "composed: ground pick round trip");
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!engine_init(game_dir)) { std::printf("SKIP: engine_init failed (game data missing)\n"); return 0; }
    if (!load_level(38, 50)) { std::printf("SKIP: level 38 missing\n"); return 0; }
    const PortSettings defaults = g_settings;
    const Camera cam = player_camera(g_state->local_player & 7);
    Camera rolled = cam;
    rolled.roll = 0x60;
    rolled.pitch = cam.pitch - 0x10;

    faithful_case(320, 200, cam, 0x28, "320x200");
    faithful_case(640, 480, cam, 0x28, "640x480");
    faithful_case(320, 200, rolled, 0x28, "320x200 rolled");
    faithful_case(640, 480, rolled, 0x28, "640x480 rolled");
    faithful_case(640, 480, cam, 0x1c, "640x480 window");

    g_settings.render_extended = true;
    g_settings.draw_distance = 48;
    ext_case(320, 200, cam, "320x200");
    ext_case(640, 480, cam, "640x480");
    ext_case(1920, 1080, cam, "1920x1080");
    ext_case(1920, 1080, rolled, "1920x1080 roll");
    composed_case(cam);
    g_settings = defaults;

    std::printf(g_fail ? "project_test: %d FAILURES\n" : "project_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
