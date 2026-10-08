# Round 7 (task A): the extended renderer (draw distance, LOD, any view size). The simulation plus the
# renderer sources explicitly (as render_reference_test) plus the extended renderer's own files.
mc_unit_test(render_ext_test tests/render_ext_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp
  mcengine/render_ext.cpp mcengine/render_ext_raster.cpp)
