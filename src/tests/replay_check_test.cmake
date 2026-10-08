# Round 10 task E: replay check (mcengine/replay_check.h) - movie 0 twice, a perturbed second pass caught at
# the right tick, a port v2 movie, a save state (docs/analysis/port_desync.md).
mc_unit_test(replay_check_test tests/replay_check_test.cpp ${MC_SIM_ALL} mcengine/replay_check.cpp mcengine/frontend.cpp
    mcengine/savegame.cpp mcengine/ui_draw.cpp mcengine/fli.cpp)
