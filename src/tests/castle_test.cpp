// Unit test for castle.cpp (castles, balloons: class 3 states 4..10) and scenery.cpp (class 2 scenery,
// class 11 switches). argv[1] = game dir.
//
//  1. the engine's own snapshot (movie/gam00000.dat, 413 ticks into level 38): every castle / balloon /
//     spilled mana ball / tree in it must be producible by the translated code, and the things that
//     are at rest there must be fixed points of the handlers;
//  2. a freshly generated level 38 run for the same 412 updates: scenery and switches must then equal
//     the snapshot byte for byte, the castle that still stands on its site field by field;
//  3. by construction: damage, mana spill, balloon ferry, upgrade, quake timer, destruction, trees,
//     dolmen, every switch kind;
//  4. smoke runs (snapshot + the recorded movie, a few other levels) with statistics.
//
// Two handlers of other tasks are needed to drive a castle through its states and are replaced by
// test-local stand-ins bound to the original addresses (only in this executable):
//   0x26f10 effect_castle_raise_terrain_s44 / 0x26b50 effect_type41_s43_update (task C): the stand-in
//     stamps the footprints at once, writes build step 2 into the castle and deletes the effect;
//   0x25980 effect_mana_ball_update (task C): the stand-in only moves a ball that a balloon has
//     called (flag 0x40 + target) onto that balloon.
#include "sim.h"
#include "castle.h"
#include "scenery.h"
#include "player.h"
#include "demo.h"
#include "level_features.h"
#include "mc_math.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { \
    std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); g_fail++; } } while (0)

static bool live(const Thing *t) { return t->cls != 0; }
static bool is_castle(const Thing *t) { return t->cls == 3 && t->type == 2; }
static bool is_balloon(const Thing *t) { return t->cls == 3 && t->type == 3; }
static bool is_ball(const Thing *t) { return t->cls == 10 && t->type == 0x27; }

// ---- stand-ins for handlers of other tasks ---------------------------------------------------------

static bool g_standins_on = false;
static int g_standin_builds = 0;

static void standin_build_effect(Thing *e) {
    if (!g_standins_on) return;
    Thing *c = thing_at(e->caster % MC_THING_SLOTS);
    if (e->type == 0x2a) {
        // what player_spawn_3f360 does for a castle that exists at level start: one stamp per size
        for (int lvl = 0; lvl <= (int)e->castle_size; lvl++) {
            Thing *s0 = thing_at(0);
            s0->x = e->x; s0->y = e->y; s0->z = e->z;
            s0->type = 0;
            s0->aux = 0;
            s0->owner = e->owner;
            s0->castle_size = (uint8_t)lvl;
            castle_stamp_footprint(s0);
        }
    }
    c->cast_ticks = 2;
    g_standin_builds++;
    thing_mark_delete(e);
}

static void standin_mana_ball(Thing *b) {
    if (!g_standins_on) return;
    if (!(b->flags & 0x40) || b->target == 0) return;
    Thing *tgt = thing_at(b->target % MC_THING_SLOTS);
    if (!is_balloon(tgt)) return;
    Pos p = *thing_pos(tgt);
    thing_move_to(b, &p);
}

static int g_drop_calls = 0;
static void count_drop_mana_ball(Thing *) { g_drop_calls++; }

// ---- helpers ---------------------------------------------------------------------------------------

// The list pass of thing_update_all_3dce0 for the two lists this subsystem walks.
static void rebuild_lists() {
    Thing *pt = nullptr, *mt = nullptr;
    g_cfg->player_list = 0;
    g_cfg->mana_ball_list = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (t->cls == 3 && t->health >= 0 && !(t->flags & 0x10)) {
            if (!pt) g_cfg->player_list = (uint32_t)i; else pt->next = (uint32_t)i;
            pt = t; t->next = 0;
        } else if (t->cls == 10 && (t->type == 0x27 || t->type == 0x28)) {
            if (!mt) g_cfg->mana_ball_list = (uint32_t)i; else mt->next = (uint32_t)i;
            mt = t; t->next = 0;
        }
    }
}

// Every linked thing is found in the list of its cell, the lists are loop-free.
static int check_cell_lists(const char *what) {
    int bad = 0, linked = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t) || !(t->flags & 4)) continue;
        linked++;
        unsigned idx = g_cell_things[mc_cell_of(t->x, t->y)];
        bool found = false;
        for (int n = 0; idx != 0 && n < MC_THING_SLOTS; n++) {
            if (idx >= (unsigned)MC_THING_SLOTS) break;
            if ((int)idx == i) { found = true; break; }
            idx = thing_at(idx)->cell_next;
        }
        if (!found) bad++;
    }
    if (bad) { std::printf("FAIL %s: %d of %d linked things are not in their cell list\n", what, bad, linked); g_fail++; }
    return linked;
}

static int count_things(bool (*pred)(const Thing *)) {
    int n = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) if (live(thing_at(i)) && pred(thing_at(i))) n++;
    return n;
}

static Thing *player_thing(int p) { return thing_at(g_state->players[p].thing); }
static PlayerBlock *pblock(int p) { return &g_state->players[p].blk; }
static Thing *castle_of(int p) { return pblock(p)->castle ? thing_at(pblock(p)->castle) : nullptr; }

static const int32_t k_level_health[8] = {0, 20000, 40000, 40000, 60000, 60000, 80000, 80000};
static const int32_t k_level_mana[8]   = {5000, 10000, 20000, 40000, 80000, 160000, 320000, 30000000};

// Bytes of a Thing that depend on what else shares the cell / the per-tick lists.
static Thing masked(const Thing &t) {
    Thing m = t;
    m.next = 0;
    m.cell_next = m.cell_prev = 0;
    return m;
}

// ---- 1. the snapshot -------------------------------------------------------------------------------

static std::vector<Thing> g_snap_things;
static PlayerBlock g_snap_blocks[8];

static void test_tables() {
    static const int balloons[9] = {0, 1, 1, 1, 2, 2, 3, 3, 0}, guards[9] = {0, 0, 0, 4, 6, 14, 18, 34, 0};
    for (int l = 0; l < 9; l++) {
        CHECK_EQ(castle_level_balloons((unsigned)l), balloons[l]);
        CHECK_EQ(castle_level_guards((unsigned)l), guards[l]);
    }
    CHECK_EQ(castle_level_balloons(0xffff), 0);
}

static void test_snapshot_static() {
    int castles = 0, balloons = 0, trees = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t)) continue;
        if (is_castle(t)) {
            castles++;
            Thing *owner = thing_at(t->owner);
            PlayerBlock *P = player_block(owner);
            int lvl = t->aux;
            std::printf("snapshot castle %d: owner %d (player %d) level %d state %d health %d / %d mana %d / %d sprite 0x%x z %d ext %d x %d\n",
                        i, t->owner, P->player_no, lvl, t->state, t->health, t->max_health, t->mana, t->mana_total, t->sprite, t->z, t->ext_x, t->ext_h);
            CHECK(lvl >= 1 && lvl <= 7);
            CHECK_EQ(t->state, 4);                           // castle_build_update step 2
            CHECK_EQ(t->cast_ticks, 0);
            CHECK_EQ(t->duration, 0);
            CHECK_EQ(t->max_health, k_level_health[lvl & 7]);   // castle_set_level_stats (castle_begin_build_stage)
            CHECK_EQ(t->mana_total, k_level_mana[lvl & 7]);
            CHECK(t->flags & 2);                             // colour applied once ...
            CHECK_EQ(t->sprite, 0xb1 + P->player_no);        // ... sprite += player number
            CHECK_EQ(t->mana_owner, t->owner);               // castle_active_update, every tick
            CHECK_EQ(t->z, (int16_t)terrain_height_at(thing_pos(t)));
            CHECK_EQ(P->castle, i);                          // castle_begin_build_stage
            CHECK_EQ(P->castle_level, lvl);
            CHECK_EQ(t->player, 0);                          // the castle's own +0xa0 stays the dummy block
            Thing copy = *t;                                 // thing_set_castle_extents, every second tick
            copy.ext_z0 = copy.ext_x = copy.ext_y = copy.ext_h = 0;
            thing_set_castle_extents(&copy, lvl);
            CHECK(std::memcmp(&copy, t, sizeof copy) == 0);
            // balloons: exactly the level's allowance, guards: no more than it
            int nb = 0, ng = 0;
            for (int k = 0; k < 3; k++) {
                if (P->balloons[k] == 0) continue;
                nb++;
                Thing *b = thing_at(P->balloons[k]);
                CHECK(is_balloon(b));
                CHECK_EQ(b->state, 9);
                CHECK_EQ(b->owner, t->owner);
                CHECK_EQ(b->sprite, 0xa9 + P->player_no);
                CHECK_EQ(b->player, 0);
                // Thing.tick starts at index & 0xff and counts updates, so the ages (mod 256) tell at
                // which of its updates the castle launched the balloon: castle_active_update manages
                // balloons only when its tick is even.
                int age_c = (t->tick - (i & 0xff)) & 0xff, age_b = (b->tick - (P->balloons[k] & 0xff)) & 0xff;
                int launch = ((age_c - age_b) & 0xff) + (P->balloons[k] > i ? 1 : 0);
                int tick_then = (i + launch - 1) & 0xff;
                std::printf("   balloon %d launched at castle update %d (castle tick %d)\n", P->balloons[k], launch, tick_then);
                CHECK_EQ(tick_then & 1, 0);
            }
            for (int k = 0; k < 34; k++) if (P->guards[k]) ng++;
            CHECK_EQ(nb, castle_level_balloons((unsigned)lvl));
            CHECK(ng <= castle_level_guards((unsigned)lvl));
            // P+0x122 only ever grows by the balloon capacity per managed balloon
            CHECK_EQ(P->balloon_total % 10000, 0);
            std::printf("   balloons %d, guards %d, P.balloon_total %d (= %d castle updates with a balloon), P.balloon_mana %d\n",
                        nb, ng, P->balloon_total, P->balloon_total / 10000, P->balloon_mana);
        } else if (is_balloon(t)) {
            balloons++;
            Thing *tgt = thing_at(t->target);
            const MoveDesc *d = mc_move_desc(t->desc);
            int ground = (int16_t)terrain_height_at(thing_pos(t));
            std::printf("snapshot balloon %d: owner %d cargo %d target %d (class %d type 0x%x) height above ground %d (window %d..%d)",
                        i, t->owner, t->mana, t->target, tgt->cls, tgt->type, t->z - ground, d->clear_hi, d->clear_lo);
            CHECK(t->z - ground >= d->clear_hi);             // pos_follow_ground never leaves it below desc+0xc
            CHECK_EQ(t->speed_cur, 0x30);
            if (tgt->cls == 3) {                             // unloading: sits exactly on the castle
                CHECK_EQ(t->x, tgt->x);
                CHECK_EQ(t->y, tgt->y);
                CHECK_EQ(t->mana, 0);
                CHECK_EQ(t->mana_owner, t->owner);
                CHECK_EQ(t->z - ground, d->clear_hi);
                std::printf(" - at the castle\n");
            } else if (tgt->cls == 10) {                     // fetching: heading for an own ball
                CHECK(is_ball(tgt));
                CHECK_EQ(tgt->mana_owner, t->owner);
                int now = pos_angle_to(thing_pos(t), thing_pos(tgt)) & 0x7ff;
                int dist = pos_dist_xy(thing_pos(t), thing_pos(tgt));
                std::printf(" - %d from its ball, yaw 0x%x (direction now 0x%x)\n", dist, t->yaw, now);
                CHECK(angle_diff(t->yaw, now) < 0x40);
                // beyond 0x400 the ball is not called yet
                if (dist > 0x400 + 0x30) CHECK((tgt->flags & 0x40) == 0);
            } else {
                std::printf("\n");
            }
        } else if (t->cls == 2) {
            trees++;
            CHECK_EQ(t->state, 0);
            CHECK(t->flags & 0x20000);                       // set by scenery_tree_update on every tick
            CHECK_EQ(t->z, (int16_t)terrain_height_at(thing_pos(t)));
            CHECK(terrain_type_mask_at(thing_pos(t)) != 1);  // a tree on water would have been deleted
        }
    }
    CHECK_EQ(castles, 2);
    CHECK_EQ(balloons, 2);
    CHECK_EQ(trees, 150);

    // Mana balls owned by the two wizards whose first castles are gone: what castle_destroyed_update
    // -> castle_spill_mana throws out at level 0 (everything, in equal parts of excess / n).
    for (int p = 1; p < 4; p++) {
        int owner = g_state->players[p].thing, n = 0, part = 0, others = 0;
        int spd_lo = 0x7fff, spd_hi = 0;
        for (int i = 1; i < MC_THING_SLOTS; i++) {
            Thing *b = thing_at(i);
            if (!live(b) || !is_ball(b) || b->mana_owner != owner) continue;
            if (b->mana != 1000 && b->mana != 1009) { others++; continue; }
            n++;
            part = b->mana;
            if (b->speed_cur < spd_lo) spd_lo = b->speed_cur;
            if (b->speed_cur > spd_hi) spd_hi = b->speed_cur;
        }
        std::printf("snapshot: player %d (thing %d) owns %d spilled balls of %d mana (speed %d..%d) and %d other balls\n",
                    p, owner, n, part, n ? spd_lo : 0, spd_hi, others);
        if (n) {
            CHECK(spd_lo >= 0x10 && spd_hi <= 0x3f);         // rng % 0x30 + 0x10
            CHECK(n <= 0x20);
            CHECK_EQ(n, part * n / 1000);                    // n = excess / 1000, part = excess / n
        }
        // player 2's first castle held 8072..8079 mana when it fell, player 3's the 5000 it started with
        if (p == 2) { CHECK_EQ(n, 8); CHECK_EQ(part, 1009); }
        if (p == 3) { CHECK_EQ(n, 5); CHECK_EQ(part, 1000); }
    }
}

