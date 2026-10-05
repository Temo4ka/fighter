#include "physics/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <box2d/box2d.h>

#include "debug/category.hpp"
#include "physics/box2d_bridge.hpp"

namespace fighter::physics {

using detail::fromBox2D;
using detail::loadBody;
using detail::loadJoint;
using detail::loadWorld;
using detail::toBox2D;

namespace {

/// Debug draw size of a joint (the circle around the hinge), m.
constexpr float JointDrawSize = 0.05f;
/// Contact points looked at per shape when summing the impulse of a hit.
constexpr int MaxContactsPerShape = 16;
/// A body part has one or two shapes; more is never needed.
constexpr int MaxShapesPerBody = 8;
/// Contacts looked at per body when asking whether it touches a fighter.
constexpr int MaxContactsPerBody = 32;
/// Posed shapes closer than this touch, m (as Box2D overlap queries).
constexpr float TouchTolerance = 0.0005f;
/// A posed part that went from apart to this deep into a dynamic part in one
/// step tunnelled into it (collectTunnelHits()), m: Box2D lets touching
/// bodies sink in by less than that.
constexpr float TunnelDepth = 0.01f;
/// Shape cores closer than this intersect: the distance says nothing about
/// how deep the shapes overlap, m.
constexpr float CoreTouchDistance = 1e-5f;
/// Bisection steps of findPosedStop(): the share of the step is found to
/// 2^-20, far below a millimetre for any limb speed.
constexpr int StopSearchSteps = 20;
/// A posed striker whose motion in a step takes it less than this deeper
/// into another part does not run into it (findPosedStop()): it slides
/// along it or leaves it, m.
constexpr float MinClosingDepth = 1e-4f;

/// The closest approach of two posed shapes.
struct ShapeGap {
    float Distance = 0.0f;   ///< Between the surfaces; negative when they overlap, m.
    Vec2 Point;              ///< Midway between the surfaces.
    Vec2 Normal;             ///< From the first shape to the second.
};

b2Polygon makeBox(const ShapeDef& Shape);
float sumContactImpulse(b2ShapeId Shape, b2ShapeId Other);
std::span<b2ShapeId> getShapes(b2BodyId BodyId, std::array<b2ShapeId, MaxShapesPerBody>& Storage);
b2ShapeProxy makeLocalProxy(b2ShapeId Shape);
ShapeGap measureGap(b2ShapeId ShapeA, b2Transform TransformA, b2ShapeId ShapeB, b2Transform TransformB);
std::optional<ShapeGap> measureDeepOverlap(b2ShapeId ShapeA, b2Transform TransformA, b2ShapeId ShapeB,
                                           b2Transform TransformB);
ShapeGap measureBodyGap(b2BodyId BodyA, b2Transform TransformA, b2BodyId BodyB, b2Transform TransformB);
b2Transform makeTransform(Vec2 Position, Vec2 Rotation);
b2Transform getTransform(b2ShapeId Shape);

} // namespace

World::World(Config Settings)
    : StepPasses(std::max(Settings.StepPasses, 1)),
      SubSteps(Settings.SubSteps),
      HitSpeedThreshold(Settings.HitSpeedThreshold) {
    b2WorldDef Def = b2DefaultWorldDef();
    Def.gravity = toBox2D(Settings.Gravity);
    Def.hitEventThreshold = Settings.HitSpeedThreshold;
    Def.contactHertz = Settings.ContactHertz;
    // Fighters must never fall asleep: their motors work every step.
    Def.enableSleep = false;
    Id = b2StoreWorldId(b2CreateWorld(&Def));
}

World::~World() { destroy(); }

World::World(World&& Other) noexcept
    : Id(std::exchange(Other.Id, 0)),
      StepPasses(Other.StepPasses),
      SubSteps(Other.SubSteps),
      PartBodies(std::move(Other.PartBodies)),
      Hits(std::move(Other.Hits)),
      HitSpeedThreshold(Other.HitSpeedThreshold),
      TouchingPosed(std::move(Other.TouchingPosed)) {}

World& World::operator=(World&& Other) noexcept {
    if (this != &Other) {
        destroy();
        Id = std::exchange(Other.Id, 0);
        StepPasses = Other.StepPasses;
        SubSteps = Other.SubSteps;
        PartBodies = std::move(Other.PartBodies);
        Hits = std::move(Other.Hits);
        HitSpeedThreshold = Other.HitSpeedThreshold;
        TouchingPosed = std::move(Other.TouchingPosed);
    }
    return *this;
}

void World::destroy() {
    if (Id != 0) {
        b2DestroyWorld(loadWorld(Id));
        Id = 0;
    }
    PartBodies.clear();
    Hits.clear();
    TouchingPosed.clear();
}

Body World::createBody(const BodyDef& Def) {
    b2BodyDef BodyDefinition = b2DefaultBodyDef();
    BodyDefinition.type = toBox2D(Def.Type);
    BodyDefinition.position = toBox2D(Def.Position);
    BodyDefinition.rotation = b2MakeRot(Def.Angle);
    BodyDefinition.linearDamping = Def.LinearDamping;
    BodyDefinition.angularDamping = Def.AngularDamping;
    if (Def.Part) BodyDefinition.userData = detail::encodePartSlot(static_cast<uint32_t>(PartBodies.size()));

    const Body Handle(b2StoreBodyId(b2CreateBody(loadWorld(Id), &BodyDefinition)));
    if (Def.Part) PartBodies.push_back({.Handle = Handle, .Part = *Def.Part});
    return Handle;
}

void World::addShape(Body Target, const ShapeDef& Shape) {
    const b2BodyId BodyId = loadBody(Target.Id);

    // The debug color is the only per-shape value Box2D passes to the debug
    // draw callbacks, so it carries the category and the fighter instead.
    debug::Cat Category = debug::Cat::Hurtbox;
    debug::Side Owner = debug::Side::None;
    if (b2Body_GetType(BodyId) == b2_staticBody) {
        Category = debug::Cat::Static;
    } else if (const auto Slot = detail::decodePartSlot(b2Body_GetUserData(BodyId))) {
        Owner = PartBodies[*Slot].Part.Fighter == 0 ? debug::Side::Left : debug::Side::Right;
    }

    b2ShapeDef Def = b2DefaultShapeDef();
    Def.density = Shape.Density;
    Def.material.friction = Shape.Friction;
    Def.material.restitution = Shape.Restitution;
    Def.material.customColor = detail::encodeDebugColor(Category, Owner);
    Def.filter.groupIndex = Shape.CollisionGroup;
    Def.filter.categoryBits = Shape.CollisionCategory;
    Def.filter.maskBits = Shape.CollisionMask;
    Def.enableHitEvents = Shape.EnableHitEvents;

    switch (Shape.Kind) {
        case ShapeKind::Circle: {
            const b2Circle Circle{.center = toBox2D(Shape.Center), .radius = Shape.Radius};
            b2CreateCircleShape(BodyId, &Def, &Circle);
            break;
        }
        case ShapeKind::Capsule: {
            const b2Capsule Capsule{.center1 = toBox2D(Shape.Begin),
                                    .center2 = toBox2D(Shape.End),
                                    .radius = Shape.Radius};
            b2CreateCapsuleShape(BodyId, &Def, &Capsule);
            break;
        }
        case ShapeKind::Box: {
            const b2Polygon Box = makeBox(Shape);
            b2CreatePolygonShape(BodyId, &Def, &Box);
            break;
        }
    }
}

RevoluteJoint World::createRevoluteJoint(const RevoluteJointDef& Def) {
    const b2BodyId BodyA = loadBody(Def.BodyA.Id);
    const b2BodyId BodyB = loadBody(Def.BodyB.Id);

    b2RevoluteJointDef JointDef = b2DefaultRevoluteJointDef();
    JointDef.bodyIdA = BodyA;
    JointDef.bodyIdB = BodyB;
    JointDef.localAnchorA = b2Body_GetLocalPoint(BodyA, toBox2D(Def.Anchor));
    JointDef.localAnchorB = b2Body_GetLocalPoint(BodyB, toBox2D(Def.Anchor));
    JointDef.referenceAngle = b2RelativeAngle(b2Body_GetRotation(BodyB), b2Body_GetRotation(BodyA));
    JointDef.enableLimit = Def.EnableLimit;
    JointDef.lowerAngle = Def.LowerAngle;
    JointDef.upperAngle = Def.UpperAngle;
    JointDef.enableMotor = Def.EnableMotor;
    JointDef.maxMotorTorque = Def.MaxMotorTorque;
    JointDef.motorSpeed = Def.MotorSpeed;
    JointDef.drawSize = JointDrawSize;
    return RevoluteJoint(b2StoreJointId(b2CreateRevoluteJoint(loadWorld(Id), &JointDef)));
}

void World::setBodyType(Body Target, BodyType Type) {
    const b2BodyId BodyId = loadBody(Target.Id);
    if (b2Body_GetType(BodyId) == b2_dynamicBody) {
        if (const auto Slot = detail::decodePartSlot(b2Body_GetUserData(BodyId))) {
            PartBodies[*Slot].DynamicMass = b2Body_GetMass(BodyId);
        }
    }
    b2Body_SetType(BodyId, toBox2D(Type));
}

void World::setStrikeMass(Body Target, float Kg) {
    if (const auto Slot = detail::decodePartSlot(b2Body_GetUserData(loadBody(Target.Id)))) {
        PartBodies[*Slot].StrikeMass = Kg;
    }
}

void World::step(float Dt) {
    recordPartVelocities();
    Hits.clear();
    // Hit events are read after every Box2D step: the next one drops them.
    for (int Pass = 0; Pass < StepPasses; ++Pass) {
        b2World_Step(loadWorld(Id), Dt / static_cast<float>(StepPasses), SubSteps);
        collectHits();
    }
    collectPosedHits();
    collectTunnelHits();
}

bool World::isTouchingOtherFighter(Body Target) const {
    const b2BodyId BodyId = loadBody(Target.Id);
    const auto Slot = detail::decodePartSlot(b2Body_GetUserData(BodyId));
    if (!Slot) return false;
    const uint8_t Owner = PartBodies[*Slot].Part.Fighter;

    std::array<b2ContactData, MaxContactsPerBody> Contacts{};
    const int Count = b2Body_GetContactData(BodyId, Contacts.data(), MaxContactsPerBody);
    for (const auto& Contact : std::span(Contacts).first(static_cast<size_t>(Count))) {
        if (Contact.manifold.pointCount == 0) continue;
        for (const auto& Shape : {Contact.shapeIdA, Contact.shapeIdB}) {
            const auto Other = detail::decodePartSlot(b2Body_GetUserData(b2Shape_GetBody(Shape)));
            if (Other && PartBodies[*Other].Part.Fighter != Owner) return true;
        }
    }
    return false;
}

bool World::isOverlappingOtherFighter(Body Target) const {
    return isOverlappingOtherFighterAt(Target, Target.getPosition(), Target.getAngle(), 0.0f);
}

bool World::isOverlappingOtherFighterAt(Body Target, Vec2 Position, float Angle, float Margin) const {
    const b2BodyId BodyId = loadBody(Target.Id);
    const auto Slot = detail::decodePartSlot(b2Body_GetUserData(BodyId));
    if (!Slot) return false;

    struct Search {
        const std::vector<PartBody>* Parts = nullptr;
        uint8_t Owner = 0;
        bool Found = false;
    } State{.Parts = &PartBodies, .Owner = PartBodies[*Slot].Part.Fighter};
    const auto onOverlap = [](b2ShapeId Shape, void* Context) {
        auto* Query = static_cast<Search*>(Context);
        const auto Other = detail::decodePartSlot(b2Body_GetUserData(b2Shape_GetBody(Shape)));
        Query->Found = Other && (*Query->Parts)[*Other].Part.Fighter != Query->Owner;
        return !Query->Found;   // stop at the first one
    };

    const b2Transform Transform{toBox2D(Position), b2MakeRot(Angle)};
    std::array<b2ShapeId, MaxShapesPerBody> Storage{};
    for (const auto& Shape : getShapes(BodyId, Storage)) {
        const b2ShapeProxy Local = makeLocalProxy(Shape);
        const b2ShapeProxy Placed =
            b2MakeOffsetProxy(Local.points, Local.count, Local.radius + Margin, Transform.p, Transform.q);
        b2World_OverlapShape(loadWorld(Id), &Placed, b2DefaultQueryFilter(), onOverlap, &State);
        if (State.Found) return true;
    }
    return false;
}

std::optional<PartOverlap> World::findDeepestOverlap() const {
    const std::vector<PartOverlap> Overlaps = findOverlaps();
    if (Overlaps.empty()) return std::nullopt;
    // The first of equally deep ones, as the pairs are listed.
    return *std::ranges::max_element(Overlaps, std::ranges::less{}, &PartOverlap::Depth);
}

std::vector<PartOverlap> World::findOverlaps() const {
    std::vector<PartOverlap> Overlaps;
    std::array<b2ShapeId, MaxShapesPerBody> StorageA{};
    std::array<b2ShapeId, MaxShapesPerBody> StorageB{};
    for (size_t SlotA = 0; SlotA < PartBodies.size(); ++SlotA) {
        const PartBody& PartA = PartBodies[SlotA];
        const std::span<b2ShapeId> ShapesA = getShapes(loadBody(PartA.Handle.Id), StorageA);
        for (size_t SlotB = SlotA + 1; SlotB < PartBodies.size(); ++SlotB) {
            const PartBody& PartB = PartBodies[SlotB];
            if (PartA.Part.Fighter == PartB.Part.Fighter) continue;
            const std::span<b2ShapeId> ShapesB = getShapes(loadBody(PartB.Handle.Id), StorageB);
            std::optional<PartOverlap> Deepest;
            for (const auto& ShapeA : ShapesA) {
                for (const auto& ShapeB : ShapesB) {
                    const ShapeGap Gap = measureGap(ShapeA, getTransform(ShapeA), ShapeB, getTransform(ShapeB));
                    if (Gap.Distance >= 0.0f || (Deepest && -Gap.Distance <= Deepest->Depth)) continue;
                    Deepest = PartOverlap{.First = PartA.Part, .Second = PartB.Part, .Depth = -Gap.Distance,
                                          .Point = Gap.Point};
                }
            }
            if (Deepest) Overlaps.push_back(*Deepest);
        }
    }
    return Overlaps;
}

float World::getPosedPenetration(Body Target) const {
    const auto Slot = findSlot(Target);
    if (!Slot || Target.getType() != BodyType::Kinematic) return 0.0f;
    const PartBody& Entry = PartBodies[*Slot];
    return measurePosedPenetration(Entry, getPlacementDuringStep(Entry, 1.0f));
}

std::optional<float> World::findPosedStop(std::span<const Body> Strikers, float MaxDepth) const {
    // The pairs of a striker and a part of another fighter that it runs
    // into. A striker sinks no deeper into a part than MaxDepth, or than it
    // already was before the step (held at a contact, or pressed in by the
    // other part): there is no contact to go back to before that.
    struct Pair {
        const PartBody* Mover = nullptr;
        const PartBody* Other = nullptr;
        float Limit = 0.0f;   ///< How deep the mover may be, m.
    };
    std::vector<Pair> Pairs;
    for (const auto& Striker : Strikers) {
        const auto Slot = findSlot(Striker);
        if (!Slot || Striker.getType() != BodyType::Kinematic) continue;
        const PartBody& Mover = PartBodies[*Slot];
        const Placement MoverBefore = getPlacementDuringStep(Mover, 0.0f);
        const Placement MoverNow = getPlacementDuringStep(Mover, 1.0f);
        for (const auto& Other : PartBodies) {
            if (Other.Part.Fighter == Mover.Part.Fighter) continue;
            const Placement OtherBefore = getPlacementDuringStep(Other, 0.0f);
            const ShapeGap GapBefore =
                measureBodyGap(loadBody(Mover.Handle.Id), makeTransform(MoverBefore.Position, MoverBefore.Rotation),
                               loadBody(Other.Handle.Id), makeTransform(OtherBefore.Position, OtherBefore.Rotation));
            const float Limit = std::max(MaxDepth, -GapBefore.Distance);
            const float Now = measurePairPenetration(Mover, MoverNow, Other, getPlacementDuringStep(Other, 1.0f));
            // A posed part is a contact as soon as the striker touches it
            // (as for posed hits); a dynamic one only when the solver did
            // not keep the striker out of it.
            const bool Posed = Other.Handle.getType() == BodyType::Kinematic;
            if (Posed ? Now < -TouchTolerance : Now <= Limit) continue;
            // Closing: the striker's own motion in the step took it deeper
            // into the other part (where that part is now). A contact it
            // slides along or leaves, or one the other part pressed into it,
            // is no stop.
            const float NowFromStart = measurePairPenetration(Mover, MoverBefore, Other, getPlacementDuringStep(Other, 1.0f));
            if (Now - NowFromStart < MinClosingDepth) continue;
            Pairs.push_back({.Mover = &Mover, .Other = &Other, .Limit = Limit});
        }
    }
    if (Pairs.empty()) return std::nullopt;
    // How much deeper than allowed the strikers are at a share of their
    // motion; the opponent's parts stay where they are now.
    const auto getExcess = [&](float Fraction) {
        float Excess = std::numeric_limits<float>::lowest();
        for (const auto& Entry : Pairs) {
            const float Depth = measurePairPenetration(*Entry.Mover, getPlacementDuringStep(*Entry.Mover, Fraction),
                                                       *Entry.Other, getPlacementDuringStep(*Entry.Other, 1.0f));
            Excess = std::max(Excess, Depth - Entry.Limit);
        }
        return Excess;
    };
    if (getExcess(1.0f) <= 0.0f) return 1.0f;   // touching, not too deep
    if (getExcess(0.0f) > 0.0f) return 0.0f;    // the opponent moved into it
    // The largest share that is not too deep: Low is fine, High is not.
    float Low = 0.0f;
    float High = 1.0f;
    for (int Step = 0; Step < StopSearchSteps; ++Step) {
        const float Middle = (Low + High) * 0.5f;
        (getExcess(Middle) > 0.0f ? High : Low) = Middle;
    }
    return Low;
}

float World::getGapAt(Body First, Vec2 FirstPosition, float FirstAngle, Body Second, Vec2 SecondPosition,
                      float SecondAngle) const {
    return measureBodyGap(loadBody(First.Id), {toBox2D(FirstPosition), b2MakeRot(FirstAngle)}, loadBody(Second.Id),
                          {toBox2D(SecondPosition), b2MakeRot(SecondAngle)})
        .Distance;
}

void World::rewindBody(Body Target, float Fraction) {
    const auto Slot = findSlot(Target);
    if (!Slot) return;
    const Placement Placed = getPlacementDuringStep(PartBodies[*Slot], Fraction);
    b2Body_SetTransform(loadBody(Target.Id), toBox2D(Placed.Position), {Placed.Rotation.X, Placed.Rotation.Y});
}

void World::mirrorShapes(Body Target) {
    const b2BodyId BodyId = loadBody(Target.Id);
    std::array<b2ShapeId, MaxShapesPerBody> Storage{};
    for (const auto& Shape : getShapes(BodyId, Storage)) {
        switch (b2Shape_GetType(Shape)) {
            case b2_circleShape: {
                b2Circle Circle = b2Shape_GetCircle(Shape);
                Circle.center.x = -Circle.center.x;
                b2Shape_SetCircle(Shape, &Circle);
                break;
            }
            case b2_capsuleShape: {
                b2Capsule Capsule = b2Shape_GetCapsule(Shape);
                Capsule.center1.x = -Capsule.center1.x;
                Capsule.center2.x = -Capsule.center2.x;
                b2Shape_SetCapsule(Shape, &Capsule);
                break;
            }
            case b2_polygonShape: {
                const b2Polygon Source = b2Shape_GetPolygon(Shape);
                std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> Points{};
                const auto Count = static_cast<size_t>(Source.count);
                for (auto&& [Point, Vertex] : std::views::zip(std::span(Points).first(Count),
                                                              std::span(Source.vertices).first(Count))) {
                    Point = {-Vertex.x, Vertex.y};
                }
                const b2Hull Hull = b2ComputeHull(Points.data(), Source.count);
                const b2Polygon Mirrored = b2MakePolygon(&Hull, Source.radius);
                b2Shape_SetPolygon(Shape, &Mirrored);
                break;
            }
            default:
                break;
        }
    }
    // The center of mass moves with the shapes; a kinematic body has none.
    if (b2Body_GetType(BodyId) == b2_dynamicBody) b2Body_ApplyMassFromShapes(BodyId);
}

RevoluteJoint World::mirrorJoint(RevoluteJoint Joint) {
    const b2JointId Old = loadJoint(Joint.Id);
    const b2BodyId BodyA = b2Joint_GetBodyA(Old);
    const b2BodyId BodyB = b2Joint_GetBodyB(Old);
    const float RelativeAngle = b2RelativeAngle(b2Body_GetRotation(BodyB), b2Body_GetRotation(BodyA));
    const b2Vec2 AnchorA = b2Joint_GetLocalAnchorA(Old);
    const b2Vec2 AnchorB = b2Joint_GetLocalAnchorB(Old);

    b2RevoluteJointDef Def = b2DefaultRevoluteJointDef();
    Def.bodyIdA = BodyA;
    Def.bodyIdB = BodyB;
    Def.localAnchorA = {-AnchorA.x, AnchorA.y};
    Def.localAnchorB = {-AnchorB.x, AnchorB.y};
    // The joint angle is the relative angle minus the reference angle.
    Def.referenceAngle = -std::remainder(RelativeAngle - b2RevoluteJoint_GetAngle(Old), 2.0f * B2_PI);
    Def.enableLimit = b2RevoluteJoint_IsLimitEnabled(Old);
    Def.lowerAngle = -b2RevoluteJoint_GetUpperLimit(Old);
    Def.upperAngle = -b2RevoluteJoint_GetLowerLimit(Old);
    Def.enableMotor = b2RevoluteJoint_IsMotorEnabled(Old);
    Def.maxMotorTorque = b2RevoluteJoint_GetMaxMotorTorque(Old);
    Def.motorSpeed = -b2RevoluteJoint_GetMotorSpeed(Old);
    Def.drawSize = JointDrawSize;
    b2DestroyJoint(Old);
    return RevoluteJoint(b2StoreJointId(b2CreateRevoluteJoint(loadWorld(Id), &Def)));
}

void World::drawDebug() const {
    if constexpr (FIGHTER_DEBUG) detail::drawWorldDebug(loadWorld(Id));
}

Vec2 World::getGravity() const { return fromBox2D(b2World_GetGravity(loadWorld(Id))); }

int World::getBodyCount() const { return b2World_GetCounters(loadWorld(Id)).bodyCount; }

int World::getJointCount() const { return b2World_GetCounters(loadWorld(Id)).jointCount; }

void World::recordPartVelocities() {
    for (auto& Entry : PartBodies) {
        Entry.CenterBeforeStep = Entry.Handle.getWorldCenterOfMass();
        Entry.PositionBeforeStep = Entry.Handle.getPosition();
        Entry.AngleBeforeStep = Entry.Handle.getAngle();
        const b2Rot Rotation = b2Body_GetRotation(loadBody(Entry.Handle.Id));
        Entry.RotationBeforeStep = {Rotation.c, Rotation.s};
        Entry.VelocityBeforeStep = Entry.Handle.getLinearVelocity();
        Entry.AngularVelocityBeforeStep = Entry.Handle.getAngularVelocity();
    }
}

void World::collectHits() {
    const b2ContactEvents Events = b2World_GetContactEvents(loadWorld(Id));
    for (const auto& Event : std::span(Events.hitEvents, static_cast<size_t>(Events.hitCount))) {
        const auto SlotA = detail::decodePartSlot(b2Body_GetUserData(b2Shape_GetBody(Event.shapeIdA)));
        const auto SlotB = detail::decodePartSlot(b2Body_GetUserData(b2Shape_GetBody(Event.shapeIdB)));
        if (!SlotA || !SlotB) continue;   // a part against the arena

        const PartBody& PartA = PartBodies[*SlotA];
        const PartBody& PartB = PartBodies[*SlotB];
        if (PartA.Part.Fighter == PartB.Part.Fighter) continue;

        // A kinematic part is infinitely heavy for the solver: its contact
        // impulse grows with whatever it pushes. Report the impulse of the
        // same collision between free bodies instead (no restitution).
        const bool HasKinematic = PartA.Handle.getType() == BodyType::Kinematic ||
                                  PartB.Handle.getType() == BodyType::Kinematic;
        float Impulse = 0.0f;
        if (HasKinematic) {
            const float MassA = getStrikeMass(PartA);
            const float MassB = getStrikeMass(PartB);
            const float MassSum = MassA + MassB;
            Impulse = MassSum > 0.0f ? Event.approachSpeed * MassA * MassB / MassSum : 0.0f;
        } else {
            Impulse = sumContactImpulse(Event.shapeIdA, Event.shapeIdB);
        }

        addHit(PartA, PartB, fromBox2D(Event.point), fromBox2D(Event.normal), Event.approachSpeed, Impulse);
    }
}

void World::collectPosedHits() {
    std::vector<SlotPair> Touching;
    std::array<b2ShapeId, MaxShapesPerBody> StorageA{};
    std::array<b2ShapeId, MaxShapesPerBody> StorageB{};
    const auto SlotCount = static_cast<uint32_t>(PartBodies.size());
    for (uint32_t SlotA = 0; SlotA < SlotCount; ++SlotA) {
        const PartBody& PartA = PartBodies[SlotA];
        if (PartA.Handle.getType() != BodyType::Kinematic) continue;
        for (uint32_t SlotB = SlotA + 1; SlotB < SlotCount; ++SlotB) {
            const PartBody& PartB = PartBodies[SlotB];
            if (PartB.Handle.getType() != BodyType::Kinematic || PartA.Part.Fighter == PartB.Part.Fighter) continue;

            // The deepest contact between the shapes of the two bodies.
            const std::span<b2ShapeId> ShapesA = getShapes(loadBody(PartA.Handle.Id), StorageA);
            const std::span<b2ShapeId> ShapesB = getShapes(loadBody(PartB.Handle.Id), StorageB);
            std::optional<ShapeGap> Deepest;
            for (const auto& ShapeA : ShapesA) {
                for (const auto& ShapeB : ShapesB) {
                    const ShapeGap Gap = measureGap(ShapeA, getTransform(ShapeA), ShapeB, getTransform(ShapeB));
                    if (Gap.Distance <= TouchTolerance && (!Deepest || Gap.Distance < Deepest->Distance)) {
                        Deepest = Gap;
                    }
                }
            }
            if (!Deepest) continue;
            Touching.emplace_back(SlotA, SlotB);
            if (std::ranges::binary_search(TouchingPosed, SlotPair{SlotA, SlotB})) continue;   // not new

            // The normal at the moment of impact: from the closest points
            // before the step, if the parts were apart then. After the step
            // a fast limb is deep inside the other part, and the direction
            // between the cores says little about how it came in.
            const b2Transform BeforeA{toBox2D(PartA.PositionBeforeStep), b2MakeRot(PartA.AngleBeforeStep)};
            const b2Transform BeforeB{toBox2D(PartB.PositionBeforeStep), b2MakeRot(PartB.AngleBeforeStep)};
            std::optional<ShapeGap> Closest;
            for (const auto& ShapeA : ShapesA) {
                for (const auto& ShapeB : ShapesB) {
                    const ShapeGap Gap = measureGap(ShapeA, BeforeA, ShapeB, BeforeB);
                    if (!Closest || Gap.Distance < Closest->Distance) Closest = Gap;
                }
            }
            const Vec2 Normal = Closest && Closest->Distance > 0.0f ? Closest->Normal : Deepest->Normal;

            // Like a Box2D hit event: a new contact closing fast enough. Both
            // parts are posed, so the impulse is the one of free bodies.
            const Vec2 Relative =
                getVelocityBeforeStep(PartA, Deepest->Point) - getVelocityBeforeStep(PartB, Deepest->Point);
            const float Approach = dot(Relative, Normal);
            if (Approach < HitSpeedThreshold) continue;
            const float MassA = getStrikeMass(PartA);
            const float MassB = getStrikeMass(PartB);
            const float MassSum = MassA + MassB;
            addHit(PartA, PartB, Deepest->Point, Normal, Approach,
                   MassSum > 0.0f ? Approach * MassA * MassB / MassSum : 0.0f);
        }
    }
    TouchingPosed = std::move(Touching);   // built in sorted order
}

void World::collectTunnelHits() {
    const size_t Known = Hits.size();
    for (const auto& Posed : PartBodies) {
        if (Posed.Handle.getType() != BodyType::Kinematic) continue;
        const b2Transform PosedBefore = makeTransform(Posed.PositionBeforeStep, Posed.RotationBeforeStep);
        const b2Transform PosedNow = b2Body_GetTransform(loadBody(Posed.Handle.Id));
        for (const auto& Other : PartBodies) {
            if (Other.Part.Fighter == Posed.Part.Fighter || Other.Handle.getType() != BodyType::Dynamic) continue;
            // Apart before the step, inside each other after it.
            const ShapeGap Before =
                measureBodyGap(loadBody(Posed.Handle.Id), PosedBefore, loadBody(Other.Handle.Id),
                               makeTransform(Other.PositionBeforeStep, Other.RotationBeforeStep));
            if (Before.Distance <= TouchTolerance) continue;
            const ShapeGap Now = measureBodyGap(loadBody(Posed.Handle.Id), PosedNow, loadBody(Other.Handle.Id),
                                                b2Body_GetTransform(loadBody(Other.Handle.Id)));
            if (Now.Distance >= -TunnelDepth) continue;
            // Box2D may have seen this one after all.
            const bool Reported = std::any_of(Hits.begin(), Hits.begin() + static_cast<std::ptrdiff_t>(Known),
                                              [&](const HitEvent& Hit) {
                                                  const auto isPair = [&](PartRef First, PartRef Second) {
                                                      return First.Fighter == Posed.Part.Fighter &&
                                                             First.Part == Posed.Part.Part &&
                                                             Second.Fighter == Other.Part.Fighter &&
                                                             Second.Part == Other.Part.Part;
                                                  };
                                                  return isPair(Hit.Attacker, Hit.Victim) ||
                                                         isPair(Hit.Victim, Hit.Attacker);
                                              });
            if (Reported) continue;
            // As a posed hit: the normal from before the step, the impulse
            // of the same collision between free bodies.
            const Vec2 Relative =
                getVelocityBeforeStep(Posed, Before.Point) - getVelocityBeforeStep(Other, Before.Point);
            const float Approach = dot(Relative, Before.Normal);
            if (Approach < HitSpeedThreshold) continue;
            const float MassA = getStrikeMass(Posed);
            const float MassB = getStrikeMass(Other);
            addHit(Posed, Other, Now.Point, Before.Normal, Approach, Approach * MassA * MassB / (MassA + MassB));
        }
    }
}

void World::addHit(const PartBody& PartA, const PartBody& PartB, Vec2 Point, Vec2 Normal, float ApproachSpeed,
                   float Impulse) {
    // The attacker is the part that was moving towards the other one
    // faster before the step. The normal points from A to B.
    const float SpeedA = dot(getVelocityBeforeStep(PartA, Point), Normal);
    const float SpeedB = dot(getVelocityBeforeStep(PartB, Point), -Normal);
    const bool AttackerIsA = SpeedA >= SpeedB;
    Hits.push_back({
        .Attacker = AttackerIsA ? PartA.Part : PartB.Part,
        .Victim = AttackerIsA ? PartB.Part : PartA.Part,
        .Point = Point,
        .ApproachSpeed = ApproachSpeed,
        .Impulse = Impulse,
    });
}

Vec2 World::getVelocityBeforeStep(const PartBody& Entry, Vec2 WorldPoint) const {
    // v + w x r in 2D.
    return Entry.VelocityBeforeStep + perp(WorldPoint - Entry.CenterBeforeStep) * Entry.AngularVelocityBeforeStep;
}

std::optional<uint32_t> World::findSlot(Body Target) const {
    return detail::decodePartSlot(b2Body_GetUserData(loadBody(Target.Id)));
}

float World::measurePosedPenetration(const PartBody& Entry, const Placement& Placed) const {
    float Deepest = 0.0f;
    for (const auto& Other : PartBodies) {
        if (Other.Part.Fighter == Entry.Part.Fighter || Other.Handle.getType() != BodyType::Kinematic) continue;
        Deepest = std::max(Deepest, measurePairPenetration(Entry, Placed, Other, getPlacementDuringStep(Other, 1.0f)));
    }
    return Deepest;
}

float World::measurePairPenetration(const PartBody& Entry, const Placement& Placed, const PartBody& Other,
                                    const Placement& OtherPlaced) const {
    return -measureBodyGap(loadBody(Entry.Handle.Id), makeTransform(Placed.Position, Placed.Rotation), loadBody(Other.Handle.Id),
                           makeTransform(OtherPlaced.Position, OtherPlaced.Rotation))
                .Distance;
}

World::Placement World::getPlacementDuringStep(const PartBody& Entry, float Fraction) const {
    const b2Transform Now = b2Body_GetTransform(loadBody(Entry.Handle.Id));
    if (Fraction >= 1.0f) return {.Position = fromBox2D(Now.p), .Rotation = {Now.q.c, Now.q.s}};
    const b2Rot Before{Entry.RotationBeforeStep.X, Entry.RotationBeforeStep.Y};
    const b2Rot Turned = b2NLerp(Before, Now.q, Fraction);
    return {.Position = Entry.PositionBeforeStep + (fromBox2D(Now.p) - Entry.PositionBeforeStep) * Fraction,
            .Rotation = {Turned.c, Turned.s}};
}

float World::getStrikeMass(const PartBody& Entry) const {
    if (Entry.Handle.getType() == BodyType::Dynamic) return Entry.Handle.getMass();
    return Entry.StrikeMass > 0.0f ? Entry.StrikeMass : Entry.DynamicMass;
}

namespace {

b2Polygon makeBox(const ShapeDef& Shape) {
    // A rounded box grows by its radius, so shrink the core to keep the
    // outer size equal to HalfExtents.
    const float Rounding = Shape.Radius;
    return b2MakeOffsetRoundedBox(Shape.HalfExtents.X - Rounding, Shape.HalfExtents.Y - Rounding,
                                  toBox2D(Shape.Center), b2Rot_identity, Rounding);
}

/// Total normal impulse of the contact between two shapes during the last
/// step, N*s; 0 if they no longer touch.
float sumContactImpulse(b2ShapeId Shape, b2ShapeId Other) {
    std::array<b2ContactData, MaxContactsPerShape> Contacts{};
    const int Count = b2Shape_GetContactData(Shape, Contacts.data(), MaxContactsPerShape);
    const uint64_t OtherId = b2StoreShapeId(Other);

    float Impulse = 0.0f;
    for (const auto& Contact : std::span(Contacts).first(static_cast<size_t>(Count))) {
        const bool IsPair = b2StoreShapeId(Contact.shapeIdA) == OtherId || b2StoreShapeId(Contact.shapeIdB) == OtherId;
        if (!IsPair) continue;
        for (const auto& Point : std::span(Contact.manifold.points).first(
                 static_cast<size_t>(Contact.manifold.pointCount))) {
            Impulse += Point.totalNormalImpulse;
        }
    }
    return Impulse;
}

std::span<b2ShapeId> getShapes(b2BodyId BodyId, std::array<b2ShapeId, MaxShapesPerBody>& Storage) {
    const int Count = b2Body_GetShapes(BodyId, Storage.data(), MaxShapesPerBody);
    return std::span(Storage).first(static_cast<size_t>(Count));
}

/// The shape as a point cloud with a radius, in its body's frame.
b2ShapeProxy makeLocalProxy(b2ShapeId Shape) {
    switch (b2Shape_GetType(Shape)) {
        case b2_circleShape: {
            const b2Circle Circle = b2Shape_GetCircle(Shape);
            return b2MakeProxy(&Circle.center, 1, Circle.radius);
        }
        case b2_capsuleShape: {
            const b2Capsule Capsule = b2Shape_GetCapsule(Shape);
            const std::array<b2Vec2, 2> Points = {Capsule.center1, Capsule.center2};
            return b2MakeProxy(Points.data(), 2, Capsule.radius);
        }
        case b2_polygonShape: {
            const b2Polygon Polygon = b2Shape_GetPolygon(Shape);
            return b2MakeProxy(Polygon.vertices, Polygon.count, Polygon.radius);
        }
        default:
            return {};
    }
}

/// The closest approach of the shapes of two bodies placed at the given
/// transforms (the deepest overlap if they overlap).
ShapeGap measureBodyGap(b2BodyId BodyA, b2Transform TransformA, b2BodyId BodyB, b2Transform TransformB) {
    std::array<b2ShapeId, MaxShapesPerBody> StorageA{};
    std::array<b2ShapeId, MaxShapesPerBody> StorageB{};
    const std::span<b2ShapeId> ShapesB = getShapes(BodyB, StorageB);
    ShapeGap Closest{.Distance = std::numeric_limits<float>::max()};
    for (const auto& ShapeA : getShapes(BodyA, StorageA)) {
        for (const auto& ShapeB : ShapesB) {
            const ShapeGap Gap = measureGap(ShapeA, TransformA, ShapeB, TransformB);
            if (Gap.Distance < Closest.Distance) Closest = Gap;
        }
    }
    return Closest;
}

/// A body transform from a position and a rotation given as (cos, sin).
b2Transform makeTransform(Vec2 Position, Vec2 Rotation) { return {toBox2D(Position), {Rotation.X, Rotation.Y}}; }

/// The world transform of the body a shape belongs to.
b2Transform getTransform(b2ShapeId Shape) { return b2Body_GetTransform(b2Shape_GetBody(Shape)); }

/// The closest approach of two shapes with their bodies placed at the given
/// transforms.
ShapeGap measureGap(b2ShapeId ShapeA, b2Transform TransformA, b2ShapeId ShapeB, b2Transform TransformB) {
    b2DistanceInput Input{};
    Input.proxyA = makeLocalProxy(ShapeA);
    Input.proxyB = makeLocalProxy(ShapeB);
    Input.transformA = TransformA;
    Input.transformB = TransformB;
    // The cores without their radii: their closest points give a normal even
    // when the rounded surfaces overlap.
    Input.useRadii = false;
    b2SimplexCache Cache{};
    const b2DistanceOutput Output = b2ShapeDistance(&Input, &Cache, nullptr, 0);

    // Cores that intersect have no distance: the depth comes from the
    // contact manifold of the two shapes, as the solver sees it.
    if (Output.distance <= CoreTouchDistance) {
        if (const auto Deep = measureDeepOverlap(ShapeA, TransformA, ShapeB, TransformB)) return *Deep;
    }
    const float RadiusA = Input.proxyA.radius;
    const float RadiusB = Input.proxyB.radius;
    Vec2 Normal = fromBox2D(Output.normal);
    if (Output.distance <= 0.0f || Normal.getLength() < 0.5f) {
        // The cores intersect: apart along the line between the bodies.
        Normal = (fromBox2D(TransformB.p) - fromBox2D(TransformA.p)).getNormalized();
    }
    const Vec2 SurfaceA = fromBox2D(Output.pointA) + Normal * RadiusA;
    const Vec2 SurfaceB = fromBox2D(Output.pointB) - Normal * RadiusB;
    return {.Distance = Output.distance - RadiusA - RadiusB, .Point = (SurfaceA + SurfaceB) * 0.5f, .Normal = Normal};
}

/// How deep two shapes whose cores intersect overlap: the deepest point of
/// their contact manifold. Nullopt if the manifold has no point.
std::optional<ShapeGap> measureDeepOverlap(b2ShapeId ShapeA, b2Transform TransformA, b2ShapeId ShapeB,
                                           b2Transform TransformB) {
    const b2ShapeType TypeA = b2Shape_GetType(ShapeA);
    const b2ShapeType TypeB = b2Shape_GetType(ShapeB);
    // The functions take the "larger" shape first: polygon, capsule, circle.
    const auto getRank = [](b2ShapeType Type) {
        return Type == b2_polygonShape ? 0 : Type == b2_capsuleShape ? 1 : Type == b2_circleShape ? 2 : 3;
    };
    if (getRank(TypeA) > 2 || getRank(TypeB) > 2) return std::nullopt;
    const bool Swapped = getRank(TypeA) > getRank(TypeB);
    const b2ShapeId First = Swapped ? ShapeB : ShapeA;
    const b2ShapeId Second = Swapped ? ShapeA : ShapeB;
    const b2Transform FirstAt = Swapped ? TransformB : TransformA;
    const b2Transform SecondAt = Swapped ? TransformA : TransformB;

    b2Manifold Manifold{};
    const b2ShapeType FirstType = b2Shape_GetType(First);
    const b2ShapeType SecondType = b2Shape_GetType(Second);
    if (FirstType == b2_polygonShape) {
        const b2Polygon Polygon = b2Shape_GetPolygon(First);
        if (SecondType == b2_polygonShape) {
            const b2Polygon Other = b2Shape_GetPolygon(Second);
            Manifold = b2CollidePolygons(&Polygon, FirstAt, &Other, SecondAt);
        } else if (SecondType == b2_capsuleShape) {
            const b2Capsule Other = b2Shape_GetCapsule(Second);
            Manifold = b2CollidePolygonAndCapsule(&Polygon, FirstAt, &Other, SecondAt);
        } else {
            const b2Circle Other = b2Shape_GetCircle(Second);
            Manifold = b2CollidePolygonAndCircle(&Polygon, FirstAt, &Other, SecondAt);
        }
    } else if (FirstType == b2_capsuleShape) {
        const b2Capsule Capsule = b2Shape_GetCapsule(First);
        if (SecondType == b2_capsuleShape) {
            const b2Capsule Other = b2Shape_GetCapsule(Second);
            Manifold = b2CollideCapsules(&Capsule, FirstAt, &Other, SecondAt);
        } else {
            const b2Circle Other = b2Shape_GetCircle(Second);
            Manifold = b2CollideCapsuleAndCircle(&Capsule, FirstAt, &Other, SecondAt);
        }
    } else {
        const b2Circle Circle = b2Shape_GetCircle(First);
        const b2Circle Other = b2Shape_GetCircle(Second);
        Manifold = b2CollideCircles(&Circle, FirstAt, &Other, SecondAt);
    }
    if (Manifold.pointCount == 0) return std::nullopt;
    const auto Points = std::span(Manifold.points).first(static_cast<size_t>(Manifold.pointCount));
    const auto Deepest = std::ranges::min_element(Points, {}, &b2ManifoldPoint::separation);
    // The manifold normal points from its first shape to its second one.
    const Vec2 Normal = fromBox2D(Manifold.normal) * (Swapped ? -1.0f : 1.0f);
    return ShapeGap{.Distance = Deepest->separation, .Point = fromBox2D(Deepest->point), .Normal = Normal};
}

} // namespace

} // namespace fighter::physics
