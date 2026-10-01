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

constexpr unsigned WindowWidth = 1280;
constexpr unsigned WindowHeight = 720;

// Test fighters. In phase 2 they will be read from data/fighters/*.json.
combat::BattleConfig makeSandboxBattle() {
    combat::BattleConfig Config;
    Config.Left.Stats = {.Strength = 12, .Dexterity = 10, .Constitution = 10};
    Config.Right.Stats = {.Strength = 10, .Dexterity = 12, .Constitution = 12};
    return Config;
}

const char* getWinnerName(combat::Winner W) {
    switch (W) {
        case combat::Winner::Left: return "P1";
        case combat::Winner::Right: return "P2";
        case combat::Winner::Draw: return "draw";
    }
    return "?";
}

} // namespace

App::App(Options Settings)
    : Opts(std::move(Settings)),
      Window(sf::VideoMode({WindowWidth, WindowHeight}), "Fighter sandbox"),
      Assets(Opts.Root),
      Renderer(Assets)
#if FIGHTER_DEBUG
      , Overlay(Assets)
#endif
{
    Window.setVerticalSyncEnabled(true);
    Cam.setWindowSize(Window.getSize());

#if FIGHTER_DEBUG
    ShowcaseVisible = Opts.Showcase;
    if (Opts.Mode == "both") Overlay.setMode(render::ViewMode::Both);
    if (Opts.Mode == "textures") Overlay.setMode(render::ViewMode::TexturesOnly);
#endif

    restartBattle();
    log::info("sandbox started, root: {}", Opts.Root.string());
}

int App::run() {
    sf::Clock Clock;
    int Frame = 0;

    while (Window.isOpen()) {
        const double FrameSec = Clock.restart().asSeconds();
        handleEvents();

        const double Alpha = Loop.advance(FrameSec, [this](double Dt) { stepSimulation(Dt); });
        publishFrameStats(FrameSec);
        render(static_cast<float>(Alpha));

        if (Opts.Screenshot && ++Frame >= Opts.Frames) {
            saveScreenshot();
            Window.close();
            break;
        }
        Window.display();
    }
    return 0;
}

void App::handleEvents() {
    while (const std::optional Event = Window.pollEvent()) {
        if (Event->is<sf::Event::Closed>()) {
            Window.close();
        } else if (const auto* Resized = Event->getIf<sf::Event::Resized>()) {
            Cam.setWindowSize(Resized->size);
        } else if (Event->is<sf::Event::FocusLost>()) {
            Input.reset();
        } else if (const auto* Pressed = Event->getIf<sf::Event::KeyPressed>()) {
            onKeyPressed(Pressed->scancode);
        } else if (const auto* Released = Event->getIf<sf::Event::KeyReleased>()) {
            // Always pass releases on, otherwise a key may stick.
            Input.onKey(Released->scancode, false);
        }
    }
}

void App::onKeyPressed(sf::Keyboard::Scancode Key) {
    if (Key == sf::Keyboard::Scan::Escape) {
        Window.close();
        return;
    }
#if FIGHTER_DEBUG
    if (const auto Result = Overlay.handleKey(Key); Result.Consumed) {
        applyDebugAction(Result.Action);
        return;
    }
#endif
    Input.onKey(Key, true);
}

void App::stepSimulation(double Dt) {
    debug::beginTick();

    Previous = CurrentBattle->getSnapshot();
    CurrentBattle->update(Input.getCommands(0), Input.getCommands(1), Dt);

#if FIGHTER_DEBUG
    if (ShowcaseVisible) drawDebugShowcase();
#endif

    if (const auto Result = CurrentBattle->getResult(); Result && !ResultReported) {
        ResultReported = true;
        log::info("round over: winner {}, {:.1f} s", getWinnerName(Result->WinnerSide), Result->TimeSec);
        debug::logEvent(std::format("round over: winner {}", getWinnerName(Result->WinnerSide)));
    }
}

void App::render(float Alpha) {
    Window.clear(sf::Color::Black);
    const combat::RenderSnapshot Snapshot =
        combat::interpolate(Previous, CurrentBattle->getSnapshot(), Alpha);

#if FIGHTER_DEBUG
    if (Overlay.shouldShowTextures()) {
        Renderer.drawWorld(Window, Cam, Snapshot);
    } else {
        Overlay.drawBackdrop(Window, Cam);
    }
    if (Overlay.shouldShowPrimitives()) Overlay.drawPrimitives(Window, Cam, debug::getDrawList());
    Renderer.drawHud(Window, Cam, Snapshot);
    Overlay.drawPanel(Window, Cam, debug::getDrawList());
#else
    Renderer.drawWorld(Window, Cam, Snapshot);
    Renderer.drawHud(Window, Cam, Snapshot);
#endif
}

void App::restartBattle() {
    CurrentBattle = std::make_unique<combat::Battle>(makeSandboxBattle());
    Previous = CurrentBattle->getSnapshot();
    ResultReported = false;
    Loop.reset();
    Input.reset();
}

void App::publishFrameStats(double FrameSec) {
    // Smooth the frame time rather than FPS: an average of 1/t is skewed
    // upwards by rare fast frames.
    FrameSecSmoothed = FrameSecSmoothed == 0.0 ? FrameSec : FrameSecSmoothed * 0.95 + FrameSec * 0.05;
    if constexpr (FIGHTER_DEBUG) {
        const double Fps = FrameSecSmoothed > 0.0 ? 1.0 / FrameSecSmoothed : 0.0;
        debug::setPanel("fps", std::format("{:.0f}  ({:.2f} ms)", Fps, FrameSecSmoothed * 1000.0));
        debug::setPanel("tick", std::format("{}  (+{} this frame)", Loop.getTick(), Loop.getStepsLastAdvance()));
        debug::setPanel("sim", std::format("{}  x{:.2f}", Loop.isPaused() ? "PAUSED" : "running",
                                           Loop.getTimeScale()));
    }
}

void App::saveScreenshot() {
    sf::Texture Capture(Window.getSize());
    Capture.update(Window);
    if (Capture.copyToImage().saveToFile(*Opts.Screenshot)) {
        log::info("screenshot saved: {}", Opts.Screenshot->string());
    } else {
        log::error("failed to save screenshot: {}", Opts.Screenshot->string());
    }
}

#if FIGHTER_DEBUG
void App::applyDebugAction(render::DebugAction Action) {
    using render::DebugAction;
    switch (Action) {
        case DebugAction::None:
            break;
        case DebugAction::TogglePause:
            Loop.setPaused(!Loop.isPaused());
            break;
        case DebugAction::Step:
            if (!Loop.isPaused()) Loop.setPaused(true);
            Loop.requestSingleStep();
            break;
        case DebugAction::Slower:
            Loop.setTimeScale(Loop.getTimeScale() * 0.5);
            break;
        case DebugAction::Faster:
            Loop.setTimeScale(Loop.getTimeScale() * 2.0);
            break;
        case DebugAction::Restart:
            restartBattle();
            debug::logEvent("restart");
            break;
        case DebugAction::Reload:
            // Nothing to reload in phase 0: JSON configs arrive in phases 1-2.
            restartBattle();
            debug::logEvent("reload: no data files yet, restarted");
            break;
        case DebugAction::ToggleShowcase:
            ShowcaseVisible = !ShowcaseVisible;
            break;
    }
}
#endif

} // namespace fighter::app
