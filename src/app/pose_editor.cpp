#include "app/pose_editor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <optional>
#include <stdexcept>
#include <format>
#include <numbers>
#include <ranges>
#include <utility>
#include <vector>

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
#include "combat/clip_library.hpp"
#include "core/log.hpp"
#include "editor/arm_roles.hpp"
#include "editor/clip_edit.hpp"
#include "editor/clip_writer.hpp"
#include "editor/pose_drag.hpp"
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
/// The preview speed: its range and the presets of the Timeline panel.
constexpr float MinPlaySpeed = 0.05f;
constexpr float MaxPlaySpeed = 1.0f;
constexpr std::array PlaySpeedPresets = {0.1f, 0.25f, 0.5f, 1.0f};
/// The mouse: the radius of a handle and how near a part must be to take it, px.
constexpr float HandleRadiusPx = 8.0f;
constexpr float PartTolerancePx = 3.0f;
/// The time slider is on a key when this close to it, s.
constexpr float OnKeySec = 1e-5f;
/// The fact layer: half transparent over the ghost.
const sf::Color FactFill(240, 110, 200, 80);
const sf::Color FactLine(255, 150, 225, 170);
const sf::Color DummyFill(150, 150, 160, 60);
/// Frames before a screenshot is taken (the panels need a couple to lay out).
constexpr int ScreenshotFrame = 3;

/// The parts in the order they are drawn, far to near: legs, trunk, arms.
constexpr std::array<BodyPart, BodyPartCount> DrawOrder = {
    BodyPart::ThighR,    BodyPart::ShinR,     BodyPart::FootR,     BodyPart::ThighL,   BodyPart::ShinL,
    BodyPart::FootL,     BodyPart::Pelvis,    BodyPart::Torso,     BodyPart::Head,     BodyPart::UpperArmR,
    BodyPart::ForearmR,  BodyPart::UpperArmL, BodyPart::ForearmL,
};

/// Where the scene is on the screen: the floor point under the body, and the
/// scale.
struct View {
    sf::Vector2f Origin;
    float PixelsPerMeter = 1.0f;
};

bool isLegPart(BodyPart Part);
/// "stance_hammer - used by: Knight; moveset hammer".
std::string describeStanceUsers(const std::string& Clip, const editor::StanceUsers& Users);
/// Lower case copy, for the filter of the clip list.
std::string toLower(std::string Text);
std::string joinNames(const std::vector<std::string>& Names);
/// The joints of the panels, the arms by role: weapon arm, then the other.
std::array<BodyPart, BodyPartCount> getPanelOrder(BodyPart MainHand, bool PlayedOtherHand);
/// "Torso", or "weapon arm: forearm" (and the joint of the file) for an arm joint.
std::string getJointLabel(BodyPart Joint, BodyPart MainHand, bool PlayedOtherHand);
View makeView(sf::Vector2u Size);
sf::Vector2f toScreen(const View& Scene, Vec2 Point);
Vec2 fromScreen(const View& Scene, ImVec2 Pixel);
/// The handles of the ghost: the fists that can be moved and the blade tip.
struct HandlePoint {
    BodyPart Forearm;
    bool Tip;
    Vec2 Point;
};
std::vector<HandlePoint> getHandles(const editor::GhostContext& Context, const editor::PosedBody& Body);
void drawHandles(sf::RenderTarget& Target, const View& Scene, const editor::GhostContext& Context,
                 const editor::PosedBody& Body, float OffsetX, std::optional<HandlePoint> Hovered);
Vec2 fromScreen(const View& Scene, ImVec2 Pixel) {
    return {(Pixel.x - Scene.Origin.x) / Scene.PixelsPerMeter, (Scene.Origin.y - Pixel.y) / Scene.PixelsPerMeter};
}

std::vector<HandlePoint> getHandles(const editor::GhostContext& Context, const editor::PosedBody& Body) {
    std::vector<HandlePoint> Handles;
    for (const auto& Weapon : Body.Weapons) {
        if (Weapon.Holder == Context.WeaponHand) Handles.push_back({.Forearm = Weapon.Holder, .Tip = true, .Point = Weapon.Tip});
    }
    // The gripping fist of a two-handed weapon follows the handle.
    for (const BodyPart Forearm : {BodyPart::ForearmL, BodyPart::ForearmR}) {
        if (Forearm == Context.GripHand) continue;
        Handles.push_back({.Forearm = Forearm, .Tip = false, .Point = editor::getFistPoint(Context.Rig, Body, Forearm)});
    }
    return Handles;
}

