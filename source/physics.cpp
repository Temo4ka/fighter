#include "../include/physics.hpp"
#include "../include/hitbox.hpp"
#include "../include/config.hpp"
#include "../include/DSL.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

// ==================== SpatialGrid Implementation ====================

void SpatialGrid::clear() {
    grid.clear();
}

std::string SpatialGrid::getCellKey(const Vec2& position) {
    int cellX = static_cast<int>(position.x / cellSize);
    int cellY = static_cast<int>(position.y / cellSize);
    
    std::ostringstream oss;
    oss << cellX << "," << cellY;
    return oss.str();
}

std::vector<std::string> SpatialGrid::getCellKeys(const Entity* entity) {
    std::vector<std::string> keys;
    
    // Получаем границы объекта для определения всех ячеек
    const Hitbox& hitbox = entity->getHitbox();
    const std::vector<Rect>& rects = hitbox.getRects();
    
    for (const auto& rect : rects) {
        Vec2 minPos = rect.getPos();
        Vec2 maxPos = minPos + rect.getSize();
        
        int minCellX = static_cast<int>(minPos.x / cellSize);
        int maxCellX = static_cast<int>(maxPos.x / cellSize);
        int minCellY = static_cast<int>(minPos.y / cellSize);
        int maxCellY = static_cast<int>(maxPos.y / cellSize);
        
        for (int x = minCellX; x <= maxCellX; ++x) {
            for (int y = minCellY; y <= maxCellY; ++y) {
                std::ostringstream oss;
                oss << x << "," << y;
                keys.push_back(oss.str());
            }
        }
    }
    
    return keys;
}

void SpatialGrid::addEntity(Entity* entity) {
    std::vector<std::string> keys = getCellKeys(entity);
    for (const auto& key : keys) {
        grid[key].push_back(entity);
    }
}

void SpatialGrid::removeEntity(Entity* entity) {
    for (auto& cell : grid) {
        auto& entities = cell.second;
        entities.erase(std::remove(entities.begin(), entities.end(), entity), entities.end());
    }
}

void SpatialGrid::updateEntity(Entity* entity) {
    removeEntity(entity);
    addEntity(entity);
}

std::vector<Entity*> SpatialGrid::getNearbyEntities(const Entity* entity) {
    std::vector<Entity*> nearby;
    std::vector<std::string> keys = getCellKeys(entity);
    
    for (const auto& key : keys) {
        auto it = grid.find(key);
        if (it != grid.end()) {
            for (Entity* nearbyEntity : it->second) {
                if (nearbyEntity != entity) {
                    nearby.push_back(nearbyEntity);
                }
            }
        }
    }
    
    // Убираем дубликаты
    std::sort(nearby.begin(), nearby.end());
    nearby.erase(std::unique(nearby.begin(), nearby.end()), nearby.end());
    
    return nearby;
}

// ==================== PhysicsModule Implementation ====================

void PhysicsModule::addObject(Entity* const obj) { 
    all_objects.push_back(obj);
    spatialGrid.addEntity(obj);
}

void PhysicsModule::eraseObject(Entity* const object) {
    auto it = std::find(all_objects.begin(), all_objects.end(), object);
    if (it != all_objects.end()) {
        all_objects.erase(it);
        spatialGrid.removeEntity(object);
    }
}

void PhysicsModule::updateObjects(Time_t dt) {
    MSG("Enhanced Physics Update");
    
    // Применяем физику ко всем объектам
    for (auto obj : all_objects) {
        applyPhysics(obj, dt);
    }
    
    // Проверяем коллизии с несколькими итерациями для стабильности
    for (int i = 0; i < collisionIterations; ++i) {
        collideObjects();
    }
    
    // Применяем ограничения
    for (auto obj : all_objects) {
        applyConstraints(obj);
        MESSAGE("%p has physics coordinates: (%g, %g)", obj, obj->position.x, obj->position.y);
    }
}

void PhysicsModule::applyPhysics(Entity* obj, Time_t dt) {
    if (obj->getState() == Entity::FIXED) {
        return; // Неподвижные объекты не обновляются
    }
    
    // Применяем гравитацию
    if (obj->getState() == Entity::IN_AIR) {
        obj->velocity += gravity * dt;
    }
    
    // Обновляем позицию
    obj->position += obj->velocity * dt;
    
    // Обновляем вращение
    obj->rotation += obj->angularVelocity * dt;
    
    // Применяем сопротивление воздуха
    if (obj->airResistance > 0.0) {
        double airResist = obj->airResistance + globalAirResistance;
        obj->velocity *= (1.0 - airResist * dt);
    }
    
    // Применяем трение
    if (obj->getState() != Entity::IN_AIR && obj->friction > 0.0) {
        double friction = obj->friction + globalFriction;
        obj->velocity *= (1.0 - friction * dt);
    }
    
    // Ограничиваем максимальную скорость
    double currentSpeed = std::sqrt(obj->velocity.x * obj->velocity.x + obj->velocity.y * obj->velocity.y);
    if (currentSpeed > obj->maxVelocity) {
        double scale = obj->maxVelocity / currentSpeed;
        obj->velocity *= scale;
    }
    
    // Обновляем состояние объекта
    updateEntityState(obj);
    
    // Обновляем пространственную сетку
    spatialGrid.updateEntity(obj);
}

