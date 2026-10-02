set(render_sources
    camera.cpp
    resources.cpp
    battle_renderer.cpp
)
# The debug layer is linked into the binary only in the debug build.
if(FIGHTER_DEBUG)
    list(APPEND render_sources debug_overlay.cpp)
endif()

fighter_add_module(render
    SOURCES
        ${render_sources}
    PUBLIC_DEPS
        fighter::core
        fighter::debug
        fighter::combat   # only RenderSnapshot
        SFML::Graphics
)
