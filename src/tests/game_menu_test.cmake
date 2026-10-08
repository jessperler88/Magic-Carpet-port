# game_menu_test (round 9): the in-level pause menu (mcport/game_menu.*) - navigation, options, slots,
# config_set_keys (mcport.ini in place), drawing (screenshots). SDL-free like config_test.
mc_test(game_menu_test tests/game_menu_test.cpp)
target_sources(game_menu_test PRIVATE mcport/game_menu.cpp mcport/config.cpp mcport/gamepad.cpp)
target_include_directories(game_menu_test PRIVATE mcport
    $<TARGET_PROPERTY:$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(game_menu_test PRIVATE MC_GAMEPAD_NO_SDL)
