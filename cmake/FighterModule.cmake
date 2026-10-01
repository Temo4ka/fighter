# fighter_add_module(<name> [SOURCES ...] [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
#
# Создаёт статическую библиотеку fighter_<name> с псевдонимом fighter::<name>.
# Модуль без SOURCES (пока только заголовки) становится INTERFACE-библиотекой.
# Заголовки подключаются от корня src/: #include "core/vec2.hpp".
# Зависимости между модулями задаются только здесь — так видно, что от чего зависит,
# и случайная обратная зависимость не соберётся.
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
