#include "combat/move_input.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::combat {
namespace {

using Json = nlohmann::json;

std::string_view trim(std::string_view Text);
InputDirection readDirection(const Json& Node, std::string_view Field);

} // namespace

std::vector<InputDirection> InputRules::getTryOrder(InputDirection Direction) const {
    std::vector<InputDirection> Order = {Direction};
    const auto Add = [&](InputDirection Next) {
        if (std::ranges::find(Order, Next) == Order.end()) Order.push_back(Next);
    };
    for (const InputDirection Next : Fallbacks[static_cast<size_t>(Direction)]) Add(Next);
    Add(InputDirection::Neutral);
    return Order;
}

InputRules InputRules::getDefaults() {
    InputRules Rules;
    const auto Set = [&](InputDirection From, std::vector<InputDirection> To) {
        Rules.Fallbacks[static_cast<size_t>(From)] = std::move(To);
    };
    Set(InputDirection::UpForward, {InputDirection::Up});
    Set(InputDirection::UpBack, {InputDirection::Up});
    Set(InputDirection::DownForward, {InputDirection::Down});
    Set(InputDirection::DownBack, {InputDirection::Down});
    Rules.ComboWindowSec = 0.05f;
    return Rules;
}

std::optional<AttackButton> findAttackButton(std::string_view Name) {
    for (const AttackButton Button : AttackButtons) {
        if (getAttackButtonName(Button) == Name) return Button;
    }
    return std::nullopt;
}

std::optional<InputDirection> findInputDirection(std::string_view Name) {
    for (size_t Index = 0; Index < InputDirectionCount; ++Index) {
        const auto Direction = static_cast<InputDirection>(Index);
        if (getInputDirectionName(Direction) == Name) return Direction;
    }
    return std::nullopt;
}

MoveInput parseMoveInput(std::string_view Text) {
    MoveInput Input;
    bool HasDirection = false;
    std::string_view Rest = Text;
    while (true) {
        const size_t Plus = Rest.find('+');
        const std::string_view Part = trim(Rest.substr(0, Plus));
        if (Part.empty()) throw std::runtime_error(std::format("input '{}': an empty part", Text));
        if (const std::optional<AttackButton> Button = findAttackButton(Part)) {
            if (Input.Buttons.contains(*Button)) {
                throw std::runtime_error(std::format("input '{}': button {} is named twice", Text, Part));
            }
            Input.Buttons.add(*Button);
        } else if (const std::optional<InputDirection> Direction = findInputDirection(Part)) {
            if (HasDirection) throw std::runtime_error(std::format("input '{}': more than one direction", Text));
            Input.Direction = *Direction;
            HasDirection = true;
        } else {
            throw std::runtime_error(std::format(
                "input '{}': '{}' is neither a button (Light, Heavy, Kick, Special) nor a direction "
                "(Neutral, Forward, Back, Up, Down, UpForward, UpBack, DownForward, DownBack)",
                Text, Part));
        }
        if (Plus == std::string_view::npos) break;
        Rest.remove_prefix(Plus + 1);
    }
    if (Input.Buttons.isEmpty()) throw std::runtime_error(std::format("input '{}': no button", Text));
    return Input;
}

std::string formatMoveInput(const MoveInput& Input) {
    std::string Text;
    if (Input.Direction != InputDirection::Neutral) Text = getInputDirectionName(Input.Direction);
    for (const AttackButton Button : AttackButtons) {
        if (!Input.Buttons.contains(Button)) continue;
        if (!Text.empty()) Text += '+';
        Text += getAttackButtonName(Button);
    }
    return Text;
}

InputRules parseInputRules(std::string_view JsonText) {
    InputRules Rules;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& Field : Root.items()) {
            if (Field.key() != "fallbacks" && Field.key() != "combo_window_sec") {
                throw std::runtime_error(std::format("unknown field '{}'", Field.key()));
            }
        }
        if (const auto Window = Root.find("combo_window_sec"); Window != Root.end()) {
            Rules.ComboWindowSec = Window->get<float>();
            if (Rules.ComboWindowSec < 0.0f || Rules.ComboWindowSec > 0.5f) {
                throw std::runtime_error(
                    std::format("field 'combo_window_sec': {} is outside 0...0.5", Rules.ComboWindowSec));
            }
        }
        if (const auto Fallbacks = Root.find("fallbacks"); Fallbacks != Root.end()) {
            if (!Fallbacks->is_object()) throw std::runtime_error("field 'fallbacks': must be an object");
            for (const auto& Entry : Fallbacks->items()) {
                const std::string Field = "fallbacks." + Entry.key();
                const std::optional<InputDirection> From = findInputDirection(Entry.key());
                if (!From) throw std::runtime_error(std::format("field '{}': unknown direction", Field));
                if (!Entry.value().is_array()) {
                    throw std::runtime_error(std::format("field '{}': must be a list of directions", Field));
                }
                auto& To = Rules.Fallbacks[static_cast<size_t>(*From)];
                for (const Json& Next : Entry.value()) {
                    const InputDirection Direction = readDirection(Next, Field);
                    if (Direction == *From || std::ranges::find(To, Direction) != To.end()) {
                        throw std::runtime_error(std::format("field '{}': direction {} is repeated", Field,
                                                             getInputDirectionName(Direction)));
                    }
                    To.push_back(Direction);
                }
            }
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Rules;
}

void PressWindow::update(ButtonSet Pressed, float WindowSec, float Dt) {
    for (auto&& [Age, Button] : std::views::zip(AgeSec, AttackButtons)) {
        if (Age >= 0.0f) Age += Dt;
        if (Age > WindowSec) Age = -1.0f;
        if (Pressed.contains(Button)) Age = 0.0f;
    }
}

void PressWindow::clear() { AgeSec.fill(-1.0f); }

ButtonSet PressWindow::getButtons() const {
    ButtonSet Buttons;
    for (auto&& [Age, Button] : std::views::zip(AgeSec, AttackButtons)) {
        if (Age >= 0.0f) Buttons.add(Button);
    }
    return Buttons;
}

float PressWindow::getAgeSec() const { return std::max(0.0f, std::ranges::max(AgeSec)); }

InputRules loadInputRules(const std::filesystem::path& Path) {
    try {
        return parseInputRules(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

namespace {

std::string_view trim(std::string_view Text) {
    const size_t First = Text.find_first_not_of(' ');
    if (First == std::string_view::npos) return {};
    const size_t Last = Text.find_last_not_of(' ');
    return Text.substr(First, Last - First + 1);
}

InputDirection readDirection(const Json& Node, std::string_view Field) {
    const auto Name = Node.get<std::string>();
    const std::optional<InputDirection> Direction = findInputDirection(Name);
    if (!Direction) throw std::runtime_error(std::format("field '{}': unknown direction '{}'", Field, Name));
    return *Direction;
}

} // namespace

} // namespace fighter::combat
