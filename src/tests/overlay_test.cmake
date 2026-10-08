# Round 10 (task C): debug overlays and the Thing inspector over level 38 (PPMs into argv[2] /
# MC_OVERLAY_OUT, default the current directory); nothing in the game state changes while drawing.
mc_unit_test(overlay_test tests/overlay_test.cpp ${MC_SIM_ALL}
  mcengine/engine.cpp mcengine/tables.cpp mcengine/render_landscape.cpp mcengine/render_things.cpp
  mcengine/sprite_cache.cpp mcengine/raster.cpp mcengine/hud.cpp mcengine/ui_draw.cpp
  mcengine/render_ext.cpp mcengine/render_ext_raster.cpp mcengine/compose.cpp mcengine/debug_overlay.cpp)
