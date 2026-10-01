set(render_sources
    camera.cpp
    resources.cpp
    battle_renderer.cpp
)
# Отладочный слой попадает в бинарник только в debug-сборке.
if(FIGHTER_DEBUG)
    list(APPEND render_sources debug_overlay.cpp)
endif()

fighter_add_module(render
    SOURCES
        ${render_sources}
    PUBLIC_DEPS
        fighter::core
        fighter::debug
        fighter::combat   # только снимки RenderSnapshot
        SFML::Graphics
)
