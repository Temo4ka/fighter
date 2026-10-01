#pragma once

#include <span>
#include <string_view>

#include "core/vec2.hpp"
#include "debug/category.hpp"

// Отладочная визуализация — API для всех модулей (docs/DEVELOPMENT_PLAN.md §3.5).
//
//     debug::arrow(debug::Cat::Forces, hitPoint, impulse * 0.05f, "J=34.1");
//     debug::panel("P1 state", "Attack/active");
//
//     {   // всё, что нарисовано внутри, относится к левому бойцу (цвет Hurtbox)
//         debug::ScopedSide side(debug::Side::Left);
//         debug::poly(debug::Cat::Hurtbox, corners);
//     }
//
// Координаты мировые: метры, Y вверх. Примитивы живут до начала следующего шага симуляции.
//
// В release (FIGHTER_DEBUG=0) все функции пустые и inline — компилятор выбрасывает
// и вызовы, и вычисление аргументов без побочных эффектов. Глобальный приёмник здесь —
// осознанное исключение из правила «без глобального состояния»: только для отладки.
namespace fighter::debug {

class DrawList;

#if FIGHTER_DEBUG

// Глобальный приёмник, который рисует рендер.
DrawList& drawList();

// Вызывается в начале каждого шага симуляции: очищает примитивы прошлого шага.
void beginTick();

void line  (Cat cat, Vec2 a, Vec2 b);
void arrow (Cat cat, Vec2 from, Vec2 vec, std::string_view label = {});
void circle(Cat cat, Vec2 center, float radius);
void arc   (Cat cat, Vec2 center, float radius, float angle0, float angle1);
void poly  (Cat cat, std::span<const Vec2> points);
void point (Cat cat, Vec2 at, float size = 0.04f);
void cross (Cat cat, Vec2 at, float size = 0.12f);
void text  (Cat cat, Vec2 at, std::string_view text);

// Строка текстовой панели: перезаписывается по ключу.
void panel(std::string_view key, std::string_view value);
// Строка журнала событий (последние несколько).
void event(std::string_view message);

class ScopedSide {
public:
    explicit ScopedSide(Side side);
    ~ScopedSide();
    ScopedSide(const ScopedSide&) = delete;
    ScopedSide& operator=(const ScopedSide&) = delete;

private:
    Side previous_;
};

#else

inline void beginTick() {}

inline void line  (Cat, Vec2, Vec2) {}
inline void arrow (Cat, Vec2, Vec2, std::string_view = {}) {}
inline void circle(Cat, Vec2, float) {}
inline void arc   (Cat, Vec2, float, float, float) {}
inline void poly  (Cat, std::span<const Vec2>) {}
inline void point (Cat, Vec2, float = 0.04f) {}
inline void cross (Cat, Vec2, float = 0.12f) {}
inline void text  (Cat, Vec2, std::string_view) {}

inline void panel(std::string_view, std::string_view) {}
inline void event(std::string_view) {}

class ScopedSide {
public:
    explicit ScopedSide(Side) {}
};

#endif

} // namespace fighter::debug
