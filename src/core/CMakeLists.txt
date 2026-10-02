fighter_add_module(core
    SOURCES
        log.cpp
        fixed_step_loop.cpp
        text_file.cpp
)

# Every module depends on core, so they all see FIGHTER_DEBUG=1 or 0.
target_compile_definitions(fighter_core PUBLIC FIGHTER_DEBUG=$<BOOL:${FIGHTER_DEBUG}>)
