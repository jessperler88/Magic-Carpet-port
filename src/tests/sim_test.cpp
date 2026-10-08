// Integration test of the whole simulation without the renderer (${MC_SIM_ALL}):
//  1. level 38 generated and run for the 412 ticks that precede the engine's own snapshot
//     (movie/gam00000.dat), then compared with it slot by slot, per class;
//  2. the shipped recording (movie/mvi00000.dat, 8551 ticks) played from that snapshot with every
//     gameplay subsystem registered: pool consistency, census over time, handlers still missing;
//  3. a 3000-tick run of several campaign levels with no input.
// argv[1] = game dir.
#include "sim.h"
#include "thing.h"
#include "player.h"
#include "demo.h"
#include "level_features.h"
#include "mcfile.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static const char *kClassName[14] = { "-", "c1", "scenery", "player", "c4", "creature", "c6", "c7", "c8",
                                      "projectile", "effect", "switch", "spell", "c13" };

static bool live(const Thing &t) { return t.cls != 0 && t.cls < 14; }

// Identical apart from the three list links (next, cell_next, cell_prev).
static bool same_thing(const Thing &a, const Thing &b) {
    const uint8_t *pa = reinterpret_cast<const uint8_t *>(&a), *pb = reinterpret_cast<const uint8_t *>(&b);
    return std::memcmp(pa + 4, pb + 4, 0x10) == 0 && std::memcmp(pa + 0x18, pb + 0x18, sizeof(Thing) - 0x18) == 0;
}

struct Census { int n[14] = {}; int total = 0; };
static Census census(const Thing *things) {
    Census c;
    for (int i = 1; i < MC_THING_SLOTS; i++)
        if (live(things[i])) { c.n[things[i].cls]++; c.total++; }
    return c;
}
static void print_census(const char *what, const Census &c) {
    std::printf("%-28s %4d things:", what, c.total);
    for (int k = 1; k < 14; k++) if (c.n[k]) std::printf(" %s %d", kClassName[k], c.n[k]);
    std::printf("\n");
}

// Every cell chain is finite, links back correctly and holds only live things; every live linked
// thing is in the chain of its own cell.
static void check_pool(const char *what) {
    std::vector<int> seen(MC_THING_SLOTS, 0);
    int bad = 0;
    for (int c = 0; c < MC_MAP_CELLS; c++) {
        int guard = 0; unsigned prev = 0;
        for (unsigned i = g_cell_things[c]; i != 0; i = thing_at(i)->cell_next) {
            if (i >= MC_THING_SLOTS || ++guard > MC_THING_SLOTS) { bad++; break; }
            const Thing *t = thing_at(i);
            if (t->cls == 0 || !(t->flags & 4) || t->cell_prev != prev) bad++;
            seen[i]++;
            prev = i;
        }
    }
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        const Thing *t = thing_at(i);
        if (t->cls != 0 && (t->flags & 4) && seen[i] != 1) bad++;
        if (seen[i] > 1) bad++;
    }
    if (bad) { std::printf("FAIL pool consistency (%s): %d problems\n", what, bad); g_fail++; }
}

static std::vector<Thing> load_snapshot_things() {
    std::vector<Thing> snap;
    if (!sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat")) return snap;
    snap.assign(g_state->things, g_state->things + MC_THING_SLOTS);
    return snap;
}

// ---- 1. the 412 ticks before the snapshot -----------------------------------------------------------
static void test_replay_to_snapshot(const std::vector<Thing> &snap) {
    void (*saved_input)() = g_hook_player_local_input;
    g_hook_player_local_input = nullptr;            // nothing is known about the inputs before the recording
    g_video_mode_flags = 1;                         // the recording ran in 320x200 (level_features.h castle_footprint)
    g_cfg->flags = 0; g_cfg->paused = 0;
    CHECK(sim_load_level(38));
    thing_dispatch_reset_stats();
    for (int guard = 0; g_state->players[0].tick < 389 && guard < 1000; guard++) game_tick_sim();
    // the recording player typed the "access all spells" cheat on tick 390 (port_spells.md)
    g_state->commands[0].cmd = 0x1e; g_state->commands[0].arg = 1;
    game_tick_sim();
    for (int guard = 0; g_state->players[0].tick < 412 && guard < 1000; guard++) game_tick_sim();
    check_pool("replay to snapshot");

    print_census("snapshot", census(snap.data()));
    print_census("port, level 38 + 412 ticks", census(g_state->things));
    int same[14] = {}, same_kind[14] = {}, total[14] = {};
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        const Thing &s = snap[i], &o = g_state->things[i];
        if (!live(s)) continue;
        total[s.cls]++;
        if (o.cls == s.cls && o.type == s.type) same_kind[s.cls]++;
        if (same_thing(s, o)) same[s.cls]++;
    }
    int all_same = 0, all_total = 0;
    std::printf("slot-by-slot against the snapshot (identical bytes except list links / same class+type in the slot / snapshot things):\n");
    for (int k = 1; k < 14; k++) {
        if (!total[k]) continue;
        std::printf("  %-10s %3d / %3d / %3d\n", kClassName[k], same[k], same_kind[k], total[k]);
        all_same += same[k]; all_total += total[k];
    }
    std::printf("  %-10s %3d /     / %3d\n", "all", all_same, all_total);
    int missing = thing_dispatch_report(nullptr);
    std::printf("handlers dispatched without a port: %d\n", missing);
    if (missing) thing_dispatch_report(stdout);
    // Regression floor (measured when round 3 was integrated; raise it when it improves).
    CHECK(same[2] == total[2]);                     // every tree
    CHECK(same[5] >= 120);                          // creatures
    CHECK(census(g_state->things).n[12] == census(snap.data()).n[12]);   // same number of spells (cheat included)
    CHECK(all_same * 100 >= all_total * 70);
    g_video_mode_flags = 8;
    g_hook_player_local_input = saved_input;
}

