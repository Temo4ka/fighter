#include "physics/world.hpp"

#include <cstdint>
#include <utility>

#include <box2d/box2d.h>

namespace fighter::physics {
namespace {

b2WorldId loadWorldId(uint32_t Packed) { return b2LoadWorldId(Packed); }

} // namespace

World::World(Config Settings) : SubSteps(Settings.SubSteps) {
    b2WorldDef Def = b2DefaultWorldDef();
    Def.gravity = b2Vec2{Settings.Gravity.X, Settings.Gravity.Y};
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
    const b2Vec2 Gravity = b2World_GetGravity(loadWorldId(Id));
    return {Gravity.x, Gravity.y};
}

int World::getBodyCount() const {
    return b2World_GetCounters(loadWorldId(Id)).bodyCount;
}

} // namespace fighter::physics
