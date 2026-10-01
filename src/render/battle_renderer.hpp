#pragma once

#include <SFML/Graphics/RenderTarget.hpp>

#include "combat/snapshot.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"

// Отрисовка боя «как в игре»: фон, арена, бойцы, HUD.
// В фазе 0 бойцы — цветные прямоугольники-плейсхолдеры; cutout-спрайты частей тела
// добавит агент C, когда появятся PartTransform (фаза 2).
namespace fighter::render {

class BattleRenderer {
public:
    explicit BattleRenderer(Resources& resources);

    void drawWorld(sf::RenderTarget& target, const Camera& camera, const combat::RenderSnapshot& snapshot);
    void drawHud(sf::RenderTarget& target, const Camera& camera, const combat::RenderSnapshot& snapshot);

private:
    Resources& resources_;
};

} // namespace fighter::render
