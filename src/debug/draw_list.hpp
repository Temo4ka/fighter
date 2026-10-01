//===- debug/draw_list.hpp - Storage for debug primitives -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares Primitive, one debug shape in world coordinates, and
/// DrawList, which stores the primitives of one simulation step together with
/// the text panel and the event log.
///
/// Primitives are cleared at the start of every step (beginTick), so the
/// screen always shows data from the last step and freezes together with the
/// simulation while paused. Panel lines are not cleared: a value stays until
/// it is overwritten.
///
/// Modules normally do not use DrawList directly; they call the debug::draw*
/// functions from debug/draw.hpp.
///
//===----------------------------------------------------------------------===//

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

enum class PrimitiveKind : std::uint8_t { Line, Arrow, Circle, Arc, Poly, Point, Cross, Text };

/// One debug primitive in world coordinates. The meaning of the fields depends
/// on Kind.
struct Primitive {
    PrimitiveKind Kind = PrimitiveKind::Line;
    Cat Category = Cat::Static;
    Side Owner = Side::None;

    Vec2 A;              ///< Line, Arrow: start. Circle, Arc, Point, Cross: center. Text: anchor.
    Vec2 B;              ///< Line: end. Arrow: vector.
    float Radius = 0.0f; ///< Circle, Arc: radius, m. Point, Cross: size, m.
    float Angle0 = 0.0f; ///< Arc: start angle, rad.
    float Angle1 = 0.0f; ///< Arc: end angle, rad.

    std::uint32_t PointsBegin = 0, PointsCount = 0;  ///< Poly: vertices in DrawList.
    std::uint32_t TextBegin = 0, TextLength = 0;     ///< Arrow, Text: label in DrawList.
};

class DrawList {
public:
    static constexpr std::size_t EventLogSize = 8;

    /// Clears the primitives of the previous step.
    void beginTick();

    void addLine(Cat C, Vec2 A, Vec2 B, Side Owner);
    void addArrow(Cat C, Vec2 From, Vec2 Vec, std::string_view Label, Side Owner);
    void addCircle(Cat C, Vec2 Center, float Radius, Side Owner);
    void addArc(Cat C, Vec2 Center, float Radius, float Angle0, float Angle1, Side Owner);
    void addPoly(Cat C, std::span<const Vec2> Vertices, Side Owner);
    void addPoint(Cat C, Vec2 At, float Size, Side Owner);
    void addCross(Cat C, Vec2 At, float Size, Side Owner);
    void addText(Cat C, Vec2 At, std::string_view Text, Side Owner);

    std::span<const Primitive> getPrimitives() const { return Primitives; }
    std::span<const Vec2> getPoints(const Primitive& P) const;
    std::string_view getText(const Primitive& P) const;

    /// \name Text panel
    /// Lines are shown in the order their key first appeared.
    /// @{
    void setPanel(std::string_view Key, std::string_view Value);
    void clearPanel();
    const std::vector<std::pair<std::string, std::string>>& getPanel() const { return Panel; }
    /// @}

    /// \name Event log (for example, hits): the last EventLogSize lines
    /// @{
    void logEvent(std::string_view Message);
    const std::deque<std::string>& getEvents() const { return Events; }
    /// @}

    void clearAll();

private:
    Primitive& addPrimitive(PrimitiveKind Kind, Cat C, Side Owner);
    void attachText(Primitive& P, std::string_view Text);

    std::vector<Primitive> Primitives;
    std::vector<Vec2> Points;
    std::string TextPool;

    std::vector<std::pair<std::string, std::string>> Panel;
    std::deque<std::string> Events;
};

} // namespace fighter::debug
