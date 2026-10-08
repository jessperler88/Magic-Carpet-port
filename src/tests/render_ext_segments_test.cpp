#define _CRT_SECURE_NO_WARNINGS
// render_ext_segments_test: body segments of sleeping dragons / worms in the extended renderer.
//
// The original wakes a creature only within 24 cells of the local player; asleep, its body segments
// (state 0x78) snap onto their parent every 4th tick, so the body collapses into one jumping stack. The
// far view makes that visible; render_ext.cpp's thing_pos draws them `speed` units behind their parent
// instead (render-only). Checks, on the first level that has a sleeping segmented creature:
//  1. every visible sleeping segment is drawn `speed` (+-2) units from where its parent is drawn, over
//     several ticks and frames, while the simulation keeps them stacked;
//  2. the simulation is unaffected: the GameState checksum after the ticks is the same whether frames
//     were rendered in between or not;
//  3. an awake creature crossing its wake-timer reset (the simulation snaps segments for one tick) is
//     drawn (interpolated) without a jump: spacing kept every frame, per-frame movement bounded by the head's;
//  4. screenshots (argv[2] / MC_REXT_OUT, default current dir): rext_segments_<level>_<k>.ppm.
// Exit 0 = pass; SKIP when the game data is missing or no level has such a creature.
#include "engine.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "render.h"
#include "render_ext.h"
#include "settings.h"
#include "thing.h"
#include "player.h"
#include "mcfile.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static void write_ppm(const std::string &path, const uint8_t *px, int w, int h) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t rgb[768];
    mc_palette_to_rgb(g_palette6, rgb);
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) std::fwrite(rgb + px[i] * 3, 1, 3, f);
    std::fclose(f);
}

static uint32_t state_checksum() {
    uint32_t h = 2166136261u;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(g_state);
    for (size_t i = 0; i < sizeof(GameState); i++) h = (h ^ p[i]) * 16777619u;
    for (int i = MC_THING_SLOTS; i < thing_pool_slots(); i++) {
        const uint8_t *q = reinterpret_cast<const uint8_t *>(thing_at((unsigned)i));
        for (size_t k = 0; k < sizeof(Thing); k++) h = (h ^ q[k]) * 16777619u;
    }
    return h;
}

static bool load(int level) {
    if (!engine_load_level(level)) return false;
    g_cfg->flags = 0; g_cfg->paused = 0;
    return true;
}

