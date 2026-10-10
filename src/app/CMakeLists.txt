# Everything except main() is a library so that its parts (input) can be tested.
set(app_sources
    app.cpp
    check_data.cpp
    input.cpp
    demo_script.cpp
)
if(FIGHTER_DEBUG)
    list(APPEND app_sources debug_showcase.cpp pose_editor.cpp)
endif()

fighter_add_module(app_lib
    SOURCES
        ${app_sources}
    PUBLIC_DEPS
        fighter::core
        fighter::debug
        fighter::combat
        fighter::editor
        fighter::render
        fighter::ui
        SFML::Graphics
)
if(FIGHTER_DEBUG)
    target_link_libraries(fighter_app_lib PRIVATE ImGui-SFML::ImGui-SFML)
endif()

add_executable(fighter_app main.cpp)
target_link_libraries(fighter_app PRIVATE fighter::app_lib)
target_compile_definitions(fighter_app PRIVATE FIGHTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
fighter_target_options(fighter_app)
