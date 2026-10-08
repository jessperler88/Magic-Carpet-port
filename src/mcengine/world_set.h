// Data sets: the base game (set 0) and the Hidden Worlds expansion (set 1, from the user's own
// Magic Carpet Plus CD: HIDDEN.EXE loads data/blk1-*, pal1-0, build1-0, sky1-0, mspr1-0, hspr1-0,
// tmaps1-0 and levels/ddlevels instead of the base files; everything else is shared).
//
// The port plays the Hidden Worlds campaign as the continuation of the base campaign: campaign level
// indices HW_LEVEL_BASE + k (k = 0..24) are DDLEVELS entry k. sim_load_level selects the set from the
// index (world_set_for_level), so movies, save states and restarts need no extra field.
//
// The Hidden Worlds files live in <game dir>/hidden/ (tools/port/install_hidden_worlds.py copies them
// from the GOG CD image) or in the directory given to world_set_init / MC_HIDDEN_DIR.
#pragma once

constexpr int HW_LEVEL_BASE        = 100;   // campaign index of Hidden Worlds level 1 (DDLEVELS entry 0)
constexpr int HW_LEVEL_COUNT       = 70;    // DDLEVELS entries (25 campaign + multiplayer maps)
constexpr int HW_CAMPAIGN_LEVELS   = 25;    // "1. Goyaan" .. "25. Abnasur" (fe_screen_level_result 0x19)
constexpr int BASE_CAMPAIGN_LEVELS = 50;

// Remembers the Hidden Worlds directory (nullptr or "" = <game_dir>/hidden, or MC_HIDDEN_DIR when set)
// and resets to set 0. sim_init calls it with nullptr; mcport may call it again with a configured path.
void world_set_init(const char *game_dir, const char *hidden_dir);
const char *world_set_hidden_dir();
// True when the Hidden Worlds files are installed (levels/ddlevels.tab and data/blk1-0.dat present).
bool world_set_hidden_available();

inline bool world_is_hidden_level(int index) { return index >= HW_LEVEL_BASE && index < HW_LEVEL_BASE + HW_LEVEL_COUNT; }
inline int  world_set_for_level(int index) { return world_is_hidden_level(index) ? 1 : 0; }
// The entry of the set's levels file a campaign index loads (Hidden: index - HW_LEVEL_BASE).
inline int  world_level_file_index(int index) { return world_is_hidden_level(index) ? index - HW_LEVEL_BASE : index; }

int  world_set();               // the active set (0 / 1)
inline bool world_hidden() { return world_set() == 1; }

// The 1995 engine. HIDDEN.EXE is a 1995 build: besides the Hidden Worlds changes (world_hidden())
// it carries the 1995 versions of the functions the 1996 carpet.exe (= the port) changed - AI
// spell use and flight, castle mana spill, ... (docs/analysis/port_hidden_engine1995.md). They are
// identical in the 1995 CD CARPET.EXE. engine1995() is true while a Hidden Worlds level is active,
// and also for base levels when MC_ENGINE1995=1 was set at world_set_init (default off; used to
// check the 1995 code against recordings of the CD CARPET.EXE).
extern bool g_engine1995_force;
inline bool engine1995() { return g_engine1995_force || world_set() == 1; }
// Makes `set` active: the data-file redirect (mcfile.h) plus a reload of everything loaded from a
// set-specific file - the simulation's (sprite extents from tmaps, castle footprints from building.*)
// through g_hook_world_set_sim (sim_init), the renderer's / HUD's through g_hook_world_set_changed. Returns
// false when set 1 is requested but not installed (set 0 stays active). No-op when already active.
bool world_set_select(int set);
extern void (*g_hook_world_set_changed)(int set);   // renderer / HUD (engine_init)
extern void (*g_hook_world_set_sim)(int set);       // simulation data (sim_init)

// The campaign after `level` was won (Config.level + 1 in game_main): base level 50 continues with
// Hidden Worlds level 1 when it is installed. Returns the next campaign index.
int  world_campaign_next(int won_level);
// True when `level` (the next level to play) is past the end of the campaign (outro).
bool world_campaign_complete(int level);
// Level name for the front end / HUD ("1. Goyaan"); nullptr when not a Hidden Worlds index or unknown.
// Read from HIDDEN.EXE in the hidden directory (the name strings of its level table).
const char *world_hidden_level_name(int index);
