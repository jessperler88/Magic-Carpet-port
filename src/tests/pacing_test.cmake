mc_unit_test(pacing_test tests/pacing_test.cpp ${MC_SIM_ALL} mcport/pacing.cpp)
target_include_directories(pacing_test PRIVATE mcport)
# The headless mcport run uses the executable of the same build (not a build dependency: build mcport
# first; without it that part SKIPs).
target_compile_definitions(pacing_test PRIVATE MCPORT_EXE="$<TARGET_FILE:mcport>")
