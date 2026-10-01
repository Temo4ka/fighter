#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "debug/draw.hpp"
#include "debug/draw_list.hpp"
#include "debug/palette.hpp"

using namespace fighter;
using debug::Cat;
using debug::DrawList;
using debug::PrimitiveKind;
using debug::Side;

TEST_CASE("DrawList: primitives keep data and labels", "[debug]") {
    DrawList List;
    List.addLine(Cat::Static, {0, 0}, {1, 0}, Side::None);
    List.addArrow(Cat::Forces, {0, 1}, {0.5f, 0}, "J=3", Side::Left);
    const std::array<Vec2, 3> Triangle = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}};
    List.addPoly(Cat::Hurtbox, Triangle, Side::Right);

    const auto Prims = List.getPrimitives();
    REQUIRE(Prims.size() == 3);
    CHECK(Prims[0].Kind == PrimitiveKind::Line);
    CHECK(Prims[1].Category == Cat::Forces);
    CHECK(Prims[1].Owner == Side::Left);
    CHECK(List.getText(Prims[1]) == "J=3");
    CHECK(List.getText(Prims[0]).empty());
    REQUIRE(List.getPoints(Prims[2]).size() == 3);
    CHECK(List.getPoints(Prims[2])[2] == Vec2{0, 1});
}

TEST_CASE("DrawList: beginTick clears primitives but not the panel", "[debug]") {
    DrawList List;
    List.addPoint(Cat::Contacts, {0, 0}, 0.1f, Side::None);
    List.setPanel("fps", "60");
    List.beginTick();
    CHECK(List.getPrimitives().empty());
    REQUIRE(List.getPanel().size() == 1);
    CHECK(List.getPanel()[0].second == "60");
}

TEST_CASE("DrawList: panel line is overwritten by key and keeps its order", "[debug]") {
    DrawList List;
    List.setPanel("a", "1");
    List.setPanel("b", "2");
    List.setPanel("a", "3");
    REQUIRE(List.getPanel().size() == 2);
    CHECK(List.getPanel()[0].first == "a");
    CHECK(List.getPanel()[0].second == "3");
}

TEST_CASE("DrawList: event log is bounded", "[debug]") {
    DrawList List;
    for (int I = 0; I < 20; ++I) List.logEvent(std::to_string(I));
    REQUIRE(List.getEvents().size() == DrawList::EventLogSize);
    CHECK(List.getEvents().back() == "19");
}

TEST_CASE("Palette: Hurtbox color differs per fighter", "[debug]") {
    const auto L = debug::getColor(Cat::Hurtbox, Side::Left);
    const auto R = debug::getColor(Cat::Hurtbox, Side::Right);
    CHECK((L.R != R.R || L.G != R.G || L.B != R.B));
    CHECK(debug::getFillColor(Cat::Hitbox).A < debug::getColor(Cat::Hitbox).A);
}

#if FIGHTER_DEBUG
TEST_CASE("debug API: global sink and ScopedSide", "[debug]") {
    debug::getDrawList().clearAll();
    debug::drawLine(Cat::Static, {0, 0}, {1, 1});
    {
        debug::ScopedSide Owner(Side::Right);
        debug::drawCross(Cat::CoM, {0, 1});
    }
    debug::drawPoint(Cat::Contacts, {0, 0});

    const auto Prims = debug::getDrawList().getPrimitives();
    REQUIRE(Prims.size() == 3);
    CHECK(Prims[0].Owner == Side::None);
    CHECK(Prims[1].Owner == Side::Right);
    CHECK(Prims[2].Owner == Side::None);   // ScopedSide restored the previous side

    debug::beginTick();
    CHECK(debug::getDrawList().getPrimitives().empty());
}
#endif
