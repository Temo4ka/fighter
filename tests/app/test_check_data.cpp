#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>

#include "app/check_data.hpp"
#include "../combat/scenario.hpp"

using namespace fighter;

TEST_CASE("runDataCheck: sound data exits 0 and says so", "[app][data_check]") {
    std::ostringstream Out;
    CHECK(app::runDataCheck(FIGHTER_SOURCE_DIR, Out) == 0);
    CHECK(Out.str().find("is sound") != std::string::npos);
}

TEST_CASE("runDataCheck: a broken file is printed with its name and exits non-zero", "[app][data_check]") {
    // The scratch copy is a data directory; the root is its parent.
    const combat::test::ScratchData Data("cli_check");
    Data.write("moves/broken.json", R"({"clip": "no_such_clip", "damage": 1.0, "min_reaction": "Touch", "stamina": 1})");
    const std::filesystem::path Root = Data.getDir().string() + "_root";
    std::filesystem::remove_all(Root);
    std::filesystem::create_directories(Root);
    std::filesystem::copy(Data.getDir(), Root / "data", std::filesystem::copy_options::recursive);

    std::ostringstream Out;
    CHECK(app::runDataCheck(Root, Out) != 0);
    CHECK(Out.str().find("moves/broken.json: field 'clip': clip 'no_such_clip'") != std::string::npos);
    CHECK(Out.str().find("1 problem") != std::string::npos);
    std::filesystem::remove_all(Root);
}
