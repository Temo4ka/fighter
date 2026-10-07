#include "ui/screen_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/View.hpp>

#include "render/assets.hpp"
#include "ui/results_text.hpp"

namespace fighter::ui {
namespace {

enum class Align { Left, Center, Right };

/// The window in pixels and the measures derived from its height: the type
/// scale of data/ui.json and a spacing grid of Grid = height / 40.
struct Canvas {
    sf::RenderTarget& Target;
    const sf::Font& Font;
    const UiConfig& Config;
    float Width;
    float Height;
    float Grid;

    unsigned getSize(float Fraction) const { return std::max(8u, static_cast<unsigned>(std::lround(Height * Fraction))); }
    sf::Color getPlayerColor(int Side) const { return toColor(Side == 0 ? Config.Colors.Player1 : Config.Colors.Player2); }
    static sf::Color toColor(UiColor Color) { return {Color.R, Color.G, Color.B, Color.A}; }
};

sf::Color withAlpha(sf::Color Color, int Alpha);
sf::Color scaled(sf::Color Color, float Factor);
float drawText(const Canvas& Surface, std::string_view Line, unsigned Size, sf::Vector2f Position, sf::Color Color,
               Align Alignment);
float measureText(const Canvas& Surface, std::string_view Line, unsigned Size);
float getLineHeight(const Canvas& Surface, unsigned Size);
void fillRect(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, sf::Color Color);
void frameRect(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, float Thickness, sf::Color Color);
void drawBar(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, float Fraction, sf::Color Fill);
void drawTriangle(const Canvas& Surface, sf::Vector2f Center, float Radius, float RotationDeg, sf::Color Color);
void drawGradient(const Canvas& Surface, sf::Color Top, sf::Color Bottom);
void drawBackdrop(const Canvas& Surface);
void drawDim(const Canvas& Surface);
void drawTitle(const Canvas& Surface, std::string_view Line, float Y);
void drawHint(const Canvas& Surface, std::string_view Line);
void drawMenuList(const Canvas& Surface, std::span<const std::string_view> Items, size_t Cursor, float HighlightPos,
                  float CenterX, float Top, float Width);
void drawButtons(const Canvas& Surface, std::span<const std::string_view> Items, size_t Cursor, float HighlightPos,
                 sf::Vector2f Position, sf::Vector2f Size);
void drawSilhouette(const Canvas& Surface, sf::Vector2f Feet, float Height, sf::Color Color, bool Armed, float Facing);
void drawFighterCard(const Canvas& Surface, const ScreenFlow& Flow, const FighterCard& Card, int Side,
                     sf::Vector2f Position, sf::Vector2f Size);
void drawMainMenu(const Canvas& Surface, const ScreenFlow& Flow, float HighlightPos);
void drawPause(const Canvas& Surface, const ScreenFlow& Flow, float HighlightPos);
void drawResults(const Canvas& Surface, const ScreenFlow& Flow, const ScreenContext& Context, float HighlightPos);

constexpr float MaxStatShown = 20.0f;   ///< A stat bar is full at this value.

} // namespace

ScreenView::ScreenView(render::Resources& Resources, std::filesystem::path Directory)
    : Assets(Resources), DataDir(std::move(Directory)) {}

void ScreenView::update(double Dt, const ScreenFlow& Flow) {
    const auto Target = static_cast<float>(Flow.getCursor());
    // A new screen starts with the bar already on its item.
    HighlightPos = Flow.getScreen() != LastScreen ? Target
                                                  : easeToward(HighlightPos, Target, Dt, Flow.getConfig().HighlightSec);
    LastScreen = Flow.getScreen();
}

void ScreenView::draw(sf::RenderTarget& Target, const ScreenFlow& Flow, const ScreenContext& Context) {
    if (Flow.getScreen() == Screen::Battle) return;

    // Pixel coordinates of the window, whatever the camera left in the view.
    const sf::Vector2f Size(Target.getSize());
    Target.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, Size)));
    const Canvas Surface{Target, Assets.getFont(render::assets::MonoFontPath), Flow.getConfig(), Size.x, Size.y,
                         Size.y / 40.0f};

    switch (Flow.getScreen()) {
        case Screen::MainMenu: drawMainMenu(Surface, Flow, HighlightPos); break;
        case Screen::FighterSelect: {
            drawBackdrop(Surface);
            drawTitle(Surface, "CHOOSE YOUR FIGHTERS", Surface.Grid * 2.5f);
            if (Flow.getFighters().empty()) {
                drawText(Surface, "No fighters in data/fighters", Surface.getSize(Surface.Config.Type.Item),
                         {Surface.Width * 0.5f, Surface.Height * 0.45f}, Canvas::toColor(Surface.Config.Colors.Dim),
                         Align::Center);
            } else {
                const sf::Vector2f CardSize(std::min(Surface.Width * 0.36f, Surface.Grid * 21.0f), Surface.Grid * 27.0f);
                const float Top = Surface.Grid * 8.0f;
                for (int Side = 0; Side < 2; ++Side) {
                    const float CenterX = Surface.Width * (Side == 0 ? 0.27f : 0.73f);
                    drawFighterCard(Surface, Flow, getCard(Flow.getPickedFighter(Side)), Side,
                                    {CenterX - CardSize.x * 0.5f, Top}, CardSize);
                }
                drawText(Surface, "VS", Surface.getSize(Surface.Config.Type.Heading),
                         {Surface.Width * 0.5f, Top + CardSize.y * 0.42f}, Canvas::toColor(Surface.Config.Colors.Dim),
                         Align::Center);
            }
            drawHint(Surface, "\xE2\x86\x91\xE2\x86\x93 choose  \xC2\xB7  \xE2\x86\x90\xE2\x86\x92 side  \xC2\xB7  Enter confirm  \xC2\xB7  Esc back");
            break;
        }
        case Screen::Pause: drawPause(Surface, Flow, HighlightPos); break;
        case Screen::Results: drawResults(Surface, Flow, Context, HighlightPos); break;
        case Screen::Battle: break;
    }
}

