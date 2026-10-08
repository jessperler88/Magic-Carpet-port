# console_test (round 10, task B; docs/analysis/port_console.md): the debug command parser (every command, errors,
# help), packets byte-exactly, key-to-ASCII, line editing, the history file round trip, and the console in a
# loaded level (commands -> packets -> ticks -> scrollback; `run`; drawing: console_open.ppm). SDL-free like
# game_menu_test (only SDL_scancode.h).
mc_test(console_test tests/console_test.cpp)
target_sources(console_test PRIVATE mcport/console.cpp mcport/mclog.cpp)
target_include_directories(console_test PRIVATE mcport
    $<TARGET_PROPERTY:$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>,INTERFACE_INCLUDE_DIRECTORIES>)
