# Public API of the combat module. No SFML: the module is embedded into a larger project.
fighter_add_module(combat
    SOURCES
        battle.cpp
        snapshot.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::stats
    PRIVATE_DEPS
        fighter::debug
        fighter::physics
        fighter::rig
        fighter::anim
)
