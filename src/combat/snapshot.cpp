#include "combat/snapshot.hpp"

#include <cmath>
#include <numbers>
#include <ranges>

namespace fighter::combat {
namespace {

float lerpAngle(float From, float To, float T);

} // namespace

RenderSnapshot interpolate(const RenderSnapshot& Prev, const RenderSnapshot& Curr, float Alpha) {
    RenderSnapshot Out = Curr;
    for (auto&& [Before, After] : std::views::zip(Prev.Fighters, Out.Fighters)) {
        After.Position = lerp(Before.Position, After.Position, Alpha);

        // Parts are matched by index; if the set changed, use the current frame.
        if (Before.Parts.size() != After.Parts.size()) continue;
        for (auto&& [PartBefore, PartAfter] : std::views::zip(Before.Parts, After.Parts)) {
            PartAfter.Position = lerp(PartBefore.Position, PartAfter.Position, Alpha);
            PartAfter.Angle = lerpAngle(PartBefore.Angle, PartAfter.Angle, Alpha);
        }
    }
    return Out;
}

namespace {

/// Interpolates along the shortest arc: going from 179 to -179 degrees must
/// not spin a full turn.
float lerpAngle(float From, float To, float T) {
    constexpr float Pi = std::numbers::pi_v<float>;
    const float Delta = std::remainder(To - From, 2.0f * Pi);
    return From + Delta * T;
}

} // namespace

} // namespace fighter::combat
