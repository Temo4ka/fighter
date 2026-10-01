fighter_add_module(core
    SOURCES
        log.cpp
        fixed_step_loop.cpp
)

# FIGHTER_DEBUG=1/0 видят все модули: каждый из них зависит от core.
target_compile_definitions(fighter_core PUBLIC FIGHTER_DEBUG=$<BOOL:${FIGHTER_DEBUG}>)
