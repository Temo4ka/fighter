# fighter_add_module(<name> [SOURCES ...] [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
#
# Creates the static library fighter_<name> with the alias fighter::<name>.
# A module without SOURCES (headers only for now) becomes an INTERFACE library.
# Headers are included from the src/ root: #include "core/vec2.hpp".
# Dependencies between modules are declared only here, so it is visible what
# depends on what, and an accidental reverse dependency does not build.
function(fighter_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    set(target fighter_${name})

    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
        fighter_target_options(${target})
    else()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_DEPS})
    endif()

    add_library(fighter::${name} ALIAS ${target})
endfunction()
