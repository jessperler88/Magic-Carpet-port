// Desync tooling (port round 10, task E): dumps for the parts exchange, tick-log lines with parts, the
// replay check. See replay_check.h and docs/analysis/port_desync.md.
#define _CRT_SECURE_NO_WARNINGS
#include "replay_check.h"
#include "net.h"
#include "savegame.h"
#include "demo.h"
#include "sim.h"
#include "player.h"
#include "ai_wizard.h"
#include "settings.h"
#include "mc_globals.h"
#include "mc_types.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// ---- desync dumps -------------------------------------------------------------------------------------

static std::string s_dump_dir;

void desync_set_dump_dir(const char *dir) { s_dump_dir = dir ? dir : ""; }
const char *desync_dump_dir() { return s_dump_dir.empty() ? savegame_save_dir() : s_dump_dir.c_str(); }

bool desync_dump_path(uint32_t exchange, int player, char *buf, size_t cap) {
    const char *dir = desync_dump_dir();
    if (!dir || !dir[0] || !buf || cap == 0) return false;
    std::snprintf(buf, cap, "%s/desync_%u_p%d.mcs", dir, (unsigned)exchange, player);
    return true;
}

bool desync_dump_state(uint32_t exchange, int local_player) {
    if (!g_settings.desync_dump) return false;
    char path[1024];
    if (!desync_dump_path(exchange, local_player, path, sizeof path)) return false;
    char name[52];
    std::snprintf(name, sizeof name, "desync exchange %u player %d", (unsigned)exchange, local_player);
    const bool ok = savestate_save_file(path, name);
    if (ok) std::fprintf(stderr, "net: desync dump %s\n", path);
    else    std::fprintf(stderr, "net: desync dump failed: %s\n", savestate_error());
    return ok;
}

static uint32_t ai_seed_reader() { return g_ai_rand_seed; }

void desync_tools_install() {
    net_sync_set_dump(desync_dump_state);
    g_hook_net_ai_seed = ai_seed_reader;
}

// ---- tick log -----------------------------------------------------------------------------------------

int tick_log_format(long tick, bool parts, char *buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    if (!parts) return std::snprintf(buf, cap, "%ld %08x", tick, (unsigned)net_state_checksum());
    if (!g_hook_net_ai_seed) g_hook_net_ai_seed = ai_seed_reader;
    NetChecksumParts p;
    net_state_checksum_parts(&p);
    int n = std::snprintf(buf, cap, "%ld %08x", tick, (unsigned)p.total);
    if (n < 0 || (size_t)n >= cap) return n;
    return n + net_checksum_parts_format(p, buf, cap);
}

// ---- replay check -------------------------------------------------------------------------------------

