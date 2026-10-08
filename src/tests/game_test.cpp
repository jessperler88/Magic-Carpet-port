// Game flow test (port round 5, task D), ${MC_SIM_ALL} + game.cpp, no renderer:
//  1. level_skip_number_329c0 by construction;
//  2. the front-end bracket (game_before_frontend / game_after_frontend): F1 / F2 switches, level skip,
//     sound and music bank set 0, title flags, 320x200 <-> 640x480 toggle state;
//  3. a new campaign: level 0 through game_level_begin, the level track (seed = level, one LCG step),
//     a scripted win through the mana target (castle mana above level.win_percent): status bit 2 after
//     the original's 0x11 ticks, "Space" (command 0x1b) ends the loop with GAME_LEVEL_WON,
//     game_level_end: status 2, statistics checked against hand-computed values, Config.level + 1,
//     spell_found (GameState+0x3bd6) written and kept by the next level;
//  4. Esc (command 0x1d): GAME_LEVEL_LOST, status 8, Config.level unchanged;
//  5. restart (status |= 0xc as Shift+R / respawn without castle): level_finish_3d4e0 inside the loop,
//     GAME_RUNNING, the level is reloaded; Shift+Q (command 2): GAME_QUIT;
//  6. three consecutive levels started, run and ended: pool / cell chains / free stack intact, the
//     Thing pool after a level start identical to a fresh load of the same level.
// argv[1] = game dir.
#include "game.h"
#include "sim.h"
#include "thing.h"
#include "player.h"
#include "input.h"
#include "sound.h"
#include "sndbank.h"
#include "mc_globals.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)

// Records what game flow asks of the audio side.
struct FlowBackend : SoundBackend {
    std::vector<int> tracks;
    int music_stops = 0;
    bool music_play(int track, const uint8_t *, uint32_t) override { tracks.push_back(track); return true; }
    void music_stop() override { music_stops++; }
    bool music_done() override { return false; }
};
static FlowBackend s_backend;
static int s_fades = 0, s_mode_changes = 0, s_titles = 0;

static PlayerRec &local() { return g_state->players[g_state->local_player & 7]; }
static Thing *local_thing() { return thing_at(local().thing % MC_THING_SLOTS); }

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
    // free stack: distinct empty slots, and every empty slot is on it (nothing leaked)
    std::vector<int> on_free(MC_THING_SLOTS, 0);
    int empty = 0;
    for (int k = 0; k <= g_state->free_top; k++) {
        int i = g_state->free_list[k];
        if (i < 1 || i >= MC_THING_SLOTS || thing_at((unsigned)i)->cls != 0 || on_free[i]++) bad++;
    }
    for (int i = 1; i < MC_THING_SLOTS; i++) if (thing_at(i)->cls == 0) empty++;
    if (empty != g_state->free_top + 1) {
        std::printf("  %s: %d empty slots, free stack %d\n", what, empty, g_state->free_top + 1);
        bad++;
    }
    for (int k = 0; k <= g_state->active_top; k++) {
        int i = g_state->active_list[k];
        if (i < 1 || i >= MC_THING_SLOTS || thing_at((unsigned)i)->cls == 0) bad++;
    }
    int live = 999 - empty;
    std::printf("  pool (%s): %d live, %d free, %d recyclable, %d problems\n", what, live, g_state->free_top + 1,
                g_state->active_top + 1, bad);
    if (bad) { std::printf("FAIL pool consistency (%s): %d problems\n", what, bad); g_fail++; }
}

// Runs until the loop ends or `max` ticks passed; returns the result and the tick count in *ticks.
static GameStatus run(int max, int *ticks) {
    GameStatus s = GAME_RUNNING;
    int n = 0;
    while (n < max && (s = game_level_tick()) == GAME_RUNNING) n++;
    if (ticks) *ticks = n;
    return s;
}

static void test_skip() {
    int skipped[] = {8, 17, 28, 33, 39};
    for (int l = 0; l < 60; l++) {
        bool s = false;
        for (int k : skipped) s |= (k == l);
        CHECK_EQ(game_level_skip_number(l), s ? l + 1 : l);
    }
}

