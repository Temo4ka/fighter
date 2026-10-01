# Всё приложение, кроме main(), — библиотека: так его части (ввод) можно тестировать.
set(app_sources
    app.cpp
    input.cpp
)
if(FIGHTER_DEBUG)
    list(APPEND app_sources debug_showcase.cpp)
endif()

fighter_add_module(app_lib
    SOURCES
        ${app_sources}
    PUBLIC_DEPS
        fighter::core
        fighter::debug
        fighter::combat
        fighter::render
        SFML::Graphics
)

add_executable(fighter_app main.cpp)
target_link_libraries(fighter_app PRIVATE fighter::app_lib)
target_compile_definitions(fighter_app PRIVATE FIGHTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
fighter_target_options(fighter_app)
