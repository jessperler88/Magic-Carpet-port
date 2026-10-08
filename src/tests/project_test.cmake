# Round 10 (task C): the projection export of render.h (render_project_world, anchors, picks) for the
# faithful and the extended renderer and through the compositor. Renderer sources as render_ext_test.
mc_unit_test(project_test tests/project_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp
  mcengine/render_ext.cpp mcengine/render_ext_raster.cpp mcengine/compose.cpp)
