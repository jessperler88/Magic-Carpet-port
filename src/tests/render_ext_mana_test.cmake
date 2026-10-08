# render_ext_mana_test: far (asleep) mana balls drawn in their current owner's colour by the extended renderer.
mc_unit_test(render_ext_mana_test tests/render_ext_mana_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp
  mcengine/render_ext.cpp mcengine/render_ext_raster.cpp)
