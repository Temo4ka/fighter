fighter_add_module(anim
    SOURCES
        clip.cpp
    PUBLIC_DEPS
        fighter::core
    PRIVATE_DEPS
        nlohmann_json::nlohmann_json
)
