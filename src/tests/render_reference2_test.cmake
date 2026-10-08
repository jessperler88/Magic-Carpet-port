# Round 6 (task D): pixel comparison with the original in 640x480 and with the render options, the help
# screen and the credits roll switched on in turn (tools/reference/fb: patch_carpet.py --hires / --schedule,
# docs/analysis/port_render_reference2.md). Same source list as render_reference_test.
mc_unit_test(render_reference2_test tests/render_reference2_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp)
# The default entry (render_reference2_test) runs the 640x480 set (movie0_fb640); the option set:
add_test(NAME render_reference_options COMMAND render_reference2_test "${MC_GAME_DIR_CMAKE}"
  "${CMAKE_SOURCE_DIR}/../extracted/reference/movie0_fbopt")
