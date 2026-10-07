#include "ui/screen_view.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/View.hpp>

#include "render/assets.hpp"
#include "ui/results_text.hpp"

namespace fighter::ui {
namespace {

const sf::Color TextColor(220, 220, 220);
const sf::Color SelectedColor(255, 214, 90);
const sf::Color DimmedColor(120, 120, 130);
const sf::Color PanelColor(18, 18, 26, 235);

/// Everything is sized from the window height, so the screens scale with it.
struct Canvas {
    sf::RenderTarget& Target;
    const sf::Font& Font;
    float Width;
    float Height;
    float Unit;   ///< Text size of a normal line, px.
};

void drawText(const Canvas& Surface, std::string_view Line, float Scale, sf::Vector2f Position, sf::Color Color,
              bool Centered);
void drawRect(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, sf::Color Color);
void drawItems(const Canvas& Surface, const ScreenFlow& Flow, float Top);
void drawHint(const Canvas& Surface, std::string_view Line);
void drawMainMenu(const Canvas& Surface, const ScreenFlow& Flow);
void drawFighterSelect(const Canvas& Surface, const ScreenFlow& Flow);
void drawPause(const Canvas& Surface, const ScreenFlow& Flow);
void drawResults(const Canvas& Surface, const ScreenFlow& Flow, const ScreenContext& Context);

} // namespace

void drawScreen(sf::RenderTarget& Target, render::Resources& Assets, const ScreenFlow& Flow,
                const ScreenContext& Context) {
    if (Flow.getScreen() == Screen::Battle) return;

    // Pixel coordinates of the window, whatever the camera left in the view.
    const sf::Vector2f Size(Target.getSize());
    Target.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, Size)));
    const Canvas Surface{Target, Assets.getFont(render::assets::MonoFontPath), Size.x, Size.y, Size.y / 30.0f};

    switch (Flow.getScreen()) {
        case Screen::MainMenu: drawMainMenu(Surface, Flow); break;
        case Screen::FighterSelect: drawFighterSelect(Surface, Flow); break;
        case Screen::Pause: drawPause(Surface, Flow); break;
        case Screen::Results: drawResults(Surface, Flow, Context); break;
        case Screen::Battle: break;
    }
}

