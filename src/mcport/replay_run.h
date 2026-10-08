// mcport glue of the desync tooling (port round 10, task E; mcengine/replay_check.h, docs/analysis/port_desync.md).
//
//   mcport <dir> --replay-check N [--replay-ticks T]       play movie N twice (game dir, then <save dir>/movie)
//   mcport <dir> --replay-check state:<file> [--replay-ticks T]   run a save state T ticks twice (default 1000)
// Headless (no window): prints one result line, exit code 0 = identical, 1 = differs / could not run.
// MC_TICK_LOG_PARTS=1: the MC_TICK_LOG lines also carry the checksum parts (tools/reference/tick_log_diff.py).
#pragma once
#include <cstdio>
#include <string>

struct ReplayRunArgs {
    bool        active = false;
    int         movie = -1;         // --replay-check N
    std::string state;              // --replay-check state:<file>
    long        ticks = 0;          // --replay-ticks T (0 = the whole movie / 1000 for a state)
    bool        no_restore = false; // --replay-raw: pass 2 starts with what pass 1 left in the globals
};
// Removes the --replay-* options from argv (call before config_load, which refuses unknown --options).
// False (with *err) on a malformed option.
bool replay_run_parse(int *argc, char **argv, ReplayRunArgs *out, std::string *err);
// Runs the check after engine_init (the simulation is registered). save_dir: where Alt+R recordings are.
// Returns the process exit code.
int  replay_run(const char *game_dir, const char *save_dir, const ReplayRunArgs &args);

// After engine_init: the desync dump hook (net.h parts exchange -> <save dir>/desync_<n>_p<player>.mcs) and
// the ai_seed checksum part. Reads MC_TICK_LOG_PARTS.
void desync_run_install();
// One MC_TICK_LOG line ("<tick> <checksum>" [+ parts]) + newline.
void tick_log_write(FILE *f, long tick);
