//===- render/effects.hpp - Hit flashes, shake and dust ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares BattleEffects, the subtle effects started by battle
/// events (O.7, p. 5): a flash where a strike lands, a light camera shake on
/// strong hits, dust when a fighter falls. It also remembers who was hit
/// last: that fighter is drawn behind the other one (docs/ART.md).
///
/// Effects run on simulation time, counted in steps: they freeze on pause,
/// slow down in slow motion and play the same way at any frame rate. The
/// time of a frame is the tick it shows, getFrameTick(): between the previous
/// and the current snapshot. An event of the step from tick N - 1 to tick N
/// starts at N - 1, so it is visible in the first frame after that step.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "combat/events.hpp"
#include "combat/snapshot.hpp"
#include "core/vec2.hpp"
#include "render/render_list.hpp"
#include "render/visuals.hpp"

namespace fighter::render {

/// The tick a frame shows: \p Alpha of the way from the previous snapshot
/// (tick \p CurrentTick - 1) to the current one.
double getFrameTick(uint64_t CurrentTick, float Alpha);

class BattleEffects {
public:
    explicit BattleEffects(double SimStepSec) : StepSec(SimStepSec > 0.0 ? SimStepSec : 1.0 / 60.0) {}

    /// Starts the effects of \p Event. \p After is the snapshot right after
    /// the step that produced the event.
    void onEvent(const combat::BattleEvent& Event, const combat::RenderSnapshot& After, const EffectsParams& Params);
    /// Forgets everything, for a new fight.
    void clear();

    /// The fighter drawn behind: the one hit last; before any hit, the right one.
    size_t getFarFighter() const { return FarFighter; }

    /// How far the camera is moved at \p FrameTick, m.
    Vec2 getCameraOffset(double FrameTick, const EffectsParams& Params) const;
    /// Adds the flashes and the dust visible at \p FrameTick to the Effects layer.
    void appendTo(RenderList& List, double FrameTick, const EffectsParams& Params) const;
    /// How many effects are visible at \p FrameTick, for the debug panel.
    size_t getActiveCount(double FrameTick, const EffectsParams& Params) const;

private:
    struct Flash {
        uint64_t Tick = 0;
        Vec2 Point;
        bool Blocked = false;
    };
    struct Dust {
        uint64_t Tick = 0;
        float X = 0.0f;
    };

    /// Seconds since an effect of \p Tick started; negative before it.
    double getAgeSec(uint64_t Tick, double FrameTick) const;
    void forgetOld(uint64_t NowTick);

    double StepSec;
    size_t FarFighter = 1;
    std::vector<Flash> Flashes;
    std::vector<uint64_t> Shakes;
    std::vector<Dust> DustClouds;
};

} // namespace fighter::render
