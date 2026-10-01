#include "app/debug_showcase.hpp"

#include <array>
#include <cstddef>
#include <numbers>

#include "debug/draw.hpp"

namespace fighter::app {

void drawDebugShowcase() {
    using debug::Cat;
    constexpr float Pi = std::numbers::pi_v<float>;

    // A grid of cells in the right half of the view: the panel occupies the top left.
    constexpr int PerRow = 6;
    constexpr float StepX = 0.95f, FirstX = -0.3f;
    constexpr std::array<float, 2> RowY = {3.9f, 2.55f};
    int Slot = 0;
    float CellX = FirstX;
    float CellY = RowY[0];
    auto Next = [&] {
        ++Slot;
        CellX = FirstX + StepX * static_cast<float>(Slot % PerRow);
        CellY = RowY[static_cast<size_t>(Slot / PerRow)];
    };

    auto Label = [&](Cat Category) { debug::drawText(Category, {CellX - 0.3f, CellY + 0.75f}, debug::getCatName(Category)); };

    {   // Hurtbox: the left and right fighters have different colors.
        Label(Cat::Hurtbox);
        {
            debug::ScopedSide Owner(debug::Side::Left);
            const std::array<Vec2, 4> Box = {Vec2{CellX - 0.3f, CellY}, Vec2{CellX - 0.02f, CellY}, Vec2{CellX - 0.02f, CellY + 0.5f},
                                             Vec2{CellX - 0.3f, CellY + 0.5f}};
            debug::drawPoly(Cat::Hurtbox, Box);
        }
        {
            debug::ScopedSide Owner(debug::Side::Right);
            const std::array<Vec2, 4> Box = {Vec2{CellX + 0.02f, CellY}, Vec2{CellX + 0.3f, CellY}, Vec2{CellX + 0.3f, CellY + 0.5f},
                                             Vec2{CellX + 0.02f, CellY + 0.5f}};
            debug::drawPoly(Cat::Hurtbox, Box);
        }
        Next();
    }
    {   // Hitbox: a fist in the active phase.
        Label(Cat::Hitbox);
        const std::array<Vec2, 4> Fist = {Vec2{CellX - 0.12f, CellY + 0.15f}, Vec2{CellX + 0.12f, CellY + 0.15f},
                                          Vec2{CellX + 0.12f, CellY + 0.35f}, Vec2{CellX - 0.12f, CellY + 0.35f}};
        debug::drawPoly(Cat::Hitbox, Fist);
        Next();
    }
    {   // Block
        Label(Cat::Block);
        const std::array<Vec2, 4> Guard = {Vec2{CellX - 0.08f, CellY}, Vec2{CellX + 0.08f, CellY}, Vec2{CellX + 0.08f, CellY + 0.55f},
                                           Vec2{CellX - 0.08f, CellY + 0.55f}};
        debug::drawPoly(Cat::Block, Guard);
        Next();
    }
    {   // Static
        Label(Cat::Static);
        debug::drawLine(Cat::Static, {CellX - 0.35f, CellY}, {CellX + 0.35f, CellY});
        debug::drawLine(Cat::Static, {CellX + 0.35f, CellY}, {CellX + 0.35f, CellY + 0.5f});
        Next();
    }
    {   // Joints: a joint and the arc of allowed angles.
        Label(Cat::Joints);
        const Vec2 Joint{CellX, CellY + 0.25f};
        debug::drawPoint(Cat::Joints, Joint);
        debug::drawArc(Cat::Joints, Joint, 0.22f, -0.25f * Pi, 0.75f * Pi);
        Next();
    }
    {   // TargetPose: the "ghost" of a limb.
        Label(Cat::TargetPose);
        debug::drawLine(Cat::TargetPose, {CellX - 0.25f, CellY + 0.1f}, {CellX, CellY + 0.4f});
        debug::drawLine(Cat::TargetPose, {CellX, CellY + 0.4f}, {CellX + 0.3f, CellY + 0.35f});
        Next();
    }
    {   // Motors: motor torque as an arc with an arrow.
        Label(Cat::Motors);
        const Vec2 Joint{CellX, CellY + 0.25f};
        debug::drawArc(Cat::Motors, Joint, 0.2f, 0.0f, 0.6f * Pi);
        debug::drawArrow(Cat::Motors, Joint + Vec2{-0.2f, 0.06f}, {0.0f, -0.12f}, "12 N*m");
        Next();
    }
    {   // Forces
        Label(Cat::Forces);
        debug::drawArrow(Cat::Forces, {CellX - 0.3f, CellY + 0.25f}, {0.6f, 0.15f}, "J=34");
        Next();
    }
    {   // Velocity
        Label(Cat::Velocity);
        debug::drawArrow(Cat::Velocity, {CellX - 0.25f, CellY + 0.1f}, {0.45f, 0.35f}, "2.8 m/s");
        Next();
    }
    {   // Contacts: a point and its normal.
        Label(Cat::Contacts);
        debug::drawPoint(Cat::Contacts, {CellX, CellY + 0.2f}, 0.05f);
        debug::drawArrow(Cat::Contacts, {CellX, CellY + 0.2f}, {0.0f, 0.35f});
        Next();
    }
    {   // CoM
        Label(Cat::CoM);
        debug::drawCross(Cat::CoM, {CellX, CellY + 0.25f});
    }
}

} // namespace fighter::app
