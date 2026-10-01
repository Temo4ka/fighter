#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "combat/snapshot.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

TEST_CASE("interpolate: позиции между кадрами", "[combat][snapshot]") {
    RenderSnapshot prev, curr;
    prev.fighters[0].position = {0.0f, 0.0f};
    curr.fighters[0].position = {1.0f, 2.0f};
    curr.fighters[0].hp = 50.0f;

    const auto mid = interpolate(prev, curr, 0.25f);
    CHECK(mid.fighters[0].position.x == Approx(0.25f));
    CHECK(mid.fighters[0].position.y == Approx(0.5f));
    CHECK(mid.fighters[0].hp == 50.0f);   // дискретные поля берутся из текущего кадра
}

TEST_CASE("interpolate: угол идёт по кратчайшей дуге", "[combat][snapshot]") {
    constexpr float pi = std::numbers::pi_v<float>;
    RenderSnapshot prev, curr;
    prev.fighters[0].parts = {PartTransform{BodyPart::Head, {}, 0.9f * pi}};
    curr.fighters[0].parts = {PartTransform{BodyPart::Head, {}, -0.9f * pi}};

    const float mid = interpolate(prev, curr, 0.5f).fighters[0].parts[0].angle;
    // Через ±π, а не через 0: середина около π по модулю.
    CHECK(std::abs(std::remainder(mid - pi, 2.0f * pi)) < 0.01f);
}
