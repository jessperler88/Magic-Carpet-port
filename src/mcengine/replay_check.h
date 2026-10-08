// Desync tooling (port round 10, task E; docs/analysis/port_desync.md). Nothing here exists in carpet.exe.
//
//  * Desync dumps: the dump hook of net.h's parts exchange (net_sync_set_dump) writing the complete state
//    (savegame.h savestate_save_file: GameState, Config, maps + cell heads, globals, rules, mode block, pool
//    extension) to <dir>/desync_<exchange>_p<player>.mcs; tools/reference/diff_state.py names the fields.
//  * Tick-log lines with the checksum parts (MC_TICK_LOG_PARTS=1; tools/reference/tick_log_diff.py).
//  * Replay check: play a movie (or run a save state) twice from its start and compare the per-tick
//    checksum and its parts. A difference = hidden state the movie / state does not carry, or
//    non-determinism. Read-only between ticks; the simulation itself is unchanged.
//
// Links with the whole simulation (${MC_SIM_ALL}) + savegame.cpp (+ frontend.cpp for savegame_capture).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "net.h"

// ---- desync dumps ----------------------------------------------------------------------------------
// Directory of the dumps (null / "" = savegame_save_dir(); nothing is written when both are empty).
void        desync_set_dump_dir(const char *dir);
const char *desync_dump_dir();
// The default dump hook: <dir>/desync_<exchange>_p<player>.mcs (savestate format). False when dumps are
// off (PortSettings::desync_dump) or the file could not be written. Prints the path to stderr.
bool        desync_dump_state(uint32_t exchange, int local_player);
// The path desync_dump_state writes for (exchange, player).
bool        desync_dump_path(uint32_t exchange, int player, char *buf, size_t cap);
// Installs, for the calling thread's network context: net_sync_set_dump(desync_dump_state) and
// g_hook_net_ai_seed (the ai_seed part). mcport calls it once after net_init; tests per peer thread.
void        desync_tools_install();

// ---- tick log ---------------------------------------------------------------------------------------
// "<tick> <checksum>" as MC_TICK_LOG writes it, plus " name=xxxxxxxx" per part when `parts` (no newline).
int         tick_log_format(long tick, bool parts, char *buf, size_t cap);

// ---- replay check -------------------------------------------------------------------------------------
struct ReplayCheckOptions {
    long  max_ticks = 0;            // stop each pass after this many ticks (0 = the whole movie)
    bool  restore_globals = true;   // give pass 2 the start values of the simulation globals outside the
                                    //   snapshot (g_ai_rand_seed, ...); false = report what leaks between passes
    // Called before each pass (0, 1) instead of the default preparation (movie: sim_prepare_movie, Config
    // reset, level 38 for movie 0 as `carpet -roll 1 -level 38`; state: sim_load_level(header level)).
    // Return false to abort.
    bool (*prepare)(int pass, void *user) = nullptr;
    // Called after every tick of each pass (tests: perturb the second pass). tick = 1, 2, ...
    void (*after_tick)(int pass, long tick, void *user) = nullptr;
    void *user = nullptr;
    FILE *log = nullptr;            // progress / result lines (null = quiet)
};
struct ReplayCheckResult {
    bool     ok = false;            // both passes ran and every compared tick is identical
    bool     ran = false;           // false: the movie / state could not be opened (message says why)
    long     ticks[2] = {0, 0};     // ticks played per pass
    long     compared = 0;
    long     first_diff_tick = -1;  // first tick (1-based) whose total or parts differ, -1 none
    int      first_part = -1;       // NCP_* of the first differing part at that tick
    int      parts_differ = 0;      // parts differing at that tick
    uint32_t total[2] = {0, 0};     // the totals at first_diff_tick (pass 1, pass 2)
    char     message[256] = "";
};
// The movie `number` of game_dir (or of the record dir, demo.h) - v1 / v2 / v3 through demo_open.
bool replay_check_movie(const char *game_dir, int number, const ReplayCheckOptions &opt, ReplayCheckResult *out);
// A save state (savegame.h) run for `ticks` ticks without local input (computer wizards and creatures act),
// twice from the loaded state.
bool replay_check_state(const char *state_path, long ticks, const ReplayCheckOptions &opt, ReplayCheckResult *out);
