#include "app/debug_showcase.hpp"

#include <array>
#include <numbers>

#include "debug/draw.hpp"

namespace fighter::app {

void drawDebugShowcase() {
    using debug::Cat;
    constexpr float pi = std::numbers::pi_v<float>;

    // Сетка ячеек в правой половине вида: левую верхнюю часть занимает панель.
    constexpr int kPerRow = 6;
    constexpr float kStepX = 0.95f, kFirstX = -0.3f;
    constexpr std::array<float, 2> kRowY = {3.9f, 2.55f};
    int slot = 0;
    float x = kFirstX;
    float y = kRowY[0];
    auto next = [&] {
        ++slot;
        x = kFirstX + kStepX * static_cast<float>(slot % kPerRow);
        y = kRowY[static_cast<std::size_t>(slot / kPerRow)];
    };

    auto label = [&](Cat cat) { debug::text(cat, {x - 0.3f, y + 0.75f}, debug::catName(cat)); };

    {   // Hurtbox: у левого и правого бойца разные цвета
        label(Cat::Hurtbox);
        {
            debug::ScopedSide s(debug::Side::Left);
            const std::array<Vec2, 4> box = {Vec2{x - 0.3f, y}, Vec2{x - 0.02f, y}, Vec2{x - 0.02f, y + 0.5f}, Vec2{x - 0.3f, y + 0.5f}};
            debug::poly(Cat::Hurtbox, box);
        }
        {
            debug::ScopedSide s(debug::Side::Right);
            const std::array<Vec2, 4> box = {Vec2{x + 0.02f, y}, Vec2{x + 0.3f, y}, Vec2{x + 0.3f, y + 0.5f}, Vec2{x + 0.02f, y + 0.5f}};
            debug::poly(Cat::Hurtbox, box);
        }
        next();
    }
    {   // Hitbox: кулак в активной фазе
        label(Cat::Hitbox);
        const std::array<Vec2, 4> fist = {Vec2{x - 0.12f, y + 0.15f}, Vec2{x + 0.12f, y + 0.15f}, Vec2{x + 0.12f, y + 0.35f}, Vec2{x - 0.12f, y + 0.35f}};
        debug::poly(Cat::Hitbox, fist);
        next();
    }
    {   // Block
        label(Cat::Block);
        const std::array<Vec2, 4> guard = {Vec2{x - 0.08f, y}, Vec2{x + 0.08f, y}, Vec2{x + 0.08f, y + 0.55f}, Vec2{x - 0.08f, y + 0.55f}};
        debug::poly(Cat::Block, guard);
        next();
    }
    {   // Static
        label(Cat::Static);
        debug::line(Cat::Static, {x - 0.35f, y}, {x + 0.35f, y});
        debug::line(Cat::Static, {x + 0.35f, y}, {x + 0.35f, y + 0.5f});
        next();
    }
    {   // Joints: шарнир и дуга допустимых углов
        label(Cat::Joints);
        const Vec2 j{x, y + 0.25f};
        debug::point(Cat::Joints, j);
        debug::arc(Cat::Joints, j, 0.22f, -0.25f * pi, 0.75f * pi);
        next();
    }
    {   // TargetPose: «призрак» конечности
        label(Cat::TargetPose);
        debug::line(Cat::TargetPose, {x - 0.25f, y + 0.1f}, {x, y + 0.4f});
        debug::line(Cat::TargetPose, {x, y + 0.4f}, {x + 0.3f, y + 0.35f});
        next();
    }
    {   // Motors: момент мотора — дуга со стрелкой
        label(Cat::Motors);
        const Vec2 j{x, y + 0.25f};
        debug::arc(Cat::Motors, j, 0.2f, 0.0f, 0.6f * pi);
        debug::arrow(Cat::Motors, j + Vec2{-0.2f, 0.06f}, {0.0f, -0.12f}, "12 N*m");
        next();
    }
    {   // Forces
        label(Cat::Forces);
        debug::arrow(Cat::Forces, {x - 0.3f, y + 0.25f}, {0.6f, 0.15f}, "J=34");
        next();
    }
    {   // Velocity
        label(Cat::Velocity);
        debug::arrow(Cat::Velocity, {x - 0.25f, y + 0.1f}, {0.45f, 0.35f}, "2.8 m/s");
        next();
    }
    {   // Contacts: точка и нормаль
        label(Cat::Contacts);
        debug::point(Cat::Contacts, {x, y + 0.2f}, 0.05f);
        debug::arrow(Cat::Contacts, {x, y + 0.2f}, {0.0f, 0.35f});
        next();
    }
    {   // CoM
        label(Cat::CoM);
        debug::cross(Cat::CoM, {x, y + 0.25f});
    }
}

} // namespace fighter::app