// Things at rest in the snapshot must be fixed points of two updates (one odd, one even castle tick).
static void test_snapshot_fixed_point() {
    Thing c485 = *thing_at(485), b521 = *thing_at(521), c895 = *thing_at(895), b856 = *thing_at(856);
    PlayerBlock p1 = *pblock(1), p2 = *pblock(2);
    CHECK(is_castle(&c485) && is_balloon(&b521) && is_castle(&c895) && is_balloon(&b856));
    int ball = b856.target;
    int d0 = pos_dist_xy(thing_pos(&b856), thing_pos(thing_at(ball)));
    std::vector<Thing> before(g_state->things, g_state->things + MC_THING_SLOTS);
    int things_before = count_things([](const Thing *) { return true; });

    thing_update_all();
    thing_update_all();

    // castle 485 (level 2, 13704 / 20000 mana, balloon home): nothing but the tick moves
    Thing a = masked(*thing_at(485)), b = masked(c485);
    CHECK_EQ(a.tick, (uint8_t)(b.tick + 2));
    b.tick = a.tick;
    CHECK(std::memcmp(&a, &b, sizeof a) == 0);
    // its balloon hovers on the castle and keeps the castle as its target
    a = masked(*thing_at(521)); b = masked(b521);
    b.tick = a.tick;
    CHECK(std::memcmp(&a, &b, sizeof a) == 0);
    // P+0x122 grows by the balloon capacity on the even castle tick, P+0x126 is the cargo
    CHECK_EQ(pblock(1)->balloon_total, p1.balloon_total + 10000);
    CHECK_EQ(pblock(1)->balloon_mana, 0);
    CHECK_EQ(pblock(1)->castle, 485);
    CHECK_EQ(pblock(1)->balloons[0], 521);
    // castle 895 (level 1, damaged, empty)
    a = masked(*thing_at(895)); b = masked(c895);
    b.tick = a.tick;
    CHECK(std::memcmp(&a, &b, sizeof a) == 0);
    CHECK_EQ(pblock(2)->balloon_total, p2.balloon_total + 10000);
    // its balloon flies two steps of 0x30 toward the ball it was after
    Thing *bl = thing_at(856);
    int d2 = pos_dist_xy(thing_pos(bl), thing_pos(thing_at(ball)));
    std::printf("snapshot + 2 updates: balloon 856 target %d -> %d, distance %d -> %d\n", ball, bl->target, d0, d2);
    CHECK_EQ(bl->target, ball);
    CHECK(d0 - d2 >= 0x5c && d0 - d2 <= 0x62);
    CHECK_EQ(bl->mana, b856.mana);
    CHECK_EQ(bl->health, b856.health);
    // scenery and switches: only the tick (and, every 8th tick, a switch's z snap)
    int same = 0, n = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        if (before[i].cls != 2 && before[i].cls != 11) continue;
        n++;
        a = masked(*thing_at(i)); b = masked(before[i]);
        b.tick = (uint8_t)(b.tick + 2);
        if (std::memcmp(&a, &b, sizeof a) == 0) same++;
    }
    std::printf("snapshot + 2 updates: %d of %d scenery / switch things unchanged apart from the tick\n", same, n);
    CHECK_EQ(same, n);
    CHECK_EQ(n, 155);
    CHECK_EQ(count_things([](const Thing *) { return true; }), things_before);
    check_cell_lists("snapshot + 2 updates");
}

// ---- 2. a fresh level 38 against the snapshot -------------------------------------------------------

static void test_level38_start() {
    game_tick_sim();    // tick 1: the players and the AI castles appear, the castles run step 0
    int effects = 0;
    for (int p = 0; p < 4; p++) {
        Thing *c = castle_of(p);
        int want = g_state->level.castle_level[p];
        if (want == 0) { CHECK(c == nullptr); continue; }
        CHECK(c != nullptr);
        if (!c) continue;
        // player_spawn left level - 1; castle_build_update step 0 -> castle_begin_build_stage
        CHECK_EQ(c->state, 5);
        CHECK_EQ(c->cast_ticks, 4);
        CHECK_EQ(c->aux, want);
        CHECK_EQ(c->max_health, k_level_health[want]);
        CHECK_EQ(c->health, k_level_health[want]);
        CHECK_EQ(c->mana_total, k_level_mana[want]);
        CHECK_EQ(c->mana, k_level_mana[want - 1]);           // filled to the old capacity by player_spawn
        CHECK(c->flags & 2);
        CHECK(!(c->flags & 0x40));
        CHECK_EQ(c->sprite, 0xb1 + p);
        CHECK_EQ(pblock(p)->castle, thing_index(c));
        CHECK_EQ(pblock(p)->castle_level, want);
        CHECK_EQ(c->owner, g_state->players[p].thing);
        // the terrain raiser castle_begin_build_stage created
        Thing *e = nullptr;
        for (int i = 1; i < MC_THING_SLOTS; i++) {
            Thing *t = thing_at(i);
            if (live(t) && t->cls == 10 && t->type == 0x2a && t->caster == thing_index(c)) { e = t; effects++; }
        }
        CHECK(e != nullptr);
        if (e) {
            CHECK_EQ(e->state, 0x2c);
            CHECK_EQ(e->castle_size, want);
            CHECK_EQ(e->owner, c->owner);
            CHECK_EQ(e->spell_flags, 0);
            CHECK(e->flags & 0x10000);
            CHECK(e->x == c->home.x && e->y == c->home.y && e->z == c->home.z);
        }
    }
    CHECK_EQ(effects, 3);
    CHECK_EQ(count_things(is_castle), 3);
    CHECK_EQ(count_things(is_balloon), 0);

    // Without the build effect's handler the castle waits in step 4 (and follows the ground).
    for (int i = 0; i < 5; i++) game_tick_sim();
    for (int p = 1; p < 4; p++) {
        Thing *c = castle_of(p);
        CHECK_EQ(c->state, 5);
        CHECK_EQ(c->cast_ticks, 4);
        CHECK_EQ(c->z, (int16_t)terrain_height_at(thing_pos(c)));
    }

    // With the stand-in the effect reports step 2; the castle goes to state 4 on its next update and
    // launches its balloon on the first even tick.
    g_standins_on = true;
    game_tick_sim();
    for (int p = 1; p < 4; p++) CHECK_EQ(castle_of(p)->cast_ticks, 2);
    game_tick_sim();
    for (int p = 1; p < 4; p++) {
        CHECK_EQ(castle_of(p)->state, 4);
        CHECK_EQ(castle_of(p)->cast_ticks, 0);
    }
    game_tick_sim();
    game_tick_sim();
    CHECK_EQ(count_things(is_balloon), 3);
    for (int p = 1; p < 4; p++) {
        Thing *c = castle_of(p);
        PlayerBlock *P = pblock(p);
        CHECK(P->balloons[0] != 0 && P->balloons[1] == 0 && P->balloons[2] == 0);
        Thing *b = thing_at(P->balloons[0]);
        CHECK(is_balloon(b));
        CHECK_EQ(b->state, 9);
        CHECK_EQ(b->owner, c->owner);
        CHECK_EQ(b->mana_owner, c->owner);
        CHECK_EQ(b->sprite, 0xa9 + p);
        CHECK_EQ(c->mana_owner, c->owner);
        for (int g = 0; g < 34; g++) CHECK_EQ(P->guards[g], 0);
    }
    CHECK_EQ(g_standin_builds, 3);
}

