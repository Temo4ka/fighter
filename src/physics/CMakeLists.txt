fighter_add_module(physics
    SOURCES
        world.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::debug
    PRIVATE_DEPS
        box2d   # PRIVATE: Box2D не виден модулям, которые зависят от physics
)
