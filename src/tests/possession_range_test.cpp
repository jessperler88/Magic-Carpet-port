#define _CRT_SECURE_NO_WARNINGS
// possession_range_test: PortSettings::possession_range_pct (settings.h GameplayRules).
//
// The Possession shot (projectile class 9 type 1, projectile_homing_update_448b0) lives 11 ticks in the
// original and picks its target on the first tick among mana within 0x1400 (20 cells). Checks on level 0:
//  1. default settings: the shot moves 11 times, the 11th move ends in the impact (deleted), so it is seen
//     alive after 10 ticks having flown ~10 * 0x180 units; a mana ball 24 cells ahead is not picked;
//  2. 130 %: 14 moves (13 seen alive), and the 24-cell ball is picked (radius 26 cells);
//  3. a forced rule (movie / network) wins over g_settings: forcing 100 with the setting at 130 gives 11.
// Exit 0 = pass; SKIP when the game data is missing or level 0 has no live mana ball.
#include "engine.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "settings.h"
#include "thing.h"
#include "player.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static bool load() {
    if (!engine_load_level(0)) return false;
    g_cfg->flags = 0; g_cfg->paused = 0;
    for (int t = 0; t < 40; t++) engine_tick();
    return true;
}

// A mana ball the shot may pick (projectile_pick_target_45f00 case 1: timer_a != 0, not the shooter's).
static unsigned find_ball(int owner) {
    for (uint32_t i = g_cfg->mana_ball_list; i != 0 && i < (uint32_t)thing_pool_slots(); i = thing_at(i)->next) {
        const Thing *o = thing_at(i);
        if (o->timer_a != 0 && (int32_t)o->mana_owner != owner && o->z > 0) return i;
    }
    return 0;
}

// A shot `dist` units south (+y) of the ball, aimed at it (yaw 0 = -y). `straight`: no target pick, aimed
// 0x40 above the horizon (no terrain, no mana in the way) - the lifetime alone ends it.
struct Shot { unsigned idx; int ticks; double travelled; uint16_t target; };
static Shot fire(unsigned ball, int dist, bool straight) {
    const Thing *b = thing_at(ball);
    Pos p = *thing_pos(b);
    p.y = (uint16_t)(p.y + dist);
    p.z = (int16_t)(p.z + (straight ? 0x400 : 0));
    Thing *s = thing_create(&p, 9, 1);
    Shot r{0, 0, 0, 0};
    if (!s) return r;
    s->owner = 0x7fff;                                      // nobody's
    s->yaw = straight ? 0 : (uint16_t)pos_angle_to(thing_pos(s), thing_pos(b));
    s->pitch = straight ? 0x40 : (uint16_t)pos_pitch_to(thing_pos(s), thing_pos(b));
    if (straight) s->flags |= 2;
    r.idx = thing_index(s);
    const Pos start = *thing_pos(s);
    Pos last = start;
    for (int t = 0; t < 60; t++) {
        engine_tick();
        if (t == 0) r.target = s->target;
        if (s->cls != 9 || s->type != 1 || (s->flags & 0x400)) break;  // impact: deleted / marked
        last = *thing_pos(s);
        r.ticks++;
    }
    const double dx = (int16_t)(uint16_t)(last.x - start.x), dy = (int16_t)(uint16_t)(last.y - start.y),
                 dz = last.z - start.z;
    r.travelled = std::sqrt(dx * dx + dy * dy + dz * dz);
    return r;
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!engine_init(game)) { std::printf("SKIP: no game data in %s\n", game); return 0; }
    if (!load()) { std::printf("SKIP: level 0 does not load\n"); engine_shutdown(); return 0; }
    const unsigned ball = find_ball(0x7fff);
    if (!ball) { std::printf("SKIP: no live mana ball on level 0\n"); engine_shutdown(); return 0; }

    struct Case { int setting; const GameplayRules *force; int ticks; bool picks; };
    GameplayRules faithful;
    const Case cases[] = {{100, nullptr, 10, false}, {130, nullptr, 13, true}, {130, &faithful, 10, false}};
    for (const Case &c : cases) {
        g_settings = PortSettings{};
        g_settings.possession_range_pct = c.setting;
        gameplay_force_rules(c.force);
        load();
        const Shot s = fire(ball, 0, true);
        load();
        const Shot p = fire(ball, 0x1800, false);       // 24 cells
        std::printf("setting %d%s: straight shot %d ticks, %.0f units; ball at 24 cells %s\n", c.setting,
                    c.force ? " (forced 100)" : "", s.ticks, s.travelled, p.target ? "picked" : "not picked");
        CHECK(s.ticks == c.ticks, "setting %d: the shot lived %d ticks, want %d", c.setting, s.ticks, c.ticks);
        // math_rotate_offset's fixed-point step is ~2 % short of 0x180
        CHECK(std::fabs(s.travelled / (c.ticks * 0x180) - 1.0) < 0.04, "setting %d: travelled %.0f, want ~%d",
              c.setting, s.travelled, c.ticks * 0x180);
        CHECK((p.target == ball) == c.picks, "setting %d: target %u, ball %u", c.setting, (unsigned)p.target, ball);
    }
    gameplay_force_rules(nullptr);
    g_settings = PortSettings{};

    engine_shutdown();
    std::printf(g_fail ? "possession_range_test: %d FAILED\n" : "possession_range_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
