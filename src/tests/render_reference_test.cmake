# Pixel comparison with the original's frames (tools/reference/fb, task F of round 5). Lists the
# simulation plus the renderer / HUD sources explicitly (not the whole mcengine library).
mc_unit_test(render_reference_test tests/render_reference_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp)
# The same test on the HUD reference run (fb/patch_carpet.py --hud: flight HUD drawn during the movie).
add_test(NAME render_reference_hud COMMAND render_reference_test "${MC_GAME_DIR_CMAKE}"
  "${CMAKE_SOURCE_DIR}/../extracted/reference/movie0_fbhud")
set_tests_properties(render_reference_hud PROPERTIES ENVIRONMENT "MC_RFB_HUD=1")