// The fresh level after the same 412 thing updates as the snapshot (no input, AI wizards and
// creatures not linked): scenery and switches byte for byte, castle 485 field by field.
static void test_level38_against_snapshot() {
    while (g_state->players[0].tick < 412) game_tick_sim();
    CHECK_EQ(g_state->players[0].tick, 412);
    int n = 0, same = 0, same_but_flag = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        const Thing &s = g_snap_things[i];
        if (s.cls != 2 && s.cls != 11) continue;
        n++;
        Thing a = masked(*thing_at(i)), b = masked(s);
        if (std::memcmp(&a, &b, sizeof a) == 0) { same++; continue; }
        // the recording player had used the "all spells" cheat, which clears flag 1 of every switch
        if (s.cls == 11) { a.flags &= ~1u; b.flags &= ~1u; }
        if (std::memcmp(&a, &b, sizeof a) == 0) { same_but_flag++; continue; }
        const uint8_t *pa = reinterpret_cast<const uint8_t *>(&a), *pb = reinterpret_cast<const uint8_t *>(&b);
        std::printf("FAIL thing %d (class %d) differs from the snapshot at:", i, s.cls);
        for (size_t k = 0; k < sizeof a; k++) if (pa[k] != pb[k]) std::printf(" +0x%zx (%02x vs %02x)", k, pa[k], pb[k]);
        std::printf("\n");
        g_fail++;
    }
    std::printf("level 38 + 412 updates vs snapshot: %d scenery / switch things, %d identical, %d identical apart from switch flag 1\n",
                n, same, same_but_flag);
    CHECK_EQ(n, 155);
    CHECK_EQ(same, 150);
    CHECK_EQ(same_but_flag, 5);

    // Castle 485 of player 1 still stands on its site in the snapshot (one level higher: the AI
    // upgraded it). Everything that does not depend on the level, the AI or the raised terrain matches.
    const Thing &s = g_snap_things[485];
    Thing *c = thing_at(485);
    CHECK(is_castle(c) && is_castle(&s));
    CHECK_EQ(c->tick, s.tick);
    CHECK_EQ(c->state, s.state);
    CHECK_EQ(c->flags, s.flags);
    CHECK_EQ(c->prop_flags, s.prop_flags);
    CHECK_EQ(c->sprite, s.sprite);
    CHECK_EQ(c->owner, s.owner);
    CHECK_EQ(c->mana_owner, s.mana_owner);
    CHECK_EQ(c->cast_ticks, s.cast_ticks);
    CHECK_EQ(c->duration, s.duration);
    CHECK_EQ(c->z_vel, s.z_vel);
    // The castle's RNG is stepped only by castle_spill_mana, twice per ball: the snapshot value must be
    // an even number of LCG steps away from the untouched one (the AI had overfilled the castle once).
    {
        uint32_t r = c->rng;
        int steps = 0;
        while (r != s.rng && steps < 64) { r = mc_lcg(r); steps++; }
        std::printf("level 38 + 412 updates: castle 485 RNG is %d LCG steps behind the snapshot (= %d spilled ball(s) there)\n", steps, steps / 2);
        CHECK(r == s.rng && steps % 2 == 0);
    }
    CHECK(std::memcmp(&c->home, &s.home, sizeof(Pos)) == 0);
    CHECK(c->x == s.x && c->y == s.y);
    CHECK(c->ext_z0 == s.ext_z0 && c->ext_h == s.ext_h);
    CHECK_EQ(c->ext_x * 2, s.ext_x);                         // level 1 here, level 2 there
    CHECK_EQ(pblock(1)->castle, g_snap_blocks[1].castle);
    CHECK_EQ(pblock(1)->balloons[0] != 0, g_snap_blocks[1].balloons[0] != 0);
    std::printf("level 38 + 412 updates: P1.balloon_total %d (snapshot %d: there the castle was also busy with the upgrade to level 2)\n",
                pblock(1)->balloon_total, g_snap_blocks[1].balloon_total);
    CHECK(pblock(1)->balloon_total >= g_snap_blocks[1].balloon_total);
    CHECK(pblock(1)->balloon_total - g_snap_blocks[1].balloon_total <= 30 * 10000);
    check_cell_lists("level 38 + 412 updates");
}

// ---- 3. by construction ----------------------------------------------------------------------------

static void test_take_damage() {
    Thing *c = castle_of(1);
    PlayerBlock *P = pblock(1);
    Thing saved = *c;
    // nothing pending
    CHECK_EQ(castle_take_damage(c), 0);
    // a hit: health down, slot cleared, P+0x187 = 4
    P->castle_hit_flash = 0;
    c->damage_slots[0].amount = 1234;
    c->damage_slots[0].attacker = 479;
    CHECK_EQ(castle_take_damage(c), 1);
    CHECK_EQ(c->health, saved.health - 1234);
    CHECK_EQ(c->damage_slots[0].amount, 0);
    CHECK_EQ(c->damage_slots[0].attacker, 0);
    CHECK_EQ(P->castle_hit_flash, 4);
    CHECK_EQ(c->killer, saved.killer);
    // the killing hit: killer recorded, attacker cleared, amount kept, no flash
    P->castle_hit_flash = 0;
    c->damage_slots[0].amount = saved.health;
    c->damage_slots[0].attacker = 479;
    CHECK_EQ(castle_take_damage(c), 2);
    CHECK(c->health < 0);
    CHECK_EQ(c->killer, 479);
    CHECK_EQ(c->damage_slots[0].attacker, 0);
    CHECK_EQ(c->damage_slots[0].amount, saved.health);
    CHECK_EQ(P->castle_hit_flash, 0);
    // dead: 2 without looking at the slots
    c->damage_slots[5].attacker = c->owner;
    CHECK_EQ(castle_take_damage(c), 2);
    CHECK_EQ(c->damage_slots[5].attacker, c->owner);
    *c = saved;
    // upgrade request (slot 5 attacker == owner): flag 0x40 below level 7, the request is consumed
    c->damage_slots[5].attacker = c->owner;
    c->damage_slots[5].amount = 10;
    CHECK_EQ(castle_take_damage(c), 0);
    CHECK(c->flags & 0x40);
    CHECK_EQ(c->damage_slots[5].attacker, 0);
    *c = saved;
    c->aux = 7;
    c->damage_slots[5].attacker = c->owner;
    CHECK_EQ(castle_take_damage(c), 0);
    CHECK(!(c->flags & 0x40));
    CHECK_EQ(c->damage_slots[5].attacker, 0);
    *c = saved;
    // somebody else's index in slot 5 is left alone
    c->damage_slots[5].attacker = (uint16_t)(c->owner + 1);
    CHECK_EQ(castle_take_damage(c), 0);
    CHECK(!(c->flags & 0x40));
    CHECK_EQ(c->damage_slots[5].attacker, c->owner + 1);
    *c = saved;
}

static void delete_balls_of(int owner) {
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *b = thing_at(i);
        if (live(b) && is_ball(b) && b->mana_owner == owner && !(b->flags & 0x400)) thing_free(b);
    }
}

static void test_spill() {
    Thing *c = castle_of(1);
    Thing saved = *c;
    const uint32_t rng_at_entry = c->rng;
    int owner = c->owner;
    struct Case { int32_t excess; int balls; int32_t part; int32_t rest; };
    static const Case cases[] = {
        {7500, 7, 1071, 3},        // n = excess / 1000, part = excess / n
        {999, 1, 999, 0},          // at least one ball
        {100000, 32, 3125, 0},     // at most 0x20
        {1000 * 8 + 79, 8, 1009, 7},   // the 8 x 1009 of the snapshot (player 2's first castle)
    };
    for (const Case &k : cases) {
        std::vector<Thing> before(g_state->things, g_state->things + MC_THING_SLOTS);
        c->mana = c->mana_total + k.excess;
        uint32_t rng = c->rng;
        castle_spill_mana(c);
        int n = 0;
        int32_t sum = 0;
        // the balls come out in allocation order; replay the castle's RNG to predict where they land
        std::vector<int> made;
        for (int i = 1; i < MC_THING_SLOTS; i++) if (!before[i].cls && live(thing_at(i))) made.push_back(i);
        CHECK_EQ((int)made.size(), k.balls);
        // thing_alloc pops the free stack: recover the creation order from the positions predicted below
        std::vector<bool> used(made.size(), false);
        for (int j = 0; j < k.balls; j++) {
            rng = mc_lcg(rng);
            int dist = (int16_t)(rng % 0x1400u + 0xf00);
            rng = mc_lcg(rng);
            Pos want = *thing_pos(c);
            math_rotate_offset(&want, (int)(rng & 0x7ff), 0, dist);
            CHECK(dist >= 0xf00 && dist < 0x2300);
            bool found = false;
            for (size_t m = 0; m < made.size() && !found; m++) {
                Thing *b = thing_at(made[m]);
                if (used[m] || b->x != want.x || b->y != want.y) continue;
                used[m] = found = true;
                n++;
                sum += b->mana;
                CHECK(is_ball(b));
                CHECK_EQ(b->mana, k.part);
                CHECK_EQ(b->mana_owner, owner);
                CHECK(b->home.x == 0 && b->home.y == 0);
                CHECK(b->speed_cur >= 0x10 && b->speed_cur <= 0x3f);
                CHECK_EQ(b->speed_cur, (int)(b->rng % 0x30u) + 0x10);       // one draw of the ball's own RNG
                int above = c->z - (int16_t)terrain_height_at(thing_pos(c));
                CHECK_EQ(b->z_vel, (0x400 - above) / 8);
            }
            CHECK(found);
        }
        CHECK_EQ(c->rng, rng);                               // two draws per ball, nothing else
        CHECK_EQ(n, k.balls);
        CHECK_EQ(sum, k.excess - k.rest);
        CHECK_EQ(c->mana, c->mana_total + k.rest);
        delete_balls_of(owner);
        Thing keep = *c;
        *c = saved;
        c->rng = keep.rng;
        saved.rng = keep.rng;
    }
    // at or below the capacity nothing happens; mana in transit (P+0x134) counts for the test only
    {
        int things = count_things([](const Thing *) { return true; });
        uint32_t rng = c->rng;
        c->mana = c->mana_total;
        castle_spill_mana(c);
        pblock(1)->mana_in_transit = 500;
        c->mana = c->mana_total - 200;                       // 9800 + 500 > 10000, but 9800 - 10000 < 0
        castle_spill_mana(c);
        CHECK_EQ(count_things([](const Thing *) { return true; }), things);
        CHECK_EQ(c->rng, rng);
        CHECK_EQ(c->mana, c->mana_total - 200);
        pblock(1)->mana_in_transit = 0;
    }
    // level 0: everything goes
    {
        c->aux = 0;
        c->mana = 3000;
        int before = count_things(is_ball);
        castle_spill_mana(c);
        CHECK_EQ(count_things(is_ball), before + 3);
        CHECK_EQ(c->mana, 0);
        delete_balls_of(owner);
    }
    *c = saved;
    c->rng = rng_at_entry;      // the later comparison with the snapshot castle wants the untouched RNG
}

