# scenario_test (round 10, task B; docs/analysis/port_console.md): every tests/scenarios/*.scn headless over the
# whole simulation (twice, identical checksums), a deliberately failing assertion reported with its line, and a
# record + replay round trip of a campaign scenario with debug packets.
mc_unit_test(scenario_test tests/scenario_test.cpp ${MC_SIM_ALL})
target_compile_definitions(scenario_test PRIVATE MC_SCENARIO_DIR="${CMAKE_SOURCE_DIR}/tests/scenarios")
