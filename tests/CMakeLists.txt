# TEST_CASE names are ASCII only: ctest passes the test name to Catch2 on the
# command line, and on Windows the arguments arrive in the ANSI code page, so
# Cyrillic turns into "????" and the test is not found.
add_executable(fighter_tests
    core/test_vec2.cpp
    core/test_signal.cpp
    core/test_fixed_step_loop.cpp
    debug/test_draw_list.cpp
    physics/test_world.cpp
    rig/test_rig_def.cpp
    rig/test_rig.cpp
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
    fighter::rig
    fighter::anim
    fighter::stats
    fighter::combat
    fighter::render
    fighter::app_lib
)
fighter_target_options(fighter_tests)
# Rigs and clips for the tests that build real fighters.
target_compile_definitions(fighter_tests PRIVATE FIGHTER_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

include(Catch)
# PRE_TEST: the test list is collected when ctest runs, not at build time.
catch_discover_tests(fighter_tests DISCOVERY_MODE PRE_TEST)
