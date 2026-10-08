mc_unit_test(audio_test tests/audio_test.cpp ${MC_SIM_ALL} mcport/audio_mixer.cpp)
target_include_directories(audio_test PRIVATE mcport)
