# Per-tick comparison with the original (tools/reference): movie 0 (the ctest gate) and, given a
# run_level.py reference directory, a campaign level. Lists its sources explicitly (the simulation plus
# the HUD's per-tick game-state writes hud_tick_state - the original renders once per tick) instead of
# linking the whole mcengine library.
mc_unit_test(reference_test tests/reference_test.cpp ${MC_SIM_ALL} mcengine/hud.cpp mcengine/ui_draw.cpp
             mcengine/raster.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
             mcengine/sprite_cache.cpp)
# Round 5: the campaign-level references of tools/reference/run_level.py (suite mode: every levelNN/ with
# an index.json under the directory; SKIP / pass when none exists) - long playbacks and the level
# generation of every level.
add_test(NAME reference_levels COMMAND reference_test "${MC_GAME_DIR_CMAKE}" "${CMAKE_SOURCE_DIR}/../extracted/reference")
add_test(NAME reference_gen COMMAND reference_test "${MC_GAME_DIR_CMAKE}" "${CMAKE_SOURCE_DIR}/../extracted/reference/gen")
