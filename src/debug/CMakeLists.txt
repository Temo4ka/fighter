# DrawList (the storage) is always built so it can be tested in any build.
# In release the debug::* free functions are empty inlines (see draw.hpp),
# so draw.cpp compiles to nothing.
fighter_add_module(debug
    SOURCES
        draw_list.cpp
        draw.cpp
    PUBLIC_DEPS
        fighter::core
)
