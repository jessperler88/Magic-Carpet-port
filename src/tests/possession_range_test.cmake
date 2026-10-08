# possession_range_test: PortSettings::possession_range_pct (Possession lifetime and target-pick radius).
mc_unit_test(possession_range_test tests/possession_range_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp
  mcengine/render_ext.cpp mcengine/render_ext_raster.cpp)
