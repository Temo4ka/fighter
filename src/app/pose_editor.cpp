#include "app/pose_editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <numbers>
#include <ranges>
#include <utility>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <imgui-SFML.h>
#include <imgui.h>

#include "anim/layers.hpp"
#include "anim/playback.hpp"
#include "core/log.hpp"
#include "editor/clip_edit.hpp"
#include "editor/clip_writer.hpp"
#include "editor/pose_fk.hpp"

namespace fighter::app {
namespace {

constexpr sf::Vector2u WindowSize = {1600, 900};
constexpr float DegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
/// Room left on the left for the panels, px, and under and over the body.
constexpr float PanelColumnWidth = 690.0f;
constexpr float FloorMargin = 90.0f;
constexpr float HeadroomM = 1.95f;
/// Frames before a screenshot is taken (the panels need a couple to lay out).
constexpr int ScreenshotFrame = 3;

/// The parts in the order they are drawn, far to near: legs, trunk, arms.
constexpr std::array<BodyPart, BodyPartCount> DrawOrder = {
    BodyPart::ThighR,    BodyPart::ShinR,     BodyPart::FootR,     BodyPart::ThighL,   BodyPart::ShinL,
    BodyPart::FootL,     BodyPart::Pelvis,    BodyPart::Torso,     BodyPart::Head,     BodyPart::UpperArmR,
    BodyPart::ForearmR,  BodyPart::UpperArmL, BodyPart::ForearmL,
};

/// The joints of the angle sliders, in the order of the key panel.
constexpr std::array<BodyPart, BodyPartCount> JointOrder = {
    BodyPart::Pelvis,    BodyPart::Torso,     BodyPart::Head,      BodyPart::UpperArmL, BodyPart::ForearmL,
    BodyPart::UpperArmR, BodyPart::ForearmR,  BodyPart::ThighL,    BodyPart::ShinL,     BodyPart::FootL,
    BodyPart::ThighR,    BodyPart::ShinR,     BodyPart::FootR,
};

/// Where the scene is on the screen: the floor point under the body, and the
/// scale.
struct View {
    sf::Vector2f Origin;
    float PixelsPerMeter = 1.0f;
};

bool isLegPart(BodyPart Part);
View makeView(sf::Vector2u Size);
sf::Vector2f toScreen(const View& Scene, Vec2 Point);
Vec2 toWorld(const editor::PosedPart& Posed, const rig::PartDef& Part, Vec2 Point, float OffsetX);
void drawCapsule(sf::RenderTarget& Target, const View& Scene, Vec2 From, Vec2 To, float Radius, sf::Color Fill, sf::Color Line);
void drawPolygon(sf::RenderTarget& Target, const View& Scene, const std::array<Vec2, 4>& Corners, sf::Color Fill, sf::Color Line);
void drawPart(sf::RenderTarget& Target, const View& Scene, const editor::PosedPart& Posed, const rig::PartDef& Part, float OffsetX,
              sf::Color Fill, sf::Color Line);
void drawBody(sf::RenderTarget& Target, const View& Scene, const rig::RigDef& Rig, const editor::PosedBody& Body, float OffsetX,
              std::optional<BodyPart> Highlight);
void drawFloor(sf::RenderTarget& Target, const View& Scene);
bool saveScreenshot(const sf::RenderWindow& Window, const std::filesystem::path& Path);
bool sliderAngle(const char* Label, float& Radians, editor::AngleRange Range);
bool dragSeconds(const char* Label, float& Seconds, float Speed = 0.005f);

} // namespace

PoseEditor::PoseEditor(Options Settings) : Opts(std::move(Settings)) {}

int PoseEditor::run() {
    ClipPath = Opts.Root / "data" / "poses" / (Opts.Clip + ".json");
    try {
        Context = editor::loadGhostContext(Opts.Root, Opts.Weapon);
    } catch (const std::exception& Error) {
        log::error("cannot start the pose editor: {}", Error.what());
        return EXIT_FAILURE;
    }
    if (!std::filesystem::exists(ClipPath)) {
        log::error("no clip {}", ClipPath.string());
        return EXIT_FAILURE;
    }
    if (!reload()) {
        log::error("{}", Status);
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
    int Frame = 0;
    while (Window.isOpen()) {
        while (const std::optional Event = Window.pollEvent()) {
            ImGui::SFML::ProcessEvent(Window, *Event);
            if (Event->is<sf::Event::Closed>()) Window.close();
        }
        const sf::Time Delta = Clock.restart();
        ImGui::SFML::Update(Window, Delta);

        ImGuiIO& IO = ImGui::GetIO();
        const bool Command = IO.KeyCtrl || IO.KeySuper;
        if (Command && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            if (Edited && !IO.KeyShift) {
                Status = "unsaved changes: Shift+F5 reloads the file and drops them";
            } else {
                reload();
            }
        }
        if (Playing) TimeSec = anim::advanceClipTime(Edit, TimeSec, Delta.asSeconds(), 1.0f);
        // A one-shot clip plays over and over in the preview.
        if (Playing && !Edit.Loop && Edit.isFinishedAt(TimeSec)) TimeSec = 0.0f;

        Highlight.reset();
        Window.clear(sf::Color(40, 40, 46));
        const View Scene = makeView(Window.getSize());
        drawFloor(Window, Scene);
        const anim::Pose Ghost = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
        const editor::PosedBody Body = editor::poseBody(Context.Rig, Ghost, Context.Held);
        drawPanels();
        drawBody(Window, Scene, Context.Rig, Body, anim::samplePelvisOffset(Edit, TimeSec), Highlight);
        ImGui::SFML::Render(Window);
        Window.display();

        if (Opts.Screenshot && ++Frame == ScreenshotFrame) {
            const bool Saved = saveScreenshot(Window, *Opts.Screenshot);
            ImGui::SFML::Shutdown();
            return Saved ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    ImGui::SFML::Shutdown();
    return EXIT_SUCCESS;
}

bool PoseEditor::reload() {
    try {
        Edit = anim::loadClip(ClipPath);
    } catch (const std::exception& Error) {
        Status = std::format("cannot read the clip: {}", Error.what());
        return false;
    }
    Edited = false;
    SelectedKey = std::min(SelectedKey, Edit.Keys.size() - 1);
    SelectedPelvisKey = 0;
    TimeSec = std::min(TimeSec, Edit.DurationSec);
    Status = std::format("loaded {}", ClipPath.filename().string());
    return true;
}

bool PoseEditor::save() {
    try {
        editor::saveClip(Edit, ClipPath);
    } catch (const std::exception& Error) {
        Status = std::format("not saved: {}", Error.what());
        return false;
    }
    Edited = false;
    Status = std::format("saved {}", ClipPath.filename().string());
    return true;
}

void PoseEditor::selectKey(size_t Index) {
    SelectedKey = Index;
    TimeSec = Edit.Keys[Index].TimeSec;
    Playing = false;
}

void PoseEditor::drawPanels() {
    drawClipPanel();
    drawTimelinePanel();
    drawKeyPanel();
}

void PoseEditor::drawClipPanel() {
    ImGui::SetNextWindowPos({8.0f, 8.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({362.0f, ImGui::GetIO().DisplaySize.y - 350.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Clip");
    ImGui::Text("%s%s", Opts.Clip.c_str(), Edited ? "  (unsaved)" : "");
    ImGui::Text("item: %s", Context.ItemName.empty() ? "(bare hands)" : Context.ItemName.c_str());
    ImGui::Text("stance: %s", Context.Stance.Name.c_str());
    if (editor::isPlayedOtherHand(Edit, Context.WeaponHand)) {
        ImGui::TextColored({1.0f, 0.8f, 0.3f, 1.0f}, "authored for the other hand: the ghost\nplays it with the arms swapped");
    }

    const std::string Problem = editor::findClipProblem(Edit);
    ImGui::BeginDisabled(!Problem.empty());
    if (ImGui::Button("Save (Ctrl+S)")) save();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reload (F5)")) {
        if (Edited) {
            Status = "unsaved changes: Shift+F5 reloads the file and drops them";
        } else {
            reload();
        }
    }
    if (!Problem.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", Problem.c_str());
    ImGui::TextWrapped("%s", Status.c_str());
    ImGui::Separator();

    if (ImGui::Checkbox("loop", &Edit.Loop)) markEdited();
    float Duration = Edit.DurationSec;
    if (dragSeconds("duration, s", Duration)) {
        editor::setDuration(Edit, Duration);
        TimeSec = std::min(TimeSec, Edit.DurationSec);
        markEdited();
    }
    float Active[2] = {Edit.ActiveBeginSec, Edit.ActiveEndSec};
    if (ImGui::DragFloat2("active, s", Active, 0.005f, 0.0f, Edit.DurationSec, "%.3f")) {
        editor::setActive(Edit, Active[0], Active[1]);
        markEdited();
    }
    if (ImGui::DragFloat("stiffness", &Edit.Stiffness, 0.01f, 0.1f, 4.0f, "%.2f")) markEdited();
    if (ImGui::Checkbox("allowMove", &Edit.AllowMove)) markEdited();
    for (auto&& [Label, Fade] : {std::pair{"blendIn, s", &Edit.BlendInSec}, std::pair{"blendOut, s", &Edit.BlendOutSec}}) {
        ImGui::PushID(Label);
        bool Has = Fade->has_value();
        if (ImGui::Checkbox("##has", &Has)) {
            if (Has) {
                *Fade = 0.05f;
            } else {
                Fade->reset();
            }
            markEdited();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!Has);
        float Value = Fade->value_or(0.0f);
        if (dragSeconds(Label, Value)) {
            *Fade = std::max(Value, 0.0f);
            markEdited();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("strikers")) {
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const auto Part = static_cast<BodyPart>(Index);
            if (Part == BodyPart::Pelvis || Part == BodyPart::Torso || Part == BodyPart::Head) continue;
            bool Striker = Edit.Strikers.test(Index);
            if (ImGui::Checkbox(std::string(getBodyPartName(Part)).c_str(), &Striker)) {
                Edit.Strikers.set(Index, Striker);
                markEdited();
            }
        }
    }

    if (ImGui::CollapsingHeader("joints in the clip")) {
        for (const BodyPart Part : JointOrder) {
            const bool Leg = isLegPart(Part);
            bool Keyed = editor::isJointKeyed(Edit, Part);
            ImGui::BeginDisabled(Leg);
            if (ImGui::Checkbox(std::string(getBodyPartName(Part)).c_str(), &Keyed)) {
                // A new joint starts where the ghost has it now.
                const anim::Pose Now = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
                editor::setJointKeyed(Edit, Part, Keyed, Keyed ? Now.getAngle(Part) : 0.0f);
                markEdited();
            }
            ImGui::EndDisabled();
        }
        bool Wrist = Edit.Keys.front().Target.HasWeapon;
        if (ImGui::Checkbox("Weapon (wrist)", &Wrist)) {
            const anim::Pose Now = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
            editor::setWristKeyed(Edit, Wrist, Now.HasWeapon ? Now.WeaponAngle : 0.0f);
            markEdited();
        }
    }

    if (ImGui::CollapsingHeader("pelvisX")) {
        ImGui::BeginDisabled(Edit.Loop);
        if (ImGui::Button("add key at the time")) {
            if (const auto Added = editor::addPelvisKey(Edit, TimeSec)) {
                SelectedPelvisKey = *Added;
                markEdited();
            }
        }
        ImGui::EndDisabled();
        for (size_t Index = 0; Index < Edit.PelvisTrack.size(); ++Index) {
            ImGui::PushID(static_cast<int>(Index));
            anim::PelvisKey& Key = Edit.PelvisTrack[Index];
            float Time = Key.TimeSec;
            float Offset = Key.OffsetX;
            ImGui::BeginDisabled(Index == 0);
            ImGui::SetNextItemWidth(80.0f);
            const bool TimeChanged = dragSeconds("t", Time);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            const bool OffsetChanged = ImGui::DragFloat("x", &Offset, 0.005f, -anim::MaxPelvisOffsetM,
                                                        anim::MaxPelvisOffsetM, "%.3f");
            ImGui::SameLine();
            const bool Removed = ImGui::Button("x##del");
            ImGui::EndDisabled();
            if (TimeChanged || OffsetChanged) {
                editor::setPelvisKey(Edit, Index, Time, Offset);
                markEdited();
            }
            ImGui::PopID();
            if (Removed) {
                editor::deletePelvisKey(Edit, Index);
                markEdited();
                break;
            }
        }
    }
    ImGui::End();
}

void PoseEditor::drawTimelinePanel() {
    const ImVec2 Screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({8.0f, Screen.y - 330.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({PanelColumnWidth - 16.0f, 322.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Timeline");
    ImGui::Checkbox("play", &Playing);
    ImGui::SameLine();
    ImGui::Text("%s", anim::describePlayback(Edit, TimeSec, 1.0f, anim::PoseTransition{}).c_str());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##time", &TimeSec, 0.0f, Edit.DurationSec, "%.3f s");
    // The keys as ticks under the slider.
    const ImVec2 Low = ImGui::GetItemRectMin();
    const ImVec2 High = ImGui::GetItemRectMax();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    for (auto&& [Index, Key] : std::views::zip(std::views::iota(size_t{0}), Edit.Keys)) {
        const float X = Low.x + (High.x - Low.x) * Key.TimeSec / Edit.DurationSec;
        Draw->AddLine({X, High.y}, {X, High.y + 8.0f}, Index == SelectedKey ? IM_COL32(255, 200, 60, 255) : IM_COL32(200, 200, 200, 255), 2.0f);
    }
    if (Edit.ActiveEndSec > Edit.ActiveBeginSec) {
        const float Begin = Low.x + (High.x - Low.x) * Edit.ActiveBeginSec / Edit.DurationSec;
        const float End = Low.x + (High.x - Low.x) * Edit.ActiveEndSec / Edit.DurationSec;
        Draw->AddRectFilled({Begin, High.y + 10.0f}, {End, High.y + 14.0f}, IM_COL32(230, 80, 80, 255));
    }
    ImGui::Dummy({0.0f, 18.0f});

    if (ImGui::Button("add key here")) {
        if (const auto Added = editor::addKey(Edit, TimeSec)) {
            selectKey(*Added);
            markEdited();
        } else {
            Status = "no key can be added here (at 0, past the end, or too close to another)";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("duplicate")) {
        if (const auto Copy = editor::duplicateKey(Edit, SelectedKey)) {
            selectKey(*Copy);
            markEdited();
        } else {
            Status = "no room after this key for a copy";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("delete")) {
        if (editor::deleteKey(Edit, SelectedKey)) {
            selectKey(std::min(SelectedKey, Edit.Keys.size() - 1));
            markEdited();
        } else {
            Status = "the first key and the only key cannot be deleted";
        }
    }
    ImGui::Separator();

    for (auto&& [Index, Key] : std::views::zip(std::views::iota(size_t{0}), Edit.Keys)) {
        ImGui::PushID(static_cast<int>(Index));
        if (ImGui::Selectable(std::format("key {}", Index).c_str(), Index == SelectedKey,
                              ImGuiSelectableFlags_None, {70.0f, 0.0f})) {
            selectKey(Index);
        }
        ImGui::SameLine();
        float Time = Key.TimeSec;
        ImGui::BeginDisabled(Index == 0);
        ImGui::SetNextItemWidth(110.0f);
        if (dragSeconds("t, s", Time)) {
            selectKey(Index);
            editor::moveKey(Edit, Index, Time);
            TimeSec = Edit.Keys[Index].TimeSec;
            markEdited();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::End();
}

void PoseEditor::drawKeyPanel() {
    ImGui::SetNextWindowPos({378.0f, 8.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({304.0f, ImGui::GetIO().DisplaySize.y - 350.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Key");
    anim::Keyframe& Key = Edit.Keys[SelectedKey];
    ImGui::Text("key %zu at %.3f s", SelectedKey, Key.TimeSec);
    ImGui::TextDisabled("legs are shown, not edited");
    ImGui::Separator();
    for (const BodyPart Part : JointOrder) {
        if (!Key.Target.hasJoint(Part)) continue;
        const bool Leg = isLegPart(Part);
        const editor::AngleRange Range = editor::getJointRange(Context.Rig, Part);
        float Angle = Key.Target.getAngle(Part);
        const bool Outside = Angle < Range.Lower - 1e-4f || Angle > Range.Upper + 1e-4f;
        const std::string Label(getBodyPartName(Part));
        ImGui::BeginDisabled(Leg);
        if (Outside) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::SetNextItemWidth(140.0f);
        if (sliderAngle(Label.c_str(), Angle, Range)) {
            Key.Target.setAngle(Part, Angle);
            markEdited();
        }
        if (Outside) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) Highlight = Part;
    }
    if (Key.Target.HasWeapon) {
        const editor::AngleRange Wrist = editor::getWristRange(Context.Rig);
        const float Limit = std::numbers::pi_v<float>;   // the file may not write more
        const editor::AngleRange Range = {.Lower = std::max(Wrist.Lower, -Limit), .Upper = std::min(Wrist.Upper, Limit)};
        float Angle = Key.Target.WeaponAngle;
        ImGui::SetNextItemWidth(140.0f);
        if (sliderAngle("Weapon (wrist)", Angle, Range)) {
            Key.Target.setWeaponAngle(Angle);
            markEdited();
        }
    }
    ImGui::End();
}

namespace {

bool isLegPart(BodyPart Part) { return anim::getLayer(Part) == anim::Layer::Legs; }

View makeView(sf::Vector2u Size) {
    const auto Height = static_cast<float>(Size.y);
    const float Scale = (Height - FloorMargin - 30.0f) / HeadroomM;
    return {.Origin = {PanelColumnWidth + (static_cast<float>(Size.x) - PanelColumnWidth) * 0.5f, Height - FloorMargin},
            .PixelsPerMeter = Scale};
}

sf::Vector2f toScreen(const View& Scene, Vec2 Point) {
    return {Scene.Origin.x + Point.X * Scene.PixelsPerMeter, Scene.Origin.y - Point.Y * Scene.PixelsPerMeter};
}

Vec2 toWorld(const editor::PosedPart& Posed, const rig::PartDef& Part, Vec2 Point, float OffsetX) {
    return Posed.Position + rotate(Point - editor::getPartOrigin(Part), Posed.Angle) + Vec2{OffsetX, 0.0f};
}

void drawCapsule(sf::RenderTarget& Target, const View& Scene, Vec2 From, Vec2 To, float Radius, sf::Color Fill, sf::Color Line) {
    const Vec2 Axis = To - From;
    const float Length = Axis.getLength();
    const Vec2 Side = Length > 1e-6f ? perp(Axis / Length) * Radius : Vec2{0.0f, Radius};
    sf::ConvexShape Body(4);
    Body.setPoint(0, toScreen(Scene, From + Side));
    Body.setPoint(1, toScreen(Scene, To + Side));
    Body.setPoint(2, toScreen(Scene, To - Side));
    Body.setPoint(3, toScreen(Scene, From - Side));
    Body.setFillColor(Fill);
    Target.draw(Body);
    for (const Vec2 Center : {From, To}) {
        sf::CircleShape Cap(Radius * Scene.PixelsPerMeter, 20);
        Cap.setOrigin({Radius * Scene.PixelsPerMeter, Radius * Scene.PixelsPerMeter});
        Cap.setPosition(toScreen(Scene, Center));
        Cap.setFillColor(Fill);
        Cap.setOutlineColor(Line);
        Cap.setOutlineThickness(1.0f);
        Target.draw(Cap);
    }
}

void drawPolygon(sf::RenderTarget& Target, const View& Scene, const std::array<Vec2, 4>& Corners, sf::Color Fill, sf::Color Line) {
    sf::ConvexShape Shape(Corners.size());
    for (auto&& [Index, Corner] : std::views::zip(std::views::iota(size_t{0}), Corners)) {
        Shape.setPoint(Index, toScreen(Scene, Corner));
    }
    Shape.setFillColor(Fill);
    Shape.setOutlineColor(Line);
    Shape.setOutlineThickness(1.0f);
    Target.draw(Shape);
}

void drawPart(sf::RenderTarget& Target, const View& Scene, const editor::PosedPart& Posed, const rig::PartDef& Part, float OffsetX,
              sf::Color Fill, sf::Color Line) {
    switch (Part.Shape) {
        case physics::ShapeKind::Capsule:
            drawCapsule(Target, Scene, toWorld(Posed, Part, Part.Begin, OffsetX), toWorld(Posed, Part, Part.End, OffsetX),
                        Part.Radius, Fill, Line);
            break;
        case physics::ShapeKind::Circle: {
            const Vec2 Center = toWorld(Posed, Part, Part.Center, OffsetX);
            drawCapsule(Target, Scene, Center, Center, Part.Radius, Fill, Line);
            break;
        }
        case physics::ShapeKind::Box: {
            const Vec2 Half = Part.HalfExtents;
            drawPolygon(Target, Scene,
                        {toWorld(Posed, Part, Part.Center + Vec2{-Half.X, -Half.Y}, OffsetX),
                         toWorld(Posed, Part, Part.Center + Vec2{Half.X, -Half.Y}, OffsetX),
                         toWorld(Posed, Part, Part.Center + Vec2{Half.X, Half.Y}, OffsetX),
                         toWorld(Posed, Part, Part.Center + Vec2{-Half.X, Half.Y}, OffsetX)},
                        Fill, Line);
            break;
        }
    }
}

void drawBody(sf::RenderTarget& Target, const View& Scene, const rig::RigDef& Rig, const editor::PosedBody& Body, float OffsetX,
              std::optional<BodyPart> Highlight) {
    const sf::Color Line(230, 230, 240, 200);
    for (const BodyPart Part : DrawOrder) {
        sf::Color Fill = isLegPart(Part) ? sf::Color(120, 130, 150, 170) : sf::Color(110, 170, 230, 190);
        if (Highlight == Part) Fill = sf::Color(255, 190, 70, 230);
        drawPart(Target, Scene, Body.get(Part), Rig.getPart(Part), OffsetX, Fill, Line);
        for (const auto& Shield : Body.Shields) {
            if (Shield.Holder != Part) continue;
            const Vec2 Half = Shield.HalfExtents;
            std::array<Vec2, 4> Corners = {Vec2{-Half.X, -Half.Y}, Vec2{Half.X, -Half.Y}, Vec2{Half.X, Half.Y},
                                           Vec2{-Half.X, Half.Y}};
            for (auto& Corner : Corners) Corner = Shield.Center + rotate(Corner, Shield.Angle) + Vec2{OffsetX, 0.0f};
            drawPolygon(Target, Scene, Corners, sf::Color(170, 120, 70, 200), Line);
        }
        for (const auto& Weapon : Body.Weapons) {
            if (Weapon.Holder != Part) continue;
            const Vec2 Shift = {OffsetX, 0.0f};
            drawCapsule(Target, Scene, Weapon.Fist + Shift, Weapon.Tip + Shift, Weapon.Radius,
                        sf::Color(230, 160, 70, 230), Line);
        }
    }
}

void drawFloor(sf::RenderTarget& Target, const View& Scene) {
    sf::RectangleShape Floor({static_cast<float>(Target.getSize().x), 3.0f});
    Floor.setPosition({0.0f, Scene.Origin.y});
    Floor.setFillColor(sf::Color(120, 120, 130));
    Target.draw(Floor);
    // A tick every half meter, to read the pelvis offset and the reach.
    for (int Tick = -2; Tick <= 3; ++Tick) {
        sf::RectangleShape Mark({2.0f, 10.0f});
        Mark.setPosition({Scene.Origin.x + static_cast<float>(Tick) * 0.5f * Scene.PixelsPerMeter, Scene.Origin.y + 3.0f});
        Mark.setFillColor(sf::Color(120, 120, 130));
        Target.draw(Mark);
    }
}

bool saveScreenshot(const sf::RenderWindow& Window, const std::filesystem::path& Path) {
    sf::Texture Capture(Window.getSize());
    Capture.update(Window);
    if (Capture.copyToImage().saveToFile(Path)) {
        log::info("screenshot saved to {}", Path.string());
        return true;
    }
    log::error("cannot save the screenshot {}", Path.string());
    return false;
}

/// A slider in degrees over \p Range for an angle kept in radians. True if it changed.
bool sliderAngle(const char* Label, float& Radians, editor::AngleRange Range) {
    float Degrees = Radians * DegreesPerRadian;
    if (!ImGui::SliderFloat(Label, &Degrees, Range.Lower * DegreesPerRadian, Range.Upper * DegreesPerRadian,
                            "%.1f deg")) {
        return false;
    }
    Radians = Degrees * RadiansPerDegree;
    return true;
}

bool dragSeconds(const char* Label, float& Seconds, float Speed) {
    return ImGui::DragFloat(Label, &Seconds, Speed, 0.0f, 10.0f, "%.3f");
}

} // namespace

} // namespace fighter::app
