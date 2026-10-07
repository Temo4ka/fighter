# Public API of the combat module. No SFML: the module is embedded into a larger project.
fighter_add_module(combat
    SOURCES
        battle.cpp
        clip_library.cpp
        fighter.cpp
        leg_cycle.cpp
        leg_step.cpp
        move_input.cpp
        moves.cpp
        moveset.cpp
        reactions.cpp
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
