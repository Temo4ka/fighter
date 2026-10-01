#include "combat/snapshot.hpp"

#include <cmath>
#include <numbers>

namespace fighter::combat {
namespace {

// Интерполяция угла по кратчайшей дуге: переход 179° → -179° не должен давать оборот.
float lerpAngle(float a, float b, float t) {
    constexpr float pi = std::numbers::pi_v<float>;
    float d = std::remainder(b - a, 2.0f * pi);
    return a + d * t;
}

} // namespace

RenderSnapshot interpolate(const RenderSnapshot& prev, const RenderSnapshot& curr, float alpha) {
    RenderSnapshot out = curr;
    for (std::size_t f = 0; f < out.fighters.size(); ++f) {
        const FighterView& p = prev.fighters[f];
        FighterView& o = out.fighters[f];
        o.position = lerp(p.position, o.position, alpha);

        // Части сопоставляются по индексу; если состав поменялся — берём текущий кадр.
        if (p.parts.size() != o.parts.size()) continue;
        for (std::size_t i = 0; i < o.parts.size(); ++i) {
            o.parts[i].position = lerp(p.parts[i].position, o.parts[i].position, alpha);
            o.parts[i].angle = lerpAngle(p.parts[i].angle, o.parts[i].angle, alpha);
        }
    }
    return out;
}

} // namespace fighter::combat