static Thing *make_ball(int owner, uint16_t x, uint16_t y, int mana) {
    Pos p{x, y, 0};
    p.z = (int16_t)terrain_height_at(&p);
    Thing *b = thing_create(&p, 10, 0x27);
    if (!b) return nullptr;
    b->mana = mana;
    b->mana_owner = (uint16_t)owner;
    return b;
}

static void test_find_ball() {
    Thing *c = castle_of(1);
    int owner = c->owner;
    Thing *b_near = make_ball(owner, (uint16_t)(c->x + 0x300), c->y, 100);
    Thing *b_mid  = make_ball(owner, (uint16_t)(c->x + 0x600), c->y, 100);
    Thing *b_far  = make_ball(owner, (uint16_t)(c->x + 0x900), c->y, 100);
    Thing *alien = make_ball(owner + 1, (uint16_t)(c->x + 0x100), c->y, 100);
    CHECK(b_near && b_mid && b_far && alien);
    rebuild_lists();
    Thing *s0 = thing_at(0);
    CHECK(castle_find_free_mana_ball(c, s0, s0) == b_near);
    CHECK(castle_find_free_mana_ball(c, b_near, s0) == b_mid);
    CHECK(castle_find_free_mana_ball(c, s0, b_near) == b_mid);
    CHECK(castle_find_free_mana_ball(c, b_mid, b_near) == b_far);
    Thing probe = *c;                                        // measured from the first argument
    probe.x = b_far->x;
    CHECK(castle_find_free_mana_ball(&probe, s0, s0) == b_far);
    thing_free(b_near); thing_free(b_mid); thing_free(b_far);
    rebuild_lists();
    CHECK(castle_find_free_mana_ball(c, s0, s0) == nullptr);
    thing_free(alien);
    rebuild_lists();
}

// A ball 0x1400 from the castle: the balloon is sent out, calls the ball at 0x400, swallows it, is sent
// home and unloads. The mana of castle + balloon + ball never changes.
static void test_balloon_ferry() {
    Thing *c = castle_of(1);
    PlayerBlock *P = pblock(1);
    Thing *bl = thing_at(P->balloons[0]);
    int owner = c->owner;
    const int32_t castle0 = c->mana;
    CHECK_EQ(bl->mana, 0);
    CHECK_EQ(bl->target, thing_index(c));
    // outside the castle's own box (the castle takes in own balls that touch it)
    Thing *ball = make_ball(owner, (uint16_t)(c->x + 0x1400), c->y, 700);
    CHECK(ball != nullptr);
    if (!ball) return;
    CHECK(!thing_collide(c, ball));
    int ball_idx = thing_index(ball);
    int t_sent = -1, t_called = -1, t_got = -1, t_home = -1, last_dist = 0x7fffffff;
    for (int tick = 0; tick < 400 && t_home < 0; tick++) {
        thing_update_all();
        int32_t ball_mana = (live(ball) && is_ball(ball) && !(ball->flags & 0x400) && t_got < 0) ? ball->mana : 0;
        if (c->mana + bl->mana + ball_mana != castle0 + 700) { CHECK_EQ(c->mana + bl->mana + ball_mana, castle0 + 700); break; }
        if (t_sent < 0 && bl->target == ball_idx) t_sent = tick;
        if (t_sent >= 0 && t_got < 0) {
            int d = pos_dist_xy(thing_pos(bl), thing_pos(ball));
            CHECK(d <= last_dist);                               // never flies away from it
            last_dist = d;
            if (t_called < 0 && (ball->flags & 0x40)) {
                t_called = tick;
                CHECK(d <= 0x400);
                CHECK_EQ(ball->target, thing_index(bl));
            }
        }
        if (t_got < 0 && bl->mana == 700) {
            t_got = tick;
            CHECK(ball->flags & 0x400);
            CHECK_EQ(bl->target, 0);
            CHECK_EQ(bl->health, bl->max_health);
        }
        if (t_got >= 0 && c->mana == castle0 + 700) {
            t_home = tick;
            CHECK_EQ(bl->mana, 0);
            CHECK_EQ(bl->x, c->x);
            CHECK_EQ(bl->y, c->y);
            CHECK_EQ(bl->mana_owner, owner);
        }
        // never below its clearance (desc+0xc); from above it sinks toward it by 4 / 16 per update
        int above = bl->z - (int16_t)terrain_height_at(thing_pos(bl));
        CHECK(above >= 0x200);
    }
    std::printf("balloon ferry: sent after %d updates, ball called at %d, swallowed at %d, unloaded at %d\n", t_sent, t_called, t_got, t_home);
    CHECK(t_sent >= 0 && t_sent <= 2);
    CHECK(t_called > t_sent);
    CHECK(t_got >= t_called);
    CHECK(t_home > t_got);
    // 0x1400 at 0x30 per update; the ball is called (and, with the stand-in, arrives) at 0x400
    CHECK(t_got >= 0x1000 / 0x30 - 2 && t_got <= 0x1400 / 0x30 + 4);
    CHECK(t_home - t_got >= 0x1000 / 0x30 - 4 && t_home - t_got <= 0x1400 / 0x30 + 4);
    CHECK_EQ(c->mana, castle0 + 700);
    c->mana = castle0;
    check_cell_lists("balloon ferry");
}

// The owner casts Castle on his castle (effect 27d20 writes slot 5): level 1 -> 2 with the values the
// snapshot's level-2 castle has.
static void test_upgrade() {
    Thing *c = castle_of(1);
    PlayerBlock *P = pblock(1);
    const Thing &s = g_snap_things[485];
    CHECK_EQ(c->aux, 1);
    {
        // a built-on cell (flag 0x80) in the corner of the grown box fails the site test
        Thing grown = *c;
        thing_set_castle_extents(&grown, 2);
        unsigned corner = mc_cell((unsigned)((((int16_t)c->x + 0x80) >> 8) - (grown.ext_x >> 8)),
                                  (unsigned)((((int16_t)c->y + 0x80) >> 8) - (grown.ext_y >> 8)));
        uint8_t old_flags = g_map_flags[corner];
        Thing before = *c;
        CHECK_EQ(castle_footprint_clear(c), 1);
        g_map_flags[corner] |= 0x80;
        CHECK_EQ(castle_footprint_clear(c), 0);
        g_map_flags[corner] = old_flags;
        CHECK(std::memcmp(&before, c, sizeof before) == 0);
    }
    c->damage_slots[5].attacker = c->owner;
    c->damage_slots[5].amount = 10;
    g_standins_on = false;
    thing_update_all();                                      // state 4: request seen
    CHECK_EQ(c->state, 5);
    CHECK_EQ(c->cast_ticks, 0);
    CHECK(c->flags & 0x40);
    int clear = castle_footprint_clear(c);
    std::printf("upgrade: castle_footprint_clear = %d\n", clear);
    CHECK_EQ(clear, 1);
    uint16_t sprite = c->sprite;
    thing_update_all();                                      // state 5 step 0: grow
    CHECK_EQ(c->state, 5);
    CHECK_EQ(c->cast_ticks, 4);
    CHECK_EQ(c->aux, 2);
    CHECK(!(c->flags & 0x40));
    CHECK_EQ(c->sprite, sprite);                             // the colour is added only once (flag 2)
    CHECK_EQ(c->max_health, s.max_health);
    CHECK_EQ(c->health, s.health);
    CHECK_EQ(c->mana_total, s.mana_total);
    CHECK(c->ext_z0 == s.ext_z0 && c->ext_x == s.ext_x && c->ext_y == s.ext_y && c->ext_h == s.ext_h);
    CHECK_EQ(P->castle_level, 2);
    g_standins_on = true;
    thing_update_all();                                      // the effect reports step 2
    thing_update_all();
    CHECK_EQ(c->state, 4);
    CHECK_EQ(c->cast_ticks, 0);
    thing_update_all();
    thing_update_all();
    CHECK_EQ(count_things(is_balloon), 3);                   // level 2 still has one balloon
    // now equal to the snapshot castle in every byte but the stored mana, the tick and the RNG (see
    // test_level38_against_snapshot) - including z (the terrain the footprints raise) and the 10 the
    // castle spell's effect leaves in damage slot 5
    Thing a = masked(*c), b = masked(s);
    a.tick = b.tick; a.mana = b.mana; a.rng = b.rng;
    CHECK_EQ(c->z, s.z);
    CHECK_EQ(s.damage_slots[5].amount, 10);
    CHECK_EQ(s.damage_slots[5].attacker, 0);
    const uint8_t *pa = reinterpret_cast<const uint8_t *>(&a), *pb = reinterpret_cast<const uint8_t *>(&b);
    int diff = 0;
    for (size_t k = 0; k < sizeof a; k++) if (pa[k] != pb[k]) { std::printf("   castle 485 vs snapshot: +0x%zx %02x vs %02x\n", k, pa[k], pb[k]); diff++; }
    std::printf("upgrade: castle 485 at level 2 differs from the snapshot castle in %d bytes (mana, tick, rng excluded); z %d vs %d\n", diff, c->z, s.z);
    CHECK_EQ(diff, 0);
    // level 3 has the footprint size of level 2 (no border strips to test): free to grow again
    Thing grown = *c;
    thing_set_castle_extents(&grown, 3);
    CHECK(grown.ext_x == c->ext_x && grown.ext_y == c->ext_y);
    CHECK_EQ(castle_footprint_clear(c), 1);
    // a refused upgrade: castle 2 cannot grow because castle 3 stands inside the grown box -> step 2,
    // request dropped, level unchanged, no effect
    Thing *c2 = castle_of(2);
    CHECK(c2 != nullptr && c2->aux == 1);
    CHECK_EQ(castle_footprint_clear(c2), 0);
    int effects = count_things([](const Thing *t) { return t->cls == 10 && t->type == 0x2a; });
    c2->damage_slots[5].attacker = c2->owner;
    thing_update_all();
    CHECK_EQ(c2->state, 5);
    CHECK(c2->flags & 0x40);
    thing_update_all();
    CHECK_EQ(c2->state, 5);
    CHECK_EQ(c2->cast_ticks, 2);
    CHECK(!(c2->flags & 0x40));
    CHECK_EQ(c2->aux, 1);
    CHECK_EQ(count_things([](const Thing *t) { return t->cls == 10 && t->type == 0x2a; }), effects);
    thing_update_all();
    CHECK_EQ(c2->state, 4);
    CHECK_EQ(c2->cast_ticks, 0);
    CHECK_EQ(c2->max_health, 20000);
}