void drawHandles(sf::RenderTarget& Target, const View& Scene, const editor::GhostContext& Context,
                 const editor::PosedBody& Body, float OffsetX, std::optional<HandlePoint> Hovered) {
    for (const auto& Handle : getHandles(Context, Body)) {
        const bool Lit = Hovered && Hovered->Forearm == Handle.Forearm && Hovered->Tip == Handle.Tip;
        sf::CircleShape Circle(HandleRadiusPx, 20);
        Circle.setOrigin({HandleRadiusPx, HandleRadiusPx});
        Circle.setPosition(toScreen(Scene, Handle.Point + Vec2{OffsetX, 0.0f}));
        Circle.setFillColor(Lit ? sf::Color(255, 210, 80, 150) : sf::Color(255, 255, 255, 40));
        Circle.setOutlineColor(Lit ? sf::Color(255, 220, 90) : (Handle.Tip ? sf::Color(240, 170, 80) : sf::Color(140, 220, 255)));
        Circle.setOutlineThickness(Lit ? 2.5f : 1.5f);
        Target.draw(Circle);
    }
    // Where the other hand holds a two-handed weapon.
    if (!Context.GripHand) return;
    for (const auto& Weapon : Body.Weapons) {
        if (Weapon.Holder != Context.WeaponHand) continue;
        sf::CircleShape Dot(3.0f, 12);
        Dot.setOrigin({3.0f, 3.0f});
        Dot.setPosition(toScreen(Scene, editor::getGripPoint(Weapon, Context.GripM) + Vec2{OffsetX, 0.0f}));
        Dot.setFillColor(sf::Color(140, 220, 255));
        Target.draw(Dot);
    }
}

Vec2 toWorld(const editor::PosedPart& Posed, const rig::PartDef& Part, Vec2 Point, float OffsetX);
void drawCapsule(sf::RenderTarget& Target, const View& Scene, Vec2 From, Vec2 To, float Radius, sf::Color Fill, sf::Color Line);
void drawPolygon(sf::RenderTarget& Target, const View& Scene, const std::array<Vec2, 4>& Corners, sf::Color Fill, sf::Color Line);
void drawPart(sf::RenderTarget& Target, const View& Scene, const editor::PosedPart& Posed, const rig::PartDef& Part, float OffsetX,
              sf::Color Fill, sf::Color Line);
void drawBody(sf::RenderTarget& Target, const View& Scene, const rig::RigDef& Rig, const editor::PosedBody& Body, float OffsetX,
              std::optional<BodyPart> Highlight);
void drawFloor(sf::RenderTarget& Target, const View& Scene);
/// The fact layer: the fighter of \p Frame by the shapes of \p Rig, its
/// weapons and, if \p WithDummy, the dummy, half transparent, moved by
/// \p ShiftX along the floor.
void drawFact(sf::RenderTarget& Target, const View& Scene, const rig::RigDef& Rig, const editor::FactFrame& Frame,
              float ShiftX, bool WithDummy);
const char* getAttackPhaseLabel(combat::AttackPhase Phase);
/// A part or a weapon of the fact as a capsule along its long axis (Y of its
/// frame, PartTransform::Size).
void drawTransform(sf::RenderTarget& Target, const View& Scene, const PartTransform& Part, float ShiftX, sf::Color Fill,
                   sf::Color Line);
bool saveScreenshot(const sf::RenderWindow& Window, const std::filesystem::path& Path);
bool sliderAngle(const char* Label, float& Radians, editor::AngleRange Range);
bool dragSeconds(const char* Label, float& Seconds, float Speed = 0.005f);
bool canBendElbowsBothWays(const rig::RigDef& Rig);

} // namespace

PoseEditor::PoseEditor(Options Settings) : Opts(std::move(Settings)) {}

int PoseEditor::run() {
    Selection First{.Clip = Opts.Clip.empty() ? std::string(combat::clips::Stance) : Opts.Clip, .Items = {}, .Move = {}};
    if (!Opts.Weapon.empty()) First.Items.push_back({.Id = Opts.Weapon, .Slot = std::nullopt});
    if (!open(First)) {
        log::error("cannot start the pose editor: {}", Status);
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
        // [ and ] halve and double the preview speed, as the sandbox's time scale.
        if (!IO.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) PlaySpeed = std::max(PlaySpeed * 0.5f, MinPlaySpeed);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) PlaySpeed = std::min(PlaySpeed * 2.0f, MaxPlaySpeed);
        }
        if (!IO.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) Playing = !Playing;
            if (ImGui::IsKeyPressed(ImGuiKey_Period)) stepFact(1);
            if (ImGui::IsKeyPressed(ImGuiKey_Comma)) stepFact(-1);
        }
        if (Playing) TimeSec = anim::advanceClipTime(Edit, TimeSec, Delta.asSeconds() * PlaySpeed, 1.0f);
        // A one-shot clip plays over and over in the preview.
        if (Playing && !Edit.Loop && Edit.isFinishedAt(TimeSec)) TimeSec = 0.0f;

        if (canBendElbowsBothWays(Context.Rig) && !IO.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            Bend = Bend == editor::ElbowBend::Down ? editor::ElbowBend::Up : editor::ElbowBend::Down;
        }

        Highlight.reset();
        syncStance();
        Window.clear(sf::Color(40, 40, 46));
        const View Scene = makeView(Window.getSize());
        drawFloor(Window, Scene);
        drawPanels();
        // The ghost is drawn without the pelvis offset; the mouse is taken to it.
        const Vec2 Mouse = fromScreen(Scene, IO.MousePos) - Vec2{anim::samplePelvisOffset(Edit, TimeSec), 0.0f};
        if (ShowGhost) {
            updateMouse(Mouse, Scene.PixelsPerMeter);
        } else {
            Hovered = {};
            Dragging.reset();
        }
        updateFact();
        const float OffsetX = anim::samplePelvisOffset(Edit, TimeSec);
        const anim::Pose Ghost = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
        const editor::PosedBody Body = editor::poseBody(Context.Rig, Ghost, Context.Held);
        if (!Highlight && Hovered.Kind == GrabKind::Part) Highlight = Hovered.Part;
        if (ShowGhost) drawBody(Window, Scene, Context.Rig, Body, OffsetX, Highlight);
        if (const editor::FactFrame* Shown = ShowFact ? findFactFrame() : nullptr) {
            drawFact(Window, Scene, Context.Rig, *Shown, GhostStartX, Fact->isWithDummy());
        }
        std::optional<HandlePoint> HoveredHandle;
        if (Hovered.Kind == GrabKind::Fist || Hovered.Kind == GrabKind::Tip) {
            HoveredHandle = HandlePoint{.Forearm = Hovered.Part, .Tip = Hovered.Kind == GrabKind::Tip, .Point = {}};
        }
        if (ShowGhost) drawHandles(Window, Scene, Context, Body, OffsetX, HoveredHandle);
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