static void test_frontend_bracket() {
    g_sound_available = 1; g_music_available = 1;
    g_sound_on = 0; g_music_on = 0;
    g_video_mode_flags = 8;
    s_fades = s_mode_changes = 0;
    game_before_frontend();
    CHECK_EQ(g_video_mode_flags, 1);                  // front end in 320x200
    CHECK(g_frame2 != nullptr);
    CHECK_EQ(s_mode_changes, 1);
    CHECK_EQ(g_sound_on, 1);
    CHECK_EQ(g_music_on, 1);
    CHECK(s_fades >= 2);                             // the outer-loop fade + the toggle's fade
    g_state->title_flag_a = 0; g_state->title_flag_b = 7; g_state->title_flag_c = 7;
    int lvl = game_after_frontend(8);                // 8 is never played: 9
    CHECK_EQ(lvl, 9);
    CHECK_EQ(g_cfg->level, 9);
    CHECK_EQ(g_state->title_flag_a, 1);
    CHECK_EQ(g_state->title_flag_b, 0);
    CHECK_EQ(g_state->title_flag_c, 0);
    CHECK_EQ(s_titles, 1);
    CHECK_EQ(g_video_mode_flags, 8);                 // back to 640x480 for the game
    CHECK_EQ(s_mode_changes, 2);
    CHECK(sound_sample_count() > 0);                 // data/snds0-<quality>
    CHECK(g_music_track_count >= 3);                 // data/music0-0: 4 songs (1..3 are the level tracks)
    std::printf("front-end bracket: level 8 -> %d, %d sound samples, %d music tracks, %d fades\n",
                lvl, sound_sample_count(), (int)g_music_track_count, s_fades);
    // the port runs in 320x200 (mcport): toggle back
    game_toggle_resolution();
    CHECK_EQ(g_video_mode_flags, 1);
    CHECK_EQ(g_cfg->fade_stage, 0);
}

