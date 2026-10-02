#include "physics/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <box2d/box2d.h>

#include "debug/category.hpp"
#include "physics/box2d_bridge.hpp"

namespace fighter::physics {

using detail::fromBox2D;
using detail::loadBody;
using detail::loadWorld;
using detail::toBox2D;

namespace {

/// Debug draw size of a joint (the circle around the hinge), m.
constexpr float JointDrawSize = 0.05f;
/// Contact points looked at per shape when summing the impulse of a hit.
constexpr int MaxContactsPerShape = 16;

b2Polygon makeBox(const ShapeDef& Shape);
float sumContactImpulse(b2ShapeId Shape, b2ShapeId Other);

} // namespace

World::World(Config Settings) : SubSteps(Settings.SubSteps) {
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
      Hits(std::move(Other.Hits)) {}

World& World::operator=(World&& Other) noexcept {
    if (this != &Other) {
        destroy();
        Id = std::exchange(Other.Id, 0);
        SubSteps = Other.SubSteps;
        PartBodies = std::move(Other.PartBodies);
        Hits = std::move(Other.Hits);
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

        // The attacker is the part that was moving towards the other one
        // faster before the step. The normal points from A to B.
        const Vec2 Point = fromBox2D(Event.point);
        const Vec2 Normal = fromBox2D(Event.normal);
        const float SpeedA = dot(getVelocityBeforeStep(PartA, Point), Normal);
        const float SpeedB = dot(getVelocityBeforeStep(PartB, Point), -Normal);
        const bool AttackerIsA = SpeedA >= SpeedB;

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

        Hits.push_back({
            .Attacker = AttackerIsA ? PartA.Part : PartB.Part,
            .Victim = AttackerIsA ? PartB.Part : PartA.Part,
            .Point = Point,
            .ApproachSpeed = Event.approachSpeed,
            .Impulse = Impulse,
        });
    }
}

Vec2 World::getVelocityBeforeStep(const PartBody& Entry, Vec2 WorldPoint) const {
    // v + w x r in 2D.
    return Entry.VelocityBeforeStep + perp(WorldPoint - Entry.CenterBeforeStep) * Entry.AngularVelocityBeforeStep;
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

} // namespace

} // namespace fighter::physics
