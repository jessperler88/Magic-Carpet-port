// Tick profiler (Phase 4, round 10 task D; port-only, nothing here exists in carpet.exe).
//
// Measures the wall-clock cost of one simulation tick: each phase of game_tick_sim (player.cpp) and, inside
// thing_update_all (thing.cpp), the list pass and the handler pass per Thing class. The markers in those two
// files only read a clock and add to this struct when g_tick_profile is non-null; with it null (the default:
// every test, every reference, mcport unless asked) they cost one pointer test per phase / per Thing.
// **Timing is never read by the simulation**: nothing in the tick looks at this struct.
//
// The markers are header-only (an inline variable + inline functions), so every unit test that links
// player.cpp / thing.cpp (${MC_SIM_CORE}) links without tick_profile.cpp. tick_profile.cpp holds the
// host-side helpers (window averages, the F11 text line, the JSON object for dumps).
//
// Use (host, between ticks):
//   static TickProfile prof;  g_tick_profile = &prof;      // on
//   tick_profile_begin(&prof); <one tick>; tick_profile_end(&prof);
//   TickProfileWindow w; w.add(prof); ... w.format(...)   // averages over many ticks
#pragma once
#include <chrono>
#include <cstdint>
#include <string>

enum TickPhase : int {
    TP_PALETTE,     // palette_effect_update (mode_3d == 0)
    TP_INPUT,       // g_hook_player_local_input (player_local_input)
    TP_COMMANDS,    // player_commands_process
    TP_WIN,         // game_check_level_won
    TP_THINGS,      // thing_update_all (all substeps), = TP lists + the per-class sums
    TP_MODE,        // g_hook_mode_tick
    TP_SOUND,       // g_hook_sound_update
    TP_FRAME,       // g_hook_frame_state (hud_tick_state)
    TP_PHASES
};
constexpr int TP_CLASSES = 16;      // Thing classes 0..15 (Thing.cls)

struct TickProfile {
    int64_t phase_ns[TP_PHASES];
    int64_t lists_ns;               // thing_update_all before the handler pass (free pass, lists, wake / threat / mana hooks)
    int64_t cls_ns[TP_CLASSES];     // handler pass: time in the update handlers of each class
    int32_t cls_calls[TP_CLASSES];  // handler calls per class
    int32_t substeps;               // thing_update_all calls in the tick (F3: 1 / 4 / 16)
    int64_t total_ns;               // tick_profile_begin .. tick_profile_end
    int64_t start_ns;
};

// null = profiling off (the markers do nothing). The host owns the struct.
inline TickProfile *g_tick_profile = nullptr;

inline int64_t tick_profile_now_ns() {
    return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Adds the time since *t to `phase` and moves *t to now.
inline void tick_profile_mark(TickProfile *p, int phase, int64_t *t) {
    const int64_t n = tick_profile_now_ns();
    p->phase_ns[phase] += n - *t;
    *t = n;
}
inline void tick_profile_add_class(TickProfile *p, int cls, int64_t ns) {
    const unsigned c = (unsigned)cls < (unsigned)TP_CLASSES ? (unsigned)cls : 0u;
    p->cls_ns[c] += ns;
    p->cls_calls[c]++;
}
// Host: clear before a tick / close after it.
inline void tick_profile_begin(TickProfile *p) {
    *p = TickProfile{};
    p->start_ns = tick_profile_now_ns();
}
inline void tick_profile_end(TickProfile *p) { p->total_ns = tick_profile_now_ns() - p->start_ns; }

const char *tick_profile_phase_name(int phase);     // "palette", "input", ...
const char *tick_profile_class_name(int cls);       // short: "player", "creature", ... ("c4" for unnamed)

// Sums of many ticks (the F11 line, JSON dumps, the log).
struct TickProfileWindow {
    int64_t phase_ns[TP_PHASES] = {};
    int64_t lists_ns = 0;
    int64_t cls_ns[TP_CLASSES] = {};
    int64_t cls_calls[TP_CLASSES] = {};
    int64_t total_ns = 0, worst_ns = 0;
    long ticks = 0;
    void add(const TickProfile &p);
    void clear() { *this = TickProfileWindow{}; }
    // "tick 0.84/1.90 ms: in .01 cmd .02 things .78 (lists .06 creature .41 effect .20 ..) snd .01 hud .01"
    // (averages over the window in ms, worst tick after the slash; classes above 1 % of the tick, most
    // expensive first, at most `max_classes`). "" when the window is empty.
    std::string format(int max_classes = 4) const;
    // {"ticks":N,"avg_us":..,"worst_us":..,"phases_us":{"input":..,..},"lists_us":..,"classes":{"creature":{"us":..,"calls":..},..}}
    // (per-tick averages in microseconds, 2 decimals; classes with calls only)
    std::string json() const;
};

// For JSON state dumps (task A's mode_dump): while the host profiles ticks (MC_TICK_PROFILE) it adds every
// tick here; a dump writer that finds it non-null writes `"tick_profile": g_tick_profile_dump->json()` and
// clears it (so each dump covers the ticks since the previous one). Null = no profile in dumps.
inline TickProfileWindow *g_tick_profile_dump = nullptr;
