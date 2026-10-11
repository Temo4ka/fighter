#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "combat/move_measure.hpp"
#include "editor/metrics.hpp"
#include "editor/physics_preview.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

std::string find(const std::vector<MetricLine>& Lines, const std::string& Label) {
    for (const MetricLine& Line : Lines) {
        if (Line.Label == Label) return Line.Value;
    }
    return "<missing>";
}

} // namespace

TEST_CASE("listMetrics: the rows are the same for any measure and dashes when there is nothing", "[editor][metrics]") {
    const std::vector<MetricLine> Empty = listMetrics(combat::MoveMeasure{});
    PhysicsPreview Preview({.DataDir = DataDir, .ClipName = "jab", .WeaponId = {}, .MoveId = {}, .WithDummy = true});
    const std::vector<MetricLine> Jab = listMetrics(*Preview.getMeasure());
    REQUIRE(Jab.size() == Empty.size());
    for (size_t Index = 0; Index < Jab.size(); ++Index) CHECK(Jab[Index].Label == Empty[Index].Label);
    CHECK(find(Empty, "startup") == "-");
    CHECK(find(Empty, "hit") == "-");
    CHECK(find(Empty, "problem") == "-");
}

TEST_CASE("listMetrics: a finished move gives its phases, reach, tip speed and hit", "[editor][metrics]") {
    PhysicsPreview Preview({.DataDir = DataDir, .ClipName = "jab", .WeaponId = {}, .MoveId = {}, .WithDummy = true});
    const combat::MoveMeasure& Measure = *Preview.getMeasure();
    const std::vector<MetricLine> Lines = listMetrics(Measure);
    CHECK(find(Lines, "problem") == "-");
    CHECK_THAT(find(Lines, "startup"), ContainsSubstring(" s"));
    CHECK_THAT(find(Lines, "reach"), ContainsSubstring(" m ("));
    CHECK_THAT(find(Lines, "tip speed"), ContainsSubstring("peak"));
    CHECK_THAT(find(Lines, "hit"), ContainsSubstring("damage"));
    CHECK_THAT(find(Lines, "contact"), ContainsSubstring("N*s"));
    // The same numbers as the one-line description.
    const std::string Text = combat::describeMeasure(Measure);
    CHECK_THAT(Text, ContainsSubstring("startup " + find(Lines, "startup").substr(0, 4)));

    // Without the dummy there is no hit.
    PhysicsPreview Far({.DataDir = DataDir, .ClipName = "jab", .WeaponId = {}, .MoveId = {}, .WithDummy = false});
    CHECK(find(listMetrics(*Far.getMeasure()), "hit") == "no hit");
}

TEST_CASE("listMetrics: a failed measure carries its problem", "[editor][metrics]") {
    combat::MoveMeasure Failed;
    Failed.Finished = true;
    Failed.Problem = "the move did not start";
    const std::vector<MetricLine> Lines = listMetrics(Failed);
    CHECK(find(Lines, "problem") == "the move did not start");
    CHECK(find(Lines, "startup") == "-");
}
