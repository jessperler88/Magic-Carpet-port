# mclog_test (round 10, task D): mcport's log (mcport/mclog.*): levels, file + rotation, ring buffer, sinks.
mc_unit_test(mclog_test tests/mclog_test.cpp mcport/mclog.cpp)
target_include_directories(mclog_test PRIVATE mcport)
