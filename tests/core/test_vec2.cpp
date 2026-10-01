#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "core/vec2.hpp"

using fighter::Vec2;
using Catch::Approx;

TEST_CASE("Vec2: arithmetic", "[core][vec2]") {
    constexpr Vec2 A{1.0f, 2.0f};
    constexpr Vec2 B{3.0f, -1.0f};
    STATIC_REQUIRE(A + B == Vec2{4.0f, 1.0f});
    STATIC_REQUIRE(A - B == Vec2{-2.0f, 3.0f});
    STATIC_REQUIRE(A * 2.0f == Vec2{2.0f, 4.0f});
    STATIC_REQUIRE(2.0f * A == Vec2{2.0f, 4.0f});
    STATIC_REQUIRE(-A == Vec2{-1.0f, -2.0f});
    STATIC_REQUIRE(fighter::dot(A, B) == 1.0f);
    STATIC_REQUIRE(fighter::cross(Vec2{1, 0}, Vec2{0, 1}) == 1.0f);
    STATIC_REQUIRE(fighter::perp(Vec2{1, 0}) == Vec2{0, 1});
}

TEST_CASE("Vec2: length and normalization", "[core][vec2]") {
    REQUIRE(Vec2{3.0f, 4.0f}.getLength() == Approx(5.0f));
    const Vec2 N = Vec2{3.0f, 4.0f}.getNormalized();
    CHECK(N.X == Approx(0.6f));
    CHECK(N.Y == Approx(0.8f));
    CHECK(Vec2{}.getNormalized() == Vec2{});   // no NaN
}

TEST_CASE("Vec2: rotation is counter-clockwise", "[core][vec2]") {
    const Vec2 R = fighter::rotate({1.0f, 0.0f}, std::numbers::pi_v<float> / 2.0f);
    CHECK(R.X == Approx(0.0f).margin(1e-6));
    CHECK(R.Y == Approx(1.0f));
}
