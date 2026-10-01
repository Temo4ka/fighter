#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <SFML/Graphics/RenderWindow.hpp>

#include "app/input.hpp"
#include "combat/battle.hpp"
#include "core/fixed_step_loop.hpp"
#include "render/battle_renderer.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"

#if FIGHTER_DEBUG
#include "render/debug_overlay.hpp"
#endif

// Песочница для запуска боя: окно, ввод, цикл, отрисовка.
// Главное меню, пауза и экран итогов добавит агент F (фаза 2).
namespace fighter::app {

struct Options {
    std::filesystem::path root;                    // корень проекта (assets/, data/)
    std::optional<std::filesystem::path> screenshot;  // сохранить кадр и выйти
    int frames = 60;                               // через сколько кадров делать снимок
    std::optional<std::string> mode;               // debug | both | textures (только debug-сборка)
    bool showcase = false;                         // образцы всех отладочных категорий
};

class App {
public:
    explicit App(Options options);
    int run();

private:
    void handleEvents();
    void onKeyPressed(sf::Keyboard::Scancode key);
    void stepSimulation(double dt);
    void render(float alpha);
    void restartBattle();
    void publishFrameStats(double frameSec);
    void saveScreenshot();

#if FIGHTER_DEBUG
    void applyDebugAction(render::DebugAction action);
#endif

    Options options_;
    sf::RenderWindow window_;
    render::Resources resources_;
    render::Camera camera_;
    render::BattleRenderer battleRenderer_;
#if FIGHTER_DEBUG
    render::DebugOverlay overlay_;
    bool showcase_ = false;
#endif

    InputSystem input_;
    FixedStepLoop loop_;
    std::unique_ptr<combat::Battle> battle_;
    combat::RenderSnapshot previous_;
    bool resultReported_ = false;
    double frameSecSmoothed_ = 0.0;
};

} // namespace fighter::app
