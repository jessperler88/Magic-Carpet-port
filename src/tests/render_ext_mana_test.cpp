#define _CRT_SECURE_NO_WARNINGS
// render_ext_mana_test: mana balls far from the local player in the extended renderer.
//
// A mana ball's sprite (owner colour + size) is only refreshed by effect_mana_ball_update while the ball
// is awake (within 24 cells of the local player). When a wizard dies its balls pass to a mana hoard
// marker (player_type3 death -> class 10 type 0x28), and whoever claims the hoard owns all of them
// (effect_mana_hoard_update) - the original's rule. The original never draws the stale sprite of a far
// ball; the far view did, in the dead wizard's colour, until the player came close. Checks:
//  1. a sleeping ball owned by a computer wizard, then by a hoard, then by the local player (hoard claimed):
//     at every step the frame equals the frame drawn after forcing the sprite refresh (= what an awake
//     ball would show), and the simulation's sprite is still the stale one (the case is exercised);
//  2. rendering does not change the simulation (GameState checksum).
// Exit 0 = pass; SKIP when the game data is missing or no level has a computer wizard.
#include "engine.h"
#include "constructors.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "render.h"
#include "render_ext.h"
#include "settings.h"
#include "thing.h"
#include "player.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static uint32_t state_checksum() {
    uint32_t h = 2166136261u;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(g_state);
    for (size_t i = 0; i < sizeof(GameState); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static int find_wizard() {
    for (int i = 0; i < 8; i++) {
        if (i == (g_state->local_player & 7)) continue;
        const PlayerRec &r = g_state->players[i];
        if (r.is_computer != 1 || r.thing == 0) continue;
        const Thing *t = thing_at(r.thing);
        if (t->cls == 3 && t->health > 0) return r.thing;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!engine_init(game)) { std::printf("SKIP: no game data in %s\n", game); return 0; }

    int level = -1, wizard = 0;
    for (int l = 0; l < 70 && !wizard; l++) {
        if (!engine_load_level(l)) continue;
        g_cfg->flags = 0; g_cfg->paused = 0;
        for (int t = 0; t < 10; t++) engine_tick();
        wizard = find_wizard();
        if (wizard) level = l;
    }
    if (!wizard) { std::printf("SKIP: no level with a computer wizard\n"); engine_shutdown(); return 0; }
    const unsigned local = g_state->players[g_state->local_player & 7].thing;
    std::printf("level %d: wizard thing %d, local player thing %u\n", level, wizard, local);

    // A ball 64 cells from the local player (stays asleep), owned by the wizard.
    const Thing *pl = thing_at(local);
    Pos at = *thing_pos(pl);
    at.x = (uint16_t)(at.x + 0x4000);
    at.z = (int16_t)terrain_height_at(&at);
    Thing *ball = thing_create(&at, 10, 0x27);
    if (!ball) { std::printf("FAIL: no mana ball\n"); return 1; }
    const unsigned bi = thing_index(ball);
    ball->mana = 0x2000;
    ball->mana_owner = (uint16_t)wizard;
    mana_ball_update_sprite(ball);
    ball->timer_a = 0; ball->home.x = 0; ball->home.y = 0; ball->z_vel = 0;
    engine_tick();
    CHECK(thing_at(bi)->timer_a == 0, "the far ball woke up");

    g_settings.render_extended = true;
    g_settings.draw_distance = 125;
    g_settings.fog_start_pct = 60;
    const int W = 640, H = 360;
    std::vector<uint8_t> a((size_t)W * H), b((size_t)W * H);
    auto draw = [&](std::vector<uint8_t> &px) {
        const Thing *t = thing_at(bi);
        Camera cam{};
        cam.cam_x = (uint16_t)(t->x - 0x300);          // 3 cells "west", looking +x at the ball
        cam.cam_y = t->y;
        cam.yaw = 0x200;
        cam.cam_z = t->z + 0x100;
        cam.pitch = -8;
        cam.zoom = 0x100;
        render_view_ext(FrameBuffer{px.data(), W, H}, cam);
    };
    // Draws the frame as is, then with the sprite the simulation would give an awake ball; equal frames
    // = the renderer showed the current owner. Restores the stale sprite so the simulation is untouched.
    auto compare = [&](const char *what, bool expect_stale) {
        Thing *t = thing_at(bi);
        const uint16_t stale = t->sprite;
        const bool is_stale = (int)(int16_t)stale != mana_ball_sprite(t);
        CHECK(is_stale == expect_stale, "%s: simulation sprite stale = %d, expected %d", what, (int)is_stale, (int)expect_stale);
        draw(a);
        Thing save = *t;
        mana_ball_update_sprite(t);
        draw(b);
        *t = save;
        size_t diff = 0;
        for (size_t i = 0; i < a.size(); i++) diff += a[i] != b[i];
        std::printf("%s: sprite %#x -> current %#x, %zu pixels differ\n", what, (unsigned)stale, (unsigned)mana_ball_sprite(t), diff);
        CHECK(diff == 0, "%s: far ball drawn with a stale sprite (%zu pixels)", what, diff);
        CHECK(g_render_ext_stats.things_drawn > 0, "%s: nothing drawn", what);
    };
    compare("wizard-owned", false);

    // The wizard dies: its balls pass to a hoard marker (player.cpp, the death of a player Thing).
    Thing *hoard = thing_create(thing_pos(thing_at((unsigned)wizard)), 10, 0x28);
    if (!hoard) { std::printf("FAIL: no hoard\n"); return 1; }
    const unsigned hi = thing_index(hoard);
    thing_at(bi)->mana_owner = (uint16_t)hi;
    engine_tick();
    CHECK(thing_at(bi)->mana_owner == hi, "ball not owned by the hoard");
    compare("hoard-owned", true);

    // The local player claims the hoard (damage slot 1, as the Possession spell does).
    thing_at(hi)->damage_slots[1].attacker = (uint16_t)local;
    engine_tick();
    CHECK(thing_at(bi)->mana_owner == local, "hoard claim did not pass the ball (owner %u)", (unsigned)thing_at(bi)->mana_owner);
    const uint32_t sum0 = state_checksum();
    compare("claimed by the local player", true);
    CHECK(state_checksum() == sum0, "rendering changed the simulation");

    g_settings = PortSettings{};
    engine_shutdown();
    std::printf(g_fail ? "FAILED (%d)\n" : "PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
