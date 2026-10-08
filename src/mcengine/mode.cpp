// Port-only game modes: the mode state block (mode.h). Round 10 (task A).
#include "mode.h"
#include "settings.h"
#include "player.h"
#include <cstring>

ModeState g_mode;
void (*g_hook_mode_tick)() = nullptr;        // player.h (defined here: every unit test links mode.cpp)
void (*g_hook_mode_level_start)() = nullptr;
uint32_t (*g_hook_mode_checksum_globals)(uint32_t h) = nullptr;

namespace {
uint32_t fnv(uint32_t h, const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
}

const char *mode_name(uint32_t mode) {
    switch (mode) {
    case GAME_MODE_ORIGINAL: return "original";
    case GAME_MODE_CONQUEST: return "conquest";
    default: return "?";
    }
}

void mode_begin(uint32_t mode, const ModeParams &params) {
    g_mode = ModeState{};
    if (mode == GAME_MODE_ORIGINAL || mode >= GAME_MODE_COUNT) { mode_end(); return; }
    g_mode.version = MODE_STATE_VERSION;
    g_mode.mode = mode;
    g_mode.params = params;
    GameplayRules r = gameplay_rules();
    r.mode = (int)mode;
    gameplay_force_rules(&r);
    g_hook_mode_tick = mode_tick;
}

void mode_end() {
    const bool was = mode_active();
    g_mode = ModeState{};
    if (g_hook_mode_tick == mode_tick) g_hook_mode_tick = nullptr;
    if (was) gameplay_force_rules(nullptr);
}

void mode_level_start() {
    g_mode.tick = 0;
    g_mode.rng = g_mode.params.seed ^ 0x9e3779b9u;
    for (ModePlayer &p : g_mode.players) p = ModePlayer{};
    if (g_hook_mode_level_start) g_hook_mode_level_start();
}

void mode_tick() {
    if (!mode_active()) return;
    g_mode.tick++;
}

uint32_t mode_rng_next() {
    g_mode.rng = g_mode.rng * 1664525u + 1013904223u;
    return g_mode.rng;
}

uint32_t mode_checksum(uint32_t h) {
    h = fnv(h, &g_mode, sizeof g_mode);
    return g_hook_mode_checksum_globals ? g_hook_mode_checksum_globals(h) : h;
}

size_t mode_serialised_size() { return 4 + sizeof(ModeState); }

void mode_serialise(uint8_t *out) {
    const uint32_t v = MODE_STATE_VERSION;
    std::memcpy(out, &v, 4);
    std::memcpy(out + 4, &g_mode, sizeof g_mode);
}

bool mode_deserialise(const uint8_t *in, size_t n) {
    if (!in || n != mode_serialised_size()) return false;
    uint32_t v;
    std::memcpy(&v, in, 4);
    if (v != MODE_STATE_VERSION) return false;
    ModeState m;
    std::memcpy(&m, in + 4, sizeof m);
    if (m.mode >= GAME_MODE_COUNT) return false;
    g_mode = m;
    g_hook_mode_tick = mode_active() ? mode_tick : nullptr;
    return true;
}
