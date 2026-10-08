// Simulation-side glue that links without the renderer (the "sim core": thing, terrain_gen,
// terrain_paint, spatial, level_features, constructors, player, demo, sim). Unit tests of game-logic
// subsystems build on this instead of re-implementing level loading; engine.cpp layers the renderer
// on top.
#pragma once
#include <cstdint>
#include "mc_types.h"

// Allocates the game state, loads the data files the simulation needs (tmaps.dat sprite sizes,
// building.tab / .dat, search.dat), binds the handlers of the sim-core subsystems (level features,
// constructors, players) and connects their hooks. A subsystem outside the core is registered by
// the caller afterwards (`xxx_register_handlers()`).
bool sim_init(const char *game_dir);
// Registers every gameplay subsystem outside the core (effects, projectiles, spells, castles,
// scenery / switches, creatures, local input) and connects the hooks between them. Lives in
// sim_all.cpp so a unit test that lists only ${MC_SIM_CORE} does not have to link them; a test or
// program that wants the whole simulation lists ${MC_SIM_ALL} and calls this after sim_init.
void sim_register_gameplay();
// Call before demo_open(): puts the port in the state the original plays the shipped recording in
// (verified per tick against tools/reference): 320x200 game logic (g_video_mode_flags 1, the mode
// movie 0 was recorded in), pointer centred, and the "user input aborts the movie" check
// (input_changed_34090) off.
void sim_prepare_movie();
const char *sim_game_dir();

// Loads campaign level `index` (0-based) and runs the level start of level_load_and_init_3d3b0:
// terrain_build, feature generation, Thing spawning (switch_activate(0)), player records.
bool sim_load_level(int index);
// Phase 4 (round 10): the same level start from an in-memory LevelData (no levels.tab entry): the per-level
// reset, then everything from "Generate map" on. `index` becomes Config.level. sim_load_level(index) =
// unpack levels.dat entry `index` + this. A mode level is loaded under MODE_LEVEL_INDEX (mode.h); when a
// mode is active, mode_level_start() runs right after the player records.
bool sim_load_level_data(const LevelData &data, int index);
// Where sim_load_level gets a level that is not in levels.dat (a generated mode level, and its restart
// through level_finish): asked first for every index; true = `out` filled, use it. Null = levels.dat only.
extern bool (*g_hook_level_source)(int index, LevelData *out);

// Loads the demo recorder's snapshot pair, e.g. ("movie/gam00000.dat", "movie/map00000.dat"): the raw
// GameState (pointers converted to the port's indices) plus the five maps, 413 ticks into level 38.
bool sim_load_snapshot(const char *gam_rel, const char *map_rel);
