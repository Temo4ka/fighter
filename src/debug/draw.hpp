//===- debug/draw.hpp - Debug drawing API -----------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the debug drawing API that every module may call
/// (docs/DEVELOPMENT_PLAN.md, section 3.5).
///
/// Coordinates are in world space: meters, Y up. Primitives live until the
/// start of the next simulation step.
///
/// \code
///   debug::drawArrow(debug::Cat::Forces, HitPoint, Impulse * 0.05f, "J=34.1");
///   debug::setPanel("P1 state", "Attack/active");
///
///   {   // everything drawn inside belongs to the left fighter (Hurtbox color)
///       debug::ScopedSide Owner(debug::Side::Left);
///       debug::drawPoly(debug::Cat::Hurtbox, Corners);
///   }
/// \endcode
///
/// In the release build (FIGHTER_DEBUG=0) every function is an empty inline
/// function, so the compiler drops both the calls and the evaluation of
/// side-effect-free arguments. The global sink is a deliberate exception to
/// the "no global state" rule and exists only for debugging.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <string_view>

#include "core/vec2.hpp"
#include "debug/category.hpp"

namespace fighter::debug {

class DrawList;

#if FIGHTER_DEBUG

/// The global sink that the renderer draws.
DrawList& getDrawList();

/// Called at the start of every simulation step: clears the primitives of the
/// previous step.
void beginTick();

void drawLine  (Cat C, Vec2 A, Vec2 B);
void drawArrow (Cat C, Vec2 From, Vec2 Vec, std::string_view Label = {});
void drawCircle(Cat C, Vec2 Center, float Radius);
void drawArc   (Cat C, Vec2 Center, float Radius, float Angle0, float Angle1);
void drawPoly  (Cat C, std::span<const Vec2> Vertices);
void drawPoint (Cat C, Vec2 At, float Size = 0.04f);
void drawCross (Cat C, Vec2 At, float Size = 0.12f);
void drawText  (Cat C, Vec2 At, std::string_view Text);

/// Sets a text panel line; a line with the same key is overwritten.
void setPanel(std::string_view Key, std::string_view Value);
/// Appends a line to the event log (only the last few are kept).
void logEvent(std::string_view Message);

/// Marks every primitive drawn during its lifetime as belonging to \p Owner.
class ScopedSide {
public:
    explicit ScopedSide(Side Owner);
    ~ScopedSide();
    ScopedSide(const ScopedSide&) = delete;
    ScopedSide& operator=(const ScopedSide&) = delete;

private:
    Side Previous;
};

#else

inline void beginTick() {}

inline void drawLine  (Cat, Vec2, Vec2) {}
inline void drawArrow (Cat, Vec2, Vec2, std::string_view = {}) {}
inline void drawCircle(Cat, Vec2, float) {}
inline void drawArc   (Cat, Vec2, float, float, float) {}
inline void drawPoly  (Cat, std::span<const Vec2>) {}
inline void drawPoint (Cat, Vec2, float = 0.04f) {}
inline void drawCross (Cat, Vec2, float = 0.12f) {}
inline void drawText  (Cat, Vec2, std::string_view) {}

inline void setPanel(std::string_view, std::string_view) {}
inline void logEvent(std::string_view) {}

class ScopedSide {
public:
    explicit ScopedSide(Side) {}
};

#endif

} // namespace fighter::debug
