#include "combat/moves.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <ranges>
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
std::vector<std::string> readNames(const Json& Root, std::string_view Key);
MoveIntent readIntent(const Json& Node);

} // namespace

bool MoveDef::canChainTo(std::string_view NextId) const { return std::ranges::find(ChainTo, NextId) != ChainTo.end(); }

bool MoveDef::hasTag(std::string_view Tag) const { return std::ranges::find(Tags, Tag) != Tags.end(); }

std::string_view MoveDef::getHeight() const {
    for (const std::string_view Height : HeightTags) {
        if (hasTag(Height)) return Height;
    }
    return {};
}

const MoveDef* findMoveById(std::span<const MoveDef> Moves, std::string_view Id) {
    const auto Found = std::ranges::find(Moves, Id, &MoveDef::Id);
    return Found != Moves.end() ? &*Found : nullptr;
}

MoveDef parseMoveDef(std::string_view JsonText, std::string Id) {
    MoveDef Move;
    Move.Id = std::move(Id);
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        checkFieldNames(Root, {"clip", "uses_weapon", "damage", "min_reaction", "stamina", "close_clip",
                               "close_range_m", "chain_to", "tags", "ai"});

        Move.Clip = getField(Root, "clip").get<std::string>();
        if (Move.Clip.empty()) throw std::runtime_error("field 'clip': must not be empty");
        if (Root.contains("uses_weapon")) Move.UsesWeapon = Root.at("uses_weapon").get<bool>();

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
        Move.ChainTo = readNames(Root, "chain_to");
        Move.Tags = readNames(Root, "tags");
        const auto Heights = std::ranges::count_if(Move.Tags, [](const std::string& Tag) {
            return std::ranges::find(HeightTags, Tag) != HeightTags.end();
        });
        if (Heights > 1) throw std::runtime_error("field 'tags': more than one height (high, mid, low)");
        if (Root.contains("ai")) Move.Intent = readIntent(Root.at("ai"));
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Move;
}

std::vector<MoveDef> loadMoves(const std::filesystem::path& Dir) {
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
            Moves.push_back(parseMoveDef(readTextFile(File), File.stem().string()));
        } catch (const std::runtime_error& Failure) {
            throw std::runtime_error(std::format("{}: {}", File.string(), Failure.what()));
        }
    }
    for (const auto& [Move, File] : std::views::zip(Moves, Files)) {
        for (const std::string& Next : Move.ChainTo) {
            if (!findMoveById(Moves, Next)) {
                throw std::runtime_error(
                    std::format("{}: field 'chain_to': there is no move '{}' in {}", File.string(), Next, Dir.string()));
            }
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

std::vector<std::string> readNames(const Json& Root, std::string_view Key) {
    std::vector<std::string> Names;
    const auto Found = Root.find(Key);
    if (Found == Root.end()) return Names;
    if (!Found->is_array()) throw std::runtime_error(std::format("field '{}': must be a list of names", Key));
    for (const Json& Entry : *Found) {
        auto Name = Entry.get<std::string>();
        if (Name.empty()) throw std::runtime_error(std::format("field '{}': a name must not be empty", Key));
        if (std::ranges::find(Names, Name) != Names.end()) {
            throw std::runtime_error(std::format("field '{}': '{}' is listed twice", Key, Name));
        }
        Names.push_back(std::move(Name));
    }
    return Names;
}

MoveIntent readIntent(const Json& Node) {
    if (!Node.is_object()) throw std::runtime_error("field 'ai': must be an object");
    for (const auto& Field : Node.items()) {
        if (Field.key() != "range_m" && Field.key() != "role" && Field.key() != "weight") {
            throw std::runtime_error(std::format("field 'ai.{}': unknown field", Field.key()));
        }
    }
    MoveIntent Intent;
    if (const auto Range = Node.find("range_m"); Range != Node.end()) {
        if (!Range->is_array() || Range->size() != 2) {
            throw std::runtime_error("field 'ai.range_m': must be [min, max]");
        }
        Intent.MinRangeM = (*Range)[0].get<float>();
        Intent.MaxRangeM = (*Range)[1].get<float>();
        if (Intent.MinRangeM < 0.0f || Intent.MaxRangeM < Intent.MinRangeM) {
            throw std::runtime_error(
                std::format("field 'ai.range_m': [{}, {}] must have 0 <= min <= max", Intent.MinRangeM, Intent.MaxRangeM));
        }
    }
    if (const auto Role = Node.find("role"); Role != Node.end()) Intent.Role = Role->get<std::string>();
    if (const auto Weight = Node.find("weight"); Weight != Node.end()) {
        Intent.Weight = Weight->get<float>();
        if (Intent.Weight < 0.0f) throw std::runtime_error(std::format("field 'ai.weight': {} must not be negative", Intent.Weight));
    }
    return Intent;
}

} // namespace

} // namespace fighter::combat