// A head (cls 5, not a segment) whose child chain is asleep and far from the local player.
static int find_sleeping_head() {
    const Thing *pl = thing_at(g_state->players[g_state->local_player & 7].thing);
    for (int i = 1; i < thing_pool_slots(); i++) {
        const Thing *t = thing_at((unsigned)i);
        if (t->cls != 5 || t->state == 0x78 || t->child == 0 || t->health < 0) continue;
        const Thing *c = thing_at(t->child);
        if (c->state != 0x78 || c->timer_a != 0) continue;
        const int dx = (int16_t)(uint16_t)(t->x - pl->x), dy = (int16_t)(uint16_t)(t->y - pl->y);
        if (dx * dx + dy * dy < 0x3000 * 0x3000) continue;     // > 48 cells from the player: stays asleep
        return i;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    const char *outenv = std::getenv("MC_REXT_OUT");
    const std::string out = argc > 2 ? argv[2] : outenv ? outenv : ".";
    if (!engine_init(game)) { std::printf("SKIP: no game data in %s\n", game); return 0; }

    int level = -1, head = 0;
    for (int l = 0; l < 70 && !head; l++) {
        if (!load(l)) continue;
        for (int t = 0; t < 40; t++) engine_tick();
        head = find_sleeping_head();
        if (head) level = l;
    }
    if (!head) { std::printf("SKIP: no level with a sleeping dragon / worm\n"); engine_shutdown(); return 0; }
    std::printf("level %d: head %d (type %d)\n", level, head, (int)thing_at((unsigned)head)->type);

    // 2. reference run without rendering
    load(level);
    for (int t = 0; t < 40; t++) engine_tick();
    for (int t = 0; t < 60; t++) engine_tick();
    const uint32_t sum_plain = state_checksum();

    // 1 + 3. the same run, rendering 3 frames per tick from a camera ~40 cells off, looking at the head
    load(level);
    for (int t = 0; t < 40; t++) engine_tick();
    g_settings.render_extended = true;
    g_settings.draw_distance = 125;
    g_settings.fog_start_pct = 60;
    const int W = 960, H = 540;
    std::vector<uint8_t> px((size_t)W * H);
    const FrameBuffer fb{px.data(), W, H};
    int checked = 0, shots = 0, stacked = 0;
    double worst = 0;
    for (int t = 0; t < 60; t++) {
        engine_tick();
        const Thing *h = thing_at((unsigned)head);
        if (h->cls != 5) break;
        // camera 40 cells "south-east" of the head, looking at it
        const int ox = 0x2000, oy = 0x1c00;
        Camera cam{};
        cam.cam_x = (uint16_t)(h->x + ox);
        cam.cam_y = (uint16_t)(h->y + oy);
        const double a = std::atan2(-(double)ox, (double)oy);                 // yaw 0 = -y, 0x200 = +x
        cam.yaw = (int)std::lround(a * 2048.0 / 6.283185307179586) & 0x7ff;
        cam.cam_z = std::max<int>(h->z, g_map_height[mc_cell_of((uint16_t)cam.cam_x, (uint16_t)cam.cam_y)] * 0x20) + 0x600;
        cam.pitch = -24;
        cam.zoom = 0x100;
        for (int f = 0; f < 3; f++) {
            render_view_ext(fb, cam);
            for (unsigned s = h->child, guard = 0; s != 0 && guard < 64; s = thing_at(s)->child, guard++) {
                const Thing *seg = thing_at(s);
                int32_t x, y, z, X, Y, Z;
                if (seg->timer_a != 0 || !render_ext_segment_pos(s, &x, &y, &z)) continue;
                if (seg->parent == (unsigned)head) { X = h->x; Y = h->y; Z = h->z; }
                else if (!render_ext_segment_pos(seg->parent, &X, &Y, &Z)) continue;
                const double dx = (int16_t)(uint16_t)(x - X), dy = (int16_t)(uint16_t)(y - Y), dz = z - Z;
                const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double err = std::fabs(d - (double)(uint16_t)seg->speed);
                if (err > worst) worst = err;
                checked++;
                const Thing *par = thing_at(seg->parent);
                if (seg->x == par->x && seg->y == par->y) stacked++;
            }
        }
        if (t % 20 == 19) {
            char name[96];
            std::snprintf(name, sizeof name, "/rext_segments_%02d_%d.ppm", level, shots++);
            write_ppm(out + name, px.data(), W, H);
        }
    }
    std::printf("segments checked %d (simulation stacked on their parent: %d), worst spacing error %.2f\n", checked, stacked, worst);
    CHECK(checked > 0, "no sleeping segment was drawn");
    CHECK(worst <= 2.0, "segment spacing off by %.2f units", worst);
    g_settings = PortSettings{};
    const uint32_t sum_rendered = state_checksum();
    CHECK(sum_plain == sum_rendered, "rendering changed the simulation (%08x vs %08x)", sum_plain, sum_rendered);

    // 4. awake creature (local player kept 8 cells off the head), drawn with the render-time interpolation
    //    (3 frames per tick, alpha 0 / 1/3 / 2/3, as mcport does): its wake timer runs out every 17th tick, and
    //    on that tick the simulation snaps a quarter of the segments onto their parent. Drawn, every segment
    //    must stay `speed` (+-2) from its drawn parent on every frame, and move per frame no further than its
    //    chain allows (twice the head's step + 8) - no one-frame jump at the reset.
    load(level);
    for (int t = 0; t < 40; t++) engine_tick();
    g_settings.render_extended = true;
    g_settings.draw_distance = 125;
    g_settings.fog_start_pct = 60;
    {
        const int n = thing_pool_slots();
        std::vector<int16_t> prev((size_t)n * 3);
        std::vector<int32_t> last((size_t)n * 3, 0);
        std::vector<uint8_t> seen((size_t)n, 0);
        int resets = 0, snaps = 0, awake_checked = 0;
        double worst_gap = 0, worst_jump = 0;
        // what the renderer draws for a Thing it does not lay out: the interpolated simulation position
        auto lerp_pos = [&](unsigned i, uint32_t a, int32_t &x, int32_t &y, int32_t &z) {
            const Thing *t = thing_at(i);
            const int16_t *p = &prev[(size_t)i * 3];
            x = (uint16_t)((uint16_t)p[0] + (int32_t)(((int64_t)(int16_t)(uint16_t)(t->x - (uint16_t)p[0]) * a) >> 16));
            y = (uint16_t)((uint16_t)p[1] + (int32_t)(((int64_t)(int16_t)(uint16_t)(t->y - (uint16_t)p[1]) * a) >> 16));
            z = (int32_t)p[2] + (int32_t)(((int64_t)((int32_t)t->z - p[2]) * a) >> 16);
        };
        int32_t hx0 = 0, hy0 = 0;
        bool have_h0 = false;
        for (int t = 0; t < 120; t++) {
            Thing *h = thing_at((unsigned)head);
            if (h->cls != 5) break;
            Thing *pl = thing_at(g_state->players[g_state->local_player & 7].thing);
            Pos p = *thing_pos(pl); p.x = (uint16_t)(h->x + 0x800); p.y = h->y;
            thing_move_to(pl, &p);
            for (int i = 0; i < n; i++) {
                const Thing *q = thing_at((unsigned)i);
                prev[(size_t)i * 3] = (int16_t)q->x; prev[(size_t)i * 3 + 1] = (int16_t)q->y; prev[(size_t)i * 3 + 2] = (int16_t)q->z;
            }
            engine_tick();
            if (h->timer_a == 0) resets++;
            for (unsigned s = h->child, guard = 0; s != 0 && guard < 64; s = thing_at(s)->child, guard++) {
                const Thing *seg = thing_at(s);
                if (seg->x == thing_at(seg->parent)->x && seg->y == thing_at(seg->parent)->y) snaps++;
            }
            Camera cam{};
            cam.cam_x = (uint16_t)(h->x + 0x800);
            cam.cam_y = h->y;
            cam.yaw = 0x600;                                                    // looking -x, at the head
            cam.cam_z = std::max<int>(h->z, g_map_height[mc_cell_of((uint16_t)cam.cam_x, (uint16_t)cam.cam_y)] * 0x20) + 0x400;
            cam.pitch = -24;
            cam.zoom = 0x100;
            for (int f = 0; f < 3; f++) {
                const uint32_t alpha = (uint32_t)(f * 0x10000 / 3);
                g_render_interp = RenderInterp{};
                g_render_interp.active = true;
                g_render_interp.alpha = alpha;
                g_render_interp.prev_pos = reinterpret_cast<const int16_t (*)[3]>(prev.data());
                g_render_interp.prev_count = n;
                render_view_ext(fb, cam);
                int32_t hx, hy, hz;
                lerp_pos((unsigned)head, alpha, hx, hy, hz);
                double hstep = 0;
                if (have_h0) {
                    const double hdx = (int16_t)(uint16_t)(hx - hx0), hdy = (int16_t)(uint16_t)(hy - hy0);
                    hstep = std::sqrt(hdx * hdx + hdy * hdy);
                }
                hx0 = hx; hy0 = hy;
                for (unsigned s = h->child, guard = 0; s != 0 && guard < 64; s = thing_at(s)->child, guard++) {
                    const Thing *seg = thing_at(s);
                    int32_t x, y, z, X, Y, Z;
                    if (!render_ext_segment_pos(s, &x, &y, &z)) lerp_pos(s, alpha, x, y, z);
                    if (!render_ext_segment_pos(seg->parent, &X, &Y, &Z)) lerp_pos(seg->parent, alpha, X, Y, Z);
                    const double dx = (int16_t)(uint16_t)(x - X), dy = (int16_t)(uint16_t)(y - Y), dz = z - Z;
                    worst_gap = std::max(worst_gap, std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - (double)(uint16_t)seg->speed));
                    awake_checked++;
                    if (have_h0 && seen[s]) {
                        const double mx = (int16_t)(uint16_t)(x - last[(size_t)s * 3]), my = (int16_t)(uint16_t)(y - last[(size_t)s * 3 + 1]);
                        worst_jump = std::max(worst_jump, std::sqrt(mx * mx + my * my) - (2.0 * hstep + 8.0));
                    }
                    last[(size_t)s * 3] = x; last[(size_t)s * 3 + 1] = y; last[(size_t)s * 3 + 2] = z; seen[s] = 1;
                }
                have_h0 = true;
            }
        }
        g_render_interp = RenderInterp{};
        std::printf("awake: %d wake-timer resets, %d simulation snaps, segments checked %d, worst spacing error %.2f, "
                    "worst jump beyond the chain's step %.2f\n", resets, snaps, awake_checked, worst_gap, worst_jump);
        CHECK(resets >= 2 && snaps > 0, "the awake run did not cross a wake-timer reset with a snap");
        CHECK(awake_checked > 0, "no awake segment was drawn");
        CHECK(worst_gap <= 2.0, "awake segment spacing off by %.2f units", worst_gap);
        CHECK(worst_jump <= 0.0, "a segment jumped %.2f units further than its chain moved", worst_jump);
    }
    g_settings = PortSettings{};

    engine_shutdown();
    std::printf(g_fail ? "render_ext_segments_test: %d FAILED\n" : "render_ext_segments_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
