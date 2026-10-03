# Public API of the combat module. No SFML: the module is embedded into a larger project.
fighter_add_module(combat
    SOURCES
        battle.cpp
        fighter.cpp
        moves.cpp
        snapshot.cpp
        tuning.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::stats
        fighter::physics   # only the HitEvent contract (events.hpp); Box2D stays private to physics
    PRIVATE_DEPS
        fighter::debug
        fighter::rig
        fighter::anim
        nlohmann_json::nlohmann_json
)