static void test_win() {
    game_campaign_new();
    CHECK_EQ(game_campaign_level(), 0);
    s_backend.tracks.clear();
    CHECK(game_level_begin(game_campaign_level()));
    CHECK(game_in_level());
    CHECK_EQ(local().status, 0);
    // level track: seed = level 0, one LCG step before the first loop: (int16)(0x24df) % 3 + 1 = 9439 % 3 + 1 = 2
    CHECK_EQ(s_backend.tracks.size(), 1u);
    if (!s_backend.tracks.empty()) CHECK_EQ(s_backend.tracks[0], 9439 % 3 + 1);
    CHECK_EQ(g_state->level_music_track, 9439 % 3 + 1);
    CHECK_EQ(music_track_for_level(0), 9439 % 3 + 1);   // sound.cpp's first-start formula agrees
    int t = 0;
    CHECK_EQ(run(5, &t), GAME_RUNNING);              // the first tick spawns the players
    CHECK(local().active == 1);
    PlayerBlock *P = player_block(local_thing());
    std::printf("level 0: win_percent %d, world mana %u, %u creatures, wizard thing %d\n",
                (int)g_state->level.win_percent, (unsigned)g_cfg->total_mana, (unsigned)g_state->creature_count,
                (int)local().thing);
    CHECK(g_cfg->total_mana > 0);

    // Scripted win: a castle for the local wizard (as the castle spell founds one) holding all the
    // world's mana, kept there before every tick.
    Thing *c = P->castle ? thing_at(P->castle) : thing_create(&local_thing()->home, 3, 2);
    CHECK(c != nullptr);
    if (!c) return;
    c->owner = local_thing()->owner;
    P->castle = thing_index(c);
    int won_at = -1, ticks = 0;
    GameStatus s = GAME_RUNNING;
    for (; ticks < 100 && s == GAME_RUNNING; ticks++) {
        thing_at(P->castle)->mana = (int32_t)g_cfg->total_mana;
        s = game_level_tick();
        if (won_at < 0 && (local().status & 2)) won_at = ticks + 1;
        if (won_at > 0 && ticks + 1 == won_at + 3) player_queue_command(0x1b, 0);   // Space, 3 ticks later
    }
    std::printf("win: status bit 2 after %d ticks above the target, loop ended %d ticks later with %d\n",
                won_at, ticks - won_at, (int)s);
    CHECK_EQ(won_at, 0x11);                          // win_timer 0..0x10, then the bit
    CHECK_EQ(s, GAME_LEVEL_WON);
    CHECK_EQ(ticks - won_at, 4);                     // queued after tick won_at+3, executed in the next tick
    CHECK_EQ(local().status, 10);                    // command 0x1b: 2 | 8
    CHECK_EQ(game_level_tick(), GAME_LEVEL_WON);     // stays ended

    // Hand-checkable statistics: 4 of the level's spells present, 2 of them found; 5 kills of 20
    // creatures; 4 hits of 10 shots; castle with half the world's mana; 1234 timer ticks.
    std::memset(g_state->spells_present, 0, sizeof g_state->spells_present);
    for (int i = 0; i < 4; i++) g_state->spells_present[i] = 1;
    std::memset(P->spell_thing, 0, sizeof P->spell_thing);
    std::memset(P->spell_found, 0, sizeof P->spell_found);
    P->spell_thing[0] = 5; P->spell_thing[1] = 6; P->spell_thing[10] = 7;   // 10: found, not in the level
    g_state->creature_count = 20;
    P->kills = 5; P->shots = 10; P->hits = 4;
    thing_at(P->castle)->mana = (int32_t)(g_cfg->total_mana / 2);
    P->mana_in_transit = 0;
    const int32_t expect_mana = (int32_t)((uint32_t)(int32_t)(g_cfg->total_mana / 2) * 100u) / (int32_t)g_cfg->total_mana;
    g_timer_ticks = P->start_tick + 1234;
    const int level_before = game_campaign_level();
    s_backend.music_stops = 0;
    game_level_end();
    GameLevelStats st;
    game_level_stats(&st);
    std::printf("stats: spells %d%% kills %d%% accuracy %d%% mana %d%% overall %d%% time %u ticks\n",
                st.pct_spells, st.pct_kills, st.pct_accuracy, st.pct_mana, st.pct_overall, st.elapsed_ticks);
    CHECK_EQ(local().status, 2);
    CHECK_EQ(st.pct_spells, 50);
    CHECK_EQ(st.pct_kills, 25);
    CHECK_EQ(st.pct_accuracy, 40);
    CHECK_EQ(st.pct_mana, expect_mana);              // 49 or 50 (odd world mana)
    CHECK_EQ(st.pct_overall, (50 + 25 + 40 + expect_mana) / 4);
    CHECK_EQ(st.elapsed_ticks, 1234u);
    CHECK_EQ(game_campaign_level(), level_before + 1);
    CHECK(P->spell_found[0] == 1 && P->spell_found[1] == 1 && P->spell_found[10] == 1);
    CHECK(P->spell_found[2] == 0 && P->spell_found[3] == 0);
    CHECK(!game_in_level());
    g_timer_ticks = 0;

    // The next campaign level keeps the found spells (GameState+0x3bd6 survives players_init_records).
    CHECK(game_level_begin(game_campaign_level()));
    CHECK_EQ(g_cfg->level, 1);
    const PlayerBlock &B = local().blk;
    CHECK(B.spell_found[0] == 1 && B.spell_found[1] == 1 && B.spell_found[10] == 1 && B.spell_found[2] == 0);
}

static void test_lose() {
    // continues in level 1 from test_win
    int t = 0;
    CHECK_EQ(run(50, &t), GAME_RUNNING);
    // Esc: command 0x1b is refused (the level is not won), 0x1d = leave the game
    player_queue_command(0x1b, 0);
    player_queue_command(0x1d, 0);
    GameStatus s = run(5, &t);
    CHECK_EQ(s, GAME_LEVEL_LOST);
    CHECK_EQ(t, 0);                                  // the very next tick
    CHECK_EQ(local().status, 8);
    game_level_end();
    CHECK_EQ(local().status, 8);
    CHECK_EQ(game_campaign_level(), 1);              // not advanced
    std::printf("lose: Esc -> GAME_LEVEL_LOST, status %d, level still %d\n", (int)local().status, game_campaign_level());
}

