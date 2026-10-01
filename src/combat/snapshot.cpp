#include "combat/snapshot.hpp"

#include <cmath>
#include <numbers>

namespace fighter::combat {
namespace {

/// Interpolates along the shortest arc: going from 179 to -179 degrees must
/// not spin a full turn.
float lerpAngle(float A, float B, float T) {
    constexpr float Pi = std::numbers::pi_v<float>;
    const float Delta = std::remainder(B - A, 2.0f * Pi);
    return A + Delta * T;
}

} // namespace

RenderSnapshot interpolate(const RenderSnapshot& Prev, const RenderSnapshot& Curr, float Alpha) {
    RenderSnapshot Out = Curr;
    for (std::size_t F = 0; F < Out.Fighters.size(); ++F) {
        const FighterView& P = Prev.Fighters[F];
        FighterView& O = Out.Fighters[F];
        O.Position = lerp(P.Position, O.Position, Alpha);

        // Parts are matched by index; if the set changed, use the current frame.
        if (P.Parts.size() != O.Parts.size()) continue;
        for (std::size_t I = 0; I < O.Parts.size(); ++I) {
            O.Parts[I].Position = lerp(P.Parts[I].Position, O.Parts[I].Position, Alpha);
            O.Parts[I].Angle = lerpAngle(P.Parts[I].Angle, O.Parts[I].Angle, Alpha);
        }
    }
    return Out;
}

} // namespace fighter::combat
