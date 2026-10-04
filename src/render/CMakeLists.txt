set(render_sources
    camera.cpp
    resources.cpp
    visuals.cpp
    sprites.cpp
    render_list.cpp
    effects.cpp
    scene.cpp
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
        fighter::combat   # only RenderSnapshot, BattleEvent and FighterConfig
        SFML::Graphics
    PRIVATE_DEPS
        nlohmann_json::nlohmann_json
)
