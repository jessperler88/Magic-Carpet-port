// Replay check test (port round 10, task E; mcengine/replay_check.h, docs/analysis/port_desync.md).
//  1. Movie 0 (the original's mvi00000.dat) played twice from its start: every tick's checksum and parts
//     identical (MC_REPLAY_TICKS=n limits the passes; default: the whole movie, 8550 ticks).
//  2. The same with a perturbation in the second pass (a creature's health after tick 777): caught at tick
//     777, part things.creature, 1 part.
//  3. A port movie (mvx version 2: recorded here with possession_range_pct 150) replays identically.
//  4. A save state taken in level 5 after 150 ticks, run 300 ticks twice without input: identical; with a
//     perturbed g_rng16 in pass 2 at tick 50: caught at tick 50 (part rng16).
//  5. Information only: movie 0 with restore_globals off (pass 2 starts with what pass 1 left in the globals
//     outside the snapshot) - prints whether that matters.
// argv[1] = game dir. Exit 0 = pass, SKIP without movie 0.
#define _CRT_SECURE_NO_WARNINGS
#include "replay_check.h"
#include "net.h"
#include "savegame.h"
#include "sim.h"
#include "player.h"
#include "input.h"
#include "thing.h"
#include "demo.h"
#include "settings.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "crash_handler.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)

static int g_slot = 0;
static void perturb_creature(int pass, long tick, void *) {
    if (pass != 1 || tick != 777) return;
    for (int i = 1; i < MC_THING_SLOTS; i++)
        if (g_state->things[i].cls == 5 && g_state->things[i].health > 0) { g_slot = i; g_state->things[i].health += 3; return; }
}
static void perturb_rng16(int pass, long tick, void *) {
    if (pass == 1 && tick == 50) g_rng16 ^= 0x40;
}

static double secs_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game)) { std::printf("SKIP: game data not found in %s\n", game); return 0; }
    sim_register_gameplay();
    const fs::path tmp = fs::temp_directory_path() / "mc_replay_check_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "movie", ec);

    // 1. movie 0, twice
    ReplayCheckOptions o;
    o.log = stdout;
    if (const char *e = std::getenv("MC_REPLAY_TICKS")) o.max_ticks = std::atol(e);
    ReplayCheckResult r;
    auto t0 = std::chrono::steady_clock::now();
    replay_check_movie(game, 0, o, &r);
    if (!r.ran) {
        std::printf("SKIP: %s\n", r.message);
        return 0;
    }
    std::printf("  movie 0: %ld / %ld ticks, %ld compared, %.1f s\n", r.ticks[0], r.ticks[1], r.compared, secs_since(t0));
    CHECK(r.ok);
    CHECK_EQ(r.first_diff_tick, -1);
    CHECK(r.compared == r.ticks[0] && r.ticks[0] == r.ticks[1]);
    if (o.max_ticks <= 0) CHECK(r.ticks[0] >= 8500);

    // 2. a perturbed second pass
    ReplayCheckOptions p = o;
    p.max_ticks = 1000;
    p.after_tick = perturb_creature;
    replay_check_movie(game, 0, p, &r);
    CHECK(g_slot > 0);
    CHECK(!r.ok);
    CHECK_EQ(r.first_diff_tick, 777);
    CHECK_EQ(r.first_part, NCP_THINGS + 5);
    CHECK_EQ(r.parts_differ, 1);
    CHECK(r.total[0] != r.total[1]);
    std::printf("  perturbed: things[%d] health +3 after tick 777 of pass 2 -> %s\n", g_slot, r.message);

    // 3. a port movie (mvx v2: non-faithful rules)
    demo_set_record_dir(tmp.string().c_str());
    g_settings.possession_range_pct = 150;
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    input_reset();
    CHECK(sim_load_level(12));
    g_cfg->movie = 9;
    g_cfg->flags |= 2;                                       // command 0xc: record from the next packet
    void (*saved_input)() = g_hook_player_local_input;
    for (int i = 0; i < 400; i++) {
        // some steering so that the packets are not all empty
        input_mouse_move(320 + ((i / 40) & 1 ? 90 : -90), 200 + ((i / 70) % 3 - 1) * 30);
        if (i % 60 == 5) input_mouse_button(0, true);
        if (i % 60 == 8) input_mouse_button(0, false);
        game_tick_sim();
    }
    CHECK(demo_recording());
    const long written = demo_packets_written();
    demo_close();
    g_settings.possession_range_pct = 100;
    CHECK(fs::exists(tmp / "movie" / "mvx00009.dat"));
    ReplayCheckOptions m = o;
    m.max_ticks = 0;
    m.prepare = [](int, void *) { sim_prepare_movie(); return true; };
    replay_check_movie(tmp.string().c_str(), 9, m, &r);
    std::printf("  port movie 9 (mvx v2, %ld packets): %s\n", written, r.message);
    CHECK(r.ok);
    CHECK(r.ticks[0] >= 390);
    g_hook_player_local_input = saved_input;

    // 4. a save state
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    input_reset();
    CHECK(sim_load_level(5));
    for (int i = 0; i < 150; i++) game_tick_sim();
    const std::string state = (tmp / "state_l5.mcs").string();
    CHECK(savestate_save_file(state.c_str(), "replay check test"));
    ReplayCheckOptions s = o;
    replay_check_state(state.c_str(), 300, s, &r);
    CHECK(r.ok);
    CHECK_EQ(r.compared, 300);
    s.after_tick = perturb_rng16;
    replay_check_state(state.c_str(), 300, s, &r);
    CHECK(!r.ok);
    CHECK_EQ(r.first_diff_tick, 50);
    CHECK_EQ(r.first_part, NCP_RNG16);
    std::printf("  state, g_rng16 changed in pass 2 at tick 50 -> %s\n", r.message);

    // 5. information: what the globals outside the movie's snapshot carry from one pass into the next
    ReplayCheckOptions g = o;
    g.restore_globals = false;
    g.max_ticks = o.max_ticks > 0 ? o.max_ticks : 3000;
    g.log = nullptr;
    replay_check_movie(game, 0, g, &r);
    std::printf("  movie 0 without restoring the globals between the passes (%ld ticks): %s\n", g.max_ticks, r.message);

    demo_set_record_dir("");
    fs::remove_all(tmp, ec);
    if (g_fail) { std::printf("replay_check_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("replay_check_test: OK\n");
    return 0;
}
