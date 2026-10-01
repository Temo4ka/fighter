#include "app/app.hpp"

#include <format>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include "core/log.hpp"
#include "debug/draw.hpp"

#if FIGHTER_DEBUG
#include "app/debug_showcase.hpp"
#include "debug/draw_list.hpp"
#endif

namespace fighter::app {
namespace {

constexpr unsigned kWindowWidth = 1280;
constexpr unsigned kWindowHeight = 720;

// Тестовые бойцы. В фазе 2 они будут читаться из data/fighters/*.json.
combat::BattleConfig sandboxBattle() {
    combat::BattleConfig config;
    config.left.stats = {.strength = 12, .dexterity = 10, .constitution = 10};
    config.right.stats = {.strength = 10, .dexterity = 12, .constitution = 12};
    return config;
}

const char* winnerName(combat::Winner w) {
    switch (w) {
        case combat::Winner::Left: return "P1";
        case combat::Winner::Right: return "P2";
        case combat::Winner::Draw: return "draw";
    }
    return "?";
}

} // namespace

App::App(Options options)
    : options_(std::move(options)),
      window_(sf::VideoMode({kWindowWidth, kWindowHeight}), "Fighter sandbox"),
      resources_(options_.root),
      battleRenderer_(resources_)
#if FIGHTER_DEBUG
      , overlay_(resources_)
#endif
{
    window_.setVerticalSyncEnabled(true);
    camera_.setWindowSize(window_.getSize());

#if FIGHTER_DEBUG
    showcase_ = options_.showcase;
    if (options_.mode == "both") overlay_.setMode(render::ViewMode::Both);
    if (options_.mode == "textures") overlay_.setMode(render::ViewMode::TexturesOnly);
#endif

    restartBattle();
    log::info("sandbox started, root: {}", options_.root.string());
}

int App::run() {
    sf::Clock clock;
    int frame = 0;

    while (window_.isOpen()) {
        const double frameSec = clock.restart().asSeconds();
        handleEvents();

        const double alpha = loop_.advance(frameSec, [this](double dt) { stepSimulation(dt); });
        publishFrameStats(frameSec);
        render(static_cast<float>(alpha));

        if (options_.screenshot && ++frame >= options_.frames) {
            saveScreenshot();
            window_.close();
            break;
        }
        window_.display();
    }
    return 0;
}

void App::handleEvents() {
    while (const std::optional event = window_.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window_.close();
        } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            camera_.setWindowSize(resized->size);
        } else if (event->is<sf::Event::FocusLost>()) {
            input_.reset();
        } else if (const auto* pressed = event->getIf<sf::Event::KeyPressed>()) {
            onKeyPressed(pressed->scancode);
        } else if (const auto* released = event->getIf<sf::Event::KeyReleased>()) {
            // Отпускание передаём всегда, иначе клавиша может «залипнуть».
            input_.onKey(released->scancode, false);
        }
    }
}

void App::onKeyPressed(sf::Keyboard::Scancode key) {
    if (key == sf::Keyboard::Scan::Escape) {
        window_.close();
        return;
    }
#if FIGHTER_DEBUG
    if (const auto result = overlay_.handleKey(key); result.consumed) {
        applyDebugAction(result.action);
        return;
    }
#endif
    input_.onKey(key, true);
}

void App::stepSimulation(double dt) {
    debug::beginTick();

    previous_ = battle_->snapshot();
    battle_->update(input_.commands(0), input_.commands(1), dt);

#if FIGHTER_DEBUG
    if (showcase_) drawDebugShowcase();
#endif

    if (const auto result = battle_->result(); result && !resultReported_) {
        resultReported_ = true;
        log::info("round over: winner {}, {:.1f} s", winnerName(result->winner), result->timeSec);
        debug::event(std::format("round over: winner {}", winnerName(result->winner)));
    }
}

void App::render(float alpha) {
    window_.clear(sf::Color::Black);
    const combat::RenderSnapshot snapshot = combat::interpolate(previous_, battle_->snapshot(), alpha);

#if FIGHTER_DEBUG
    if (overlay_.showTextures()) {
        battleRenderer_.drawWorld(window_, camera_, snapshot);
    } else {
        overlay_.drawBackdrop(window_, camera_);
    }
    if (overlay_.showPrimitives()) overlay_.drawPrimitives(window_, camera_, debug::drawList());
    battleRenderer_.drawHud(window_, camera_, snapshot);
    overlay_.drawPanel(window_, camera_, debug::drawList());
#else
    battleRenderer_.drawWorld(window_, camera_, snapshot);
    battleRenderer_.drawHud(window_, camera_, snapshot);
#endif
}

void App::restartBattle() {
    battle_ = std::make_unique<combat::Battle>(sandboxBattle());
    previous_ = battle_->snapshot();
    resultReported_ = false;
    loop_.reset();
    input_.reset();
}

void App::publishFrameStats(double frameSec) {
    // Сглаживаем время кадра, а не FPS: среднее от 1/t завышается редкими быстрыми кадрами.
    frameSecSmoothed_ = frameSecSmoothed_ == 0.0 ? frameSec : frameSecSmoothed_ * 0.95 + frameSec * 0.05;
    if constexpr (FIGHTER_DEBUG) {
        const double fps = frameSecSmoothed_ > 0.0 ? 1.0 / frameSecSmoothed_ : 0.0;
        debug::panel("fps", std::format("{:.0f}  ({:.2f} ms)", fps, frameSecSmoothed_ * 1000.0));
        debug::panel("tick", std::format("{}  (+{} this frame)", loop_.tick(), loop_.stepsLastAdvance()));
        debug::panel("sim", std::format("{}  x{:.2f}", loop_.paused() ? "PAUSED" : "running", loop_.timeScale()));
    }
}

void App::saveScreenshot() {
    sf::Texture texture(window_.getSize());
    texture.update(window_);
    if (texture.copyToImage().saveToFile(*options_.screenshot)) {
        log::info("screenshot saved: {}", options_.screenshot->string());
    } else {
        log::error("failed to save screenshot: {}", options_.screenshot->string());
    }
}

#if FIGHTER_DEBUG
void App::applyDebugAction(render::DebugAction action) {
    using render::DebugAction;
    switch (action) {
        case DebugAction::None:
            break;
        case DebugAction::TogglePause:
            loop_.setPaused(!loop_.paused());
            break;
        case DebugAction::Step:
            if (!loop_.paused()) loop_.setPaused(true);
            loop_.requestSingleStep();
            break;
        case DebugAction::Slower:
            loop_.setTimeScale(loop_.timeScale() * 0.5);
            break;
        case DebugAction::Faster:
            loop_.setTimeScale(loop_.timeScale() * 2.0);
            break;
        case DebugAction::Restart:
            restartBattle();
            debug::event("restart");
            break;
        case DebugAction::Reload:
            // В фазе 0 перечитывать нечего: JSON-конфиги появятся в фазах 1–2.
            restartBattle();
            debug::event("reload: no data files yet, restarted");
            break;
        case DebugAction::ToggleShowcase:
            showcase_ = !showcase_;
            break;
    }
}
#endif

} // namespace fighter::app
