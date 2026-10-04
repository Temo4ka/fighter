#include "combat/moves.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::combat {
namespace {

using Json = nlohmann::json;

void checkFieldNames(const Json& Root, std::initializer_list<std::string_view> Allowed);
const Json& getField(const Json& Root, std::string_view Key);
float readNonNegative(const Json& Root, std::string_view Key);
std::optional<MoveButton> findMoveButton(std::string_view Name);

} // namespace

const MoveDef* findMove(std::span<const MoveDef> Moves, MoveButton Button, std::string_view WeaponClass) {
    const MoveDef* Unarmed = nullptr;
    for (const MoveDef& Move : Moves) {
        if (Move.Button != Button) continue;
        if (!WeaponClass.empty() && Move.Weapon == WeaponClass) return &Move;
        if (Move.Weapon.empty() && !Unarmed) Unarmed = &Move;
    }
    return Unarmed;
}

bool MoveDef::canChainTo(MoveButton Next) const { return std::ranges::find(ChainTo, Next) != ChainTo.end(); }

MoveDef parseMoveDef(std::string_view JsonText, std::string Id) {
    MoveDef Move;
    Move.Id = std::move(Id);
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        checkFieldNames(Root, {"button", "clip", "weapon", "damage", "min_reaction", "stamina",
                               "close_clip", "close_range_m", "chain_to"});

        const auto ButtonName = getField(Root, "button").get<std::string>();
        const std::optional<MoveButton> Button = findMoveButton(ButtonName);
        if (!Button) {
            throw std::runtime_error(
                std::format("field 'button': unknown button '{}' (expected Jab, HeavyPunch, BodyKick or LowKick)",
                            ButtonName));
        }
        Move.Button = *Button;

        Move.Clip = getField(Root, "clip").get<std::string>();
        if (Move.Clip.empty()) throw std::runtime_error("field 'clip': must not be empty");
        if (Root.contains("weapon")) {
            Move.Weapon = Root.at("weapon").get<std::string>();
            if (Move.Weapon.empty()) throw std::runtime_error("field 'weapon': must not be empty (omit it instead)");
        }

        Move.Damage = readNonNegative(Root, "damage");
        if (Root.contains("min_reaction")) {
            const auto LevelName = Root.at("min_reaction").get<std::string>();
            const std::optional<ReactionLevel> Level = findReactionLevel(LevelName);
            // A move that knocks down on any touch would make the reaction
            // thresholds meaningless.
            if (!Level || *Level == ReactionLevel::Knockdown) {
                throw std::runtime_error(std::format(
                    "field 'min_reaction': '{}' is not one of None, Touch, Flinch, Stagger, Knockback", LevelName));
            }
            Move.MinReaction = *Level;
        }
        Move.Stamina = readNonNegative(Root, "stamina");

        // The close-range clip and its range come together.
        if (Root.contains("close_clip") != Root.contains("close_range_m")) {
            throw std::runtime_error("fields 'close_clip' and 'close_range_m' must be given together");
        }
        if (Root.contains("close_clip")) {
            Move.CloseClip = Root.at("close_clip").get<std::string>();
            if (Move.CloseClip.empty()) throw std::runtime_error("field 'close_clip': must not be empty");
            Move.CloseRangeM = readNonNegative(Root, "close_range_m");
        }
        if (Root.contains("chain_to")) {
            const Json& Buttons = Root.at("chain_to");
            if (!Buttons.is_array()) throw std::runtime_error("field 'chain_to': must be a list of buttons");
            for (const auto& Entry : Buttons) {
                const auto Name = Entry.get<std::string>();
                const std::optional<MoveButton> Next = findMoveButton(Name);
                if (!Next) {
                    throw std::runtime_error(std::format(
                        "field 'chain_to': unknown button '{}' (expected Jab, HeavyPunch, BodyKick or LowKick)", Name));
                }
                if (Move.canChainTo(*Next)) {
                    throw std::runtime_error(std::format("field 'chain_to': button '{}' is listed twice", Name));
                }
                Move.ChainTo.push_back(*Next);
            }
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Move;
}

std::vector<MoveDef> loadMoveSet(const std::filesystem::path& Dir) {
    std::error_code Error;
    std::vector<std::filesystem::path> Files;
    for (const auto& Entry : std::filesystem::directory_iterator(Dir, Error)) {
        if (Entry.is_regular_file() && Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    if (Error) throw std::runtime_error(std::format("{}: cannot list the directory: {}", Dir.string(), Error.message()));
    // directory_iterator has no fixed order; sort so the set and the errors are reproducible.
    std::ranges::sort(Files);

    std::vector<MoveDef> Moves;
    for (const auto& File : Files) {
        try {
            MoveDef Move = parseMoveDef(readTextFile(File), File.stem().string());
            const auto Clash = std::ranges::find_if(Moves, [&](const MoveDef& Other) {
                return Other.Button == Move.Button && Other.Weapon == Move.Weapon;
            });
            if (Clash != Moves.end()) {
                throw std::runtime_error(std::format("button {} is already taken by '{}'{}",
                                                     getMoveButtonName(Move.Button), Clash->Id,
                                                     Move.Weapon.empty() ? "" : " for weapon " + Move.Weapon));
            }
            Moves.push_back(std::move(Move));
        } catch (const std::runtime_error& Failure) {
            throw std::runtime_error(std::format("{}: {}", File.string(), Failure.what()));
        }
    }
    return Moves;
}

namespace {

void checkFieldNames(const Json& Root, std::initializer_list<std::string_view> Allowed) {
    for (const auto& Field : Root.items()) {
        if (std::ranges::find(Allowed, Field.key()) == Allowed.end()) {
            throw std::runtime_error(std::format("unknown field '{}'", Field.key()));
        }
    }
}

const Json& getField(const Json& Root, std::string_view Key) {
    const auto Found = Root.find(Key);
    if (Found == Root.end()) throw std::runtime_error(std::format("missing field '{}'", Key));
    return *Found;
}

float readNonNegative(const Json& Root, std::string_view Key) {
    const auto Value = getField(Root, Key).get<float>();
    if (Value < 0.0f) throw std::runtime_error(std::format("field '{}': {} must not be negative", Key, Value));
    return Value;
}

std::optional<MoveButton> findMoveButton(std::string_view Name) {
    for (size_t Index = 0; Index < MoveButtonCount; ++Index) {
        const auto Button = static_cast<MoveButton>(Index);
        if (getMoveButtonName(Button) == Name) return Button;
    }
    return std::nullopt;
}

} // namespace

} // namespace fighter::combat