// A quake hit (thing_area_damage_quake_11450) arms duration = 0x1e: the castle idles, re-arms the
// owner's Castle spell, and rebuilds its terrain (step 3 -> effect 0x2a through thing_create).
static void test_quake_timer() {
    Thing *c = castle_of(2);
    CHECK_EQ(c->state, 4);
    c->duration = 0x1e;
    c->damage_slots[0].amount = 500;                         // pending damage waits while the timer runs
    c->damage_slots[0].attacker = 479;
    int32_t health = c->health;
    g_standins_on = false;
    for (int k = 0x1d; k >= 1; k--) {
        thing_update_all();
        CHECK_EQ(c->duration, k);
        CHECK_EQ(c->state, 4);
    }
    CHECK_EQ(c->health, health);
    thing_update_all();
    CHECK_EQ(c->state, 5);
    CHECK_EQ(c->cast_ticks, 3);
    CHECK_EQ(c->duration, 0);
    int before = count_things([](const Thing *t) { return t->cls == 10 && t->type == 0x2a; });
    thing_update_all();
    CHECK_EQ(c->cast_ticks, 4);
    CHECK_EQ(count_things([](const Thing *t) { return t->cls == 10 && t->type == 0x2a; }), before + 1);
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *e = thing_at(i);
        if (!live(e) || e->cls != 10 || e->type != 0x2a || e->caster != thing_index(c)) continue;
        CHECK_EQ(e->castle_size, c->aux);
        CHECK_EQ(e->owner, c->owner);
        CHECK_EQ(e->spell_flags, 1);                         // the constructor's value: the slow variant
        CHECK(!(e->flags & 0x10000));
    }
    CHECK_EQ(c->aux, 1);                                     // no level change
    g_standins_on = true;
    thing_update_all();
    thing_update_all();
    CHECK_EQ(c->state, 4);
    thing_update_all();                                      // now the waiting hit lands
    CHECK_EQ(c->health, health - 500);
    CHECK_EQ(pblock(2)->castle_hit_flash, 4);
    // sequencer steps nobody writes in retail: 5 -> effect 0x29, step 6; 1 / 6 wait; > 6 nothing
    c->state = 5; c->cast_ticks = 5;
    g_standins_on = false;
    castle_build_update(c);
    CHECK_EQ(c->cast_ticks, 6);
    int e29 = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *e = thing_at(i);
        if (live(e) && e->cls == 10 && e->type == 0x29 && e->caster == thing_index(c)) { e29++; CHECK_EQ(e->state, 0x2b); thing_free(e); }
    }
    CHECK_EQ(e29, 1);
    c->cast_ticks = 7;
    Thing snap = *c;
    castle_build_update(c);
    CHECK(std::memcmp(&snap, c, sizeof snap) == 0);
    castle_build_seq_set_done(c);
    CHECK_EQ(c->cast_ticks, 2);
    castle_build_update(c);
    CHECK_EQ(c->state, 4);
    CHECK_EQ(c->cast_ticks, 0);
    g_standins_on = true;
}

// Destruction. Player 3's level-1 castle with its 5000 mana: the snapshot shows what the original
// left of it - no castle (P.castle 0, P.castle_level still 1), no balloon, 5 balls of 1000.
static void test_destroy() {
    Thing *c = castle_of(3);
    PlayerBlock *P = pblock(3);
    int idx = thing_index(c), owner = c->owner;
    CHECK_EQ(c->aux, 1);
    CHECK_EQ(c->mana, 5000);
    CHECK(P->balloons[0] != 0);
    int balloon = P->balloons[0];
    int balls0 = count_things(is_ball);
    g_drop_calls = 0;
    c->damage_slots[0].amount = c->health + 1;
    c->damage_slots[0].attacker = 479;
    thing_update_all();                                      // the hit: state 6
    CHECK_EQ(c->state, 6);
    CHECK_EQ(c->killer, 479);
    thing_update_all();                                      // collapse to level 0
    CHECK_EQ(P->castle, 0);
    CHECK_EQ(P->castle_level, 1);
    CHECK(c->flags & 0x400);
    CHECK_EQ(c->aux, 0);
    CHECK_EQ(c->mana, 0);
    CHECK_EQ(P->balloons[0], 0);
    CHECK(thing_at(balloon)->flags & 0x400);
    CHECK_EQ(g_drop_calls, 1);                               // thing_drop_mana_ball_25fe0 for the balloon
    int n = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *b = thing_at(i);
        if (!live(b) || !is_ball(b) || b->mana_owner != owner) continue;
        n++;
        CHECK_EQ(b->mana, 1000);
        CHECK(b->speed_cur >= 0x10 && b->speed_cur <= 0x3f);
    }
    CHECK_EQ(n, 5);
    CHECK_EQ(count_things(is_ball), balls0 + 5);
    thing_update_all();
    CHECK(!live(thing_at(idx)) || !is_castle(thing_at(idx)));
    CHECK(!live(thing_at(balloon)) || !is_balloon(thing_at(balloon)));
    CHECK_EQ(count_things(is_castle), 2);
    CHECK_EQ(count_things(is_balloon), 2);
    std::printf("destroy: level-1 castle of player 3 gone, %d balls of 1000 (snapshot: 5), P.castle %d, P.castle_level %d (snapshot: %d, %d)\n",
                n, P->castle, P->castle_level, g_snap_blocks[3].castle, g_snap_blocks[3].castle_level);
    CHECK_EQ(g_snap_blocks[3].castle, 0);
    CHECK_EQ(g_snap_blocks[3].castle_level, 1);

    // A level-2 castle (player 1, upgraded above) loses one level and stays.
    c = castle_of(1);
    P = pblock(1);
    CHECK_EQ(c->aux, 2);
    c->mana = 19500;                                         // above 90 % of the capacity: the tenth spills 1500
    c->health = -301;
    balls0 = count_things(is_ball);
    thing_update_all();
    CHECK_EQ(c->state, 6);
    thing_update_all();
    CHECK_EQ(c->state, 4);
    CHECK_EQ(c->aux, 1);
    CHECK_EQ(c->duration, 5);
    CHECK_EQ(c->cast_ticks, 0);
    CHECK_EQ(c->max_health, 20000);
    CHECK_EQ(c->health, 20000 - 301);                        // castle_apply_level_stats keeps the overkill (max half)
    CHECK_EQ(c->mana_total, 10000);
    CHECK_EQ(P->castle, thing_index(c));
    CHECK(!(c->flags & 0x400));
    // collapse: 19500 > 18000 -> 1 ball of 1500; then 18000 > 10000 -> 8 balls of 1000
    int b1500 = 0, b1000 = 0, other = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *b = thing_at(i);
        if (!live(b) || !is_ball(b) || b->mana_owner != c->owner) continue;
        if (b->mana == 1500) b1500++; else if (b->mana == 1000) b1000++; else other++;
    }
    CHECK_EQ(b1500, 1);
    CHECK_EQ(b1000, 8);
    CHECK_EQ(other, 0);
    CHECK_EQ(c->mana, 10000);
    Thing copy = *c;
    thing_set_castle_extents(&copy, 1);
    CHECK(std::memcmp(&copy, c, sizeof copy) == 0);
    // the rebuild timer runs out and the terrain is raised again
    for (int k = 4; k >= 1; k--) { thing_update_all(); CHECK_EQ(c->duration, k); }
    thing_update_all();
    CHECK_EQ(c->state, 5);
    CHECK_EQ(c->cast_ticks, 3);
    for (int k = 0; k < 3; k++) thing_update_all();
    CHECK_EQ(c->state, 4);
    delete_balls_of(c->owner);
    // an exhausted pool postpones the collapse
    c->health = -1;
    thing_update_all();
    CHECK_EQ(c->state, 6);
    int32_t top = g_state->free_top;
    g_state->free_top = -1;
    bool pool_empty = thing_free_count() == 0;
    if (pool_empty) {
        castle_destroyed_update(c);
        CHECK_EQ(c->state, 4);
        CHECK_EQ(c->aux, 1);
    } else {
        std::printf("destroy: (free_top = -1 does not empty the pool in the port; exhausted-pool path not exercised)\n");
    }
    g_state->free_top = top;
    c->health = c->max_health;
    c->state = 4;
    c->duration = 0;
    check_cell_lists("destroy");
}