namespace {

// level_load_file_3d160's Config / GameState reset that sim_load_level does not do (as game.cpp
// level_reset_config and reference_test's copy): needed before a second level load in one process.
void level_reset_config_local() {
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

void say(const ReplayCheckOptions &o, const char *fmt, const char *a = "", long b = 0, long c = 0) {
    if (!o.log) return;
    std::fprintf(o.log, fmt, a, b, c);
    std::fflush(o.log);
}

std::string temp_state_path() {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::temp_directory_path(ec);
    if (ec) p = ".";
    char name[64];
    std::snprintf(name, sizeof name, "mc_replay_check_%p.mcs", (void *)&s_dump_dir);
    return (p / name).string();
}

// One pass: `step` plays one tick and says whether there is more. Records the parts after every tick; on
// the second pass compares with the first as it goes and stops at the first difference.
template <class Step>
void run_pass(int pass, const ReplayCheckOptions &opt, Step step, std::vector<NetChecksumParts> &rec,
              ReplayCheckResult *out) {
    bool more = true;
    long t = 0;
    while (more && (opt.max_ticks <= 0 || t < opt.max_ticks)) {
        more = step();
        t++;
        if (opt.after_tick) opt.after_tick(pass, t, opt.user);
        NetChecksumParts p;
        net_state_checksum_parts(&p);
        if (pass == 0) { rec.push_back(p); continue; }
        if ((size_t)(t - 1) >= rec.size()) break;
        out->compared++;
        const NetChecksumParts &a = rec[(size_t)(t - 1)];
        int count = 0;
        const int first = net_checksum_parts_first_diff(a, p, &count);
        if (first >= 0 || a.total != p.total) {
            out->first_diff_tick = t;
            out->first_part = first;
            out->parts_differ = count;
            out->total[0] = a.total;
            out->total[1] = p.total;
            break;
        }
    }
    out->ticks[pass] = t;
}

void finish(const ReplayCheckOptions &opt, ReplayCheckResult *out, const char *what) {
    out->ran = true;
    if (out->first_diff_tick >= 0) {
        std::snprintf(out->message, sizeof out->message,
                      "%s: pass 2 differs at tick %ld: first part %s (%d part(s) differ), totals %08x / %08x", what,
                      out->first_diff_tick, net_checksum_part_name(out->first_part), out->parts_differ,
                      (unsigned)out->total[0], (unsigned)out->total[1]);
        out->ok = false;
    } else if (out->ticks[1] != out->ticks[0]) {
        std::snprintf(out->message, sizeof out->message, "%s: pass 1 played %ld ticks, pass 2 %ld (identical up to there)",
                      what, out->ticks[0], out->ticks[1]);
        out->ok = false;
    } else {
        std::snprintf(out->message, sizeof out->message, "%s: identical, %ld ticks compared (checksum + %d parts each)",
                      what, out->compared, (int)NCP_COUNT);
        out->ok = out->compared > 0;
    }
    if (opt.log) { std::fprintf(opt.log, "replay check %s\n", out->message); std::fflush(opt.log); }
}

} // namespace

bool replay_check_movie(const char *game_dir, int number, const ReplayCheckOptions &opt, ReplayCheckResult *out) {
    ReplayCheckResult dummy;
    if (!out) out = &dummy;
    *out = ReplayCheckResult();
    if (!g_state || !g_cfg) { std::snprintf(out->message, sizeof out->message, "no game state (sim_init)"); return false; }
    if (!g_hook_net_ai_seed) g_hook_net_ai_seed = ai_seed_reader;
    void (*saved_input)() = g_hook_player_local_input;
    const std::string tmp = temp_state_path();
    std::vector<NetChecksumParts> rec;
    bool ok = true;
    char what[64];
    std::snprintf(what, sizeof what, "movie %d", number);
    for (int pass = 0; pass < 2 && ok; pass++) {
        g_cfg->flags = 0;
        g_cfg->paused = 0;
        if (opt.prepare) {
            if (!opt.prepare(pass, opt.user)) { std::snprintf(out->message, sizeof out->message, "%s: prepare failed", what); ok = false; break; }
        } else {
            level_reset_config_local();
            sim_prepare_movie();
            // As `carpet -roll 1 -level 38` (mcport start_movie, reference_test): the level generation leaves
            // g_rng16 at the value movie 0 continues from (neither snapshot file holds it).
            if (number == 0 && !sim_load_level(38)) { std::snprintf(out->message, sizeof out->message, "level 38 not loaded"); ok = false; break; }
        }
        // The simulation globals outside the snapshot: pass 2 gets pass 1's start values (savestate GLOB /
        // CONF / RULE chunks). The movie's snapshot then replaces GameState, maps and pool.
        if (opt.restore_globals) {
            if (pass == 0) {
                if (!savestate_save_file(tmp.c_str(), "replay check")) {
                    std::snprintf(out->message, sizeof out->message, "%s: start state not saved: %s", what, savestate_error());
                    ok = false;
                    break;
                }
            } else if (!savestate_load_file(tmp.c_str())) {
                std::snprintf(out->message, sizeof out->message, "%s: start state not restored: %s", what, savestate_error());
                ok = false;
                break;
            }
        }
        if (!demo_open(game_dir, number)) {
            std::snprintf(out->message, sizeof out->message, "%s: not found (mvi / mvx in %s/movie or the record dir)", what,
                          game_dir ? game_dir : "?");
            ok = false;
            break;
        }
        say(opt, "replay check %s: pass %ld\n", what, pass + 1);
        run_pass(pass, opt, [] { return demo_step(); }, rec, out);
        demo_close();
    }
    std::remove(tmp.c_str());
    g_hook_player_local_input = saved_input;
    if (!ok) { if (opt.log) std::fprintf(opt.log, "replay check: %s\n", out->message); return false; }
    finish(opt, out, what);
    return out->ok;
}

bool replay_check_state(const char *state_path, long ticks, const ReplayCheckOptions &opt, ReplayCheckResult *out) {
    ReplayCheckResult dummy;
    if (!out) out = &dummy;
    *out = ReplayCheckResult();
    if (!g_state || !g_cfg) { std::snprintf(out->message, sizeof out->message, "no game state (sim_init)"); return false; }
    SaveStateHeader h{};
    if (!state_path || !savestate_read_header(state_path, &h)) {
        std::snprintf(out->message, sizeof out->message, "%s: not a valid save state", state_path ? state_path : "?");
        return false;
    }
    if (!g_hook_net_ai_seed) g_hook_net_ai_seed = ai_seed_reader;
    void (*saved_input)() = g_hook_player_local_input;
    g_hook_player_local_input = nullptr;                // nobody steers: the same (empty) local packet each pass
    ReplayCheckOptions o = opt;
    if (o.max_ticks <= 0 || o.max_ticks > ticks) o.max_ticks = ticks;
    std::vector<NetChecksumParts> rec;
    bool ok = true;
    char what[64];
    std::snprintf(what, sizeof what, "state level %d tick %u", (int)h.level, (unsigned)h.tick);
    for (int pass = 0; pass < 2 && ok; pass++) {
        g_cfg->flags = 0;
        g_cfg->paused = 0;
        if (o.prepare) {
            if (!o.prepare(pass, o.user)) { std::snprintf(out->message, sizeof out->message, "%s: prepare failed", what); ok = false; break; }
        } else {
            level_reset_config_local();
            if (!sim_load_level(h.level)) {
                std::snprintf(out->message, sizeof out->message, "%s: level not loaded (a mode level needs a prepare callback)", what);
                ok = false;
                break;
            }
        }
        if (!savestate_load_file(state_path)) {
            std::snprintf(out->message, sizeof out->message, "%s: not loaded: %s", what, savestate_error());
            ok = false;
            break;
        }
        say(o, "replay check %s: pass %ld\n", what, pass + 1);
        run_pass(pass, o, [] { game_tick_sim(); return true; }, rec, out);
    }
    g_hook_player_local_input = saved_input;
    if (!ok) { if (o.log) std::fprintf(o.log, "replay check: %s\n", out->message); return false; }
    finish(o, out, what);
    return out->ok;
}
