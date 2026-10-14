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
    rig/test_rig_wrist.cpp
    anim/test_clip.cpp
    anim/test_playback.cpp
    anim/test_layers.cpp
    editor/test_arm_ik.cpp
    editor/test_arm_roles.cpp
    editor/test_pose_drag.cpp
    editor/test_pose_fk.cpp
    editor/test_clip_writer.cpp
    editor/test_clip_edit.cpp
    editor/test_ghost.cpp
    editor/test_catalog.cpp
    editor/test_physics_preview.cpp
    editor/test_editor_config.cpp
    editor/test_trajectory.cpp
    editor/test_metrics.cpp
    editor/test_clip_check.cpp
    editor/test_clip_history.cpp
    editor/test_pose_ops.cpp
    editor/test_new_clip.cpp
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
    combat/test_lunge.cpp
    combat/test_wrist.cpp
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
    fighter::editor
    fighter::stats
    fighter::combat
    fighter::render
    fighter::app_lib
    fighter::ui
    fighter::placeholder_lib
)
fighter_target_options(fighter_tests)
# The data the tests run on: data/ with the hand-tuned directories replaced by
# the frozen copies in tests/data (fighters, items, moves, movesets, poses), so
# that tuning data/ does not break the tests. Files that exist only in data/
# (the rig, combat.json, a new file) come from data/. --check-data checks the
# real data/.
set(FIGHTER_TEST_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data")
set(FIGHTER_TEST_DATA_STAMP "${CMAKE_CURRENT_BINARY_DIR}/data.stamp")
file(GLOB_RECURSE FIGHTER_TEST_DATA_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/data/*" "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
file(GLOB FIGHTER_TEST_DATA_FROZEN LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
list(TRANSFORM FIGHTER_TEST_DATA_FROZEN PREPEND "${FIGHTER_TEST_DATA_DIR}/")
add_custom_command(OUTPUT "${FIGHTER_TEST_DATA_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${FIGHTER_TEST_DATA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/data" "${FIGHTER_TEST_DATA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${FIGHTER_TEST_DATA_FROZEN}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${FIGHTER_TEST_DATA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${FIGHTER_TEST_DATA_STAMP}"
    DEPENDS ${FIGHTER_TEST_DATA_SOURCES}
    COMMENT "Assembling the test data directory")
add_custom_target(fighter_test_data DEPENDS "${FIGHTER_TEST_DATA_STAMP}")
add_dependencies(fighter_tests fighter_test_data)
target_compile_definitions(fighter_tests PRIVATE FIGHTER_DATA_DIR="${FIGHTER_TEST_DATA_DIR}")
# Tests read the sample files in data/ from the source tree.
target_compile_definitions(fighter_tests PRIVATE FIGHTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(Catch)
# PRE_TEST: the test list is collected when ctest runs, not at build time.
# Catch tags become ctest labels, so `-LE slow` skips the long scenarios.
catch_discover_tests(fighter_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS)
