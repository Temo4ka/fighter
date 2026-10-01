fighter_add_module(physics
    SOURCES
        world.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::debug
    PRIVATE_DEPS
        box2d   # PRIVATE: modules that depend on physics do not see Box2D
)
