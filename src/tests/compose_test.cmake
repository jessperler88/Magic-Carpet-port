mc_test(compose_test tests/compose_test.cpp)
# Timing of the composed present (SDL; not part of the ctest gate - it opens windows).
add_executable(compose_present_bench tests/compose_present_bench.cpp mcport/platform_sdl.cpp)
target_include_directories(compose_present_bench PRIVATE mcport)
target_link_libraries(compose_present_bench PRIVATE mcengine
  $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
  $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
