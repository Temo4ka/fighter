# The generator is a library so that its geometry and drawing can be tested;
# fighter_placeholder_gen is the command line around it.
add_library(fighter_placeholder_lib STATIC placeholder_gen.cpp)
add_library(fighter::placeholder_lib ALIAS fighter_placeholder_lib)
target_include_directories(fighter_placeholder_lib PUBLIC ${PROJECT_SOURCE_DIR}/tools)
target_link_libraries(fighter_placeholder_lib
    PUBLIC
        fighter::core
        fighter::physics
        fighter::rig
        fighter::stats
        SFML::Graphics
    PRIVATE
        nlohmann_json::nlohmann_json
)
fighter_target_options(fighter_placeholder_lib)

add_executable(fighter_placeholder_gen main.cpp)
target_link_libraries(fighter_placeholder_gen PRIVATE fighter::placeholder_lib)
target_compile_definitions(fighter_placeholder_gen PRIVATE FIGHTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
fighter_target_options(fighter_placeholder_gen)