void PhysicsModule::updateEntityState(Entity* obj) {
    // Определяем состояние объекта на основе его физических свойств
    if (obj->getState() == Entity::FIXED) {
        return;
    }
    
    double speed = std::sqrt(obj->velocity.x * obj->velocity.x + obj->velocity.y * obj->velocity.y);
    
    if (speed < 0.1) {
        obj->setState(Entity::STILL);
    } else if (obj->getState() == Entity::IN_AIR) {
        // Остаемся в воздухе
    } else if (std::abs(obj->velocity.y) < 0.1) {
        if (speed > 0.1) {
            obj->setState(Entity::ON_MOVE);
        } else {
            obj->setState(Entity::STILL);
        }
    } else {
        obj->setState(Entity::IN_AIR);
    }
}

void PhysicsModule::collideObjects() {
    // Используем пространственную сетку для оптимизации
    for (auto obj : all_objects) {
        if (obj->getState() == Entity::FIXED) continue;
        
        std::vector<Entity*> nearby = spatialGrid.getNearbyEntities(obj);
        
        for (auto other : nearby) {
            if (obj != other && Hitbox::checkHitboxCollision(obj->getHitbox(), other->getHitbox())) {
                MSG("HERE");
                handleCollision(obj, other);
            }
        }
    }
}

void PhysicsModule::handleCollision(Entity* obj1, Entity* obj2) {
    if (advancedPhysics) {
        resolveCollision(obj1, obj2);
    } else {
        // Простая обработка - останавливаем объекты
        if (obj1->getState() != Entity::FIXED) {
            obj1->velocity = Vec2(0, 0);
        }
        if (obj2->getState() != Entity::FIXED) {
            obj2->velocity = Vec2(0, 0);
        }
    }
}

void PhysicsModule::resolveCollision(Entity* obj1, Entity* obj2) {
    // Определяем нормаль коллизии
    Vec2 normal = obj2->position - obj1->position;
    double distance = std::sqrt(normal.x * normal.x + normal.y * normal.y);
    
    if (distance < 0.001) return; // Объекты слишком близко
    
    normal = normal / distance;
    
    // Отделяем объекты
    double separation = 0.1; // Минимальное расстояние
    if (distance < separation) {
        Vec2 correction = normal * (separation - distance) * 0.5;
        if (obj1->getState() != Entity::FIXED) {
            obj1->position -= correction;
        }
        if (obj2->getState() != Entity::FIXED) {
            obj2->position += correction;
        }
    }
    
    // Применяем упругое столкновение
    if (obj1->getState() != Entity::FIXED && obj2->getState() != Entity::FIXED) {
        elasticCollision(obj1, obj2);
    }
}

void PhysicsModule::elasticCollision(Entity* obj1, Entity* obj2) {
    // Формула упругого столкновения
    Vec2 relativeVelocity = obj2->velocity - obj1->velocity;
    Vec2 normal = (obj2->position - obj1->position).normalized();
    
    double velocityAlongNormal = relativeVelocity.dot(normal);
    
    // Если объекты удаляются друг от друга, не обрабатываем
    if (velocityAlongNormal > 0) return;
    
    // Вычисляем импульс
    double restitution = std::min(obj1->elasticity, obj2->elasticity);
    double j = -(1 + restitution) * velocityAlongNormal;
    j /= 1/obj1->mass + 1/obj2->mass;
    
    // Применяем импульс
    Vec2 impulse = j * normal;
    
    if (obj1->getState() != Entity::FIXED) {
        obj1->velocity -= impulse / obj1->mass;
    }
    if (obj2->getState() != Entity::FIXED) {
        obj2->velocity += impulse / obj2->mass;
    }
}

void PhysicsModule::applyImpulse(Entity* obj, const Vec2& impulse) {
    if (obj->getState() != Entity::FIXED) {
        obj->velocity += impulse / obj->mass;
    }
}

void PhysicsModule::applyAngularImpulse(Entity* obj, double torque) {
    if (obj->getState() != Entity::FIXED) {
        // Простая модель - считаем момент инерции как массу * радиус²
        double momentOfInertia = obj->mass * 100.0; // Примерное значение
        obj->angularVelocity += torque / momentOfInertia;
    }
}

void PhysicsModule::applyConstraints(Entity* obj) {
    // Ограничиваем объекты границами экрана
    if (obj->position.x < 0) {
        obj->position.x = 0;
        if (obj->velocity.x < 0) obj->velocity.x = 0;
    }
    if (obj->position.x > WINDOW_WID) {
        obj->position.x = WINDOW_WID;
        if (obj->velocity.x > 0) obj->velocity.x = 0;
    }
    if (obj->position.y > WINDOW_HGT) {
        obj->position.y = WINDOW_HGT;
        if (obj->velocity.y > 0) obj->velocity.y = 0;
        obj->setState(Entity::STILL);
    }
    
    // Ограничиваем угловую скорость
    if (std::abs(obj->angularVelocity) > 10.0) {
        obj->angularVelocity = (obj->angularVelocity > 0) ? 10.0 : -10.0;
    }
}