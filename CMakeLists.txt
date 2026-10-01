cmake_minimum_required(VERSION 3.28)

project(fighter
    VERSION 0.1.0
    DESCRIPTION "Physics-based 2D combat module"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FIGHTER_DEBUG       "Отладочная визуализация и инструменты (debug-сборка)" OFF)
option(FIGHTER_SANITIZE    "AddressSanitizer + UBSan (только clang/gcc)"         OFF)
option(FIGHTER_BUILD_APP   "Собирать песочницу fighter_app"                      ON)
option(FIGHTER_BUILD_TESTS "Собирать тесты"                                      ON)

include(cmake/CompilerOptions.cmake)
include(cmake/FighterModule.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(FIGHTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