bool PoseEditor::open(const Selection& Choice) {
    editor::GhostContext NewContext;
    anim::Clip NewClip;
    const std::filesystem::path NewPath = Opts.Root / "data" / "poses" / (Choice.Clip + ".json");
    try {
        NewContext = editor::loadGhostContext(Opts.Root, Choice.Items);
        if (!std::filesystem::exists(NewPath)) throw std::runtime_error(std::format("no clip {}", NewPath.string()));
        NewClip = anim::loadClip(NewPath);
    } catch (const std::exception& Error) {
        Status = std::format("not opened: {}", Error.what());
        return false;
    }
    Context = std::move(NewContext);
    Edit = std::move(NewClip);
    ClipName = Choice.Clip;
    ClipPath = NewPath;
    HandItems = Choice.Items;
    MoveId = Choice.Move;
    Edited = false;
    ClipHadLegs = editor::isLegClip(Edit);
    TimeSec = 0.0f;
    Playing = false;
    SelectedKey = 0;
    SelectedPelvisKey = 0;
    Highlight.reset();
    Hovered = {};
    Dragging.reset();
    FactStep.reset();
    refreshCatalog();
    const bool KeepDummy = Fact && Fact->isWithDummy();
    Fact.emplace(editor::PreviewSettings{.DataDir = Opts.Root / "data",
                                         .ClipName = ClipName,
                                         .WeaponId = findWeaponId(),
                                         .MoveId = MoveId,
                                         .WithDummy = KeepDummy});
    syncStance();
    Status = std::format("opened {}", ClipPath.filename().string());
    return true;
}

void PoseEditor::requestOpen(Selection Choice) {
    if (!Edited) {
        open(Choice);
        return;
    }
    Pending = std::move(Choice);
    AskUnsaved = true;
}

void PoseEditor::refreshCatalog() {
    const std::filesystem::path DataDir = Opts.Root / "data";
    try {
        Fighters = editor::listFighters(DataDir);
        HandItemList = editor::listHandItems(DataDir);
        ClipList = editor::listClips(DataDir);
        MoveList = editor::findMovesPlayingClip(DataDir, ClipName);
        Users = editor::findStanceUsers(DataDir, ClipName);
    } catch (const std::exception& Error) {
        Status = std::format("the lists of the Open panel are incomplete: {}", Error.what());
    }
}

void PoseEditor::syncStance() {
    if (ClipName == Context.StanceName) Context.Stance = Edit;
}

std::string PoseEditor::findWeaponId() const {
    for (const stats::ItemRef& Item : HandItems) {
        const auto Found = std::ranges::find(HandItemList, Item.Id, &editor::HandItemEntry::Id);
        if (Found != HandItemList.end() && !Found->Shield) return Item.Id;
    }
    return {};
}

bool PoseEditor::reload() {
    try {
        Edit = anim::loadClip(ClipPath);
    } catch (const std::exception& Error) {
        Status = std::format("cannot read the clip: {}", Error.what());
        return false;
    }
    Edited = false;
    ClipHadLegs = editor::isLegClip(Edit);
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
    ClipHadLegs = editor::isLegClip(Edit);
    Status = std::format("saved {}", ClipPath.filename().string());
    return true;
}

void PoseEditor::updateFact() {
    // A time set by the slider, playback or a key shows the frame of that time.
    if (FactStep && (Playing || TimeSec != FactStepTimeSec)) FactStep.reset();
    const anim::Pose Start = editor::composeGhostPose(Context.Stance, Edit, 0.0f, Context.WeaponHand);
    GhostStartX = editor::poseBody(Context.Rig, Start, Context.Held).get(BodyPart::Pelvis).Position.X;
    // An edit in progress (a drag, a slider held) runs once it is done: a run
    // from the start takes a moment.
    if (Dragging || ImGui::IsAnyItemActive()) return;
    if (Fact->setClip(Edit)) FactStep.reset();
}

