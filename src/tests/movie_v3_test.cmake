# Port round 10 (task A): movie format v3 - an rts run recorded with a scripted local player and order blobs,
# replayed tick for tick; v1 / v2 recordings and the original's movie unchanged (docs/analysis/port_mode.md).
mc_unit_test(movie_v3_test tests/movie_v3_test.cpp ${MC_SIM_ALL})
