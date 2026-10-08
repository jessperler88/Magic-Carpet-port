// scenario_test (port round 10, task B; docs/analysis/port_console.md): the scenario files and the debug
// commands, headless over the whole simulation (${MC_SIM_ALL}, no renderer).
//
//   scenario_test <game> [<file.scn | dir>]     default dir: src/tests/scenarios
//
//   1. every *.scn runs headless (level L / rts header) and passes all its assertions;
//   2. each runs twice in the same process (full reset between): identical per-tick net_state_checksum;
//   3. a deliberately failing assertion is reported with its file and line; a parse error names its line;
//   4. record + replay: a campaign scenario with debug packets is recorded as a movie (demo.cpp recorder) and
//      the playback reproduces the record run's state tick by tick (reference_player_test's round trip).
// MC_SCN_VERBOSE=1 prints every assertion / message line.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "sim.h"
#include "thing.h"
#include "player.h"
#include "demo.h"
#include "net.h"
#include "mode.h"
#include "mode_level.h"
#include "ai_wizard.h"
#include "debug_cmd.h"
#include "scenario.h"
#include "mc_globals.h"
#include "mcfile.h"
#include "crash_handler.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef MC_SCENARIO_DIR
#define MC_SCENARIO_DIR MC_REPO_DIR "/src/tests/scenarios"
#endif

static std::string g_game;
static bool g_verbose = false;
static uint32_t g_ai_seed0 = 0;
static uint16_t g_ai_human0 = 0;

// ---- level start (reference_player_test.cpp start_level, without the movie's -custom flag) ------------------
static void level_reset_config_local() {
    uint8_t *st = reinterpret_cast<uint8_t *>(g_state);
    uint8_t *cf = reinterpret_cast<uint8_t *>(g_cfg);
    st[0x244] = 0;
    g_cfg->fade_stage = 0;
    std::memset(cf + 0x5d, 0, 0x10);
    g_cfg->substeps = 0;
    g_cfg->palette_effect = 0;
    std::memset(cf + 0xb8, 0, 0xe);
    std::memset(cf + 0x8e1a, 0, 4);
    std::memset(g_cfg->creature_lists, 0, sizeof g_cfg->creature_lists);
    g_cfg->player_list = 0;
    g_cfg->mana_ball_list = 0;
    g_cfg->wizard_list = 0;
    g_cfg->projectile_list = 0;
    g_cfg->flags &= 0x3fff;
    g_cfg->paused &= ~1;
}

static bool start_scenario_level(const Scenario &s) {
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    sim_prepare_movie();
    level_reset_config_local();
    // the AI's CRT seed lives outside GameState: every run starts from the same one
    g_ai_rand_seed = g_ai_seed0;
    g_ai_human_wizard = g_ai_human0;
    debug_cmd_clear_queue();
    debug_cmd_take_messages();
    // The two Thing stacks keep stale entries above their tops from the previous run (the original's image
    // does the same; net_state_checksum hashes all 1000 entries): start every run as a fresh process would.
    std::memset(g_state->free_list, 0, sizeof g_state->free_list);
    std::memset(g_state->active_list, 0, sizeof g_state->active_list);
    if (s.rts) {
        if (!mode_start_run(g_game.c_str(), GAME_MODE_CONQUEST, s.rts_params)) return false;
        return sim_load_level(MODE_LEVEL_INDEX);
    }
    return sim_load_level(s.level < 0 ? 0 : s.level);
}

static void end_scenario_level(const Scenario &s) {
    if (s.rts) mode_stop_run();
    g_hook_player_local_input = nullptr;
    debug_cmd_clear_queue();
}

// One simulation tick as the hosts run it: the queued packet into the command slot, the tick, and (until the
// requested g_hook_debug_tick is in game_tick_sim) the god refresh - idempotent, so harmless once it is.
static void one_tick() {
    debug_cmd_pump();
    game_tick_sim();
    debug_cmd_tick();
}

// ---- state hash of the round trip (reference_player_test.cpp normalise) ---------------------------------------
static void normalise(GameState *s) {
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing &t = s->things[i];
        if (t.cls == 0) { t.next = 0; t.desc = 0; t.player = 0; }
    }
    for (int p = 0; p < 8; p++) {
        PlayerRec &r = s->players[p];
        for (PlayerMsg &m : r.messages) m.ticks = 0;
        r.blk.castle_hit_flash = 0;
        r.blk.hit_flash = 0;
        r.blk.damage_flash = 0;
        std::memset(r.blk.spell_flash, 0, sizeof r.blk.spell_flash);
    }
    for (int i = s->free_top + 1; i < 1000; i++) if (i >= 0) s->free_list[i] = 0;
    for (int i = s->active_top + 1; i < 1000; i++) if (i >= 0) s->active_list[i] = 0;
}
static uint64_t state_hash() {
    static GameState tmp;
    std::memcpy(&tmp, g_state, sizeof tmp);
    normalise(&tmp);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&tmp);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof tmp; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// ---- running a scenario ---------------------------------------------------------------------------------------
