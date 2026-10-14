# Command sources: human, replay and the simple AI (task 2.9). No SFML: the
# module is embedded together with combat.
fighter_add_module(ai
    SOURCES
        ai_config.cpp
        ai_controller.cpp
        controller.cpp
        repertoire.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::stats
        fighter::combat
    PRIVATE_DEPS
        fighter::debug
        nlohmann_json::nlohmann_json
)
