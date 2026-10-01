#include "physics/world.hpp"

#include <utility>

#include <box2d/box2d.h>

namespace fighter::physics {
namespace {

b2WorldId load(std::uint32_t id) { return b2LoadWorldId(id); }

} // namespace

World::World(Config config) : subSteps_(config.subSteps) {
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = b2Vec2{config.gravity.x, config.gravity.y};
    id_ = b2StoreWorldId(b2CreateWorld(&def));
}

World::~World() { destroy(); }

World::World(World&& other) noexcept
    : id_(std::exchange(other.id_, 0)), subSteps_(other.subSteps_) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = std::exchange(other.id_, 0);
        subSteps_ = other.subSteps_;
    }
    return *this;
}

void World::destroy() {
    if (id_ != 0) {
        b2DestroyWorld(load(id_));
        id_ = 0;
    }
}

void World::step(float dt) {
    b2World_Step(load(id_), dt, subSteps_);
}

Vec2 World::gravity() const {
    const b2Vec2 g = b2World_GetGravity(load(id_));
    return {g.x, g.y};
}

int World::bodyCount() const {
    return b2World_GetCounters(load(id_)).bodyCount;
}

} // namespace fighter::physics
