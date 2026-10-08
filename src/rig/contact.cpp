#include "rig/contact.hpp"

#include <bitset>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <tuple>

#include "debug/draw.hpp"

namespace fighter::rig {
namespace {

/// Overlaps of the fighters shallower than this are not marked, m (Box2D
/// lets touching bodies sink into each other by a few millimetres).
constexpr float MinDrawnOverlap = 0.005f;
constexpr float OverlapMarkRadius = 0.04f;   // m

bool isArm(BodyPart Part);
/// Did a pass of Rig::stopPosedLimbs() move a limb back?
bool hasMovedBack(const PosedStop& Stop);
// Only the debug build fills the panel.
[[maybe_unused]] std::string describePosed(const PosedStop& Stop, const std::bitset<BodyPartCount>& Held);

} // namespace

void ContactResolver::beforeStep(Rig& First, Rig& Second, float Dt) {
    // A posed limb held pressed into a body lying on the floor would clamp
    // its limp parts to the floor: the ragdoll pulled away levers them up
    // into the limb (nothing moves a posed limb out of the way). On a lying
    // opponent the limb stops at the touch. The posture is the one the
    // fighters planned this step with.
    StopDepths = {Second.getPosture() == Posture::KnockedDown ? 0.0f : Params.StopDepth,
                  First.getPosture() == Posture::KnockedDown ? 0.0f : Params.StopDepth};
    Spacing = keepApart(First, Second, Params.Spacing, Dt);
    for (auto&& [Body, Shift, Side] : {std::tuple(&First, &WallShift[0], &WallSide[0]),
                                       std::tuple(&Second, &WallShift[1], &WallSide[1])}) {
        *Shift = Body->getPosture() == Posture::KnockedDown ? 0.0f : Body->getController().getWallShift();
        *Side = Body->getWallSide();
    }
}

std::array<PosedStop, 2> ContactResolver::afterStep(Rig& First, Rig& Second) {
    const std::array<Rig*, 2> Bodies = {&First, &Second};
    // The first pass of each stops its strikers too and tells the clip.
    Posed = {First.stopPosedLimbs(StopDepths[0], true), Second.stopPosedLimbs(StopDepths[1], true)};
    Held = {Posed[0].HeldLimbs, Posed[1].HeldLimbs};
    PosedPasses = 2;
    // Each was stopped against the other's limbs as they were then: the
    // second one's checks stand (the first one's moves came before them),
    // the first one's do not if the second one moved something back. So
    // while the one checked last moved a limb back, the other one goes
    // again.
    bool MovedBack = hasMovedBack(Posed[1]);
    size_t Next = 0;
    while (MovedBack && PosedPasses < MaxPosedPasses) {
        const PosedStop Again = Bodies[Next]->stopPosedLimbs(StopDepths[Next], false);
        Held[Next] |= Again.HeldLimbs;
        MovedBack = hasMovedBack(Again);
        Next = 1 - Next;
        ++PosedPasses;
    }
    if constexpr (FIGHTER_DEBUG) {
        if (MovedBack) debug::logEvent(std::format("posed stop: still moving limbs back after {} passes", PosedPasses));
    }
    return {PosedStop{.StrikeKept = Posed[0].StrikeKept, .HeldLimbs = Held[0]},
            PosedStop{.StrikeKept = Posed[1].StrikeKept, .HeldLimbs = Held[1]}};
}

float ContactResolver::onStrikeLanded(Rig& Attacker, Rig& Victim) {
    const float Apart = pushApartOnHit(Attacker, Victim);
    if (Apart > 0.0f) {
        PushOut = std::format("last: P{} hit P{} at close range, apart {:.2f} m", Attacker.getFighterIndex() + 1,
                              Victim.getFighterIndex() + 1, Apart);
    }
    return Apart;
}

float ContactResolver::getOverlapTolerance(const physics::PartOverlap& Overlap) const {
    return isArm(Overlap.First.Part) && isArm(Overlap.Second.Part) ? Params.ArmOverlapTolerance
                                                                   : Params.OverlapTolerance;
}

std::optional<physics::PartOverlap> ContactResolver::findWorstOverlap(const physics::World& PhysWorld) const {
    std::optional<physics::PartOverlap> Worst;
    float WorstExcess = 0.0f;
    for (const auto& Overlap : PhysWorld.findOverlaps()) {
        const float Excess = Overlap.Depth - getOverlapTolerance(Overlap);
        if (Worst && Excess <= WorstExcess) continue;
        Worst = Overlap;
        WorstExcess = Excess;
    }
    return Worst;
}

void ContactResolver::drawDebug(const physics::World& PhysWorld, double TimeSec,
                                const std::array<const char*, 2>& Names) {
    if constexpr (FIGHTER_DEBUG) {
        // The invariant first: its panel line stays near the top.
        const std::optional<physics::PartOverlap> Now = findWorstOverlap(PhysWorld);
        const auto getExcess = [&](const physics::PartOverlap& Overlap) {
            return Overlap.Depth - getOverlapTolerance(Overlap);
        };
        if (Now && (!WorstOverlap || getExcess(*Now) > getExcess(*WorstOverlap))) {
            WorstOverlap = Now;
            WorstOverlapSec = TimeSec;
        }
        const auto describe = [&](const physics::PartOverlap& Overlap) {
            return std::format("{:.3f} m {} {} / {} {}{}", Overlap.Depth, Names[Overlap.First.Fighter],
                               getBodyPartName(Overlap.First.Part), Names[Overlap.Second.Fighter],
                               getBodyPartName(Overlap.Second.Part),
                               getExcess(Overlap) > 0.0f
                                   ? std::format(" OVER {:.3f} allowed", getOverlapTolerance(Overlap))
                                   : std::string());
        };
        std::string Text = Now ? describe(*Now) : "-";
        if (WorstOverlap) Text += std::format(" (worst {} at {:.1f} s)", describe(*WorstOverlap), WorstOverlapSec);
        debug::setPanel("overlap", Text);
        if (Now && Now->Depth >= MinDrawnOverlap) {
            debug::drawCircle(debug::Cat::Contacts, Now->Point, OverlapMarkRadius);
            debug::drawText(debug::Cat::Contacts, Now->Point, std::format("overlap {:.3f}", Now->Depth));
        }

        // Stage 1: the walls and a fighter lying on the floor.
        std::string Bounds;
        for (auto&& [Name, Shift, Side, OffLying] : std::views::zip(Names, WallShift, WallSide, Spacing.OffLying)) {
            if (!Bounds.empty()) Bounds += "; ";
            Bounds += std::format("{} {}", Name, Side < 0 ? "left wall" : Side > 0 ? "right wall" : "free");
            if (Shift != 0.0f) Bounds += std::format(" stopped {:+.3f} m", Shift);
            if (OffLying != 0.0f) Bounds += std::format(", steps off the lying body {:+.3f} m", OffLying);
        }
        debug::setPanel("contact walls", Bounds);
        // Stage 1: the spacing of two standing fighters.
        debug::setPanel("contact spacing",
                        !Spacing.Standing ? std::string("one is down")
                                          : std::format("{} needs {:.3f} m: slows {:.3f}, pushes {:.3f} m ({:.2f} m/s){}",
                                                        Spacing.Overlap > 0.0f
                                                            ? std::format("bodies overlap {:.3f} m,", Spacing.Overlap)
                                                            : std::string("pelvises"),
                                                        Spacing.Needed, Spacing.Slowed, Spacing.Pushed,
                                                        Spacing.PushSpeed,
                                                        Spacing.Hard    ? ", hard"
                                                        : Spacing.Eased ? ", eased"
                                                                        : ""));
        // Stage 3: the posed limbs after the step.
        std::string Stops;
        for (auto&& [Name, Stop, Limbs] : std::views::zip(Names, Posed, Held)) {
            const std::string Did = describePosed(Stop, Limbs);
            if (Did.empty()) continue;
            if (!Stops.empty()) Stops += "; ";
            Stops += std::format("{} {}", Name, Did);
        }
        debug::setPanel("contact posed", Stops.empty() ? std::format("- ({} passes)", PosedPasses)
                                                       : std::format("{} ({} passes)", Stops, PosedPasses));
        // Stage 4: the push-out of a hit at close range.
        debug::setPanel("contact push-out", PushOut);
    }
}

namespace {

bool isArm(BodyPart Part) {
    return Part == BodyPart::UpperArmL || Part == BodyPart::ForearmL || Part == BodyPart::UpperArmR ||
           Part == BodyPart::ForearmR;
}

bool hasMovedBack(const PosedStop& Stop) {
    return Stop.HeldLimbs.any() || (Stop.StrikeKept && *Stop.StrikeKept < 1.0f);
}

std::string describePosed(const PosedStop& Stop, const std::bitset<BodyPartCount>& Held) {
    std::string Text;
    if (Stop.StrikeKept) {
        Text = *Stop.StrikeKept < 1.0f ? std::format("strike stopped at {:.2f} of the step", *Stop.StrikeKept)
                                       : std::string("strike touches");
    }
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Held.test(Index)) continue;
        if (!Text.empty()) Text += ", ";
        Text += std::format("{} held back", getBodyPartName(static_cast<BodyPart>(Index)));
    }
    return Text;
}

} // namespace

} // namespace fighter::rig
