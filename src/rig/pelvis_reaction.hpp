//===- rig/pelvis_reaction.hpp - Pelvis reaction to a hit -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares rig::PelvisReaction, the additive reaction of the
/// kinematic pelvis and legs to a hit (docs/MECHANICS.md, section 4.4): the
/// pelvis tilts and sinks, the knee of the leg that was hit low buckles,
/// each on a damped spring that goes back to zero. The pelvis stays
/// kinematic: the rig adds these offsets on top of the clip's pose.
///
/// A hit excites the springs by its strength (m/s, the same quantity the
/// reaction levels compare), its direction and where it landed relative to
/// the pelvis:
///  - tilt: the hit's lever about the pelvis (cross(point - pelvis,
///    direction), m); a high hit tilts the pelvis the way it pushes, a low
///    one the other way;
///  - sink: a hit near the pelvis height (fading out at SinkReach above or
///    below) and a push downward;
///  - buckle: a hit below the pelvis, full at BuckleReach under it, on the
///    nearest leg.
/// Each amplitude is the peak the spring reaches from rest; an excitation
/// adds the spring velocity that gives it. Pure logic, no physics.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <vector>

#include "core/vec2.hpp"

namespace fighter::rig {

class PelvisReaction {
public:
    /// rig::ControlParams fields "pelvis*" and "kneeBuckle*" (docs/TUNING.md).
    struct Params {
        float TiltPerStrength = 0.7f;     ///< rad per (m/s of strength x m of lever).
        float MaxTilt = 0.2f;             ///< rad.
        float SinkPerStrength = 0.08f;    ///< m per m/s.
        float SinkReach = 0.5f;           ///< m above or below the pelvis where the sink fades out.
        float MaxSink = 0.06f;            ///< m.
        float PelvisHertz = 3.0f;         ///< Tilt and sink spring.
        float PelvisDampingRatio = 0.5f;  ///< (0, 1].
        float BucklePerStrength = 0.8f;   ///< rad of heel lift per m/s.
        float BuckleReach = 0.4f;         ///< m below the pelvis where the buckle is full.
        float MaxBuckle = 0.35f;          ///< rad.
        float BuckleHertz = 4.0f;         ///< Knee spring.
        float BuckleDampingRatio = 0.7f;  ///< (0, 1].
    };

    /// The peaks one hit asked for (after the limits), for the debug log.
    struct Excitation {
        float Tilt = 0.0f;     ///< rad, counterclockwise positive.
        float Sink = 0.0f;     ///< m, down positive.
        float Buckle = 0.0f;   ///< rad.
        size_t Leg = 0;        ///< The leg whose knee buckles.
    };

    PelvisReaction() = default;
    PelvisReaction(Params Settings, size_t LegCount) : Config(Settings), Knees(LegCount) {}

    /// A hit of \p Strength (m/s) pushing along the unit vector \p Direction
    /// landed \p FromPelvis (m, world) from the pelvis, nearest to the leg
    /// \p NearLeg. Returns the peaks it excites.
    Excitation excite(float Strength, Vec2 Direction, Vec2 FromPelvis, size_t NearLeg);
    /// Advances the springs by \p Dt.
    void advance(float Dt);
    /// Back to rest (a knockdown, the start of a fight).
    void reset();
    /// The body was mirrored about the vertical (a turn): the tilt flips.
    void mirror();

    /// The pelvis turn to add, rad (counterclockwise positive, world).
    float getTilt() const { return Tilt.Offset; }
    /// How far the pelvis goes down, m (never up).
    float getSink() const;
    /// How far the heel of leg \p Leg lifts, rad (never below 0): the knee
    /// bends forward over the planted toes.
    float getBuckle(size_t Leg) const;
    size_t getLegCount() const { return Knees.size(); }
    /// Is any spring away from rest?
    bool isActive() const;

    /// The share of the starting speed / angular frequency that a damped
    /// spring from rest reaches as its first peak: exp(-z acos(z) / sqrt(1 -
    /// z^2)) for \p DampingRatio z in (0, 1), 1/e at 1.
    static float getPeakShare(float DampingRatio);

private:
    /// One damped spring towards zero.
    struct Spring {
        float Offset = 0.0f;
        float Speed = 0.0f;
    };

    static void kick(Spring& State, float Peak, float Hertz, float DampingRatio);
    static void step(Spring& State, float Hertz, float DampingRatio, float Limit, float Dt);

    Params Config;
    Spring Tilt;
    Spring Sink;
    std::vector<Spring> Knees;
};

} // namespace fighter::rig
