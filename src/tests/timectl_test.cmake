# timectl_test (round 10, task D): time control scheduling (mcport/timectl.*), the tick profiler
# (mcengine/tick_profile.*: the markers in player.cpp / thing.cpp change nothing), the PNG writer
# (mcport/screenshot.*) and a headless mcport run with pause / steps / speeds (identical tick checksums).
mc_unit_test(timectl_test tests/timectl_test.cpp ${MC_SIM_ALL} mcengine/tick_profile.cpp mcport/timectl.cpp mcport/screenshot.cpp)
target_include_directories(timectl_test PRIVATE mcport)
# The headless mcport run uses the executable of the same build (build mcport first; without it that part SKIPs).
target_compile_definitions(timectl_test PRIVATE MCPORT_EXE="$<TARGET_FILE:mcport>")
