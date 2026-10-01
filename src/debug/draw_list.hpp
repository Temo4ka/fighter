#pragma once

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/vec2.hpp"
#include "debug/category.hpp"

namespace fighter::debug {

// Один отладочный примитив в мировых координатах.
struct Primitive {
    enum class Kind : std::uint8_t { Line, Arrow, Circle, Arc, Poly, Point, Cross, Text };

    Kind kind = Kind::Line;
    Cat cat = Cat::Static;
    Side side = Side::None;

    Vec2 a;              // Line: начало; Arrow: начало; Circle/Arc/Point/Cross: центр; Text: якорь
    Vec2 b;              // Line: конец; Arrow: вектор
    float radius = 0.0f; // Circle/Arc: радиус, м; Point/Cross: размер, м
    float angle0 = 0.0f; // Arc: начальный угол, рад
    float angle1 = 0.0f; // Arc: конечный угол, рад

    std::uint32_t pointsBegin = 0, pointsCount = 0;   // Poly: вершины в DrawList
    std::uint32_t textBegin = 0, textLength = 0;      // Arrow/Text: подпись в DrawList
};

// Хранилище отладочных данных одного шага симуляции и текстовой панели.
//
// Примитивы очищаются в начале каждого шага (beginTick) — на экране всегда данные
// последнего шага, а на паузе они «замирают» вместе с симуляцией.
// Строки панели не очищаются: значение живёт, пока его не перезапишут.
class DrawList {
public:
    static constexpr std::size_t kEventLogSize = 8;

    void beginTick();

    void line(Cat cat, Vec2 a, Vec2 b, Side side);
    void arrow(Cat cat, Vec2 from, Vec2 vec, std::string_view label, Side side);
    void circle(Cat cat, Vec2 center, float radius, Side side);
    void arc(Cat cat, Vec2 center, float radius, float angle0, float angle1, Side side);
    void poly(Cat cat, std::span<const Vec2> points, Side side);
    void point(Cat cat, Vec2 at, float size, Side side);
    void cross(Cat cat, Vec2 at, float size, Side side);
    void text(Cat cat, Vec2 at, std::string_view text, Side side);

    std::span<const Primitive> primitives() const { return primitives_; }
    std::span<const Vec2> points(const Primitive& p) const;
    std::string_view text(const Primitive& p) const;

    // --- Текстовая панель ---
    // Строки выводятся в порядке первого появления ключа.
    void setPanel(std::string_view key, std::string_view value);
    void clearPanel();
    const std::vector<std::pair<std::string, std::string>>& panel() const { return panel_; }

    // --- Журнал событий (например, попаданий): последние kEventLogSize строк ---
    void logEvent(std::string_view message);
    const std::deque<std::string>& events() const { return events_; }

    void clearAll();

private:
    Primitive& push(Primitive::Kind kind, Cat cat, Side side);
    void attachText(Primitive& p, std::string_view text);

    std::vector<Primitive> primitives_;
    std::vector<Vec2> points_;
    std::string textPool_;

    std::vector<std::pair<std::string, std::string>> panel_;
    std::deque<std::string> events_;
};

} // namespace fighter::debug
