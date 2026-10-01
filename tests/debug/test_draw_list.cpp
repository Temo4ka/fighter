#include <catch2/catch_test_macros.hpp>

#include <array>

#include "debug/draw.hpp"
#include "debug/draw_list.hpp"
#include "debug/palette.hpp"

using namespace fighter;
using debug::Cat;
using debug::DrawList;
using debug::Primitive;
using debug::Side;

TEST_CASE("DrawList: примитивы хранят данные и подписи", "[debug]") {
    DrawList list;
    list.line(Cat::Static, {0, 0}, {1, 0}, Side::None);
    list.arrow(Cat::Forces, {0, 1}, {0.5f, 0}, "J=3", Side::Left);
    const std::array<Vec2, 3> tri = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}};
    list.poly(Cat::Hurtbox, tri, Side::Right);

    const auto prims = list.primitives();
    REQUIRE(prims.size() == 3);
    CHECK(prims[0].kind == Primitive::Kind::Line);
    CHECK(prims[1].cat == Cat::Forces);
    CHECK(prims[1].side == Side::Left);
    CHECK(list.text(prims[1]) == "J=3");
    CHECK(list.text(prims[0]).empty());
    REQUIRE(list.points(prims[2]).size() == 3);
    CHECK(list.points(prims[2])[2] == Vec2{0, 1});
}

TEST_CASE("DrawList: beginTick очищает примитивы, но не панель", "[debug]") {
    DrawList list;
    list.point(Cat::Contacts, {0, 0}, 0.1f, Side::None);
    list.setPanel("fps", "60");
    list.beginTick();
    CHECK(list.primitives().empty());
    REQUIRE(list.panel().size() == 1);
    CHECK(list.panel()[0].second == "60");
}

TEST_CASE("DrawList: строка панели перезаписывается по ключу и сохраняет порядок", "[debug]") {
    DrawList list;
    list.setPanel("a", "1");
    list.setPanel("b", "2");
    list.setPanel("a", "3");
    REQUIRE(list.panel().size() == 2);
    CHECK(list.panel()[0].first == "a");
    CHECK(list.panel()[0].second == "3");
}

TEST_CASE("DrawList: журнал событий ограничен", "[debug]") {
    DrawList list;
    for (int i = 0; i < 20; ++i) list.logEvent(std::to_string(i));
    REQUIRE(list.events().size() == DrawList::kEventLogSize);
    CHECK(list.events().back() == "19");
}

TEST_CASE("Палитра: Hurtbox различает бойцов", "[debug]") {
    const auto l = debug::color(Cat::Hurtbox, Side::Left);
    const auto r = debug::color(Cat::Hurtbox, Side::Right);
    CHECK((l.r != r.r || l.g != r.g || l.b != r.b));
    CHECK(debug::fillColor(Cat::Hitbox).a < debug::color(Cat::Hitbox).a);
}

#if FIGHTER_DEBUG
TEST_CASE("debug::*: глобальный приёмник и ScopedSide", "[debug]") {
    debug::drawList().clearAll();
    debug::line(Cat::Static, {0, 0}, {1, 1});
    {
        debug::ScopedSide side(Side::Right);
        debug::cross(Cat::CoM, {0, 1});
    }
    debug::point(Cat::Contacts, {0, 0});

    const auto prims = debug::drawList().primitives();
    REQUIRE(prims.size() == 3);
    CHECK(prims[0].side == Side::None);
    CHECK(prims[1].side == Side::Right);
    CHECK(prims[2].side == Side::None);   // ScopedSide восстановил прежнюю сторону

    debug::beginTick();
    CHECK(debug::drawList().primitives().empty());
}
#endif