const FighterCard& ScreenView::getCard(const std::string& FileName) {
    const auto Found = Cards.find(FileName);
    if (Found != Cards.end()) return Found->second;
    return Cards.emplace(FileName, loadFighterCard(DataDir, FileName)).first->second;
}

namespace {

sf::Color withAlpha(sf::Color Color, int Alpha) {
    Color.a = static_cast<uint8_t>(std::clamp(Alpha, 0, 255));
    return Color;
}

sf::Color scaled(sf::Color Color, float Factor) {
    const auto Scale = [Factor](uint8_t Channel) {
        return static_cast<uint8_t>(std::clamp(static_cast<float>(Channel) * Factor, 0.0f, 255.0f));
    };
    return {Scale(Color.r), Scale(Color.g), Scale(Color.b), Color.a};
}

sf::Text makeText(const Canvas& Surface, std::string_view Line, unsigned Size) {
    return sf::Text(Surface.Font, sf::String::fromUtf8(Line.begin(), Line.end()), Size);
}

float measureText(const Canvas& Surface, std::string_view Line, unsigned Size) {
    return makeText(Surface, Line, Size).getLocalBounds().size.x;
}

float getLineHeight(const Canvas& Surface, unsigned Size) {
    return Surface.Font.getLineSpacing(Size);
}

float drawText(const Canvas& Surface, std::string_view Line, unsigned Size, sf::Vector2f Position, sf::Color Color,
               Align Alignment) {
    sf::Text Text = makeText(Surface, Line, Size);
    Text.setFillColor(Color);
    const float Width = Text.getLocalBounds().size.x;
    float OffsetX = 0.0f;
    if (Alignment == Align::Center) OffsetX = Width * 0.5f;
    if (Alignment == Align::Right) OffsetX = Width;
    Text.setPosition({std::round(Position.x - OffsetX), std::round(Position.y)});
    Surface.Target.draw(Text);
    return Width;
}

void fillRect(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, sf::Color Color) {
    sf::RectangleShape Rect(Size);
    Rect.setPosition(Position);
    Rect.setFillColor(Color);
    Surface.Target.draw(Rect);
}

void frameRect(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, float Thickness, sf::Color Color) {
    fillRect(Surface, Position, {Size.x, Thickness}, Color);
    fillRect(Surface, {Position.x, Position.y + Size.y - Thickness}, {Size.x, Thickness}, Color);
    fillRect(Surface, Position, {Thickness, Size.y}, Color);
    fillRect(Surface, {Position.x + Size.x - Thickness, Position.y}, {Thickness, Size.y}, Color);
}

void drawBar(const Canvas& Surface, sf::Vector2f Position, sf::Vector2f Size, float Fraction, sf::Color Fill) {
    fillRect(Surface, Position, Size, Canvas::toColor(Surface.Config.Colors.Track));
    fillRect(Surface, Position, {Size.x * std::clamp(Fraction, 0.0f, 1.0f), Size.y}, Fill);
}

void drawTriangle(const Canvas& Surface, sf::Vector2f Center, float Radius, float RotationDeg, sf::Color Color) {
    sf::CircleShape Shape(Radius, 3);
    Shape.setOrigin({Radius, Radius});
    Shape.setPosition(Center);
    Shape.setRotation(sf::degrees(RotationDeg));
    Shape.setFillColor(Color);
    Surface.Target.draw(Shape);
}

void drawGradient(const Canvas& Surface, sf::Color Top, sf::Color Bottom) {
    sf::VertexArray Quad(sf::PrimitiveType::TriangleStrip, 4);
    Quad[0] = {{0.0f, 0.0f}, Top, {}};
    Quad[1] = {{Surface.Width, 0.0f}, Top, {}};
    Quad[2] = {{0.0f, Surface.Height}, Bottom, {}};
    Quad[3] = {{Surface.Width, Surface.Height}, Bottom, {}};
    Surface.Target.draw(Quad);
}

/// The arena as a stage: a gradient wall, a floor band with its line.
void drawBackdrop(const Canvas& Surface) {
    const UiPalette& Colors = Surface.Config.Colors;
    drawGradient(Surface, Canvas::toColor(Colors.BackgroundTop), Canvas::toColor(Colors.BackgroundBottom));
    const float FloorY = Surface.Height * 0.8f;
    fillRect(Surface, {0.0f, FloorY}, {Surface.Width, Surface.Height - FloorY}, Canvas::toColor(Colors.Floor));
    fillRect(Surface, {0.0f, FloorY}, {Surface.Width, std::max(2.0f, Surface.Grid * 0.12f)},
             withAlpha(Canvas::toColor(Colors.Accent), 90));
}

void drawDim(const Canvas& Surface) {
    fillRect(Surface, {0.0f, 0.0f}, {Surface.Width, Surface.Height}, {0, 0, 0, static_cast<uint8_t>(Surface.Config.DimAlpha)});
}

void drawTitle(const Canvas& Surface, std::string_view Line, float Y) {
    const unsigned Size = Surface.getSize(Surface.Config.Type.Heading);
    const sf::Color Accent = Canvas::toColor(Surface.Config.Colors.Accent);
    const float Width = drawText(Surface, Line, Size, {Surface.Width * 0.5f, Y}, Accent, Align::Center);
    const float LineY = Y + getLineHeight(Surface, Size) + Surface.Grid * 0.4f;
    fillRect(Surface, {(Surface.Width - Width) * 0.5f, LineY}, {Width, std::max(2.0f, Surface.Grid * 0.15f)},
             withAlpha(Accent, 140));
}

void drawHint(const Canvas& Surface, std::string_view Line) {
    drawText(Surface, Line, Surface.getSize(Surface.Config.Type.Hint),
             {Surface.Width * 0.5f, Surface.Height - Surface.Grid * 2.2f}, Canvas::toColor(Surface.Config.Colors.Dim),
             Align::Center);
}

void drawMenuList(const Canvas& Surface, std::span<const std::string_view> Items, size_t Cursor, float HighlightPos,
                  float CenterX, float Top, float Width) {
    const UiPalette& Colors = Surface.Config.Colors;
    const unsigned Size = Surface.getSize(Surface.Config.Type.Item);
    const float RowHeight = static_cast<float>(Size) * 1.9f;
    const float BarHeight = RowHeight * 0.86f;
    const float Left = CenterX - Width * 0.5f;

    // The selection bar goes first, the items over it.
    const float BarY = Top + HighlightPos * RowHeight;
    fillRect(Surface, {Left, BarY}, {Width, BarHeight}, Canvas::toColor(Colors.Accent));
    drawTriangle(Surface, {Left + Surface.Grid * 1.0f, BarY + BarHeight * 0.5f}, Surface.Grid * 0.45f, 90.0f,
                 Canvas::toColor(Colors.OnAccent));

    for (size_t Index = 0; Index < Items.size(); ++Index) {
        const float RowY = Top + static_cast<float>(Index) * RowHeight;
        const float TextY = RowY + (BarHeight - getLineHeight(Surface, Size)) * 0.5f;
        const bool Selected = Index == Cursor;
        drawText(Surface, Items[Index], Size, {CenterX, TextY},
                 Canvas::toColor(Selected ? Colors.OnAccent : Colors.Text), Align::Center);
    }
}

void drawButtons(const Canvas& Surface, std::span<const std::string_view> Items, size_t Cursor, float HighlightPos,
                 sf::Vector2f Position, sf::Vector2f Size) {
    const UiPalette& Colors = Surface.Config.Colors;
    const unsigned TextSize = Surface.getSize(Surface.Config.Type.Body * 1.15f);
    const float Gap = Surface.Grid * 0.8f;
    const float ButtonWidth = (Size.x - Gap * static_cast<float>(Items.size() - 1)) / static_cast<float>(Items.size());
    const float Step = ButtonWidth + Gap;

    for (size_t Index = 0; Index < Items.size(); ++Index)
        fillRect(Surface, {Position.x + static_cast<float>(Index) * Step, Position.y}, {ButtonWidth, Size.y},
                 Canvas::toColor(Colors.Track));
    fillRect(Surface, {Position.x + HighlightPos * Step, Position.y}, {ButtonWidth, Size.y},
             Canvas::toColor(Colors.Accent));
    for (size_t Index = 0; Index < Items.size(); ++Index) {
        const float CenterX = Position.x + static_cast<float>(Index) * Step + ButtonWidth * 0.5f;
        drawText(Surface, Items[Index], TextSize, {CenterX, Position.y + (Size.y - getLineHeight(Surface, TextSize)) * 0.5f},
                 Canvas::toColor(Index == Cursor ? Colors.OnAccent : Colors.Text), Align::Center);
    }
}

/// A plain standing figure; \p Armed adds a blade in the front hand.
void drawSilhouette(const Canvas& Surface, sf::Vector2f Feet, float Height, sf::Color Color, bool Armed, float Facing) {
    const float Unit = Height / 10.0f;
    const auto Block = [&](float Dx, float Top, float Width, float BlockHeight) {
        fillRect(Surface, {Feet.x + Dx * Unit - Width * Unit * 0.5f, Feet.y - Top * Unit}, {Width * Unit, BlockHeight * Unit},
                 Color);
    };
    Block(-0.6f, 4.6f, 0.9f, 4.6f);   // legs
    Block(0.6f, 4.6f, 0.9f, 4.6f);
    Block(0.0f, 7.8f, 2.6f, 3.4f);    // torso
    Block(-1.8f, 7.6f, 0.8f, 3.0f);   // arms
    Block(1.8f, 7.6f, 0.8f, 3.0f);
    sf::CircleShape Head(Unit * 0.95f);
    Head.setOrigin({Unit * 0.95f, Unit * 0.95f});
    Head.setPosition({Feet.x, Feet.y - 9.0f * Unit});
    Head.setFillColor(Color);
    Surface.Target.draw(Head);
    if (Armed) {
        const float BladeX = Feet.x + Facing * 2.4f * Unit;
        fillRect(Surface, {BladeX - Unit * 0.18f, Feet.y - 9.6f * Unit}, {Unit * 0.36f, Unit * 5.2f}, scaled(Color, 1.35f));
    }
}

void drawStatRow(const Canvas& Surface, std::string_view Label, int Value, sf::Vector2f Position, float Width,
                 sf::Color Fill) {
    const UiPalette& Colors = Surface.Config.Colors;
    const unsigned Size = Surface.getSize(Surface.Config.Type.Body);
    const float RowHeight = getLineHeight(Surface, Size);
    drawText(Surface, Label, Size, Position, Canvas::toColor(Colors.Dim), Align::Left);
    const float BarLeft = Position.x + Surface.Grid * 3.2f;
    const float BarWidth = Width - Surface.Grid * 3.2f - Surface.Grid * 2.6f;
    drawBar(Surface, {BarLeft, Position.y + RowHeight * 0.3f}, {BarWidth, RowHeight * 0.4f},
            static_cast<float>(Value) / MaxStatShown, Fill);
    drawText(Surface, std::to_string(Value), Size, {Position.x + Width, Position.y}, Canvas::toColor(Colors.Text),
             Align::Right);
}

void drawFighterCard(const Canvas& Surface, const ScreenFlow& Flow, const FighterCard& Card, int Side,
                     sf::Vector2f Position, sf::Vector2f Size) {
    const UiPalette& Colors = Surface.Config.Colors;
    const float Pad = Surface.Grid * 1.2f;
    const sf::Color Player = Surface.getPlayerColor(Side);
    // The left card is done once the right one is being picked.
    const bool Active = Flow.getActiveSide() == Side;
    const bool Ready = Side == 0 && Flow.getActiveSide() == 1;
    const float Strength = Active ? 1.0f : 0.55f;
    const sf::Color Accent = scaled(Player, Strength);

    fillRect(Surface, Position, Size, Canvas::toColor(Colors.Panel));
    // Header strip: who is being chosen.
    const float HeaderHeight = Surface.Grid * 2.4f;
    fillRect(Surface, Position, {Size.x, HeaderHeight}, Accent);
    const unsigned BodySize = Surface.getSize(Surface.Config.Type.Body);
    const float HeaderTextY = Position.y + (HeaderHeight - getLineHeight(Surface, BodySize)) * 0.5f;
    drawText(Surface, Side == 0 ? "PLAYER 1" : "PLAYER 2", BodySize, {Position.x + Pad, HeaderTextY},
             Canvas::toColor(Colors.OnAccent), Align::Left);
    drawText(Surface, Active ? "CHOOSING" : (Ready ? "READY" : ""), BodySize, {Position.x + Size.x - Pad, HeaderTextY},
             Canvas::toColor(Colors.OnAccent), Align::Right);

    // Preview.
    const float CenterX = Position.x + Size.x * 0.5f;
    drawSilhouette(Surface, {CenterX, Position.y + HeaderHeight + Surface.Grid * 9.5f}, Surface.Grid * 7.5f,
                   withAlpha(scaled(Player, Active ? 0.8f : 0.4f), 255), Card.Weapon != "Unarmed", Side == 0 ? 1.0f : -1.0f);

    // Name with the up/down markers.
    const unsigned NameSize = Surface.getSize(Surface.Config.Type.Item);
    const float NameY = Position.y + HeaderHeight + Surface.Grid * 11.2f;
    drawText(Surface, Card.Name, NameSize, {CenterX, NameY}, Canvas::toColor(Colors.Text), Align::Center);
    if (Active && Flow.getFighters().size() > 1) {
        const sf::Color Marker = Canvas::toColor(Colors.Accent);
        drawTriangle(Surface, {CenterX, NameY - Surface.Grid * 0.5f}, Surface.Grid * 0.4f, 0.0f, Marker);
        drawTriangle(Surface, {CenterX, NameY + getLineHeight(Surface, NameSize) + Surface.Grid * 0.3f},
                     Surface.Grid * 0.4f, 180.0f, Marker);
    }

    float Y = NameY + getLineHeight(Surface, NameSize) + Surface.Grid * 1.4f;
    const float InnerWidth = Size.x - Pad * 2.0f;
    const float RowStep = getLineHeight(Surface, BodySize) * 1.35f;
    if (!Card.Error.empty()) {
        drawText(Surface, "sheet error, see log", BodySize, {CenterX, Y}, Canvas::toColor(Colors.Player2), Align::Center);
    } else {
        drawText(Surface, "WEAPON", BodySize, {Position.x + Pad, Y}, Canvas::toColor(Colors.Dim), Align::Left);
        drawText(Surface, Card.Weapon, BodySize, {Position.x + Size.x - Pad, Y}, Canvas::toColor(Colors.Text), Align::Right);
        Y += RowStep * 1.4f;
        drawStatRow(Surface, "STR", Card.Strength, {Position.x + Pad, Y}, InnerWidth, Accent);
        drawStatRow(Surface, "DEX", Card.Dexterity, {Position.x + Pad, Y + RowStep}, InnerWidth, Accent);
        drawStatRow(Surface, "CON", Card.Constitution, {Position.x + Pad, Y + RowStep * 2.0f}, InnerWidth, Accent);
    }

    // Which of the fighters it is: dots along the bottom.
    const size_t Count = Flow.getFighters().size();
    const float DotStep = Surface.Grid * 1.2f;
    const float DotsLeft = CenterX - DotStep * static_cast<float>(Count - 1) * 0.5f;
    for (size_t Index = 0; Index < Count; ++Index) {
        const bool Current = Index == Flow.getPick(Side);
        sf::CircleShape Dot(Surface.Grid * (Current ? 0.34f : 0.24f));
        Dot.setOrigin({Dot.getRadius(), Dot.getRadius()});
        Dot.setPosition({DotsLeft + static_cast<float>(Index) * DotStep, Position.y + Size.y - Surface.Grid * 1.4f});
        Dot.setFillColor(Current ? Accent : Canvas::toColor(Colors.Track));
        Surface.Target.draw(Dot);
    }

    frameRect(Surface, Position, Size, Active ? std::max(3.0f, Surface.Grid * 0.2f) : 1.0f,
              Active ? Player : withAlpha(Player, 90));
    if (!Active) fillRect(Surface, Position, Size, {0, 0, 0, 60});
}

void drawMainMenu(const Canvas& Surface, const ScreenFlow& Flow, float HighlightPos) {
    const UiPalette& Colors = Surface.Config.Colors;
    drawBackdrop(Surface);

    const unsigned TitleSize = Surface.getSize(Surface.Config.Type.Title);
    const float TitleY = Surface.Height * 0.14f;
    const sf::Color Accent = Canvas::toColor(Colors.Accent);
    drawText(Surface, "FIGHTER", TitleSize, {Surface.Width * 0.5f + Surface.Grid * 0.25f, TitleY + Surface.Grid * 0.25f},
             {0, 0, 0, 160}, Align::Center);   // a hard shadow
    const float TitleWidth = drawText(Surface, "FIGHTER", TitleSize, {Surface.Width * 0.5f, TitleY}, Accent, Align::Center);
    const float RuleY = TitleY + getLineHeight(Surface, TitleSize) + Surface.Grid * 0.3f;
    fillRect(Surface, {(Surface.Width - TitleWidth) * 0.5f, RuleY}, {TitleWidth, std::max(3.0f, Surface.Grid * 0.2f)}, Accent);

    drawMenuList(Surface, Flow.getItems(), Flow.getCursor(), HighlightPos, Surface.Width * 0.5f, Surface.Height * 0.5f,
                 Surface.Grid * 16.0f);
    drawHint(Surface, "\xE2\x86\x91\xE2\x86\x93 choose  \xC2\xB7  Enter confirm");
}

void drawPause(const Canvas& Surface, const ScreenFlow& Flow, float HighlightPos) {
    const UiPalette& Colors = Surface.Config.Colors;
    drawDim(Surface);

    const unsigned ItemSize = Surface.getSize(Surface.Config.Type.Item);
    const unsigned HeadingSize = Surface.getSize(Surface.Config.Type.Heading);
    const float RowHeight = static_cast<float>(ItemSize) * 1.9f;
    const float PanelWidth = Surface.Grid * 20.0f;
    const float TitleBlock = getLineHeight(Surface, HeadingSize) + Surface.Grid * 2.0f;
    const float PanelHeight = Surface.Grid * 3.0f + TitleBlock + RowHeight * static_cast<float>(Flow.getItems().size());
    const sf::Vector2f Position((Surface.Width - PanelWidth) * 0.5f, (Surface.Height - PanelHeight) * 0.5f);

    fillRect(Surface, Position, {PanelWidth, PanelHeight}, Canvas::toColor(Colors.Panel));
    frameRect(Surface, Position, {PanelWidth, PanelHeight}, 2.0f, withAlpha(Canvas::toColor(Colors.Accent), 160));
    const float TitleY = Position.y + Surface.Grid * 1.4f;
    drawText(Surface, "PAUSED", HeadingSize, {Surface.Width * 0.5f, TitleY}, Canvas::toColor(Colors.Accent), Align::Center);
    drawMenuList(Surface, Flow.getItems(), Flow.getCursor(), HighlightPos, Surface.Width * 0.5f,
                 Position.y + Surface.Grid * 1.5f + TitleBlock, PanelWidth - Surface.Grid * 3.0f);
    drawHint(Surface, "\xE2\x86\x91\xE2\x86\x93 choose  \xC2\xB7  Enter confirm  \xC2\xB7  Esc resume");
}

void drawResults(const Canvas& Surface, const ScreenFlow& Flow, const ScreenContext& Context, float HighlightPos) {
    const UiPalette& Colors = Surface.Config.Colors;
    drawDim(Surface);
    if (!Context.Result) return;
    const ResultsText Text = describeResult(*Context.Result, Context.Names);

    const sf::Vector2f Size(std::min(Surface.Width * 0.9f, Surface.Grid * 56.0f), Surface.Height * 0.9f);
    const sf::Vector2f Position((Surface.Width - Size.x) * 0.5f, (Surface.Height - Size.y) * 0.5f - Surface.Grid * 0.5f);
    const float Pad = Surface.Grid * 1.6f;
    fillRect(Surface, Position, Size, Canvas::toColor(Colors.Panel));

    // Banner in the colour of the winner (the accent for a draw).
    const bool Draw = Context.Result->WinnerSide == combat::Winner::Draw;
    const sf::Color WinnerColor =
        Draw ? Canvas::toColor(Colors.Accent) : Surface.getPlayerColor(Context.Result->WinnerSide == combat::Winner::Left ? 0 : 1);
    const unsigned HeadingSize = Surface.getSize(Surface.Config.Type.Heading);
    const unsigned BodySize = Surface.getSize(Surface.Config.Type.Body);
    const float BannerHeight = getLineHeight(Surface, HeadingSize) + Surface.Grid * 1.4f;
    fillRect(Surface, Position, {Size.x, BannerHeight}, WinnerColor);
    drawText(Surface, Text.Headline, HeadingSize,
             {Position.x + Size.x * 0.5f, Position.y + (BannerHeight - getLineHeight(Surface, HeadingSize)) * 0.5f},
             Canvas::toColor(Colors.OnAccent), Align::Center);
    float Y = Position.y + BannerHeight + Surface.Grid * 0.6f;
    drawText(Surface, Text.Detail, BodySize, {Position.x + Size.x * 0.5f, Y}, Canvas::toColor(Colors.Dim), Align::Center);
    Y += getLineHeight(Surface, BodySize) + Surface.Grid * 1.0f;

    // Table columns: label on the left, one centred cell per fighter.
    const float LabelX = Position.x + Pad;
    const std::array<float, 2> CellX = {Position.x + Size.x * 0.58f, Position.x + Size.x * 0.84f};

    // HP bars, one per fighter, above the columns.
    const float BarHeight = Surface.Grid * 0.9f;
    for (size_t Side = 0; Side < 2; ++Side) {
        const float Hp = Context.Result->Fighters[Side].Hp;
        const float MaxHp = Context.MaxHp[Side] > 0.0f ? Context.MaxHp[Side] : std::max(Hp, 1.0f);
        const sf::Color Player = Surface.getPlayerColor(static_cast<int>(Side));
        const float RowY = Y + static_cast<float>(Side) * (getLineHeight(Surface, BodySize) + BarHeight + Surface.Grid * 0.8f);
        drawText(Surface, Context.Names[Side], BodySize, {LabelX, RowY}, Player, Align::Left);
        drawText(Surface, std::format("{:.0f} / {:.0f} HP", Hp, MaxHp), BodySize, {Position.x + Size.x - Pad, RowY},
                 Canvas::toColor(Colors.Text), Align::Right);
        drawBar(Surface, {LabelX, RowY + getLineHeight(Surface, BodySize) + Surface.Grid * 0.1f},
                {Size.x - Pad * 2.0f, BarHeight}, Hp / MaxHp, Player);
    }
    Y += 2.0f * (getLineHeight(Surface, BodySize) + BarHeight + Surface.Grid * 0.8f) + Surface.Grid * 0.6f;

    // Column heads, then the rows that fit above the buttons.
    for (size_t Side = 0; Side < 2; ++Side)
        drawText(Surface, Context.Names[Side], BodySize, {CellX[Side], Y}, Surface.getPlayerColor(static_cast<int>(Side)),
                 Align::Center);
    Y += getLineHeight(Surface, BodySize) * 1.3f;
    fillRect(Surface, {LabelX, Y}, {Size.x - Pad * 2.0f, 1.0f}, Canvas::toColor(Colors.Track));
    Y += Surface.Grid * 0.4f;

    const float ButtonHeight = Surface.Grid * 2.6f;
    const float ButtonsY = Position.y + Size.y - Pad - ButtonHeight;
    const float RowStep = getLineHeight(Surface, BodySize) * 1.12f;
    const float RowsBottom = ButtonsY - Surface.Grid * 1.0f;
    bool Cut = false;
    for (const ResultsRow& Row : Text.Rows) {
        if (Y + RowStep > RowsBottom) {
            Cut = true;
            break;
        }
        if (Row.IsHeader) {
            Y += RowStep * 0.25f;
            drawText(Surface, Row.Label, BodySize, {LabelX, Y}, Canvas::toColor(Colors.Accent), Align::Left);
        } else {
            drawText(Surface, Row.Label, BodySize, {LabelX + Surface.Grid, Y}, Canvas::toColor(Colors.Dim), Align::Left);
            for (size_t Side = 0; Side < 2; ++Side)
                drawText(Surface, Row.Cells[Side], BodySize, {CellX[Side], Y}, Canvas::toColor(Colors.Text), Align::Center);
        }
        Y += RowStep;
    }
    if (Cut) drawText(Surface, "...", BodySize, {LabelX + Surface.Grid, Y - RowStep * 0.5f}, Canvas::toColor(Colors.Dim), Align::Left);

    drawButtons(Surface, Flow.getItems(), Flow.getCursor(), HighlightPos, {LabelX, ButtonsY}, {Size.x - Pad * 2.0f, ButtonHeight});
    drawHint(Surface, "\xE2\x86\x90\xE2\x86\x92 choose  \xC2\xB7  Enter confirm  \xC2\xB7  Esc main menu");
}

} // namespace

} // namespace fighter::ui
