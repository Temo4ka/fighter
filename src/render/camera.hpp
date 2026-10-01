#pragma once

#include <SFML/Graphics/View.hpp>
#include <SFML/System/Vector2.hpp>

#include "core/vec2.hpp"

// Камера: единственное место в проекте, где мировые координаты превращаются в пиксели
// (docs/DEVELOPMENT_PLAN.md §3.1).
//
// Мир: метры, ось Y вверх, (0, 0) — центр арены на уровне пола.
// Экран: пиксели, ось Y вниз, (0, 0) — левый верхний угол окна.
//
// Рисовать в мировых единицах можно двумя способами:
//  - установить worldView() и передавать в SFML координаты toDraw(world);
//  - перевести точку в пиксели worldToPixel() и рисовать в стандартном виде окна
//    (так рисуется текст, чтобы он не масштабировался вместе с миром).
namespace fighter::render {

class Camera {
public:
    struct Config {
        float viewHeightM = 6.0f;      // сколько метров мира помещается по вертикали
        Vec2 centerM{0.0f, 2.5f};      // центр вида: пол на 0.5 м выше нижнего края
    };

    Camera() : Camera(Config{}) {}
    explicit Camera(Config config);

    // Ширина вида подстраивается под пропорции окна, высота в метрах сохраняется.
    void setWindowSize(sf::Vector2u sizePx);

    sf::Vector2f worldToPixel(Vec2 world) const;
    Vec2 pixelToWorld(sf::Vector2f pixel) const;

    float pixelsPerMeter() const;
    Vec2 viewSizeM() const { return {viewHeightM_ * aspect(), viewHeightM_}; }
    Vec2 centerM() const { return center_; }
    sf::Vector2u windowSize() const { return windowPx_; }

    // Вид SFML в мировых единицах. Y в нём направлен вниз, поэтому точки
    // переводятся через toDraw(): переворачиваем Y у координат, а не у вида —
    // иначе текстуры отрисовывались бы вверх ногами.
    sf::View worldView() const;
    static sf::Vector2f toDraw(Vec2 world) { return {world.x, -world.y}; }

    // Вид в пикселях окна (для HUD и текста). Стандартный вид окна SFML после
    // изменения размера не обновляется, поэтому берём его отсюда.
    sf::View screenView() const;

private:
    float aspect() const;

    float viewHeightM_;
    Vec2 center_;
    sf::Vector2u windowPx_{1280, 720};
};

} // namespace fighter::render