struct RunOut {
    std::vector<uint32_t> sums;          // net_state_checksum after every tick
    std::vector<NetChecksumParts> parts; // and its parts (the first differing one is named)
    std::vector<uint64_t> rec_hashes;    // record run: state hash of every recorded tick
    std::vector<std::string> failures;
    int passed = 0, ticks = 0, packets = 0;
    bool started = false;
};

// record: `movie_dir` != "" records the run as movie `movie` from tick 2 on (reference_player_test record()).
static bool run_scenario(const Scenario &s, RunOut *out, const std::string &movie_dir = "", int movie = 0) {
    if (!start_scenario_level(s)) { std::printf("  %s: the level could not be loaded\n", s.name.c_str()); return false; }
    ScenarioRunner run;
    run.start(s);
    static ScenarioRunner *s_run = nullptr;
    s_run = &run;
    g_hook_player_local_input = nullptr;
    if (!s.inputs.empty()) g_hook_player_local_input = [] { s_run->local_input(); };
    if (!movie_dir.empty()) {
        fs::create_directories(fs::path(movie_dir) / "movie");
        demo_set_record_dir(movie_dir.c_str());
        g_cfg->movie = (uint16_t)movie;
    }
    auto absorb = [&](const ScenarioStep &st) {
        out->packets += (int)st.packets.size();
        for (const std::string &f : st.failures) { out->failures.push_back(f); std::printf("  %s\n", f.c_str()); }
        if (g_verbose)
            for (const std::string &o : st.output) std::printf("    %s\n", o.c_str());
    };
    absorb(run.step(0));
    const int limit = s.stop > 0 ? s.stop : 100000;
    bool recording = false;
    for (int t = 1; t <= limit && run.running(); t++) {
        one_tick();
        out->ticks = t;
        out->sums.push_back(net_state_checksum());
        NetChecksumParts np;
        net_state_checksum_parts(&np);
        out->parts.push_back(np);
        if (g_verbose)
            for (const std::string &m : debug_cmd_take_messages()) std::printf("    [%d] %s\n", t, m.c_str());
        if (!movie_dir.empty()) {
            if (g_cfg->flags & 2) {
                recording = true;
                out->rec_hashes.push_back(state_hash());
            } else if (!recording && g_state->players[0].thing != 0) {
                g_cfg->flags |= 2;                          // the next tick's first packet opens the movie
            }
        }
        absorb(run.step(t));
    }
    if (!movie_dir.empty()) demo_close();
    debug_cmd_take_messages();
    out->passed = run.passed();
    out->started = recording || movie_dir.empty();
    if (run.running()) out->failures.push_back(s.name + ": did not finish in " + std::to_string(limit) + " ticks");
    end_scenario_level(s);
    return true;
}

// Playback of the recording; compares with the record run's hashes (playback step n = recorded tick n).
static int playback_compare(const Scenario &s, const std::string &dir, int movie, const std::vector<uint64_t> &hashes) {
    if (!start_scenario_level(s)) return 1;
    if (!demo_open(dir.c_str(), movie)) { std::printf("  playback: movie %d missing in %s\n", movie, dir.c_str()); return 1; }
    size_t k = 0, applied = 0;
    int first_bad = 0;
    for (int steps = 0; steps < 1000000; steps++) {
        const bool more = demo_step();
        debug_cmd_tick();                                   // as in the record run (see one_tick)
        applied += debug_cmd_take_messages().size();        // debug packets of the movie applied in this tick
        if (k < hashes.size()) {
            if (state_hash() != hashes[k] && !first_bad) first_bad = (int)k + 1;
            k++;
        }
        if (!more) break;
    }
    demo_close();
    debug_cmd_take_messages();
    end_scenario_level(s);
    std::printf("  round trip: %zu recorded ticks, %zu compared, %zu debug packets applied in the playback, first differing "
                "playback step: %d (0 = none)\n", hashes.size(), k, applied, first_bad);
    return first_bad != 0 || k == 0 || k != hashes.size() || applied == 0;
}

