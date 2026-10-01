# Публичный API модуля боя. Без SFML: модуль встраивается в более крупный проект.
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
