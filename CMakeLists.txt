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

option(FIGHTER_DEBUG       "Debug visualization and tools (debug build)"       OFF)
option(FIGHTER_SANITIZE    "AddressSanitizer + UBSan (clang/gcc only)"         OFF)
option(FIGHTER_BUILD_APP   "Build the fighter_app sandbox"                      ON)
option(FIGHTER_BUILD_TESTS "Build the tests"                                    ON)

include(cmake/CompilerOptions.cmake)
include(cmake/FighterModule.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(FIGHTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
