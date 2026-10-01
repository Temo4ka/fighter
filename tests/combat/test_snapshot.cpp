#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "combat/snapshot.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

TEST_CASE("interpolate: positions between frames", "[combat][snapshot]") {
    RenderSnapshot Prev, Curr;
    Prev.Fighters[0].Position = {0.0f, 0.0f};
    Curr.Fighters[0].Position = {1.0f, 2.0f};
    Curr.Fighters[0].Hp = 50.0f;

    const auto Mid = interpolate(Prev, Curr, 0.25f);
    CHECK(Mid.Fighters[0].Position.X == Approx(0.25f));
    CHECK(Mid.Fighters[0].Position.Y == Approx(0.5f));
    CHECK(Mid.Fighters[0].Hp == 50.0f);   // discrete fields come from the current frame
}

TEST_CASE("interpolate: angle takes the shortest arc", "[combat][snapshot]") {
    constexpr float Pi = std::numbers::pi_v<float>;
    RenderSnapshot Prev, Curr;
    Prev.Fighters[0].Parts = {PartTransform{BodyPart::Head, {}, 0.9f * Pi}};
    Curr.Fighters[0].Parts = {PartTransform{BodyPart::Head, {}, -0.9f * Pi}};

    const float Mid = interpolate(Prev, Curr, 0.5f).Fighters[0].Parts[0].Angle;
    // Through +-pi rather than through 0: the midpoint is near pi modulo 2*pi.
    CHECK(std::abs(std::remainder(Mid - Pi, 2.0f * Pi)) < 0.01f);
}
