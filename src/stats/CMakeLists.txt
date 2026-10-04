fighter_add_module(stats
    SOURCES
        stats.cpp
        validation.cpp
        equipment.cpp
        fighter_sheet.cpp
        loading.cpp
        describe.cpp
    PUBLIC_DEPS
        fighter::core
    # PRIVATE: json types stay out of the public headers.
    PRIVATE_DEPS
        nlohmann_json::nlohmann_json
)
