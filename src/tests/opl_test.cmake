mc_unit_test(opl_test tests/opl_test.cpp ${MC_SIM_ALL} mcport/audio_mixer.cpp mcport/opl_chip.cpp mcport/opl_driver.cpp)
target_include_directories(opl_test PRIVATE mcport)
