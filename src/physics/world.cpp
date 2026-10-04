#include "physics/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
/// Bisection steps of findPosedStop(): the share of the step is found to
/// 2^-20, far below a millimetre for any limb speed.
constexpr int StopSearchSteps = 20;
/// A pair of posed parts held at a contact deeper than this many stop depths
/// before the step sank in while nothing stopped it (findPosedStop()).
constexpr float InsideFactor = 2.0f;

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
b2Transform getTransform(b2ShapeId Shape);

} // namespace

World::World(Config Settings) : SubSteps(Settings.SubSteps), HitSpeedThreshold(Settings.HitSpeedThreshold) {
    b2WorldDef Def = b2DefaultWorldDef();
    Def.gravity = toBox2D(Settings.Gravity);
    Def.hitEventThreshold = Settings.HitSpeedThreshold;
    // Fighters must never fall asleep: their motors work every step.
    Def.enableSleep = false;
    Id = b2StoreWorldId(b2CreateWorld(&Def));
}

World::~World() { destroy(); }

World::World(World&& Other) noexcept
    : Id(std::exchange(Other.Id, 0)),
      SubSteps(Other.SubSteps),
      PartBodies(std::move(Other.PartBodies)),
      Hits(std::move(Other.Hits)),
      HitSpeedThreshold(Other.HitSpeedThreshold),
      TouchingPosed(std::move(Other.TouchingPosed)) {}

World& World::operator=(World&& Other) noexcept {
    if (this != &Other) {
        destroy();
        Id = std::exchange(Other.Id, 0);
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
    b2World_Step(loadWorld(Id), Dt, SubSteps);
    collectHits();
    collectPosedHits();
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

    const b2Transform Transform = b2Body_GetTransform(BodyId);
    std::array<b2ShapeId, MaxShapesPerBody> Storage{};
    for (const auto& Shape : getShapes(BodyId, Storage)) {
        const b2ShapeProxy Local = makeLocalProxy(Shape);
        const b2ShapeProxy Placed =
            b2MakeOffsetProxy(Local.points, Local.count, Local.radius, Transform.p, Transform.q);
        b2World_OverlapShape(loadWorld(Id), &Placed, b2DefaultQueryFilter(), onOverlap, &State);
        if (State.Found) return true;
    }
    return false;
}

float World::getPosedPenetration(Body Target) const {
    const auto Slot = findSlot(Target);
    if (!Slot || Target.getType() != BodyType::Kinematic) return 0.0f;
    const PartBody& Entry = PartBodies[*Slot];
    return measurePosedPenetration(Entry, getTransformDuringStep(Entry, 1.0f));
}

std::optional<float> World::findPosedStop(std::span<const Body> Strikers, float MaxDepth, bool Holding) const {
    // The pairs of a striker and an opponent's posed part to keep apart. A
    // pair that touched before the step is a contact that began while
    // nothing stopped it (in the startup of a kick): it is left alone, there
    // is no contact to go back to and it can be no hit. A pair held at the
    // contact is about MaxDepth deep, so while holding the limit has room.
    const float MaxBefore = Holding ? MaxDepth * InsideFactor : 0.0f;
    struct Pair {
        const PartBody* Mover = nullptr;
        const PartBody* Other = nullptr;
    };
    std::vector<Pair> Pairs;
    for (const auto& Striker : Strikers) {
        const auto Slot = findSlot(Striker);
        if (!Slot || Striker.getType() != BodyType::Kinematic) continue;
        const PartBody& Mover = PartBodies[*Slot];
        for (const auto& Other : PartBodies) {
            if (Other.Part.Fighter == Mover.Part.Fighter || Other.Handle.getType() != BodyType::Kinematic) continue;
            const float Before = measurePairPenetration(Mover, getTransformDuringStep(Mover, 0.0f), Other,
                                                        getTransformDuringStep(Other, 0.0f));
            if (Before <= MaxBefore) Pairs.push_back({.Mover = &Mover, .Other = &Other});
        }
    }
    // The opponent's parts stay where they are now.
    const auto getDeepest = [&](float Fraction) {
        float Deepest = 0.0f;
        for (const auto& Entry : Pairs) {
            Deepest = std::max(Deepest, measurePairPenetration(*Entry.Mover,
                                                               getTransformDuringStep(*Entry.Mover, Fraction),
                                                               *Entry.Other, getTransformDuringStep(*Entry.Other, 1.0f)));
        }
        return Deepest;
    };
    const auto isTooDeep = [&](float Fraction) { return getDeepest(Fraction) > MaxDepth; };
    const float DeepestNow = getDeepest(1.0f);
    if (DeepestNow <= 0.0f) return std::nullopt;   // no contact
    if (DeepestNow <= MaxDepth) return 1.0f;
    if (isTooDeep(0.0f)) return 0.0f;   // the opponent moved into it
    // The largest share that is not too deep: Low is fine, High is not.
    float Low = 0.0f;
    float High = 1.0f;
    for (int Step = 0; Step < StopSearchSteps; ++Step) {
        const float Middle = (Low + High) * 0.5f;
        (isTooDeep(Middle) ? High : Low) = Middle;
    }
    return Low;
}

void World::rewindBody(Body Target, float Fraction) {
    const auto Slot = findSlot(Target);
    if (!Slot) return;
    const Transform Placed = getTransformDuringStep(PartBodies[*Slot], Fraction);
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
    Hits.clear();
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

float World::measurePosedPenetration(const PartBody& Entry, const Transform& Placed) const {
    float Deepest = 0.0f;
    for (const auto& Other : PartBodies) {
        if (Other.Part.Fighter == Entry.Part.Fighter || Other.Handle.getType() != BodyType::Kinematic) continue;
        Deepest = std::max(Deepest, measurePairPenetration(Entry, Placed, Other, getTransformDuringStep(Other, 1.0f)));
    }
    return Deepest;
}

float World::measurePairPenetration(const PartBody& Entry, const Transform& Placed, const PartBody& Other,
                                    const Transform& OtherPlaced) const {
    const b2Transform Own{toBox2D(Placed.Position), {Placed.Rotation.X, Placed.Rotation.Y}};
    const b2Transform Theirs{toBox2D(OtherPlaced.Position), {OtherPlaced.Rotation.X, OtherPlaced.Rotation.Y}};
    std::array<b2ShapeId, MaxShapesPerBody> OwnStorage{};
    std::array<b2ShapeId, MaxShapesPerBody> OtherStorage{};
    const std::span<b2ShapeId> OtherShapes = getShapes(loadBody(Other.Handle.Id), OtherStorage);
    float Deepest = 0.0f;
    for (const auto& OwnShape : getShapes(loadBody(Entry.Handle.Id), OwnStorage)) {
        for (const auto& OtherShape : OtherShapes) {
            Deepest = std::max(Deepest, -measureGap(OwnShape, Own, OtherShape, Theirs).Distance);
        }
    }
    return Deepest;
}

World::Transform World::getTransformDuringStep(const PartBody& Entry, float Fraction) const {
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

} // namespace

} // namespace fighter::physics