static void test_restart_and_quit() {
    CHECK(game_level_begin(2));
    s_backend.tracks.clear();
    int t = 0;
    CHECK_EQ(run(200, &t), GAME_RUNNING);
    const uint32_t tick_before = local().tick;
    CHECK(tick_before >= 200);
    // Shift+R as player_function_keys does it (status |= 0xc, castle 0), through game state
    local().status |= 0xc;
    local().blk.castle = 0;
    GameStatus s = game_level_tick();
    CHECK_EQ(s, GAME_RUNNING);                       // restart inside the loop
    CHECK_EQ(game_level_restarts(), 1);
    CHECK_EQ(local().status, 0);
    CHECK(local().tick < 5);                         // the level was reloaded: fresh player records
    CHECK_EQ(s_backend.tracks.size(), 1u);           // a new track: seed 2 stepped twice
    {
        int16_t seed = 2;
        seed = (int16_t)(seed * 0x24a1 + 0x24df);
        seed = (int16_t)(seed * 0x24a1 + 0x24df);
        const int expect = (int)((uint32_t)(int32_t)seed % 3u) + 1;
        if (!s_backend.tracks.empty()) CHECK_EQ(s_backend.tracks[0], expect);
        std::printf("restart: level 2 reloaded after %u ticks, track %d (expected %d)\n", (unsigned)tick_before,
                    s_backend.tracks.empty() ? -1 : s_backend.tracks[0], expect);
    }
    CHECK_EQ(run(100, &t), GAME_RUNNING);
    check_pool("level 2 after the restart");
    // Shift+Q: command 2
    player_queue_command(2, 0);
    s = run(5, &t);
    CHECK_EQ(s, GAME_QUIT);
    CHECK_EQ(local().quit, 1);
    game_level_end();
    CHECK(!game_in_level());
    CHECK(!game_level_begin(3));                     // no level while quitting (game_main's outer loop is over)
    local().quit = 0;                                // the test goes on
}

static std::vector<uint8_t> pool_image() {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(g_state->things);
    std::vector<uint8_t> v(p, p + sizeof g_state->things);
    v.insert(v.end(), g_map_height, g_map_height + 0x10000);
    return v;
}

static void test_consecutive() {
    const int levels[3] = {3, 4, 5};
    // reference: each level freshly loaded
    std::vector<std::vector<uint8_t>> fresh;
    for (int l : levels) {
        CHECK(sim_load_level(l));
        fresh.push_back(pool_image());
    }
    for (int k = 0; k < 3; k++) {
        CHECK(game_level_begin(levels[k]));
        std::vector<uint8_t> img = pool_image();
        size_t diff = 0;
        for (size_t i = 0; i < img.size(); i++) diff += img[i] != fresh[(size_t)k][i];
        std::printf("level %d started after %s: %zu bytes of the Thing pool / height map differ from a fresh load\n",
                    levels[k], k ? "the previous one" : "level 2", diff);
        CHECK_EQ(diff, 0u);
        int t = 0;
        CHECK_EQ(run(1500, &t), GAME_RUNNING);
        check_pool("after 1500 ticks");
        player_queue_command(0x1d, 0);               // leave
        CHECK_EQ(run(5, &t), GAME_LEVEL_LOST);
        game_level_end();
        check_pool("after the level end");
    }
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game)) { std::printf("SKIP: game data not found in %s\n", game); return 0; }
    sim_register_gameplay();
    sound_set_backend(&s_backend);
    g_hook_player_local_input = nullptr;             // scripted commands only
    g_hook_game_fade_out = [] { s_fades++; };
    g_hook_game_video_mode_changed = [] { s_mode_changes++; };
    g_hook_game_title_screen = [] { s_titles++; };
    g_video_mode_flags = 1;

    test_skip();
    test_frontend_bracket();
    test_win();
    test_lose();
    test_restart_and_quit();
    test_consecutive();
    sound_set_backend(nullptr);

    if (g_fail) { std::printf("game_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("game_test: OK\n");
    return 0;
}
