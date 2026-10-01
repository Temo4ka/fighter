#include "combat/battle.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "debug/draw.hpp"

namespace fighter::combat {
namespace {

// Параметры кинематической заглушки. В фазе 2 их заменит физика тела.
constexpr float kWalkSpeed = 3.0f;    // м/с
constexpr float kJumpSpeed = 5.0f;    // м/с
constexpr float kHalfWidth = 0.25f;   // м
constexpr float kHeight = 1.8f;       // м
constexpr float kStartX = 2.0f;       // м от центра арены

[[maybe_unused]] const char* sideName(std::size_t i) { return i == 0 ? "P1" : "P2"; }

} // namespace

Battle::Battle(const BattleConfig& config) : config_(config) {
    const stats::BalanceTable balance = stats::BalanceTable::defaults();
    const std::array<const FighterConfig*, 2> cfg = {&config_.left, &config_.right};

    for (std::size_t i = 0; i < fighters_.size(); ++i) {
        FighterState& f = fighters_[i];
        f.profile = stats::computeProfile(cfg[i]->stats, cfg[i]->loadout, balance);
        f.hp = f.profile.maxHp;
        f.position = {i == 0 ? -kStartX : kStartX, 0.0f};
        f.facingRight = (i == 0);
    }
    publishSnapshot();
}

void Battle::update(const PlayerCommands& left, const PlayerCommands& right, double dt) {
    if (result_) return;

    const float fdt = static_cast<float>(dt);
    updateFighter(fighters_[0], left, fdt);
    updateFighter(fighters_[1], right, fdt);

    // Бойцы смотрят друг на друга.
    fighters_[0].facingRight = fighters_[0].position.x <= fighters_[1].position.x;
    fighters_[1].facingRight = !fighters_[0].facingRight;

    elapsedSec_ += dt;
    ++tick_;

    if (elapsedSec_ >= config_.roundTimeSec) {
        const float hl = fighters_[0].hp;
        const float hr = fighters_[1].hp;
        finish(hl > hr ? Winner::Left : hr > hl ? Winner::Right : Winner::Draw);
    }

    publishSnapshot();
    drawDebug();
}

void Battle::updateFighter(FighterState& f, const PlayerCommands& cmd, float dt) {
    f.velocity.x = std::clamp(cmd.moveX, -1.0f, 1.0f) * kWalkSpeed;
    if (cmd.jump && f.grounded) {
        f.velocity.y = kJumpSpeed;
        f.grounded = false;
    }
    if (!f.grounded) f.velocity += config_.arena.gravity * dt;

    f.position += f.velocity * dt;

    if (f.position.y <= 0.0f) {
        f.position.y = 0.0f;
        f.velocity.y = 0.0f;
        f.grounded = true;
    }
    const float limit = config_.arena.halfWidthM - kHalfWidth;
    f.position.x = std::clamp(f.position.x, -limit, limit);
}

void Battle::finish(Winner winner) {
    BattleResult r;
    r.winner = winner;
    r.timeSec = elapsedSec_;
    r.left.hpLeft = fighters_[0].hp;
    r.right.hpLeft = fighters_[1].hp;
    result_ = r;
}

void Battle::publishSnapshot() {
    snapshot_.tick = tick_;
    snapshot_.timeLeftSec = std::max(0.0, config_.roundTimeSec - elapsedSec_);
    snapshot_.arena.halfWidthM = config_.arena.halfWidthM;
    for (std::size_t i = 0; i < fighters_.size(); ++i) {
        const FighterState& f = fighters_[i];
        FighterView& v = snapshot_.fighters[i];
        v.position = f.position;
        v.size = {2.0f * kHalfWidth, kHeight};
        v.facingRight = f.facingRight;
        v.hp = f.hp;
        v.maxHp = f.profile.maxHp;
    }
}

void Battle::drawDebug() const {
    if constexpr (FIGHTER_DEBUG) {
        const float hw = config_.arena.halfWidthM;
        debug::line(debug::Cat::Static, {-hw, 0.0f}, {hw, 0.0f});
        debug::line(debug::Cat::Static, {-hw, 0.0f}, {-hw, 4.0f});
        debug::line(debug::Cat::Static, {hw, 0.0f}, {hw, 4.0f});

        for (std::size_t i = 0; i < fighters_.size(); ++i) {
            const FighterState& f = fighters_[i];
            debug::ScopedSide side(i == 0 ? debug::Side::Left : debug::Side::Right);

            const Vec2 p = f.position;
            const std::array<Vec2, 4> box = {
                Vec2{p.x - kHalfWidth, p.y},
                Vec2{p.x + kHalfWidth, p.y},
                Vec2{p.x + kHalfWidth, p.y + kHeight},
                Vec2{p.x - kHalfWidth, p.y + kHeight},
            };
            debug::poly(debug::Cat::Hurtbox, box);

            const Vec2 com = p + Vec2{0.0f, kHeight * 0.55f};
            debug::cross(debug::Cat::CoM, com);
            if (f.velocity.lengthSquared() > 1e-4f) {
                debug::arrow(debug::Cat::Velocity, com, f.velocity * 0.2f,
                             std::format("{:.1f} m/s", f.velocity.length()));
            }

            debug::panel(std::format("{} pos", sideName(i)),
                         std::format("({:+.2f}, {:+.2f}) m  {}", p.x, p.y, f.grounded ? "ground" : "air"));
            debug::panel(std::format("{} hp", sideName(i)),
                         std::format("{:.0f} / {:.0f}", f.hp, f.profile.maxHp));
        }
        debug::panel("round", std::format("{:.1f} s left", snapshot_.timeLeftSec));
    }
}

} // namespace fighter::combat
