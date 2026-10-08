# fli_test: FLI player, cue scripts, palette fades / effects (round 5, task C). Links the simulation (sound.cpp
# for the cue scripts, text.cpp for the subtitles) and the 2D layer for the subtitle font.
mc_unit_test(fli_test tests/fli_test.cpp mcengine/fli.cpp mcengine/ui_draw.cpp ${MC_SIM_ALL})
