#include "app/app.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include "core/log.hpp"
#include "debug/draw.hpp"
#include "stats/describe.hpp"
#include "stats/equipment.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"
#include "ui/fighter_list.hpp"
#include "ui/screen_view.hpp"
#include "ui/ui_config.hpp"

#if FIGHTER_DEBUG
#include "app/debug_showcase.hpp"
#include "debug/draw_list.hpp"
#endif

namespace fighter::app {
namespace {

/// The window is 16:9, as large as fits in this share of the desktop, but
/// not larger than PreferredWindowWidth; it can be resized afterwards.
constexpr unsigned PreferredWindowWidth = 1600;
constexpr float DesktopShare = 0.95f;

sf::Vector2u getWindowSize();
std::optional<ui::MenuKey> getMenuKey(sf::Keyboard::Scancode Key);
ui::ScreenContext makeScreenContext(const combat::Battle& Fight);
combat::FighterConfig makeFighterConfig(const std::filesystem::path& Root, const std::string& FighterName);
void publishStatsPanel(const combat::BattleConfig& Config, const stats::BalanceTable& Balance);

// The built-in test fighters, used when --left / --right are not given.
// With --left <name> / --right <name> a fighter comes from data/fighters.
combat::BattleConfig makeSandboxBattle(const Options& Opts) {
    combat::BattleConfig Config;
    Config.Left.Stats = {.Strength = 12, .Dexterity = 10, .Constitution = 10};
    Config.Right.Stats = {.Strength = 10, .Dexterity = 12, .Constitution = 12};
    if (Opts.LeftFighter) Config.Left = makeFighterConfig(Opts.Root, *Opts.LeftFighter);
    if (Opts.RightFighter) Config.Right = makeFighterConfig(Opts.Root, *Opts.RightFighter);
    Config.DataDir = Opts.Root / "data";
    return Config;
}

std::string getWinnerName(combat::Winner Outcome) {
    switch (Outcome) {
        case combat::Winner::Left: return "P1";
        case combat::Winner::Right: return "P2";
        case combat::Winner::Draw: return "draw";
    }
    return "?";
}

} // namespace

App::App(Options Settings)
    : Opts(std::move(Settings)),
      Window(sf::VideoMode(getWindowSize()), "Fighter sandbox"),
      Assets(Opts.Root),
      Renderer(Assets, FixedStepLoop::Config{}.StepSec),
#if FIGHTER_DEBUG
      Overlay(Assets),
#endif
      Flow(ui::listFighters(Opts.Root / "data" / "fighters"))
{
    Window.setVerticalSyncEnabled(true);
    Cam.setWindowSize(Window.getSize());
    RendererEvents = BattleEvents.connect([this](const combat::BattleEvent& Event, const combat::RenderSnapshot& After) {
        Renderer.onBattleEvent(Event, After);
    });

#if FIGHTER_DEBUG
    ShowcaseVisible = Opts.Showcase;
    if (Opts.Mode == "both") Overlay.setMode(render::ViewMode::Both);
    if (Opts.Mode == "textures") Overlay.setMode(render::ViewMode::TexturesOnly);
#endif

    loadUiConfig();
    FlowCommands = Flow.connectCommands([this](ui::FlowCommand Command) { onFlowCommand(Command); });
    FlowEvents = BattleEvents.connect([this](const combat::BattleEvent& Event, const combat::RenderSnapshot&) {
        Flow.onBattleEvent(Event);
    });
    // Without the menu flag the app starts straight in a battle, as the sandbox always did.
    if (!Opts.Menu) {
        restartBattle();
        Flow.beginBattle();
    }
    log::info("sandbox started, root: {}", Opts.Root.string());
}

int App::run() {
    sf::Clock Clock;
    int Frame = 0;

    while (Window.isOpen()) {
        // A screenshot run advances exactly one step per frame, so frame N
        // always shows tick N regardless of the display rate.
        const double RealFrameSec = Clock.restart().asSeconds();
        const double FrameSec = Opts.Screenshot ? Loop.getStepSec() : RealFrameSec;
        if (Frame < static_cast<int>(Opts.Keys.size()))
            onKeyPressed(Opts.Keys[static_cast<size_t>(Frame)]);
        handleEvents();

        // Only a running battle advances; the pause and the other screens freeze it.
        if (CurrentBattle && Flow.getScreen() == ui::Screen::Battle)
            LastAlpha = static_cast<float>(Loop.advance(FrameSec, [this](double Dt) { stepSimulation(Dt); }));
        publishFrameStats(RealFrameSec);
        render(LastAlpha);

        ++Frame;
        if (Opts.Screenshot && Frame >= Opts.Frames) {
            saveScreenshot();
            Window.close();
            break;
        }
        Window.display();
    }
    return 0;
}

std::vector<sf::Keyboard::Scancode> parseMenuKeys(std::string_view List) {
    using Scan = sf::Keyboard::Scan;
    std::vector<sf::Keyboard::Scancode> Keys;
    while (!List.empty()) {
        const size_t Comma = List.find(',');
        const std::string_view Name = List.substr(0, Comma);
        List = Comma == std::string_view::npos ? std::string_view{} : List.substr(Comma + 1);
        if (Name == "up") Keys.push_back(Scan::Up);
        else if (Name == "down") Keys.push_back(Scan::Down);
        else if (Name == "left") Keys.push_back(Scan::Left);
        else if (Name == "right") Keys.push_back(Scan::Right);
        else if (Name == "enter") Keys.push_back(Scan::Enter);
        else if (Name == "esc") Keys.push_back(Scan::Escape);
    }
    return Keys;
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
    if (Flow.getScreen() != ui::Screen::Battle) {
        if (const auto MenuKey = getMenuKey(Key)) Flow.onKey(*MenuKey);
        return;
    }
    if (Key == sf::Keyboard::Scan::Escape) {
        Input.reset();   // a key held now must not stick through the pause
        Flow.onKey(ui::MenuKey::Back);
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
    if (Opts.Demo) {
        const DemoInput Scripted = getDemoInput(*Opts.Demo, Loop.getTick(), Previous);
        CurrentBattle->update(Scripted.Left, Scripted.Right, Dt);
    } else {
        CurrentBattle->update(Input.getCommands(0), Input.getCommands(1), Dt);
    }
    for (const combat::BattleEvent& Event : CurrentBattle->getEvents())
        BattleEvents.emit(Event, CurrentBattle->getSnapshot());

#if FIGHTER_DEBUG
    if (ShowcaseVisible) drawDebugShowcase();
#endif

    Flow.update(Dt);

    if (const auto Result = CurrentBattle->getResult(); Result && !ResultReported) {
        ResultReported = true;
        log::info("round over: winner {}, {:.1f} s", getWinnerName(Result->WinnerSide), Result->TimeSec);
        debug::logEvent(std::format("round over: winner {}", getWinnerName(Result->WinnerSide)));
    }
}

void App::render(float Alpha) {
    Window.clear(sf::Color::Black);
    if (!hasBattleOnScreen()) {
        ui::drawScreen(Window, Assets, Flow, {});
        return;
    }
    const combat::RenderSnapshot Snapshot =
        combat::interpolate(Previous, CurrentBattle->getSnapshot(), Alpha);

    Renderer.buildFrame(Cam, Snapshot, Alpha);

#if FIGHTER_DEBUG
    if (Overlay.shouldShowTextures()) {
        Renderer.drawWorld(Window);
    } else {
        Overlay.drawBackdrop(Window, Cam);
    }
    if (Overlay.shouldShowPrimitives()) Overlay.drawPrimitives(Window, Cam, debug::getDrawList());
    Renderer.drawHud(Window);
    Overlay.drawPanel(Window, Cam, debug::getDrawList());
    // The pause and results go over the debug panel, which stays readable underneath.
    ui::drawScreen(Window, Assets, Flow, makeScreenContext(*CurrentBattle));
#else
    Renderer.drawWorld(Window);
    Renderer.drawHud(Window);
    ui::drawScreen(Window, Assets, Flow, makeScreenContext(*CurrentBattle));
#endif
}

void App::onFlowCommand(ui::FlowCommand Command) {
    switch (Command) {
        case ui::FlowCommand::StartBattle:
            if (!startMenuBattle()) Flow.onStartFailed();
            break;
        case ui::FlowCommand::RestartBattle:
            restartBattle();
            break;
        case ui::FlowCommand::Quit:
            Window.close();
            break;
    }
}

bool App::startMenuBattle() {
    Opts.LeftFighter = Flow.getPickedFighter(0);
    Opts.RightFighter = Flow.getPickedFighter(1);
    try {
        return restartBattle();
    } catch (const std::exception& Error) {
        log::error("cannot start the battle: {}", Error.what());
        return false;
    }
}

bool App::hasBattleOnScreen() const {
    return CurrentBattle && (Flow.getScreen() == ui::Screen::Battle || Flow.getScreen() == ui::Screen::Pause ||
                             Flow.getScreen() == ui::Screen::Results);
}

void App::loadUiConfig() {
    try {
        Flow.setConfig(ui::loadUiConfig(Opts.Root / "data" / "ui.json"));
    } catch (const std::exception& Error) {
        log::error("cannot load data/ui.json: {}", Error.what());
    }
}

bool App::restartBattle() {
    // The battle reads the data files (rigs, poses) when it is created.
    std::unique_ptr<combat::Battle> Fresh;
    try {
        const combat::BattleConfig Config = makeSandboxBattle(Opts);
        // The same table the battle loads, to show the profiles in the debug panel.
        const stats::BalanceTable Balance = stats::loadBalanceTable(Config.DataDir / "balance.json");
        Fresh = std::make_unique<combat::Battle>(Config);
        publishStatsPanel(Config, Balance);
    } catch (const std::exception& Error) {
        if (!CurrentBattle) throw;   // at startup there is nothing to fall back to
        // A typo in a JSON file during live tuning must not close the sandbox.
        log::error("cannot restart the battle: {}", Error.what());
        debug::logEvent(std::format("reload failed: {}", Error.what()));
        return false;
    }
    CurrentBattle = std::move(Fresh);
    const combat::BattleConfig& Config = CurrentBattle->getConfig();
    Renderer.startBattle({render::makeFighterLook(Config.Left, Skins[0], "P1"),
                          render::makeFighterLook(Config.Right, Skins[1], "P2")});
    Previous = CurrentBattle->getSnapshot();
    ResultReported = false;
    Loop.reset();
    Input.reset();
    return true;
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
            if (restartBattle()) {
                Flow.beginBattle();
                debug::logEvent("restart");
            }
            break;
        case DebugAction::Reload:
            // A new battle re-reads data/rigs and data/poses.
            loadUiConfig();
            if (restartBattle()) {
                Flow.beginBattle();
                debug::logEvent("reload: data files re-read, battle restarted");
            }
            if (Renderer.reloadVisuals()) debug::logEvent("reload: visuals re-read");
            break;
        case DebugAction::ToggleShowcase:
            ShowcaseVisible = !ShowcaseVisible;
            break;
    }
}
#endif

namespace {

sf::Vector2u getWindowSize() {
    const sf::Vector2u Desktop = sf::VideoMode::getDesktopMode().size;
    const float FitWidth = std::min(static_cast<float>(Desktop.x) * DesktopShare,
                                    static_cast<float>(Desktop.y) * DesktopShare * 16.0f / 9.0f);
    const auto Width = static_cast<unsigned>(std::clamp(FitWidth, 640.0f, static_cast<float>(PreferredWindowWidth)));
    return {Width, Width * 9 / 16};
}

std::optional<ui::MenuKey> getMenuKey(sf::Keyboard::Scancode Key) {
    using Scan = sf::Keyboard::Scan;
    switch (Key) {
        case Scan::Up: return ui::MenuKey::Up;
        case Scan::Down: return ui::MenuKey::Down;
        case Scan::Left: return ui::MenuKey::Left;
        case Scan::Right: return ui::MenuKey::Right;
        case Scan::Enter: return ui::MenuKey::Confirm;
        case Scan::Escape: return ui::MenuKey::Back;
        default: return std::nullopt;
    }
}

ui::ScreenContext makeScreenContext(const combat::Battle& Fight) {
    const combat::BattleConfig& Config = Fight.getConfig();
    ui::ScreenContext Context;
    Context.Names = {Config.Left.Name.empty() ? "P1" : Config.Left.Name, Config.Right.Name.empty() ? "P2" : Config.Right.Name};
    if (const auto& Result = Fight.getResult()) Context.Result = &*Result;
    return Context;
}

combat::FighterConfig makeFighterConfig(const std::filesystem::path& Root, const std::string& FighterName) {
    const std::filesystem::path DataDir = Root / "data";
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const stats::FighterSheet Sheet = stats::loadFighterSheet(DataDir / "fighters" / (FighterName + ".json"));
    stats::ResolvedFighter Fighter = stats::resolveFighterSheet(Sheet, Catalog);
    return {.Name = std::move(Fighter.Name), .Stats = Fighter.BaseStats, .Loadout = std::move(Fighter.Gear)};
}

void publishStatsPanel(const combat::BattleConfig& Config, const stats::BalanceTable& Balance) {
    if constexpr (FIGHTER_DEBUG) {
        const std::array<std::pair<const char*, const combat::FighterConfig*>, 2> Sides = {{
            {"P1", &Config.Left}, {"P2", &Config.Right},
        }};
        for (const auto& [Side, Fighter] : Sides) {
            const stats::PhysicalProfile Profile = stats::computeProfile(Fighter->Stats, Fighter->Loadout, Balance);
            for (const stats::ProfileLine& Line :
                 stats::describeProfile(Fighter->Stats, Fighter->Loadout, Profile, Balance)) {
                const bool IsBuild = Line.Label == "build" && !Fighter->Name.empty();
                debug::setPanel(std::format("{} {}", Side, Line.Label),
                                IsBuild ? std::format("{}: {}", Fighter->Name, Line.Text) : Line.Text);
            }
        }
    }
}

} // namespace

} // namespace fighter::app