const editor::FactFrame* PoseEditor::findFactFrame() {
    if (FactStep) return Fact->getFrame(*FactStep);
    const std::optional<size_t> Index = Fact->findFrame(TimeSec);
    return Index ? Fact->getFrame(*Index) : nullptr;
}

void PoseEditor::stepFact(int Direction) {
    const std::optional<size_t> Current = FactStep ? FactStep : Fact->findFrame(TimeSec);
    if (!Current) return;
    if (Direction < 0 && *Current == 0) return;
    const size_t Next = Direction < 0 ? *Current - 1 : *Current + 1;
    const editor::FactFrame* Frame = Fact->getFrame(Next);
    if (!Frame) {
        Status = "the move ends here";
        return;
    }
    Playing = false;
    FactStep = Next;
    TimeSec = std::min(Frame->ClipSec, Edit.DurationSec);
    FactStepTimeSec = TimeSec;
}

void PoseEditor::selectKey(size_t Index) {
    SelectedKey = Index;
    TimeSec = Edit.Keys[Index].TimeSec;
    Playing = false;
}

void PoseEditor::updateMouse(Vec2 Mouse, float PixelsPerMeter) {
    if (Dragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Dragging.reset();
        } else {
            Hovered = Dragging->Target;
            if (Mouse != Dragging->From) applyDrag(Mouse);
            return;
        }
    }
    Hovered = {};
    // Over a panel the mouse belongs to Dear ImGui.
    if (ImGui::GetIO().WantCaptureMouse) return;
    const anim::Pose Ghost = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
    Hovered = findGrab(editor::poseBody(Context.Rig, Ghost, Context.Held), Mouse, PixelsPerMeter);
    if (Hovered.Kind == GrabKind::None || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    // The mouse edits a key: off a key, the time moves onto the nearest one.
    const size_t Key = editor::findNearestKey(Edit, TimeSec);
    if (std::abs(Edit.Keys[Key].TimeSec - TimeSec) > OnKeySec) {
        Status = std::format("the mouse edits key {}: the time moved onto it", Key);
    }
    selectKey(Key);
    Dragging = Drag{.Target = Hovered,
                    .From = Mouse,
                    .StartGhost = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand)};
}

void PoseEditor::applyDrag(Vec2 Mouse) {
    const Drag& Current = *Dragging;
    editor::GhostEdit Change;
    switch (Current.Target.Kind) {
        case GrabKind::None: return;
        case GrabKind::Part:
            Change = editor::rotateGhostJoint(Context, Current.StartGhost, Current.Target.Part, Current.From, Mouse, Bend);
            break;
        case GrabKind::Fist:
            Change = editor::moveGhostFist(Context, Current.StartGhost, Current.Target.Part, Mouse, Bend);
            break;
        case GrabKind::Tip: Change = editor::aimGhostWeapon(Context, Current.StartGhost, Mouse, Bend); break;
    }
    editor::applyGhostEdit(Change, Edit, SelectedKey, editor::isPlayedOtherHand(Edit, Context.WeaponHand));
    markEdited();
}

PoseEditor::Grab PoseEditor::findGrab(const editor::PosedBody& Body, Vec2 Mouse, float PixelsPerMeter) const {
    const float HandleRadius = HandleRadiusPx / PixelsPerMeter;
    // The tip before the fists: on a short blade they are close.
    std::vector<HandlePoint> Handles = getHandles(Context, Body);
    std::ranges::stable_partition(Handles, [](const HandlePoint& Handle) { return Handle.Tip; });
    for (const auto& Handle : Handles) {
        if ((Handle.Point - Mouse).getLength() <= HandleRadius) {
            return {.Kind = Handle.Tip ? GrabKind::Tip : GrabKind::Fist, .Part = Handle.Forearm};
        }
    }
    // The nearest part on top: draw order back to front.
    for (const BodyPart Part : std::views::reverse(DrawOrder)) {
        if (!editor::isDraggablePart(Context.Rig, Part)) continue;
        if (editor::getDistanceToPart(Context.Rig.getPart(Part), Body.get(Part), Mouse) <= PartTolerancePx / PixelsPerMeter) {
            return {.Kind = GrabKind::Part, .Part = Part};
        }
    }
    return {};
}

void PoseEditor::drawPanels() {
    drawClipPanel();
    drawTimelinePanel();
    drawKeyPanel();
    drawFactPanel();
    // Last: opening something else replaces the state the panels above drew.
    drawOpenPanel();
    drawUnsavedPopup();
}