static void test_guards() {
    // A level-3 castle wants 4 guards (creature type 0xf), one per 0x10 managed updates.
    Thing *c = castle_of(1);
    PlayerBlock *P = pblock(1);
    Thing saved = *c;
    c->aux = 3;
    c->mana = 0;
    c->z_vel = 0;
    int made = 0, calls = 0;
    int at[4] = {-1, -1, -1, -1};
    for (; calls < 80 && made < 4; calls++) {
        castle_manage_balloons_and_guards(c);
        int n = 0;
        for (int g = 0; g < 34; g++) if (P->guards[g]) n++;
        if (n > made) { at[made] = calls; made = n; }
    }
    std::printf("guards: level 3 -> %d guards after %d managed updates (created at calls %d, %d, %d, %d)\n", made, calls, at[0], at[1], at[2], at[3]);
    CHECK_EQ(made, 4);
    CHECK_EQ(at[0], 0);
    CHECK_EQ(at[1], 16);
    CHECK_EQ(at[2], 32);
    CHECK_EQ(at[3], 48);
    Pos want = *thing_pos(c);
    want.x = (uint16_t)(want.x + 0x80);
    want.y = (uint16_t)(want.y + 0x280);
    for (int g = 0; g < 4; g++) {
        Thing *gt = thing_at(P->guards[g]);
        CHECK(gt->cls == 5 && gt->type == 0xf);
        CHECK_EQ(gt->owner, c->owner);
        CHECK_EQ(gt->mana_owner, c->owner);
        CHECK_EQ(gt->yaw, 0x200);
        CHECK_EQ(gt->target_yaw, 0x200);
        CHECK(gt->x == want.x && gt->y == want.y);
        CHECK_EQ(gt->z, (int16_t)terrain_height_at(&want));
    }
    for (int g = 4; g < 34; g++) CHECK_EQ(P->guards[g], 0);
    // a guard that died (state 0x5f) frees its slot and restarts the cooldown
    Thing *g1 = thing_at(P->guards[1]);
    int g1_idx = P->guards[1];
    g1->state = 0x5f;
    c->z_vel = 0;
    castle_manage_balloons_and_guards(c);
    CHECK_EQ(P->guards[1], 0);
    CHECK_EQ(c->z_vel, 0x10);
    for (int k = 0; k < 16; k++) castle_manage_balloons_and_guards(c);
    CHECK(P->guards[1] != 0);
    // the level allows one balloon here too; a second and third slot would be released
    CHECK(P->balloons[0] != 0);
    thing_free(thing_at(g1_idx));
    for (int g = 0; g < 4; g++) { if (P->guards[g]) thing_free(thing_at(P->guards[g])); P->guards[g] = 0; }
    *c = saved;

    // three balloons at level 6, back to one at level 2: the surplus drops its cargo and goes
    c->aux = 6;
    for (int k = 0; k < 4; k++) castle_manage_balloons_and_guards(c);
    CHECK(P->balloons[0] && P->balloons[1] && P->balloons[2]);
    int b1 = P->balloons[1], b2 = P->balloons[2];
    for (int g = 0; g < 34; g++) { if (P->guards[g]) thing_free(thing_at(P->guards[g])); P->guards[g] = 0; }
    c->aux = saved.aux;
    g_drop_calls = 0;
    castle_manage_balloons_and_guards(c);
    CHECK_EQ(g_drop_calls, 2);
    CHECK(P->balloons[0] != 0 && P->balloons[1] == 0 && P->balloons[2] == 0);
    CHECK((thing_at(b1)->flags & 0x400) && (thing_at(b2)->flags & 0x400));
    thing_free(thing_at(b1));
    thing_free(thing_at(b2));
    *c = saved;
}

// A balloon takes damage through player_take_damage_42770 but does not handle its own death: the
// castle drops its cargo (thing_drop_mana_ball_25fe0), deletes it and launches a new one.
static void test_balloon_death() {
    Thing *c = castle_of(2);
    PlayerBlock *P = pblock(2);
    CHECK(c != nullptr && c->state == 4);
    int bi = P->balloons[0];
    Thing *b = thing_at(bi);
    CHECK(is_balloon(b) && b->state == 9);
    int32_t hp = b->health;
    b->damage_slots[0].amount = 2500;
    b->damage_slots[0].attacker = 479;
    thing_update_all();
    CHECK_EQ(b->health, hp - 2500);
    CHECK_EQ(b->damage_slots[0].attacker, 0);
    // parked at its castle the balloon is repaired on every update (the unload branch restores the
    // health before the damage is applied), so only a hit above max_health kills it there
    b->damage_slots[0].amount = 2500;
    b->damage_slots[0].attacker = 479;
    thing_update_all();
    CHECK_EQ(b->health, b->max_health - 2500);
    b->damage_slots[0].amount = b->max_health + 1;
    b->damage_slots[0].attacker = 479;
    thing_update_all();
    CHECK(b->health < 0);
    CHECK_EQ(b->killer, 479);
    g_drop_calls = 0;
    int t_gone = -1, t_new = -1;
    for (int k = 0; k < 8 && t_new < 0; k++) {
        thing_update_all();
        if (t_gone < 0 && P->balloons[0] == 0) { t_gone = k; CHECK_EQ(g_drop_calls, 1); }
        else if (t_gone >= 0 && P->balloons[0] != 0) t_new = k;
    }
    std::printf("balloon death: slot freed %d update(s) after the kill, new balloon %d update(s) later\n", t_gone + 1, t_new - t_gone);
    CHECK(t_gone >= 0 && t_gone <= 1);                       // the castle's next even tick
    CHECK_EQ(t_new, t_gone + 2);                             // and the one after
    Thing *nb = thing_at(P->balloons[0]);
    CHECK(is_balloon(nb) && nb->state == 9 && nb->health == nb->max_health);
    CHECK_EQ(nb->owner, c->owner);
    CHECK_EQ(count_things(is_balloon), 3);
    check_cell_lists("balloon death");
}

static void test_scenery() {
    // a tree takes damage: survives 100, burns at the next 1000
    Thing *tree = nullptr;
    for (int i = 1; i < MC_THING_SLOTS && !tree; i++)
        if (live(thing_at(i)) && thing_at(i)->cls == 2 && thing_at(i)->state == 0 && thing_at(i)->sprite == 0x54) tree = thing_at(i);
    CHECK(tree != nullptr);
    if (!tree) return;
    Thing *wiz = player_thing(1);
    CHECK_EQ(tree->health, 300);
    tree->damage_slots[0].amount = 100;
    tree->damage_slots[0].attacker = thing_index(wiz);
    scenery_tree_update(tree);
    CHECK_EQ(tree->health, 200);
    CHECK_EQ(tree->state, 0);
    CHECK_EQ(tree->damage_slots[0].attacker, 0);
    CHECK_EQ(tree->damage_slots[0].amount, 100);             // only the attacker is cleared
    int fires = count_things([](const Thing *t) { return t->cls == 10 && t->type == 6; });
    std::vector<uint8_t> was_live(MC_THING_SLOTS);
    for (int i = 1; i < MC_THING_SLOTS; i++) was_live[i] = thing_at(i)->cls;
    uint32_t rng = tree->rng;
    tree->damage_slots[0].amount = 1000;
    tree->damage_slots[0].attacker = (uint16_t)pblock(1)->balloons[0];   // a balloon: the fire belongs to its owner
    scenery_tree_update(tree);
    int32_t life = (int32_t)(mc_lcg(rng) % 0x3cu + 0x82);
    CHECK_EQ(tree->state, 1);
    CHECK_EQ(tree->health, life);
    CHECK(life >= 130 && life <= 189);
    CHECK_EQ(tree->rng, mc_lcg(rng));
    CHECK(!(tree->flags & 8));
    CHECK(tree->flags & 0x20000);
    CHECK(tree->flags & 4);
    CHECK_EQ(count_things([](const Thing *t) { return t->cls == 10 && t->type == 6; }), fires + 1);
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *f = thing_at(i);
        if (was_live[i] || !live(f) || f->cls != 10 || f->type != 6) continue;
        CHECK_EQ(f->owner, thing_index(wiz));
        CHECK_EQ(f->health, life);
        CHECK_EQ(f->z_vel, (tree->ext_h * 3) / 4);
        thing_free(f);
    }
    // burning: health counts down; below 0x3c the burnt sprite and state 2
    int calls = 0;
    while (tree->state == 1 && calls < 300) { scenery_tree_s1_update(tree); calls++; }
    CHECK_EQ(calls, life - 0x3b);
    CHECK_EQ(tree->state, 2);
    CHECK_EQ(tree->sprite, 0xe3);
    CHECK_EQ(tree->health, 0x3b);
    Thing snap = *tree;
    scenery_tree_s2_update(tree);
    CHECK(std::memcmp(&snap, tree, sizeof snap) == 0);       // a burnt tree stays as it is
    // the other sprite
    Thing *tree2 = nullptr;
    for (int i = 1; i < MC_THING_SLOTS && !tree2; i++)
        if (live(thing_at(i)) && thing_at(i)->cls == 2 && thing_at(i)->state == 0 && thing_at(i)->sprite == 0x53) tree2 = thing_at(i);
    CHECK(tree2 != nullptr);
    if (tree2) {
        tree2->state = 1;
        tree2->health = 0x3c;
        scenery_tree_s1_update(tree2);
        CHECK_EQ(tree2->state, 2);
        CHECK_EQ(tree2->sprite, 0xe2);
    }
    // a tree whose cell turned into water: splash (effect 5) with the tree's owner, tree deleted
    Pos water{};
    bool have_water = false;
    for (int cell = 0; cell < MC_MAP_CELLS && !have_water; cell++) {
        Pos p{(uint16_t)(((cell & 0xff) << 8) | 0x80), (uint16_t)((cell & 0xff00) | 0x80), 0};
        if (terrain_type_mask_at(&p) == 1) { water = p; have_water = true; }
    }
    CHECK(have_water);
    Thing *tree3 = nullptr;
    for (int i = MC_THING_SLOTS - 1; i > 0 && !tree3; i--)
        if (live(thing_at(i)) && thing_at(i)->cls == 2 && thing_at(i)->state == 0) tree3 = thing_at(i);
    if (have_water && tree3) {
        thing_move_to(tree3, &water);
        int splashes = count_things([](const Thing *t) { return t->cls == 10 && t->type == 5; });
        scenery_tree_update(tree3);
        CHECK(tree3->flags & 0x400);
        CHECK_EQ(tree3->z, (int16_t)terrain_height_at(&water));
        CHECK_EQ(count_things([](const Thing *t) { return t->cls == 10 && t->type == 5; }), splashes + 1);
    }
    // standing stone / bad stone: recyclable, on the ground; dolmen: flag 0x1000 on living players inside
    Thing *wiz0 = player_thing(0);
    Pos p = *thing_pos(wiz0);
    Thing *stone = thing_create(&p, 2, 1), *bad = thing_create(&p, 2, 3), *dolmen = thing_create(&p, 2, 2);
    CHECK(stone && bad && dolmen);
    if (stone && bad && dolmen) {
        CHECK_EQ(stone->state, 3);
        CHECK_EQ(bad->state, 9);
        CHECK_EQ(dolmen->state, 6);
        CHECK(thing_update_fn(2, 3) == scenery_standing_stone_update);
        CHECK(thing_update_fn(2, 9) == scenery_standing_stone_update);
        CHECK(thing_update_fn(2, 6) == scenery_dolmen_update);
        stone->z = 77;
        scenery_standing_stone_update(stone);
        CHECK(stone->flags & 0x20000);
        CHECK_EQ(stone->z, (int16_t)terrain_height_at(&p));
        wiz0->flags &= ~0x1000u;
        wiz0->z = (int16_t)terrain_height_at(&p);
        uint32_t dflags = dolmen->flags;
        scenery_dolmen_update(dolmen);
        CHECK(wiz0->flags & 0x1000);
        CHECK_EQ(dolmen->flags, dflags);                     // the dolmen itself is never made recyclable
        CHECK(!(player_thing(1)->flags & 0x1000));           // far away
        wiz0->flags &= ~0x1000u;
        int32_t hp = wiz0->health;
        wiz0->health = -1;                                   // a dead wizard does not regenerate
        scenery_dolmen_update(dolmen);
        CHECK(!(wiz0->flags & 0x1000));
        wiz0->health = hp;
        thing_free(stone); thing_free(bad); thing_free(dolmen);
        // the flyer (player.cpp) consumes the flag: castle-grade mana regeneration for that tick
        thing_update_all();
        CHECK_EQ(wiz0->mana_cost, 100);
        wiz0->flags |= 0x1000;
        thing_update_all();
        CHECK_EQ(wiz0->mana_cost, 1000);
        CHECK(!(wiz0->flags & 0x1000));
    }
    check_cell_lists("scenery");
}

