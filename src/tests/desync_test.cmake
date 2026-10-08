# Round 10 task E: checksum parts, the parts exchange on a desync (MemLan + two TCP processes), desync dumps,
# tools/reference/diff_state.py on the dumps (docs/analysis/port_desync.md).
mc_unit_test(desync_test tests/desync_test.cpp ${MC_SIM_ALL} mcengine/replay_check.cpp mcengine/frontend.cpp
    mcengine/savegame.cpp mcengine/ui_draw.cpp mcengine/fli.cpp mcport/net_tcp.cpp)
target_include_directories(desync_test PRIVATE mcport)
target_compile_definitions(desync_test PRIVATE MC_TOOLS_DIR="${CMAKE_SOURCE_DIR}/../tools")
if(WIN32)
  target_link_libraries(desync_test PRIVATE ws2_32)
endif()
# Listen on loopback only: no Windows Firewall prompt when the TCP parts run (net_tcp.cpp MC_NET_BIND).
set_tests_properties(desync_test PROPERTIES ENVIRONMENT "MC_NET_BIND=127.0.0.1")
