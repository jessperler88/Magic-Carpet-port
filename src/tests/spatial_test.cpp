// Unit test for spatial.cpp (collision searches, area damage, pending-damage slots, terrain checks)
// and the castle site tests of level_features.cpp, on a generated level 38 and on the engine's own
// snapshot (movie/gam00000.dat). Also the reference for how a game-logic test uses the sim core
// (sim.h + ${MC_SIM_CORE}). argv[1] = game dir.
#include "sim.h"
#include "spatial.h"
#include "level_features.h"
#include "player.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { \
    std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); g_fail++; } } while (0)

static bool live(const Thing *t) { return t->cls != 0 && (t->flags & 4); }

static bool filter_pass(const Thing *t, const Thing *o) {
    if (t->filter_cls == 0xff) return true;
    if (t->filter_cls != o->cls) return false;
    return t->filter_type == 0xff || t->filter_type == o->type;
}

// A probe thing that is not in any cell list: the searches only read its position / extents / owner.
static Thing make_probe(const Thing *at, int ext) {
    Thing p{};
    p.cls = 10;
    p.owner = 999;                    // nobody's
    p.filter_cls = p.filter_type = 0xff;
    p.x = at->x; p.y = at->y; p.z = at->z;
    p.ext_z0 = at->ext_z0;
    p.ext_x = p.ext_y = (int16_t)ext;
    p.ext_h = 0x400;
    return p;
}

static void test_searches(const char *what) {
    int searched = 0, found = 0, mana = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t)) continue;
        Thing probe = make_probe(t, 0x180);
        // brute force: does anything satisfy the predicate at all?
        bool any = false, any_ball = false;
        for (int j = 1; j < MC_THING_SLOTS; j++) {
            Thing *o = thing_at(j);
            if (!live(o)) continue;
            if ((o->flags & 8) && o->owner != probe.owner && thing_collide(&probe, o)) any = true;
            if ((o->flags & 8) && o->cls == 10 && o->type == 0x27 && thing_collide(&probe, o)) any_ball = true;
        }
        Thing *hit = thing_find_collision(&probe);
        searched++;
        if (hit) {
            found++;
            CHECK(hit->flags & 8);
            CHECK(thing_collide(&probe, hit));
            CHECK(filter_pass(&probe, hit));
        }
        // The probe sits on a linked thing: when that one is collidable the search must find
        // something, and it never invents a hit. (Not an equivalence: a thing whose box is larger
        // than the probe's ring range - a castle - overlaps from a cell the walk does not visit,
        // which is why the area functions reach castles through the player list.)
        if (t->flags & 8) CHECK(hit != nullptr);
        if (!any) CHECK(hit == nullptr);
        Thing *ball = thing_find_mana_ball_touching(&probe);
        if (ball) { mana++; CHECK(ball->cls == 10 && ball->type == 0x27 && thing_collide(&probe, ball)); }
        if (!any_ball) CHECK(ball == nullptr);
        if (t->cls == 10 && t->type == 0x27 && (t->flags & 8)) CHECK(ball != nullptr);
        // class filter
        probe.filter_cls = t->cls; probe.filter_type = t->type;
        Thing *same = thing_find_collision_other_owner(&probe);
        CHECK(same != nullptr);
        if (same) CHECK(same->cls == t->cls && same->type == t->type);
        CHECK_EQ(thing_exists_near_pos(thing_pos(t), t->cls, t->type), 1);
        CHECK_EQ(thing_exists_near_pos(thing_pos(t), 13, 0x7f), 0);
    }
    std::printf("%s: %d probes, %d collisions, %d mana balls touched\n", what, searched, found, mana);
    CHECK(searched > 100);
    CHECK(found > 50);
}