// ---- 2. the shipped recording -----------------------------------------------------------------------
static void test_movie(const char *game_dir) {
    g_cfg->flags = 0; g_cfg->paused = 0;
    uint16_t saved_mode = g_video_mode_flags;
    sim_prepare_movie();                            // game logic as the original plays the movie (sim.h)
    if (!demo_open(game_dir, 0)) { std::printf("movie 0 missing, playback skipped\n"); g_video_mode_flags = saved_mode; return; }
    thing_dispatch_reset_stats();
    int ticks = 0;
    bool more = true;
    while (more && ticks < 20000) {
        more = demo_step();
        ticks++;
        if (ticks == 1 || ticks % 2000 == 0 || !more) {
            char label[64];
            std::snprintf(label, sizeof label, "movie tick %d", ticks);
            print_census(label, census(g_state->things));
            check_pool(label);
        }
    }
    g_video_mode_flags = saved_mode;
    std::printf("movie 0: %d ticks played, %ld / %ld packets\n", ticks, demo_packets_read(), demo_packets_total());
    CHECK(demo_packets_read() == demo_packets_total());
    for (int p = 0; p < g_state->player_count && p < 8; p++) {
        const PlayerRec &r = g_state->players[p];
        const Thing *t = thing_at(r.thing % MC_THING_SLOTS);
        std::printf("  player %d: thing %d at cell %d,%d z=%d health %d / %d, mana %d, state %d\n", p, r.thing,
                    t->x >> 8, t->y >> 8, t->z, (int)t->health, (int)t->max_health, (int)t->mana, t->state);
    }
    int missing = thing_dispatch_report(nullptr);
    std::printf("handlers dispatched without a port during the movie: %d\n", missing);
    if (missing) thing_dispatch_report(stdout);
    demo_close();
}

// ---- 3. campaign levels without input -----------------------------------------------------------------
static void test_levels() {
    void (*saved_input)() = g_hook_player_local_input;
    g_hook_player_local_input = nullptr;
    static const int kLevels[] = { 0, 1, 5, 12, 24, 38, 44, 60, 69 };
    for (int lv : kLevels) {
        g_cfg->flags = 0; g_cfg->paused = 0;
        if (!sim_load_level(lv)) { std::printf("FAIL: level %d did not load\n", lv); g_fail++; continue; }
        thing_dispatch_reset_stats();
        Census c0 = census(g_state->things);
        for (int t = 0; t < 3000; t++) game_tick_sim();
        Census c1 = census(g_state->things);
        char label[64];
        std::snprintf(label, sizeof label, "level %d after 3000 ticks", lv);
        check_pool(label);
        std::printf("level %2d: %3d -> %3d things (creatures %3d -> %3d, effects %3d -> %3d, projectiles %d), unported handlers %d\n",
                    lv, c0.total, c1.total, c0.n[5], c1.n[5], c0.n[10], c1.n[10], c1.n[9], thing_dispatch_report(nullptr));
        thing_dispatch_report(stdout);
    }
    g_hook_player_local_input = saved_input;
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }
    sim_register_gameplay();

    std::vector<Thing> snap = load_snapshot_things();
    if (snap.empty()) { std::printf("snapshot failed to load\n"); return 2; }
    test_replay_to_snapshot(snap);
    test_movie(game_dir);
    test_levels();

    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
