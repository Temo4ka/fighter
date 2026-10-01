# The order only matters for readability: dependencies go top-down
# (docs/DEVELOPMENT_PLAN.md, section 4).
add_subdirectory(core)
add_subdirectory(debug)
add_subdirectory(physics)
add_subdirectory(rig)
add_subdirectory(anim)
add_subdirectory(stats)
add_subdirectory(combat)
add_subdirectory(render)

if(FIGHTER_BUILD_APP)
    add_subdirectory(app)
endif()