static void test_damage() {
    // a damageable creature and a probe on top of it
    Thing *victim = nullptr;
    for (int i = 1; i < MC_THING_SLOTS && !victim; i++) {
        Thing *t = thing_at(i);
        if (live(t) && t->cls == 5 && (t->prop_flags & 1) && (t->flags & 8)) victim = t;
    }
    CHECK(victim != nullptr);
    if (!victim) return;
    std::memset(victim->damage_slots, 0, sizeof victim->damage_slots);
    Thing probe = make_probe(victim, 0x100);
    probe.owner = 777;
    thing_area_damage(&probe, 0, 50);
    CHECK_EQ(victim->damage_slots[0].amount, 50);
    CHECK_EQ(victim->damage_slots[0].attacker, 777);
    thing_area_damage(&probe, 0, 0x10000 + 25);            // the amount is a 16-bit argument
    CHECK_EQ(victim->damage_slots[0].amount, 75);
    thing_area_damage_fire(&probe, 0, 10);
    CHECK_EQ(victim->damage_slots[0].amount, 85);
    thing_area_damage_fire(&probe, 3, 10);                 // slot != 0: nothing
    thing_area_damage_quake(&probe, 0, 15);
    CHECK_EQ(victim->damage_slots[0].amount, 100);
    // own things are never hit
    probe.owner = victim->owner;
    thing_area_damage(&probe, 0, 1000);
    CHECK_EQ(victim->damage_slots[0].amount, 100);
    // a slot the victim does not accept
    probe.owner = 777;
    uint16_t saved = victim->prop_flags;
    victim->prop_flags = 1;
    thing_area_damage(&probe, 2, 40);
    CHECK_EQ(victim->damage_slots[2].amount, 0);
    victim->prop_flags = 1 | 4;
    thing_area_damage(&probe, 2, 40);
    CHECK_EQ(victim->damage_slots[2].amount, 40);
    CHECK_EQ(victim->damage_slots[2].attacker, 777);
    victim->prop_flags = saved;
    // thing_add_pending_damage: adds while no attacker is recorded, replaces afterwards
    std::memset(victim->damage_slots, 0, sizeof victim->damage_slots);
    victim->damage_slots[1].amount = 5;
    thing_add_pending_damage(&probe, victim, 1, 7);
    CHECK_EQ(victim->damage_slots[1].amount, 12);
    CHECK_EQ(victim->damage_slots[1].attacker, 777);
    thing_add_pending_damage(&probe, victim, 1, 9);
    CHECK_EQ(victim->damage_slots[1].amount, 9);
    std::memset(victim->damage_slots, 0, sizeof victim->damage_slots);

    // cell_kill_things: creatures of other owners die, scenery is deleted
    unsigned cell = (((unsigned)(victim->y >> 8)) << 8) | (victim->x >> 8);
    int32_t health = victim->health;
    cell_kill_things(cell, victim->owner);
    CHECK_EQ(victim->health, health);
    if (victim->type != 0x10 && victim->type != 6 && victim->type != 8) {
        cell_kill_things(cell, 777);
        CHECK_EQ(victim->health, -1);
        CHECK_EQ(victim->killer, 777);
        victim->health = health;
        victim->killer = victim->last_attacker = 0;
    }
}

static void test_terrain_checks() {
    int ok = 0, n = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t) || t->cls != 5) continue;
        n++;
        // where a creature stands, its own terrain mask is satisfied
        if (creature_check_terrain(t, thing_pos(t), 1) == 0) ok++;
        // far above every clearance window
        Pos high = *thing_pos(t);
        high.z = 0x7000;
        CHECK(creature_check_terrain(t, &high, 2) != 0);
    }
    std::printf("creatures standing on allowed terrain: %d / %d\n", ok, n);
    CHECK(n > 0 && ok * 10 >= n * 9);
}

static void test_castle_sites() {
    // a built-on cell (wizard castles of level 38) blocks the 8x8 block down-right of it
    int built = -1;
    for (int c = 0; c < MC_MAP_CELLS && built < 0; c++)
        if (g_map_flags[c] & 0x80) built = c;
    CHECK(built >= 0);
    if (built >= 0) {
        Pos p{};
        p.x = (uint16_t)((((built & 0xff) + 3) & 0xff) << 8);
        p.y = (uint16_t)((((built >> 8) + 3) & 0xff) << 8);
        CHECK_EQ(castle_site_clear_at_pos(&p), 0);
    }
    int clear = 0;
    for (int y = 8; y < 256; y += 16)
        for (int x = 8; x < 256; x += 16) {
            Pos p{};
            p.x = (uint16_t)(x << 8); p.y = (uint16_t)(y << 8);
            clear += castle_site_clear_at_pos(&p);
        }
    std::printf("castle sites clear on a 16x16 grid: %d / 256\n", clear);
    CHECK(clear > 0 && clear < 256);
    // every player castle of the snapshot can be asked whether it may grow; the call must leave its
    // extents untouched
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t) || t->cls != 3 || t->type != 2) continue;
        int16_t ex = t->ext_x, ey = t->ext_y;
        int r = castle_footprint_clear(t);
        CHECK(r == 0 || r == 1);
        CHECK_EQ(t->ext_x, ex);
        CHECK_EQ(t->ext_y, ey);
        std::printf("castle %d (owner %d, level %d): may grow = %d\n", i, t->owner, t->aux, r);
    }
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }

    if (!sim_load_level(38)) { std::printf("level 38 failed to load\n"); return 2; }
    test_searches("level 38");
    test_damage();
    test_terrain_checks();
    test_castle_sites();

    if (!sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat")) { std::printf("snapshot failed to load\n"); return 2; }
    test_searches("snapshot");
    test_damage();
    test_castle_sites();

    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
