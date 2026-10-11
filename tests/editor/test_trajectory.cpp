#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "combat/move_measure.hpp"
#include "editor/ghost.hpp"
#include "editor/physics_preview.hpp"
#include "editor/trajectory.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

anim::Clip loadPose(const std::string& Name) { return anim::loadClip(DataDir / "poses" / (Name + ".json")); }

GhostContext makeContext(const std::string& Item) { return loadGhostContext(DataDir.parent_path(), Item); }

} // namespace

TEST_CASE("findStrikerParts: the strikers of the clip as it is played", "[editor][trajectory]") {
    anim::Clip Clip;
    CHECK(findStrikerParts(Clip).empty());
    Clip.Strikers.set(static_cast<size_t>(BodyPart::ForearmR));
    Clip.Strikers.set(static_cast<size_t>(BodyPart::FootL));
    const std::vector<BodyPart> Parts = findStrikerParts(Clip);
    REQUIRE(Parts.size() == 2);
    CHECK(Parts[0] == BodyPart::ForearmR);
    CHECK(Parts[1] == BodyPart::FootL);
}

TEST_CASE("sampleGhostPath: the tip of the jab over the clip, the active window marked", "[editor][trajectory]") {
    const GhostContext Context = makeContext("");
    const anim::Clip Jab = loadPose("jab");
    const std::optional<TipPath> Path = sampleGhostPath(Context, Jab, false, 0.02f);
    REQUIRE(Path.has_value());
    REQUIRE(Path->Points.size() > 4);
    CHECK(Path->Points.front().TimeSec == 0.0f);
    CHECK(Path->Points.back().TimeSec == Approx(Jab.DurationSec));
    CHECK(Path->TravelM > 0.2f);
    CHECK(Jab.isStriker(Path->Part));
    size_t ActivePoints = 0;
    for (const TipPoint& Point : Path->Points) {
        CHECK(Point.Active == Jab.isActiveAt(Point.TimeSec));
        if (Point.Active) ++ActivePoints;
    }
    CHECK(ActivePoints > 0);
    CHECK(ActivePoints < Path->Points.size());
    // The fist is at its farthest forward in the active phase.
    const auto Farthest = std::ranges::max_element(Path->Points, {}, [](const TipPoint& Point) { return Point.Tip.X; });
    CHECK(Jab.isActiveAt(Farthest->TimeSec + 0.02f * 0.5f));
}

TEST_CASE("sampleGhostPath: a smaller step samples more points; no strikers or a bad step give nothing", "[editor][trajectory]") {
    const GhostContext Context = makeContext("");
    const anim::Clip Jab = loadPose("jab");
    const size_t Coarse = sampleGhostPath(Context, Jab, false, 0.05f)->Points.size();
    const size_t Fine = sampleGhostPath(Context, Jab, false, 0.01f)->Points.size();
    CHECK(Fine > Coarse);
    CHECK_FALSE(sampleGhostPath(Context, Jab, false, 0.0f).has_value());
    anim::Clip NoStrikers = Jab;
    NoStrikers.Strikers.reset();
    CHECK_FALSE(sampleGhostPath(Context, NoStrikers, false, 0.02f).has_value());
}

TEST_CASE("sampleGhostPath: the tip of a weapon is beyond the fist", "[editor][trajectory]") {
    const GhostContext Context = makeContext("short_sword");
    const anim::Clip Clip = loadPose("sword_slash");
    const std::optional<TipPath> Fist = sampleGhostPath(Context, Clip, false, 0.02f);
    const std::optional<TipPath> Blade = sampleGhostPath(Context, Clip, true, 0.02f);
    REQUIRE(Fist.has_value());
    REQUIRE(Blade.has_value());
    REQUIRE(Fist->Points.size() == Blade->Points.size());
    CHECK(Fist->Part == Blade->Part);
    float Gap = 0.0f;
    for (const auto& [Near, Far] : std::views::zip(Fist->Points, Blade->Points)) {
        Gap = std::max(Gap, (Far.Tip - Near.Tip).getLength());
    }
    CHECK(Gap > 0.3f);
}

TEST_CASE("sampleFactPath: the fact's tip in clip time, the active phase marked, reaching like the measure", "[editor][trajectory]") {
    PhysicsPreview Preview({.DataDir = DataDir, .ClipName = "jab", .WeaponId = {}, .MoveId = {}, .WithDummy = false});
    const GhostContext Context = makeContext("");
    const anim::Clip Jab = loadPose("jab");
    Preview.setClip(Jab);
    const std::optional<TipPath> Ghost = sampleGhostPath(Context, Jab, false, 0.02f);
    REQUIRE(Ghost.has_value());
    const std::optional<TipPath> Fact = sampleFactPath(Preview, Ghost->Part, false);
    REQUIRE(Fact.has_value());
    CHECK(Fact->Points.size() > 10);
    CHECK(Fact->TravelM > 0.2f);
    const bool AnyActive = std::ranges::any_of(Fact->Points, &TipPoint::Active);
    CHECK(AnyActive);
    // The same run as the measure's: the farthest forward tip in the active
    // phase is the reach from the pelvis, which stands at x = 0 here.
    const combat::MoveMeasure* Measure = Preview.getMeasure();
    REQUIRE(Measure != nullptr);
    float Reach = 0.0f;
    for (const TipPoint& Point : Fact->Points) {
        if (Point.Active) Reach = std::max(Reach, Point.Tip.X);
    }
    CHECK(Reach == Approx(Measure->getReachM()).margin(0.02));
}

TEST_CASE("findTipSpeed: the speed of the tip in the active phase", "[editor][trajectory]") {
    combat::MoveMeasure Measure;
    CHECK(findTipSpeed(Measure).PeakMps == 0.0f);

    combat::StrikerPath Path;
    // 1 m/s, then 3 m/s, then 3 m/s, in the active phase; the startup is not counted.
    Path.Points = {{.TimeSec = 0.0f, .Tip = {0.0f, 1.0f}, .Phase = combat::AttackPhase::Startup},
                   {.TimeSec = 0.1f, .Tip = {5.0f, 1.0f}, .Phase = combat::AttackPhase::Active},
                   {.TimeSec = 0.2f, .Tip = {5.1f, 1.0f}, .Phase = combat::AttackPhase::Active},
                   {.TimeSec = 0.3f, .Tip = {5.4f, 1.0f}, .Phase = combat::AttackPhase::Active},
                   {.TimeSec = 0.4f, .Tip = {5.7f, 1.0f}, .Phase = combat::AttackPhase::Recovery}};
    Path.ReachM = 5.4f;
    Path.TravelM = 5.7f;
    Measure.Paths.push_back(Path);
    const TipSpeed Speed = findTipSpeed(Measure);
    CHECK(Speed.PeakMps == Approx(3.0f));
    CHECK(Speed.ActiveMeanMps == Approx(0.4f / 0.2f));
}
