// Port-only game modes (Phase 4, round 10 scaffolding; docs/ROADMAP_PHASE4.md, docs/port/BRIEFING_round10.md).
// Nothing here exists in carpet.exe.
//
// The original has exactly two "modes": the campaign and a network game. Phase 4 adds new ones (first:
// Conquest, the RTS / RPG hybrid). A mode is selected by the run (`mcport <dir> rts ...`, later the setup
// screen / lobby), never by the config file, and is carried by GameplayRules.mode (settings.h), which movies,
// network sessions and save states force exactly like the other gameplay rules.
//
// Simulated state of a mode lives OUTSIDE GameState (a fixed 0x38d03-byte image): in g_mode below. It is
// - hashed by net_state_checksum (only while a mode is active, so the original's checksum is unchanged),
// - saved in the "MODE" savestate chunk (savegame.h),
// - carried by movie format v3 and the gax snapshot (demo.h, round 10 task A).
// With mode 0 (GAME_MODE_ORIGINAL) the block is all zero and nothing reads it: every reference stays identical.
//
// Determinism: everything in g_mode is written only by the simulation (tick, packets, level start), integers
// only, never from the local player, the camera, settings or real time.
#pragma once
#include <cstddef>
#include <cstdint>

enum : uint32_t {
    GAME_MODE_ORIGINAL = 0,     // the campaign / network game of carpet.exe
    GAME_MODE_CONQUEST = 1,     // Phase 4: PvP RTS / RPG hybrid on a generated world
    GAME_MODE_COUNT
};

// Level index a mode level is loaded under (Config.level is u16; the campaign uses 0..69). sim_load_level
// asks g_hook_level_source (sim.h) for it instead of levels.dat.
constexpr int MODE_LEVEL_INDEX = 0x100;

#pragma pack(push, 1)
// What a mode run was started with. Exchanged in the lobby / written into movies and save states as is.
struct ModeParams {
    uint32_t seed = 0;          // match seed: the level generator and ModeRng derive from it
    uint32_t world_size = 256;  // cells per side (256 only until round 11)
    uint32_t bots = 0;          // computer wizards besides the human players
    uint32_t humans = 1;        // human players (1 offline)
    uint32_t flags = 0;         // MODE_PARAM_* bits
    uint32_t map = 0;           // template level (50..69, the multiplayer maps); 0 = chosen from the seed
    uint32_t reserved[2] = {};
};
static_assert(sizeof(ModeParams) == 32);

enum : uint32_t {
    MODE_PARAM_DEBUG = 1u << 0,         // debug commands (packets 0x40.., debug_cmd.h) accepted in this run
    MODE_PARAM_RESEED = 1u << 1,        // the template's terrain generator is re-seeded from the seed (else its own)
};

// Per player state of the mode (index = player record 0..7).
struct ModePlayer {
    uint32_t flags = 0;         // MODE_PLAYER_* bits
    int32_t  reserved[15] = {};
};
static_assert(sizeof(ModePlayer) == 64);

enum : uint32_t {
    MODE_PLAYER_GOD = 1u << 0,          // debug: health / mana kept full every tick (debug command "god")
};

struct ModeState {
    uint32_t   version = 0;     // MODE_STATE_VERSION while a mode is active, 0 otherwise
    uint32_t   mode = 0;        // GAME_MODE_*
    ModeParams params;
    uint32_t   tick = 0;        // mode ticks run since the level start (mode_tick)
    uint32_t   rng = 0;         // ModeRng state (mode_rng_next); seeded from params.seed at the level start
    uint32_t   debug_flags = 0; // reserved for simulated debug switches (MODE_DEBUG_*)
    uint32_t   template_level = 0;  // the levels.dat entry the mode level was built from (mode_level.cpp)
    uint32_t   ai_seed = 0;     // what g_ai_rand_seed (ai_wizard.h) was seeded with at the level start
    uint32_t   reserved[11] = {};
    ModePlayer players[8];
};
#pragma pack(pop)
static_assert(sizeof(ModeState) == 8 + sizeof(ModeParams) + 16 * 4 + 8 * sizeof(ModePlayer));
constexpr uint32_t MODE_STATE_VERSION = 1;

extern ModeState g_mode;

inline bool mode_active() { return g_mode.mode != GAME_MODE_ORIGINAL; }
const char *mode_name(uint32_t mode);           // "original", "conquest", "?"

// Start / stop a mode run. mode_begin clears g_mode, stores mode + params and forces GameplayRules.mode
// (the other rules from g_settings, clamped); mode_end clears g_mode and releases the forced rules.
void mode_begin(uint32_t mode, const ModeParams &params);
void mode_end();
// The level start of a mode level (sim_load_level_data calls it when a mode is active): per-level parts
// reset, ModeRng seeded from params.seed. Not called for the campaign.
void mode_level_start();
// One simulation tick of the mode: installed as g_hook_mode_tick (player.h) by mode_begin, removed by
// mode_end. Runs after thing_update_all, before the sound hook.
void mode_tick();
// Called at the end of mode_level_start (null = nothing): mode_level.cpp's level-start work that needs the
// gameplay subsystems (the AI's g_ai_rand_seed from the match seed, the human players' start spells).
// Installed by mode_level_register() (mode_level.h).
extern void (*g_hook_mode_level_start)();

// The mode's own LCG (never std::rand): deterministic from the match seed.
uint32_t mode_rng_next();

// FNV-1a 32 over the whole block (the "mode" checksum part, net.cpp), then g_hook_mode_checksum_globals:
// the simulation globals outside GameState that a mode run depends on and the original's checksum leaves
// out (mode_level.cpp: the AI's g_ai_rand_seed and g_ai_human_wizard, g_terrain_nearly_flat).
uint32_t mode_checksum(uint32_t h);
extern uint32_t (*g_hook_mode_checksum_globals)(uint32_t h);

// The block as the MODE savestate chunk / movie v3 snapshot carry it: u32 version, then the raw ModeState.
size_t mode_serialised_size();
void   mode_serialise(uint8_t *out);                    // mode_serialised_size() bytes
bool   mode_deserialise(const uint8_t *in, size_t n);   // false (g_mode unchanged) on a wrong size / version