static int check_file(const fs::path &file) {
    Scenario s;
    if (!scenario_load(file.string(), &s)) { std::printf("%s: %s\n", file.filename().string().c_str(), s.error.c_str()); return 1; }
    std::printf("%s (%s): %s, %zu lines, %zu input lines, stop %d\n", file.filename().string().c_str(), s.title.c_str(),
                s.rts ? ("rts seed " + std::to_string(s.rts_params.seed) + " bots " + std::to_string(s.rts_params.bots)).c_str()
                      : ("level " + std::to_string(s.level)).c_str(),
                s.items.size(), s.inputs.size(), s.stop);
    RunOut a, b;
    if (!run_scenario(s, &a) || !run_scenario(s, &b)) return 1;
    int rc = 0;
    if (!a.failures.empty()) { std::printf("  FAIL: %zu failure(s)\n", a.failures.size()); rc = 1; }
    if (a.passed == 0) { std::printf("  FAIL: no assertion passed\n"); rc = 1; }
    size_t diff = 0;
    while (diff < a.sums.size() && diff < b.sums.size() && a.sums[diff] == b.sums[diff]) diff++;
    const bool same = a.sums.size() == b.sums.size() && diff == a.sums.size();
    std::printf("  %d ticks, %d packets, %d assertions passed; second run %s (last checksum %08x)\n", a.ticks, a.packets, a.passed,
                same ? "identical" : ("DIFFERS from tick " + std::to_string(diff + 1)).c_str(), a.sums.empty() ? 0u : a.sums.back());
    if (!same) {
        rc = 1;
        if (diff < a.parts.size() && diff < b.parts.size()) {
            const int part = net_checksum_parts_first_diff(a.parts[diff], b.parts[diff], nullptr);
            std::printf("  first differing part at tick %zu: %s\n", diff + 1, net_checksum_part_name(part));
        }
    }
    return rc;
}

// A scenario given as text (the deliberate failures).
static int check_failures() {
    int rc = 0;
    {
        const char *text =
            "level 1\n"
            "# a deliberately failing assertion: reported with its line\n"
            "3 assert_count creature dragon == 12345\n"
            "4 assert_health me == max\n";
        Scenario s;
        if (!scenario_parse(text, "deliberate.scn", &s)) { std::printf("deliberate.scn: %s\n", s.error.c_str()); return 1; }
        RunOut a;
        std::printf("deliberate.scn (expect one failure at line 3):\n");
        run_scenario(s, &a);
        const bool ok = a.failures.size() == 1 && a.failures[0].find("deliberate.scn:3:") != std::string::npos && a.passed == 1;
        std::printf("  %s\n", ok ? "ok: the failure names its line" : "FAIL: not reported as expected");
        rc |= !ok;
    }
    {
        Scenario s;
        const bool parsed = scenario_parse("level 1\nwait 3\nspawn nonsense 1 2\n", "bad.scn", &s);
        const bool ok = !parsed && s.error.find("bad.scn:3:") != std::string::npos;
        std::printf("bad.scn: %s (%s)\n", ok ? "ok: parse error names its line" : "FAIL", s.error.c_str());
        rc |= !ok;
    }
    {
        // an assertion that is never reached before `stop`
        Scenario s;
        scenario_parse("level 1\nstop 5\n20 assert_alive me\n", "unreached.scn", &s);
        RunOut a;
        run_scenario(s, &a);
        const bool ok = a.failures.size() == 1 && a.failures[0].find("unreached.scn:3:") != std::string::npos;
        std::printf("unreached.scn: %s\n", ok ? "ok: an assertion after stop is reported" : "FAIL");
        rc |= !ok;
    }
    return rc;
}

static int check_roundtrip(const fs::path &file) {
    Scenario s;
    if (!scenario_load(file.string(), &s)) return 1;
    if (s.rts) { std::printf("%s: rts, no campaign round trip\n", file.filename().string().c_str()); return 0; }
    const std::string dir = (fs::temp_directory_path() / "mc_scenario_roundtrip").string();
    const int movie = 31000;
    std::printf("record + replay %s:\n", file.filename().string().c_str());
    RunOut a;
    if (!run_scenario(s, &a, dir, movie) || !a.started) { std::printf("  recording did not start\n"); return 1; }
    std::printf("  recorded %ld packets\n", demo_packets_written());
    return playback_compare(s, dir, movie, a.rec_hashes);
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    g_verbose = std::getenv("MC_SCN_VERBOSE") != nullptr;
    if (!sim_init(g_game.c_str())) { std::printf("SKIP: game data not found in %s\n", g_game.c_str()); return 0; }
    sim_register_gameplay();
    mode_level_register();
    g_ai_seed0 = g_ai_rand_seed;
    g_ai_human0 = g_ai_human_wizard;

    fs::path root = argc > 2 ? fs::path(argv[2]) : fs::path(MC_SCENARIO_DIR);
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_regular_file(root, ec)) files.push_back(root);
    else
        for (const auto &e : fs::directory_iterator(root, ec))
            if (e.path().extension() == ".scn") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::printf("FAIL: no scenario files in %s\n", root.string().c_str()); return 1; }
    int failed = 0;
    for (const fs::path &f : files) failed += check_file(f);
    if (argc <= 2) {
        failed += check_failures();
        for (const fs::path &f : files)
            if (f.filename() == "god.scn" || f.filename() == "spawn_kill.scn" || f.filename() == "input.scn") failed += check_roundtrip(f);
    }
    std::printf("%s: %d check(s) failed\n", failed ? "FAIL" : "OK", failed);
    return failed ? 1 : 0;
}
