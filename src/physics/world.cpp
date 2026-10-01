#include "physics/world.hpp"

#include <utility>

#include <box2d/box2d.h>

namespace fighter::physics {
namespace {

b2WorldId loadWorldId(std::uint32_t Packed) { return b2LoadWorldId(Packed); }

} // namespace

World::World(Config C) : SubSteps(C.SubSteps) {
    b2WorldDef Def = b2DefaultWorldDef();
    Def.gravity = b2Vec2{C.Gravity.X, C.Gravity.Y};
    Id = b2StoreWorldId(b2CreateWorld(&Def));
}

World::~World() { destroy(); }

World::World(World&& Other) noexcept
    : Id(std::exchange(Other.Id, 0)), SubSteps(Other.SubSteps) {}

World& World::operator=(World&& Other) noexcept {
    if (this != &Other) {
        destroy();
        Id = std::exchange(Other.Id, 0);
        SubSteps = Other.SubSteps;
    }
    return *this;
}

void World::destroy() {
    if (Id != 0) {
        b2DestroyWorld(loadWorldId(Id));
        Id = 0;
    }
}

void World::step(float Dt) {
    b2World_Step(loadWorldId(Id), Dt, SubSteps);
}

Vec2 World::getGravity() const {
    const b2Vec2 G = b2World_GetGravity(loadWorldId(Id));
    return {G.x, G.y};
}

int World::getBodyCount() const {
    return b2World_GetCounters(loadWorldId(Id)).bodyCount;
}

} // namespace fighter::physics
