# DrawList (хранилище) собирается всегда — его можно тестировать в любой сборке.
# Свободные функции debug::* в release — пустые inline (см. draw.hpp),
# поэтому draw.cpp в release ничего не содержит.
fighter_add_module(debug
    SOURCES
        draw_list.cpp
        draw.cpp
    PUBLIC_DEPS
        fighter::core
)
