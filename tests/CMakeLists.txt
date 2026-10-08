# TEST_CASE names are ASCII only: ctest passes the test name to Catch2 on the
# command line, and on Windows the arguments arrive in the ANSI code page, so
# Cyrillic turns into "????" and the test is not found.
add_executable(fighter_tests
    core/test_vec2.cpp
    core/test_log.cpp
    core/test_signal.cpp
    core/test_fixed_step_loop.cpp
    debug/test_draw_list.cpp
    physics/test_world.cpp
    rig/test_rig_def.cpp
    rig/test_rig.cpp
    rig/test_rig_body.cpp
    rig/test_rig_scenarios.cpp
    rig/test_rig_smooth.cpp
    anim/test_clip.cpp
    anim/test_playback.cpp
    anim/test_layers.cpp
    anim/test_clips_data.cpp
    stats/test_stats.cpp
    stats/test_balance.cpp
    stats/test_equipment.cpp
    stats/test_loading.cpp
    combat/test_battle.cpp
    combat/test_contact.cpp
    combat/test_no_passthrough.cpp
    combat/test_clip_library.cpp
    combat/test_data_check.cpp
    combat/test_move_measure.cpp
    combat/test_stand_config.cpp
    combat/test_fight.cpp
    combat/test_hands.cpp
    combat/test_commands.cpp
    combat/test_leg_cycle.cpp
    combat/test_leg_step.cpp
    combat/test_move_input.cpp
    combat/test_moves.cpp
    combat/test_moveset.cpp
    combat/test_movement.cpp
    combat/test_reactions.cpp
    combat/test_snapshot.cpp
    combat/test_tuning.cpp
    combat/test_spacing_smooth.cpp
    combat/test_stride.cpp
    render/test_camera.cpp
    render/test_pixel_view.cpp
    render/test_visuals.cpp
    render/test_render_list.cpp
    render/test_sprites.cpp
    render/test_scene.cpp
    render/test_effects.cpp
    render/test_battle_renderer.cpp
    app/test_input.cpp
    app/test_demo_script.cpp
    app/test_check_data.cpp
    ui/test_screen_flow.cpp
    ui/test_ui_data.cpp
    tools/test_placeholder_gen.cpp
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
    fighter::ui
    fighter::placeholder_lib
)
fighter_target_options(fighter_tests)
# Rigs and clips for the tests that build real fighters.
target_compile_definitions(fighter_tests PRIVATE FIGHTER_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
# Tests read the sample files in data/ from the source tree.
target_compile_definitions(fighter_tests PRIVATE FIGHTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(Catch)
# PRE_TEST: the test list is collected when ctest runs, not at build time.
# Catch tags become ctest labels, so `-LE slow` skips the long scenarios.
catch_discover_tests(fighter_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS)
