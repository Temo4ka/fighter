# Third-party libraries. All of them are downloaded at configure time
# (FetchContent) and verified by SHA256, so no system installs are needed.
#
# A new library is added only after agreement (see docs/DEVELOPMENT_PLAN.md).

include(FetchContent)

# --- SFML 3: window, graphics, input -------------------------------------------
set(SFML_BUILD_AUDIO   OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SFML
    URL      https://github.com/SFML/SFML/archive/refs/tags/3.1.0.tar.gz
    URL_HASH SHA256=91209a112c2bd0bc6f4ce0d5f3e413cfb48b57c0de59f5507dc81f71b1ad7a5c
    SYSTEM
    EXCLUDE_FROM_ALL
)

# --- Box2D v3: rigid body and joint physics ------------------------------------
FetchContent_Declare(box2d
    URL      https://github.com/erincatto/box2d/archive/refs/tags/v3.1.1.tar.gz
    URL_HASH SHA256=fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4
    SYSTEM
    EXCLUDE_FROM_ALL
)

# --- nlohmann/json: configs (rigs, poses, balance) -----------------------------
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(SFML box2d nlohmann_json)

# --- Dear ImGui + ImGui-SFML: the pose editor panels (debug build only) -------
# ImGui-SFML 3.0 is tested with ImGui 1.91; ImGui 1.92 changed the font backend.
if(FIGHTER_DEBUG)
    FetchContent_Declare(imgui
        URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
        URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_DIR ${imgui_SOURCE_DIR} CACHE PATH "" FORCE)
    set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(imgui-sfml
        URL      https://github.com/SFML/imgui-sfml/archive/refs/tags/v3.0.tar.gz
        URL_HASH SHA256=561a04407dcc913b8f1c25a164ad4907852b51fb4f11907d1ab9db6351af911b
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(imgui-sfml)
endif()

# --- Catch2: tests -------------------------------------------------------------
if(FIGHTER_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz
        URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
