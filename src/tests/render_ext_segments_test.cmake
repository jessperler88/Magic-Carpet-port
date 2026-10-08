# render_ext_segments_test: sleeping dragon / worm segments in the extended renderer (render_ext.cpp thing_pos).
mc_unit_test(render_ext_segments_test tests/render_ext_segments_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp
  mcengine/render_ext.cpp mcengine/render_ext_raster.cpp)