void PoseEditor::drawClipPanel() {
    ImGui::SetNextWindowPos({8.0f, 8.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({362.0f, ImGui::GetIO().DisplaySize.y - 350.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Clip");
    const bool Swapped = editor::isPlayedOtherHand(Edit, Context.WeaponHand);
    ImGui::Text("%s%s", ClipName.c_str(), Edited ? "  (unsaved)" : "");
    ImGui::Text("item: %s", Context.ItemName.empty() ? "(bare hands)" : Context.ItemName.c_str());
    ImGui::Text("stance: %s (moveset %s)", Context.StanceName.c_str(), Context.MoveSetId.c_str());
    if (!Users.isEmpty()) {
        ImGui::TextWrapped("%s", describeStanceUsers(ClipName, Users).c_str());
    }
    if (ClipName == Context.StanceName) {
        ImGui::TextDisabled("the ghost's base stance is this clip:\nthe ghost follows the edits");
    }
    if (Swapped) {
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
    if (!ClipHadLegs && editor::isLegClip(Edit)) {
        ImGui::TextColored({1.0f, 0.8f, 0.3f, 1.0f},
                           "legs are keyed in a clip that had none: it\n"
                           "becomes a leg action, the game poses the legs\n"
                           "from it, not by the walk cycle (TUNING 3b)");
    }
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
        for (const BodyPart Part : getPanelOrder(Context.Rig.Weapon.Part, Swapped)) {
            bool Keyed = editor::isJointKeyed(Edit, Part);
            if (ImGui::Checkbox(getJointLabel(Part, Context.Rig.Weapon.Part, Swapped).c_str(), &Keyed)) {
                // A new joint starts where the ghost has it now.
                const anim::Pose Now = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
                editor::setJointKeyed(Edit, Part, Keyed, Keyed ? Now.getAngle(Part) : 0.0f);
                markEdited();
            }
            if (isLegPart(Part) && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("a clip with leg keys is a leg action (TUNING 3b);\nan idle stance keys all six");
            }
        }
        const bool AllLegs = std::ranges::all_of(
            std::array{BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR, BodyPart::ShinR, BodyPart::FootR},
            [&](BodyPart Leg) { return editor::isJointKeyed(Edit, Leg); });
        if (ImGui::Button(AllLegs ? "unkey all legs" : "key all legs")) {
            const anim::Pose Now = editor::composeGhostPose(Context.Stance, Edit, TimeSec, Context.WeaponHand);
            editor::setLegsKeyed(Edit, !AllLegs, Now, Context.Rig);
            markEdited();
        }
        bool Wrist = Edit.Keys.front().Target.HasWeapon;
        if (ImGui::Checkbox("weapon arm: wrist", &Wrist)) {
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
    // The readout is in clip time; the speed only slows the preview down.
    ImGui::Text("%s", anim::describePlayback(Edit, TimeSec, 1.0f, anim::PoseTransition{}).c_str());
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("speed ([ ])", &PlaySpeed, MinPlaySpeed, MaxPlaySpeed, "%.2fx", ImGuiSliderFlags_Logarithmic);
    for (const float Preset : PlaySpeedPresets) {
        ImGui::SameLine();
        if (ImGui::Button(std::format("{:g}x", Preset).c_str())) PlaySpeed = Preset;
    }
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
    ImGui::TextDisabled("arms by role; legs: thigh, shin, foot");
    ImGui::TextDisabled("mouse: drag a part to turn its joint\n(a leg part too, no leg IK);\ndrag a circle: a fist (arm IK)\nor the blade tip (wrist)");
    // An elbow that bends one way only (the humanoid) leaves nothing to choose.
    if (canBendElbowsBothWays(Context.Rig)) {
        bool ElbowUp = Bend == editor::ElbowBend::Up;
        if (ImGui::Checkbox("IK: elbow up (E)", &ElbowUp)) Bend = ElbowUp ? editor::ElbowBend::Up : editor::ElbowBend::Down;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("the bend the arm IK prefers; a bend past the elbow limits\n"
                              "falls back to the other one");
        }
    }
    if (editor::isPlayedOtherHand(Edit, Context.WeaponHand)) ImGui::TextDisabled("(the file stores them on the other side)");
    ImGui::Separator();
    const BodyPart MainHand = Context.Rig.Weapon.Part;
    const bool Swapped = editor::isPlayedOtherHand(Edit, Context.WeaponHand);
    for (const BodyPart Part : getPanelOrder(MainHand, Swapped)) {
        if (!Key.Target.hasJoint(Part)) continue;
        const editor::AngleRange Range = editor::getJointRange(Context.Rig, Part);
        float Angle = Key.Target.getAngle(Part);
        const bool Outside = Angle < Range.Lower - 1e-4f || Angle > Range.Upper + 1e-4f;
        const std::string Label = getJointLabel(Part, MainHand, Swapped);
        if (Outside) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::SetNextItemWidth(120.0f);
        if (sliderAngle(Label.c_str(), Angle, Range)) {
            editor::setJointAngle(Edit, SelectedKey, Part, Angle, Context.Rig);
            markEdited();
        }
        if (Outside) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) Highlight = editor::getGhostPart(Part, Swapped);
        // The wrist belongs to the weapon arm: right after its forearm.
        if (Part == editor::getRoleJoint(editor::ArmRole::Weapon, editor::ArmSegment::Forearm, MainHand, Swapped) &&
            Key.Target.HasWeapon) {
            const editor::AngleRange Wrist = editor::getWristRange(Context.Rig);
            const float Limit = std::numbers::pi_v<float>;   // the file may not write more
            const editor::AngleRange WristRange = {.Lower = std::max(Wrist.Lower, -Limit),
                                                   .Upper = std::min(Wrist.Upper, Limit)};
            float WristAngle = Key.Target.WeaponAngle;
            ImGui::SetNextItemWidth(120.0f);
            if (sliderAngle("weapon arm: wrist", WristAngle, WristRange)) {
                Key.Target.setWeaponAngle(WristAngle);
                markEdited();
            }
        }
    }
    ImGui::End();
}

void PoseEditor::drawOpenPanel() {
    const ImVec2 Screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({Screen.x - 298.0f, 8.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({290.0f, 400.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Open");
    // The choice is made after the lists are drawn: opening replaces them.
    std::optional<Selection> Chosen;
    // A fighter: its weapon in the hands and the stance of its moveset.
    const auto Current = std::ranges::find_if(Fighters, [&](const editor::FighterEntry& Fighter) {
        return Fighter.StanceClip == ClipName && Fighter.HandItems == HandItems;
    });
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::BeginCombo("fighter", Current == Fighters.end() ? "(choose)" : Current->Name.c_str())) {
        for (const editor::FighterEntry& Fighter : Fighters) {
            std::string Items;
            for (const stats::ItemRef& Item : Fighter.HandItems) Items += (Items.empty() ? "" : " + ") + Item.Id;
            const std::string Label = std::format("{}: {} -> {} -> {}", Fighter.Name, Items.empty() ? "bare hands" : Items,
                                                  Fighter.MoveSetId, Fighter.StanceClip);
            if (ImGui::Selectable(Label.c_str(), Current != Fighters.end() && Current->File == Fighter.File)) {
                Chosen = Selection{.Clip = Fighter.StanceClip, .Items = Fighter.HandItems, .Move = {}};
            }
        }
        ImGui::EndCombo();
    }
    // An item: what the hands hold, the clip stays.
    std::string ItemLabel = "(bare hands)";
    if (!HandItems.empty()) {
        ItemLabel.clear();
        for (const stats::ItemRef& Item : HandItems) ItemLabel += (ItemLabel.empty() ? "" : " + ") + Item.Id;
    }
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::BeginCombo("item", ItemLabel.c_str())) {
        if (ImGui::Selectable("(bare hands)", HandItems.empty())) Chosen = Selection{.Clip = ClipName, .Items = {}, .Move = MoveId};
        for (const editor::HandItemEntry& Item : HandItemList) {
            const std::string Label = std::format("{} ({}){}{}", Item.Name, Item.Id, Item.Shield ? " shield" : "",
                                                  Item.TwoHanded ? " 2h" : "");
            const bool Selected = HandItems.size() == 1 && HandItems.front().Id == Item.Id;
            if (ImGui::Selectable(Label.c_str(), Selected)) {
                Chosen = Selection{.Clip = ClipName, .Items = {{.Id = Item.Id, .Slot = std::nullopt}}, .Move = MoveId};
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("open the stance of these items")) {
        Chosen = Selection{.Clip = Context.StanceName, .Items = HandItems, .Move = {}};
    }
    ImGui::Separator();
    // A clip: a searchable list of data/poses.
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputTextWithHint("clip", "type to filter", ClipFilter.data(), ClipFilter.size());
    const std::string Filter = toLower(ClipFilter.data());
    if (ImGui::BeginListBox("##clips", {-FLT_MIN, 12.0f * ImGui::GetTextLineHeightWithSpacing()})) {
        for (const std::string& Clip : ClipList) {
            if (!Filter.empty() && !toLower(Clip).contains(Filter)) continue;
            if (ImGui::Selectable(Clip.c_str(), Clip == ClipName)) Chosen = Selection{.Clip = Clip, .Items = HandItems, .Move = {}};
        }
        ImGui::EndListBox();
    }
    ImGui::End();
    if (Chosen) requestOpen(std::move(*Chosen));
}

void PoseEditor::drawUnsavedPopup() {
    if (AskUnsaved) {
        ImGui::OpenPopup("Unsaved changes");
        AskUnsaved = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%s has unsaved changes.", ClipName.c_str());
    if (Pending) ImGui::Text("Open %s?", Pending->Clip.c_str());
    const std::string Problem = editor::findClipProblem(Edit);
    if (!Problem.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "cannot save: %s", Problem.c_str());
    ImGui::BeginDisabled(!Problem.empty());
    const bool SaveIt = ImGui::Button("Save");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool Discard = ImGui::Button("Discard");
    ImGui::SameLine();
    const bool Cancel = ImGui::Button("Cancel");
    if (SaveIt || Discard || Cancel) {
        ImGui::CloseCurrentPopup();
        const std::optional<Selection> Choice = std::exchange(Pending, std::nullopt);
        if (Choice && !Cancel && (Discard || save())) {
            Edited = false;
            open(*Choice);
        }
    }
    ImGui::EndPopup();
}

void PoseEditor::drawFactPanel() {
    const ImVec2 Screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({Screen.x - 378.0f, Screen.y - 298.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({370.0f, 290.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Fact");
    ImGui::Checkbox("ghost", &ShowGhost);
    ImGui::SameLine();
    ImGui::Checkbox("fact", &ShowFact);
    ImGui::SameLine();
    bool WithDummy = Fact->isWithDummy();
    if (ImGui::Checkbox("dummy", &WithDummy)) {
        Fact->setWithDummy(WithDummy);
        FactStep.reset();
    }
    if (ImGui::Button("< step (,)")) stepFact(-1);
    ImGui::SameLine();
    if (ImGui::Button("step > (.)")) stepFact(1);
    ImGui::SameLine();
    ImGui::TextDisabled("Space: play/pause");
    if (!MoveList.empty()) {
        const auto describeMove = [](const editor::MoveCandidate& Move) {
            return Move.Close ? std::format("{} (close clip)", Move.MoveId) : Move.MoveId;
        };
        std::string Shown = "(the first that can start)";
        for (const editor::MoveCandidate& Move : MoveList) {
            if (Move.MoveId == MoveId) Shown = describeMove(Move);
        }
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("move", Shown.c_str())) {
            if (ImGui::Selectable("(the first that can start)", MoveId.empty())) {
                MoveId.clear();
                Fact->setMove(MoveId);
                FactStep.reset();
            }
            for (const editor::MoveCandidate& Move : MoveList) {
                if (ImGui::Selectable(describeMove(Move).c_str(), Move.MoveId == MoveId)) {
                    MoveId = Move.MoveId;
                    Fact->setMove(MoveId);
                    FactStep.reset();
                }
            }
            ImGui::EndCombo();
        }
    }
    if (const combat::StandSetup* Setup = Fact->getSetup()) {
        ImGui::Text("move %s (%s), %s", Setup->MoveId.c_str(), Setup->InputText.c_str(),
                    Setup->WeaponId.empty() ? "bare hands" : Setup->WeaponName.c_str());
    }
    if (!Fact->getProblem().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.4f, 0.4f, 1.0f});
        ImGui::TextWrapped("%s", Fact->getProblem().c_str());
        ImGui::PopStyleColor();
    }
    const std::optional<size_t> Index = FactStep ? FactStep : Fact->findFrame(TimeSec);
    const editor::FactFrame* Frame = Index ? Fact->getFrame(*Index) : nullptr;
    if (!Frame) {
        ImGui::End();
        return;
    }
    ImGui::Text("step %zu of %zu%s, clip %.3f s, %s", *Index, Fact->getFrameCount(), Fact->isComplete() ? "" : "+",
                Frame->ClipSec, getAttackPhaseLabel(Frame->Phase));
    const editor::FactFrame* First = Fact->getFrame(0);
    if (const editor::FactFrame* Second = Fact->getFrame(1); First && Second) {
        const double Rate = (Second->ClipSec - First->ClipSec) / editor::PhysicsPreview::StepSec;
        ImGui::TextDisabled("the clip runs at x%.2f of real time (speed, weapon)", Rate);
    }
    // The target at the frame's own clip time.
    const anim::Pose Ghost = editor::composeGhostPose(Context.Stance, Edit, Frame->ClipSec, Context.WeaponHand);
    const editor::PosedBody Target = editor::poseBody(Context.Rig, Ghost, Context.Held);
    const std::vector<editor::JointError> Errors = editor::compareToTarget(Context.Rig, Target, Frame->Attacker);
    const bool Swapped = editor::isPlayedOtherHand(Edit, Context.WeaponHand);
    if (const editor::JointError* Worst = editor::findWorstJoint(Errors)) {
        ImGui::Text("worst joint: %s %+.1f deg", getJointLabel(Worst->Part, Context.Rig.Weapon.Part, Swapped).c_str(),
                    Worst->ErrorRad * DegreesPerRadian);
    }
    if (ImGui::CollapsingHeader("joints: fact - target")) {
        for (const editor::JointError& Error : Errors) {
            ImGui::Text("%-24s %+7.1f deg", getJointLabel(Error.Part, Context.Rig.Weapon.Part, Swapped).c_str(),
                        Error.ErrorRad * DegreesPerRadian);
        }
    }
    ImGui::End();
}

namespace {

void drawFact(sf::RenderTarget& Target, const View& Scene, const rig::RigDef& Rig, const editor::FactFrame& Frame,
              float ShiftX, bool WithDummy) {
    if (WithDummy) {
        for (const PartTransform& Part : Frame.Dummy.Parts) drawTransform(Target, Scene, Part, ShiftX, DummyFill, FactLine);
        for (const PartTransform& Weapon : Frame.Dummy.Weapons) {
            drawTransform(Target, Scene, Weapon, ShiftX, DummyFill, FactLine);
        }
    }
    const std::vector<PartTransform>& Parts = Frame.Attacker.Parts;
    if (Parts.size() < BodyPartCount) return;
    for (const BodyPart Part : DrawOrder) {
        const PartTransform& Placed = Parts[static_cast<size_t>(Part)];
        drawPart(Target, Scene, {.Position = Placed.Position, .Angle = Placed.Angle}, Rig.getPart(Part), ShiftX, FactFill,
                 FactLine);
        for (const PartTransform& Weapon : Frame.Attacker.Weapons) {
            if (Weapon.Part == Part) drawTransform(Target, Scene, Weapon, ShiftX, FactFill, FactLine);
        }
    }
}

const char* getAttackPhaseLabel(combat::AttackPhase Phase) {
    switch (Phase) {
        case combat::AttackPhase::Startup: return "startup";
        case combat::AttackPhase::Active: return "active";
        case combat::AttackPhase::Recovery: return "recovery";
        case combat::AttackPhase::None: break;
    }
    return "no attack";
}

void drawTransform(sf::RenderTarget& Target, const View& Scene, const PartTransform& Part, float ShiftX, sf::Color Fill,
                   sf::Color Line) {
    const float Radius = Part.Size.X * 0.5f;
    const Vec2 Half = rotate({0.0f, 1.0f}, Part.Angle) * std::max(Part.Size.Y * 0.5f - Radius, 0.0f);
    const Vec2 Center = Part.Position + Vec2{ShiftX, 0.0f};
    drawCapsule(Target, Scene, Center - Half, Center + Half, Radius, Fill, Line);
}

bool isLegPart(BodyPart Part) { return anim::getLayer(Part) == anim::Layer::Legs; }

std::string joinNames(const std::vector<std::string>& Names) {
    std::string Joined;
    for (const std::string& Name : Names) Joined += (Joined.empty() ? "" : ", ") + Name;
    return Joined;
}

std::string describeStanceUsers(const std::string& Clip, const editor::StanceUsers& Users) {
    std::string Text = std::format("{} - used by: ", Clip);
    Text += Users.Fighters.empty() ? "no fighter" : joinNames(Users.Fighters);
    if (!Users.MoveSets.empty()) Text += std::format(" (moveset {})", joinNames(Users.MoveSets));
    return Text;
}

std::string toLower(std::string Text) {
    std::ranges::transform(Text, Text.begin(), [](unsigned char Char) { return static_cast<char>(std::tolower(Char)); });
    return Text;
}

std::array<BodyPart, BodyPartCount> getPanelOrder(BodyPart MainHand, bool PlayedOtherHand) {
    using editor::ArmRole;
    using editor::ArmSegment;
    return {BodyPart::Pelvis,
            BodyPart::Torso,
            BodyPart::Head,
            editor::getRoleJoint(ArmRole::Weapon, ArmSegment::Upper, MainHand, PlayedOtherHand),
            editor::getRoleJoint(ArmRole::Weapon, ArmSegment::Forearm, MainHand, PlayedOtherHand),
            editor::getRoleJoint(ArmRole::Other, ArmSegment::Upper, MainHand, PlayedOtherHand),
            editor::getRoleJoint(ArmRole::Other, ArmSegment::Forearm, MainHand, PlayedOtherHand),
            BodyPart::ThighL,
            BodyPart::ShinL,
            BodyPart::FootL,
            BodyPart::ThighR,
            BodyPart::ShinR,
            BodyPart::FootR};
}

std::string getJointLabel(BodyPart Joint, BodyPart MainHand, bool PlayedOtherHand) {
    editor::ArmSegment Segment = editor::ArmSegment::Upper;
    const auto Role = editor::getJointRole(Joint, MainHand, PlayedOtherHand, &Segment);
    if (!Role) return std::string(getBodyPartName(Joint));
    return std::format("{} arm: {}", *Role == editor::ArmRole::Weapon ? "weapon" : "other",
                       Segment == editor::ArmSegment::Upper ? "upper arm" : "forearm");
}

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
                            "%.1f deg", ImGuiSliderFlags_AlwaysClamp)) {
        return false;
    }
    Radians = Degrees * RadiansPerDegree;
    return true;
}

bool dragSeconds(const char* Label, float& Seconds, float Speed) {
    return ImGui::DragFloat(Label, &Seconds, Speed, 0.0f, 10.0f, "%.3f");
}

/// True if both elbows of \p Rig bend either way, so the arm IK has a bend to choose.
bool canBendElbowsBothWays(const rig::RigDef& Rig) {
    for (const BodyPart Forearm : {BodyPart::ForearmL, BodyPart::ForearmR}) {
        const editor::AngleRange Range = editor::getJointRange(Rig, Forearm);
        if (Range.Lower >= 0.0f || Range.Upper <= 0.0f) return false;
    }
    return true;
}

} // namespace

} // namespace fighter::app