static int count_records(unsigned dis_id) {
    int n = 0;
    for (const ThingInit &r : g_state->level.things) if (r.cls != 0 && r.dis_id == dis_id) n++;
    return n;
}

static Thing *find_switch(int id) {
    for (int i = 1; i < MC_THING_SLOTS; i++)
        if (live(thing_at(i)) && thing_at(i)->cls == 11 && thing_at(i)->owner == id) return thing_at(i);
    return nullptr;
}

static void test_switches() {
    Thing *wiz = player_thing(0);
    Pos wiz_home = *thing_pos(wiz);
    rebuild_lists();

    // switch_test_player: only on every 8th update, human wizards (type 0) only
    Thing *sw = find_switch(1);                              // level 38: state 0 "hidden, inside", fires once
    CHECK(sw != nullptr);
    if (!sw) return;
    CHECK_EQ(sw->state, 0);
    Pos inside = *thing_pos(sw);
    inside.z = (int16_t)terrain_height_at(&inside);
    sw->tick = 8;
    CHECK_EQ(switch_test_player(sw, 1), 0);                  // nobody there
    CHECK_EQ(switch_test_player(sw, 0), 1);                  // "outside" is true for the wizard far away
    CHECK_EQ(switch_test_any_player(sw, 1), 0);
    CHECK_EQ(switch_test_any_player(sw, 0), 1);
    thing_move_to(wiz, &inside);
    sw->tick = 9;
    sw->z = 5;
    CHECK_EQ(switch_test_player(sw, 1), 0);                  // off-beat: not even the z snap
    CHECK_EQ(sw->z, 5);
    sw->z = inside.z;
    sw->tick = 16;
    CHECK_EQ(switch_test_player(sw, 1), 1);
    CHECK_EQ(switch_test_any_player(sw, 1), 1);
    // an AI wizard (type 1) inside does not trip it, but counts for switch_test_any_player
    thing_move_to(wiz, &wiz_home);
    Thing *ai = player_thing(1);
    Pos ai_home = *thing_pos(ai);
    thing_move_to(ai, &inside);
    CHECK_EQ(switch_test_player(sw, 1), 0);
    CHECK_EQ(sw->z, inside.z);                               // nobody found: snapped to the ground
    CHECK_EQ(switch_test_any_player(sw, 1), 1);
    thing_move_to(ai, &ai_home);

    // once: records with DisId == SwiId are spawned and cleared, the switch goes
    int recs = count_records(1);
    int things = count_things([](const Thing *) { return true; });
    CHECK(recs > 0);
    ThingUpdateFn s0 = thing_update_fn(11, 0);
    CHECK(s0 != nullptr && s0 == thing_update_fn(11, 5) && s0 == thing_update_fn(11, 9));
    s0(sw);                                                  // wizard away: nothing
    CHECK(!(sw->flags & 0x400));
    CHECK_EQ(count_records(1), recs);
    thing_move_to(wiz, &inside);
    s0(sw);
    CHECK(sw->flags & 0x400);
    CHECK_EQ(count_records(1), 0);
    int spawned = count_things([](const Thing *) { return true; }) - things;
    std::printf("switch 1 (inside, once): %d level records -> %d things spawned\n", recs, spawned);
    CHECK(spawned > 0 && spawned <= recs * 17);
    thing_move_to(wiz, &wiz_home);
    rebuild_lists();

    // "outside, once" (state 1 / 6 / 10) fires at once for a wizard elsewhere
    Thing *sw8 = find_switch(8);
    CHECK(sw8 != nullptr);
    if (sw8) {
        ThingUpdateFn s1 = thing_update_fn(11, 1);
        CHECK(s1 != nullptr && s1 == thing_update_fn(11, 6) && s1 == thing_update_fn(11, 10) && s1 != s0);
        int r8 = count_records(8);
        CHECK(r8 > 0);
        sw8->tick = 0;
        s1(sw8);
        CHECK(sw8->flags & 0x400);
        CHECK_EQ(count_records(8), 0);
    }

    // repeating (level 38: state 2, id 6): fires, keeps the records, waits 10 updates without any
    // player Thing inside before it can fire again
    Thing *sw6 = find_switch(6);
    CHECK(sw6 != nullptr);
    if (sw6) {
        CHECK_EQ(sw6->state, 2);
        ThingUpdateFn s2 = thing_update_fn(11, 2);
        CHECK(s2 != nullptr && s2 == thing_update_fn(11, 7) && s2 == thing_update_fn(11, 11));
        int r6 = count_records(6);
        CHECK(r6 > 0);
        Pos in6 = *thing_pos(sw6);
        in6.z = (int16_t)terrain_height_at(&in6);
        sw6->tick = 0;
        s2(sw6);
        CHECK_EQ(sw6->aux, 0);
        thing_move_to(wiz, &in6);
        things = count_things([](const Thing *) { return true; });
        s2(sw6);
        CHECK_EQ(sw6->aux, 10);
        CHECK_EQ(count_records(6), r6);
        CHECK(!(sw6->flags & 0x400));
        int first = count_things([](const Thing *) { return true; }) - things;
        CHECK(first > 0);
        rebuild_lists();
        for (int k = 0; k < 5; k++) s2(sw6);                 // still inside: no countdown
        CHECK_EQ(sw6->aux, 10);
        thing_move_to(wiz, &wiz_home);
        for (int k = 9; k >= 0; k--) { s2(sw6); CHECK_EQ(sw6->aux, k); }
        s2(sw6);                                             // armed again, nobody inside
        CHECK_EQ(sw6->aux, 0);
        thing_move_to(wiz, &in6);
        things = count_things([](const Thing *) { return true; });
        s2(sw6);
        CHECK_EQ(sw6->aux, 10);
        std::printf("switch 6 (inside, repeating): %d records, %d things on the first firing, %d on the second\n",
                    r6, first, count_things([](const Thing *) { return true; }) - things);
        thing_move_to(wiz, &wiz_home);
        // the "outside" variant counts down while every player Thing is inside... i.e. never here
        ThingUpdateFn s3 = thing_update_fn(11, 3);
        CHECK(s3 != nullptr && s3 == thing_update_fn(11, 8) && s3 == thing_update_fn(11, 12) && s3 != s2);
        sw6->aux = 3;
        s3(sw6);
        CHECK_EQ(sw6->aux, 3);                               // somebody is outside: no countdown
        sw6->aux = 0;
    }
    rebuild_lists();

    // creature triggers: fire 17 updates after the last creature of the type is gone
    thing_update_all();                                      // real creature lists
    Pos p = wiz_home;
    int skeletons = 0;
    for (uint32_t i = g_cfg->creature_lists[9]; i != 0; i = thing_at(i)->next) skeletons++;
    CHECK(skeletons > 0);
    CHECK_EQ(g_cfg->creature_lists[0], 0u);                  // no dragons in level 38
    Thing *dragon_sw = thing_create(&p, 11, 13), *skel_sw = thing_create(&p, 11, 22), *all_sw = thing_create(&p, 11, 30);
    CHECK(dragon_sw && skel_sw && all_sw);
    if (dragon_sw && skel_sw && all_sw) {
        CHECK_EQ(dragon_sw->state, 13);
        CHECK_EQ(skel_sw->state, 22);
        dragon_sw->owner = skel_sw->owner = all_sw->owner = 0x7001;      // no such DisId
        int calls = 0;
        while (!(dragon_sw->flags & 0x400) && calls < 100) {
            thing_update_fn(11, 13)(dragon_sw);
            calls++;
            if (calls == 1) CHECK_EQ(dragon_sw->aux, 0x10);
        }
        CHECK_EQ(calls, 17);
        for (int k = 0; k < 40; k++) { thing_update_fn(11, 22)(skel_sw); thing_update_fn(11, 30)(all_sw); }
        CHECK(!(skel_sw->flags & 0x400));
        CHECK_EQ(skel_sw->aux, 0);
        CHECK(!(all_sw->flags & 0x400));                     // "all creatures": skeletons (type 9) count
        // the villagers (types 0xc..0xf) do not count for "all": with only those lists filled it fires
        uint32_t saved_lists[20];
        std::memcpy(saved_lists, g_cfg->creature_lists, sizeof saved_lists);
        for (int k = 0; k < 20; k++) if (k < 0xc || k > 0xf) g_cfg->creature_lists[k] = 0;
        g_cfg->creature_lists[0xc] = saved_lists[9];
        calls = 0;
        while (!(all_sw->flags & 0x400) && calls < 100) { switch_creature_dead_trigger(all_sw, -1); calls++; }
        CHECK_EQ(calls, 17);
        g_cfg->creature_lists[0xc] = 0;
        g_cfg->creature_lists[0x10] = saved_lists[9];        // ... but type 0x10 (wyvern) does
        all_sw->flags &= ~0x400u;
        all_sw->aux = 0;
        for (int k = 0; k < 40; k++) switch_creature_dead_trigger(all_sw, -1);
        CHECK(!(all_sw->flags & 0x400));
        std::memcpy(g_cfg->creature_lists, saved_lists, sizeof saved_lists);
        thing_free(dragon_sw); thing_free(skel_sw); thing_free(all_sw);
    }
    // every trigger state is bound to its own creature type
    for (int st = 13; st <= 30; st++) CHECK(thing_update_fn(11, st) != nullptr);
    CHECK(thing_update_fn(11, 31) == scenery_update_none);

    // victory switch (state 4): player 0 has a castle and the "won" status bit -> fires, takes the bit back
    Thing *vic = thing_create(&p, 11, 4);
    CHECK(vic != nullptr);
    if (vic) {
        vic->owner = 0x7002;
        PlayerRec *rec = &g_state->players[0];
        ThingUpdateFn s4 = thing_update_fn(11, 4);
        CHECK(s4 != nullptr);
        rec->status |= 2;
        s4(vic);                                             // no castle: nothing
        CHECK(!(vic->flags & 0x400));
        CHECK(rec->status & 2);
        rec->blk.castle = 485;
        rec->status &= 0xfffd;
        s4(vic);                                             // castle, not won: nothing
        CHECK(!(vic->flags & 0x400));
        rec->status |= 2 | 0x10;
        s4(vic);
        CHECK(vic->flags & 0x400);
        CHECK_EQ(rec->status, 0x10);
        rec->blk.castle = 0;
        rec->status = 0;
        thing_free(vic);
    }
    rebuild_lists();
    check_cell_lists("switches");
}

