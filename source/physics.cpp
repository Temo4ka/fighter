#include "../include/physics.hpp"
#include "../include/hitbox.hpp"
#include "../include/config.hpp"

void PhysicsModule::addObject(Entity* const obj) { 
    all_objects.push_back(obj);
}

void PhysicsModule::eraseObject(Entity* const object) {
    for (auto it = all_objects.begin(); it != all_objects.end(); it++)
        if (*it == object) {
            all_objects.erase(it);
            break;
        }
}

void PhysicsModule::updateObjects(Time_t dt) {
    collideObjects();

    for (auto it : all_objects) {
        it->position += it->velocity * dt + Vec2(0, G) * dt * dt;
        it->velocity += Vec2(0, G) * dt;
    }
}

void PhysicsModule::collideObjects() {
    for (size_t f_ind = 0; f_ind < all_objects.size(); f_ind++) {
        auto f_obj = all_objects[f_ind];

        for (size_t s_ind = f_ind + 1; s_ind < all_objects.size(); s_ind++) {
            auto s_obj = all_objects[s_ind];

            if (Hitbox::checkHitboxCollision(f_obj->getHitbox(), s_obj->getHitbox())) {
                // hui znaet chto dalshe
            }
        }
    }
}