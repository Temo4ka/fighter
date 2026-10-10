#include "app/pose_editor.hpp"

#include <cstdlib>
#include <utility>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <imgui-SFML.h>
#include <imgui.h>

#include "core/log.hpp"

namespace fighter::app {
namespace {

constexpr sf::Vector2u WindowSize = {1600, 900};

} // namespace

PoseEditor::PoseEditor(Options Settings) : Opts(std::move(Settings)) {}

int PoseEditor::run() {
    const std::filesystem::path ClipPath = Opts.Root / "data" / "poses" / (Opts.Clip + ".json");
    if (!std::filesystem::exists(ClipPath)) {
        log::error("no clip {}", ClipPath.string());
        return EXIT_FAILURE;
    }

    sf::RenderWindow Window(sf::VideoMode(WindowSize), "Fighter pose editor");
    Window.setVerticalSyncEnabled(true);
    if (!ImGui::SFML::Init(Window)) {
        log::error("cannot start Dear ImGui");
        return EXIT_FAILURE;
    }
    // The layout of the panels is not saved between runs.
    ImGui::GetIO().IniFilename = nullptr;

    sf::Clock Clock;
    while (Window.isOpen()) {
        while (const std::optional Event = Window.pollEvent()) {
            ImGui::SFML::ProcessEvent(Window, *Event);
            if (Event->is<sf::Event::Closed>()) Window.close();
        }
        ImGui::SFML::Update(Window, Clock.restart());

        ImGui::SetNextWindowPos({8.0f, 8.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({320.0f, 160.0f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Pose editor");
        ImGui::Text("clip:   %s", Opts.Clip.c_str());
        ImGui::Text("weapon: %s", Opts.Weapon.empty() ? "(none)" : Opts.Weapon.c_str());
        ImGui::End();

        Window.clear(sf::Color(40, 40, 46));
        ImGui::SFML::Render(Window);
        Window.display();
    }
    ImGui::SFML::Shutdown();
    return EXIT_SUCCESS;
}

} // namespace fighter::app
