#include "combat/snapshot.hpp"

#include <cmath>
#include <numbers>
#include <ranges>
#include <vector>

namespace fighter::combat {
namespace {

float lerpAngle(float From, float To, float T);
/// Moves \p To towards \p From by 1 - \p Alpha. Transforms are matched by
/// index; if the set changed, \p To (the current frame) stays.
void interpolateTransforms(const std::vector<PartTransform>& From, std::vector<PartTransform>& To, float Alpha);

} // namespace

RenderSnapshot interpolate(const RenderSnapshot& Prev, const RenderSnapshot& Curr, float Alpha) {
    RenderSnapshot Out = Curr;
    for (auto&& [Before, After] : std::views::zip(Prev.Fighters, Out.Fighters)) {
        After.Position = lerp(Before.Position, After.Position, Alpha);

        interpolateTransforms(Before.Parts, After.Parts, Alpha);
        interpolateTransforms(Before.Weapons, After.Weapons, Alpha);
    }
    return Out;
}

namespace {

void interpolateTransforms(const std::vector<PartTransform>& From, std::vector<PartTransform>& To, float Alpha) {
    if (From.size() != To.size()) return;
    for (auto&& [PartBefore, PartAfter] : std::views::zip(From, To)) {
        PartAfter.Position = lerp(PartBefore.Position, PartAfter.Position, Alpha);
        PartAfter.Angle = lerpAngle(PartBefore.Angle, PartAfter.Angle, Alpha);
    }
}

/// Interpolates along the shortest arc: going from 179 to -179 degrees must
/// not spin a full turn.
float lerpAngle(float From, float To, float T) {
    constexpr float Pi = std::numbers::pi_v<float>;
    const float Delta = std::remainder(To - From, 2.0f * Pi);
    return From + Delta * T;
}

} // namespace

} // namespace fighter::combat