// ---- 4. smoke runs ---------------------------------------------------------------------------------

struct Stats {
    int castles = 0, balloons = 0, trees[3] = {0, 0, 0}, scenery_other = 0, switches = 0, balls_owned = 0, guards = 0;
    long castle_mana = 0, cargo = 0;
    int level_sum = 0, fetching = 0;
};
static Stats take_stats() {
    Stats s;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t)) continue;
        if (is_castle(t)) { s.castles++; s.castle_mana += t->mana; s.level_sum += t->aux; }
        else if (is_balloon(t)) { s.balloons++; s.cargo += t->mana; if (thing_at(t->target % MC_THING_SLOTS)->cls == 10) s.fetching++; }
        else if (t->cls == 2) { if (t->state < 3) s.trees[t->state]++; else s.scenery_other++; }
        else if (t->cls == 11) s.switches++;
        else if (is_ball(t) && t->mana_owner != 0) s.balls_owned++;
        else if (t->cls == 5 && t->type == 0xf) s.guards++;
    }
    return s;
}
static void print_stats(const char *what, unsigned tick, const Stats &s) {
    std::printf("%s tick %5u: castles %d (levels %d, mana %ld) balloons %d (cargo %ld, %d fetching) owned balls %d guards %d trees %d/%d/%d other scenery %d switches %d\n",
                what, tick, s.castles, s.level_sum, s.castle_mana, s.balloons, s.cargo, s.fetching, s.balls_owned, s.guards,
                s.trees[0], s.trees[1], s.trees[2], s.scenery_other, s.switches);
}

static void check_invariants(const char *what) {
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        if (!live(t)) continue;
        if (is_castle(t)) {
            if (t->aux < 0 || t->aux > 7) { std::printf("FAIL %s: castle %d level %d\n", what, i, t->aux); g_fail++; }
            if (t->state < 4 || t->state > 6) { std::printf("FAIL %s: castle %d state %d\n", what, i, t->state); g_fail++; }
            if (t->mana < 0) { std::printf("FAIL %s: castle %d mana %d\n", what, i, t->mana); g_fail++; }
        } else if (is_balloon(t) && t->state == 9 && t->health >= 0) {
            int above = t->z - (int16_t)terrain_height_at(thing_pos(t));
            // 0x10 per update down to the window; a balloon over freshly raised castle ground may be below it for a moment
            if (above < -0x400 || above > 0x2000) { std::printf("FAIL %s: balloon %d is %d above the ground\n", what, i, above); g_fail++; }
            if (t->mana < 0 || t->mana > 4000000) { std::printf("FAIL %s: balloon %d cargo %d\n", what, i, t->mana); g_fail++; }
        }
    }
}

static void smoke_movie(const char *game_dir) {
    if (!sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat")) { std::printf("FAIL snapshot reload\n"); g_fail++; return; }
    if (!demo_open(game_dir, 0)) { std::printf("smoke: movie/mvi00000.dat missing, skipped\n"); return; }
    thing_dispatch_reset_stats();
    g_standins_on = true;
    Stats s0;
    long unloaded = 0;
    int pickups = 0;
    long last_cargo = -1;
    long last_castle = -1;
    for (int tick = 0; tick < 2500; tick++) {
        game_tick_sim();
        Stats s = take_stats();
        if (tick == 0) s0 = s;
        if (last_cargo >= 0 && s.cargo > last_cargo) pickups++;
        if (last_castle >= 0 && s.castle_mana > last_castle) unloaded += s.castle_mana - last_castle;
        last_cargo = s.cargo;
        last_castle = s.castle_mana;
        if (tick % 500 == 0 || tick == 2499) print_stats("movie", g_state->players[0].tick, s);
        if (tick % 100 == 0) { check_invariants("movie"); }
    }
    Stats s1 = take_stats();
    std::printf("movie: %d ball pickups by balloons, %ld mana unloaded into castles over 2500 ticks\n", pickups, unloaded);
    CHECK_EQ(s1.castles, s0.castles);
    CHECK_EQ(s1.balloons, s0.balloons);
    CHECK_EQ(s1.trees[0] + s1.trees[1] + s1.trees[2], 150);
    CHECK(pickups > 0);
    CHECK(unloaded > 0);
    check_cell_lists("movie");
    std::printf("handlers dispatched without a port during the movie run (other tasks):\n");
    thing_dispatch_report(stdout);
    // none of this subsystem's
    CHECK(thing_update_fn(3, 4) && thing_update_fn(3, 5) && thing_update_fn(3, 6) && thing_update_fn(3, 9));
    demo_close();
    g_cfg->flags &= (uint16_t)0xfffb;
}

static void smoke_levels() {
    static const int levels[] = {0, 5, 12, 24, 38, 44};
    for (int lv : levels) {
        if (!sim_load_level(lv)) { std::printf("smoke: level %d does not load, skipped\n", lv); continue; }
        thing_dispatch_reset_stats();
        g_standins_on = true;
        g_standin_builds = 0;
        char name[32];
        std::snprintf(name, sizeof name, "level %d", lv);
        for (int tick = 0; tick < 2000; tick++) {
            game_tick_sim();
            if (tick == 0 || tick == 1999) print_stats(name, g_state->players[0].tick, take_stats());
            if (tick % 200 == 0) check_invariants(name);
        }
        check_cell_lists(name);
        // every class-2 / class-3 castle+balloon / class-11 state that came up had a handler
        for (int i = 1; i < MC_THING_SLOTS; i++) {
            Thing *t = thing_at(i);
            if (!live(t)) continue;
            if (t->cls == 2 || t->cls == 11 || is_castle(t) || is_balloon(t)) {
                if (!thing_update_fn(t->cls, t->state)) { std::printf("FAIL %s: class %d state %d has no handler\n", name, t->cls, t->state); g_fail++; }
            }
        }
    }
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }
    castle_register_handlers();
    scenery_register_handlers();
    CHECK_EQ(thing_register_update(0x26f10, standin_build_effect), 1);
    CHECK_EQ(thing_register_update(0x26b50, standin_build_effect), 1);
    CHECK_EQ(thing_register_update(0x25980, standin_mana_ball), 1);
    g_hook_thing_drop_mana_ball = count_drop_mana_ball;

    // every Table A record of classes 2 and 11 and the castle / balloon states of class 3 is bound
    for (int st = 0; st < 18; st++) CHECK(thing_update_fn(2, st) != nullptr);
    for (int st = 4; st <= 10; st++) CHECK(thing_update_fn(3, st) != nullptr);
    for (int st = 0; st < 32; st++) CHECK(thing_update_fn(11, st) != nullptr);
    for (int st = 0; st < 6; st++) CHECK(thing_update_fn(8, st) == castle_update_none);
    test_tables();

    if (!sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat")) { std::printf("snapshot failed to load\n"); return 2; }
    g_snap_things.assign(g_state->things, g_state->things + MC_THING_SLOTS);
    for (int p = 0; p < 8; p++) g_snap_blocks[p] = g_state->players[p].blk;
    test_snapshot_static();
    test_snapshot_fixed_point();

    if (!sim_load_level(38)) { std::printf("level 38 failed to load\n"); return 2; }
    g_standins_on = false;
    test_level38_start();
    test_level38_against_snapshot();
    test_take_damage();
    test_spill();
    test_find_ball();
    test_balloon_ferry();
    test_upgrade();
    test_quake_timer();
    test_guards();
    test_balloon_death();
    test_destroy();
    test_scenery();
    test_switches();

    smoke_movie(game_dir);
    smoke_levels();

    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
