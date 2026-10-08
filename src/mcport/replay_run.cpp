// mcport glue of the desync tooling (port round 10, task E). See replay_run.h.
#define _CRT_SECURE_NO_WARNINGS
#include "replay_run.h"
#include "replay_check.h"
#include "demo.h"
#include "mc_globals.h"
#include <cstdlib>
#include <cstring>

static bool s_tick_log_parts = false;

bool replay_run_parse(int *argc, char **argv, ReplayRunArgs *out, std::string *err) {
    int w = 1;
    bool ok = true;
    for (int i = 1; i < *argc; i++) {
        const char *a = argv[i];
        if (std::strcmp(a, "--replay-check") == 0 || std::strcmp(a, "--replay-ticks") == 0) {
            if (i + 1 >= *argc) { if (err) *err = std::string(a) + " needs a value"; ok = false; continue; }
            const char *v = argv[++i];
            if (a[9] == 'c') {                                      // --replay-check
                out->active = true;
                if (std::strncmp(v, "state:", 6) == 0) out->state = v + 6;
                else {
                    char *end = nullptr;
                    out->movie = (int)std::strtol(v, &end, 10);
                    if (!end || *end || out->movie < 0) { if (err) *err = std::string("--replay-check ") + v + ": movie number or state:<file>"; ok = false; }
                }
            } else {
                out->ticks = std::atol(v);
            }
            continue;
        }
        if (std::strcmp(a, "--replay-raw") == 0) { out->no_restore = true; continue; }
        argv[w++] = argv[i];
    }
    *argc = w;
    argv[w] = nullptr;
    return ok;
}

int replay_run(const char *game_dir, const char *save_dir, const ReplayRunArgs &args) {
    if (save_dir && save_dir[0]) demo_set_record_dir(save_dir);
    ReplayCheckOptions o;
    o.log = stdout;
    o.max_ticks = args.ticks;
    o.restore_globals = !args.no_restore;
    ReplayCheckResult r;
    if (!args.state.empty()) replay_check_state(args.state.c_str(), args.ticks > 0 ? args.ticks : 1000, o, &r);
    else replay_check_movie(game_dir, args.movie, o, &r);
    std::printf("replay-check: %s %s\n", r.ok ? "OK" : (r.ran ? "DIFFERS" : "FAILED"), r.message);
    std::fflush(stdout);
    return r.ok ? 0 : 1;
}

void desync_run_install() {
    desync_tools_install();
    if (const char *e = std::getenv("MC_TICK_LOG_PARTS")) s_tick_log_parts = std::atoi(e) != 0;
}

void tick_log_write(FILE *f, long tick) {
    if (!f) return;
    char line[2048];
    tick_log_format(tick, s_tick_log_parts, line, sizeof line);
    std::fprintf(f, "%s\n", line);
}
