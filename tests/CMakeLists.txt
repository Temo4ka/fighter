add_executable(fighter_tests
    core/test_vec2.cpp
    core/test_signal.cpp
    core/test_fixed_step_loop.cpp
    debug/test_draw_list.cpp
    physics/test_world.cpp
    stats/test_stats.cpp
    combat/test_battle.cpp
    combat/test_snapshot.cpp
    render/test_camera.cpp
    app/test_input.cpp
)

target_link_libraries(fighter_tests PRIVATE
    Catch2::Catch2WithMain
    fighter::core
    fighter::debug
    fighter::physics
    fighter::stats
    fighter::combat
    fighter::render
    fighter::app_lib
)
fighter_target_options(fighter_tests)

include(Catch)
# PRE_TEST: список тестов собирается при запуске ctest, а не при сборке.
catch_discover_tests(fighter_tests DISCOVERY_MODE PRE_TEST)
