# The menus and screens: the flow state machine (no window needed) and its
# placeholder drawing.
fighter_add_module(ui
    SOURCES
        screen_flow.cpp
        fighter_list.cpp
        ui_config.cpp
        results_text.cpp
        screen_view.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::combat
        fighter::render
        SFML::Graphics
    PRIVATE_DEPS
        nlohmann_json::nlohmann_json
)
