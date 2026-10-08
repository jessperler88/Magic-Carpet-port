# Port round 6 (task C): scripted recordings of the local player made by the port (demo.cpp recorder),
# played back by the original (tools/reference/run_player.py) - extracted/reference/player/<name>/.
#  - reference_player: reference_test's comparison of the port's playback with the original's dumps
#    (suite mode over every recording directory; SKIP / pass without dumps).
#  - reference_player_test (the default entry): record the scripts again in the port, play the new recordings back in
#    the port and compare with the record run tick by tick (the port's own recorder round trip).
mc_unit_test(reference_player_test tests/reference_player_test.cpp ${MC_SIM_ALL} mcengine/hud.cpp mcengine/ui_draw.cpp
             mcengine/raster.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
             mcengine/sprite_cache.cpp)
add_test(NAME reference_player COMMAND reference_test "${MC_GAME_DIR_CMAKE}" "${CMAKE_SOURCE_DIR}/../extracted/reference/player")