namespace {

void drawText(const Canvas& Surface, std::string_view Line, float Scale, sf::Vector2f Position, sf::Color Color,
              bool Centered) {
    sf::Text Text(Surface.Font, std::string(Line), static_cast<unsigned>(Surface.Unit * Scale));
    Text.setFillColor(Color);
    const float OffsetX = Centered ? Text.getLocalBounds().size.x * 0.5f : 0.0f;
    Text.setPosition({Position.x - OffsetX, Position.y});
    Surface.Target.draw(Text);
}

void drawRect(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, sf::Color Color) {
    sf::RectangleShape Rect(Size);
    Rect.setPosition(Position);
    Rect.setFillColor(Color);
    Surface.Target.draw(Rect);
}

void drawItems(const Canvas& Surface, const ScreenFlow& Flow, float Top) {
    const auto Items = Flow.getItems();
    for (size_t Index = 0; Index < Items.size(); ++Index) {
        const bool Selected = Index == Flow.getCursor();
        const std::string Line = (Selected ? "> " : "  ") + std::string(Items[Index]);
        drawText(Surface, Line, 1.3f, {Surface.Width * 0.5f, Top + static_cast<float>(Index) * Surface.Unit * 1.8f},
                 Selected ? SelectedColor : TextColor, true);
    }
}

void drawHint(const Canvas& Surface, std::string_view Line) {
    drawText(Surface, Line, 0.7f, {Surface.Width * 0.5f, Surface.Height - Surface.Unit * 1.6f}, DimmedColor, true);
}

void drawMainMenu(const Canvas& Surface, const ScreenFlow& Flow) {
    drawRect(Surface, {0.0f, 0.0f}, {Surface.Width, Surface.Height}, sf::Color(10, 10, 16));
    drawText(Surface, "FIGHTER", 3.0f, {Surface.Width * 0.5f, Surface.Height * 0.15f}, SelectedColor, true);
    drawItems(Surface, Flow, Surface.Height * 0.5f);
    drawHint(Surface, "Up/Down: choose   Enter: confirm");
}

void drawFighterSelect(const Canvas& Surface, const ScreenFlow& Flow) {
    drawRect(Surface, {0.0f, 0.0f}, {Surface.Width, Surface.Height}, sf::Color(10, 10, 16));
    drawText(Surface, "Choose fighters", 2.0f, {Surface.Width * 0.5f, Surface.Height * 0.08f}, TextColor, true);

    const auto Fighters = Flow.getFighters();
    if (Fighters.empty()) {
        drawText(Surface, "No fighters in data/fighters", 1.2f, {Surface.Width * 0.5f, Surface.Height * 0.45f},
                 DimmedColor, true);
        drawHint(Surface, "Esc: back");
        return;
    }
    for (int Side = 0; Side < 2; ++Side) {
        const float CenterX = Surface.Width * (Side == 0 ? 0.28f : 0.72f);
        const bool Active = Flow.getActiveSide() == Side;
        // The side already confirmed keeps its choice, the next one is being picked.
        drawText(Surface, Side == 0 ? "P1" : "P2", 1.6f, {CenterX, Surface.Height * 0.22f},
                 Active ? SelectedColor : DimmedColor, true);
        for (size_t Index = 0; Index < Fighters.size(); ++Index) {
            const bool Picked = Index == Flow.getPick(Side);
            const std::string Line = (Picked ? "> " : "  ") + Fighters[Index];
            const sf::Color Color = Picked ? (Active ? SelectedColor : TextColor) : DimmedColor;
            drawText(Surface, Line, 1.2f, {CenterX, Surface.Height * 0.32f + static_cast<float>(Index) * Surface.Unit * 1.6f},
                     Color, true);
        }
    }
    drawHint(Surface, "Up/Down: choose   Left/Right: side   Enter: confirm   Esc: back");
}

void drawPause(const Canvas& Surface, const ScreenFlow& Flow) {
    drawRect(Surface, {0.0f, 0.0f}, {Surface.Width, Surface.Height}, sf::Color(0, 0, 0, 140));
    const sf::Vector2f PanelSize(Surface.Unit * 16.0f, Surface.Unit * 11.0f);
    drawRect(Surface, {(Surface.Width - PanelSize.x) * 0.5f, Surface.Height * 0.5f - PanelSize.y * 0.5f}, PanelSize,
             PanelColor);
    drawText(Surface, "PAUSED", 2.0f, {Surface.Width * 0.5f, Surface.Height * 0.5f - PanelSize.y * 0.5f + Surface.Unit},
             SelectedColor, true);
    drawItems(Surface, Flow, Surface.Height * 0.5f - Surface.Unit * 1.5f);
    drawHint(Surface, "Up/Down: choose   Enter: confirm   Esc: resume");
}

void drawResults(const Canvas& Surface, const ScreenFlow& Flow, const ScreenContext& Context) {
    drawRect(Surface, {0.0f, 0.0f}, {Surface.Width, Surface.Height}, sf::Color(0, 0, 0, 190));
    if (!Context.Result) return;
    const ResultsText Text = describeResult(*Context.Result, Context.Names);

    drawText(Surface, Text.Headline, 2.6f, {Surface.Width * 0.5f, Surface.Height * 0.04f}, SelectedColor, true);
    drawText(Surface, Text.Detail, 1.1f, {Surface.Width * 0.5f, Surface.Height * 0.04f + Surface.Unit * 3.6f}, TextColor,
             true);

    for (size_t Side = 0; Side < 2; ++Side) {
        const float Left = Surface.Width * (Side == 0 ? 0.08f : 0.54f);
        const std::vector<std::string>& Lines = Text.Columns[Side];
        for (size_t Index = 0; Index < Lines.size(); ++Index) {
            drawText(Surface, Lines[Index], Index == 0 ? 1.2f : 0.75f,
                     {Left, Surface.Height * 0.2f + static_cast<float>(Index) * Surface.Unit * 0.95f},
                     Index == 0 ? SelectedColor : TextColor, false);
        }
    }
    drawItems(Surface, Flow, Surface.Height * 0.74f);
    drawHint(Surface, "Up/Down: choose   Enter: confirm   Esc: main menu");
}

} // namespace

} // namespace fighter::ui
