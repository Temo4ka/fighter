#include "combat/moveset.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::combat {
namespace {

using Json = nlohmann::json;

constexpr std::array<std::string_view, BlockZoneCount> ZoneNames = {"High", "Mid", "Low"};

BlockDef readBlock(const Json& Node);
std::optional<BlockZone> findBlockZone(std::string_view Name);
std::string readId(const Json& Node, std::string_view Field);

} // namespace

bool BlockRules::covers(BlockZone Zone, BodyPart Part) const {
    const auto& Parts = Covers[static_cast<size_t>(Zone)];
    return std::ranges::find(Parts, Part) != Parts.end();
}

MoveLibrary MoveLibrary::load(const std::filesystem::path& DataDir) {
    return build(loadMoves(DataDir / "moves"), loadMoveSets(DataDir / "movesets"),
                 loadInputRules(DataDir / "input.json"));
}

MoveLibrary MoveLibrary::build(std::vector<MoveDef> NewMoves, std::vector<MoveSet> NewSets, InputRules NewInput) {
    MoveLibrary Library;
    Library.Moves = std::move(NewMoves);
    Library.Sets = std::move(NewSets);
    Library.Input = std::move(NewInput);
    Library.validate();
    return Library;
}

const MoveSet* MoveLibrary::findSet(std::string_view Id) const {
    const auto Found = std::ranges::find(Sets, Id, &MoveSet::Id);
    return Found != Sets.end() ? &*Found : nullptr;
}

const MoveSet& MoveLibrary::selectSet(std::string_view MainSet, std::string_view OffSet) const {
    if (!MainSet.empty() && !OffSet.empty()) {
        for (const MoveSet& Set : Sets) {
            if (Set.Pair.size() == 2 && Set.Pair[0] == MainSet && Set.Pair[1] == OffSet) return Set;
        }
    }
    if (const MoveSet* Main = findSet(MainSet)) return *Main;
    return *findSet(UnarmedMoveSetId);
}

const MoveDef* MoveLibrary::findMove(const MoveSet& Set, InputDirection Direction, ButtonSet Pressed,
                                     ButtonSet Held) const {
    const MoveSetEntry* Entry = findEntry(Set, Direction, Pressed, Held);
    return Entry ? findMove(Entry->MoveId) : nullptr;
}

const MoveSetEntry* MoveLibrary::findEntry(const MoveSet& Set, InputDirection Direction, ButtonSet Pressed,
                                           ButtonSet Held) const {
    for (const InputDirection Tried : Input.getTryOrder(Direction)) {
        for (const MoveSet* Current = &Set; Current; Current = findSet(Current->Inherit)) {
            const MoveSetEntry* Best = nullptr;
            for (const MoveSetEntry& Entry : Current->Entries) {
                if (Entry.Input.Direction != Tried || !Held.containsAll(Entry.Input.Buttons) ||
                    !Entry.Input.Buttons.intersects(Pressed)) {
                    continue;
                }
                if (!Best || Entry.Input.Buttons.getSize() > Best->Input.Buttons.getSize()) Best = &Entry;
            }
            if (Best) return Best;
            if (Current->Inherit.empty()) break;
        }
    }
    return nullptr;
}

BlockRules MoveLibrary::getBlock(const MoveSet& Set, const BlockRules& Defaults) const {
    // The chain from the set to its root; the root's values go in first and
    // every child overrides what it gives.
    std::vector<const MoveSet*> Chain;
    for (const MoveSet* Current = &Set; Current; Current = findSet(Current->Inherit)) {
        Chain.push_back(Current);
        if (Current->Inherit.empty()) break;
    }
    BlockRules Rules = Defaults;
    for (const MoveSet* Current : std::views::reverse(Chain)) {
        const BlockDef& Block = Current->Block;
        if (Block.DamageScale) Rules.DamageScale = *Block.DamageScale;
        if (Block.MaxLevel) Rules.MaxLevel = *Block.MaxLevel;
        if (Block.StaminaScale) Rules.StaminaScale = *Block.StaminaScale;
        for (size_t Zone = 0; Zone < BlockZoneCount; ++Zone) {
            if (Block.Clips[Zone]) Rules.Clips[Zone] = *Block.Clips[Zone];
            if (Block.Covers[Zone]) Rules.Covers[Zone] = *Block.Covers[Zone];
        }
    }
    return Rules;
}

void MoveLibrary::validate() const {
    if (!findSet(UnarmedMoveSetId)) {
        throw std::runtime_error(std::format("there is no moveset '{}' (movesets/{}.json)", UnarmedMoveSetId,
                                             UnarmedMoveSetId));
    }
    for (const MoveSet& Set : Sets) {
        const auto Fail = [&](const std::string& What) {
            throw std::runtime_error(std::format("movesets/{}.json: {}", Set.Id, What));
        };
        for (const MoveSetEntry& Entry : Set.Entries) {
            if (!findMove(Entry.MoveId)) {
                Fail(std::format("input '{}': there is no move '{}'", formatMoveInput(Entry.Input), Entry.MoveId));
            }
        }
        // The parents must exist and must not come back to the set.
        std::vector<std::string_view> Seen = {Set.Id};
        for (std::string_view Parent = Set.Inherit; !Parent.empty();) {
            const MoveSet* Next = findSet(Parent);
            if (!Next) Fail(std::format("field 'inherit': there is no moveset '{}'", Parent));
            if (std::ranges::find(Seen, Parent) != Seen.end()) Fail("field 'inherit': the sets inherit in a circle");
            Seen.push_back(Parent);
            Parent = Next->Inherit;
        }
        for (const std::string& Member : Set.Pair) {
            const MoveSet* Item = findSet(Member);
            if (!Item) Fail(std::format("field 'pair': there is no moveset '{}'", Member));
            if (!Item->Pair.empty()) Fail(std::format("field 'pair': '{}' is a pair set itself", Member));
        }
        if (!Set.Pair.empty()) {
            const auto Twin = std::ranges::find_if(Sets, [&](const MoveSet& Other) {
                return &Other != &Set && Other.Pair == Set.Pair;
            });
            if (Twin != Sets.end()) Fail(std::format("field 'pair': movesets/{}.json is for the same pair", Twin->Id));
        }
    }
}

MoveSet parseMoveSet(std::string_view JsonText, std::string Id) {
    MoveSet Set;
    Set.Id = std::move(Id);
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& Field : Root.items()) {
            const std::string& Key = Field.key();
            if (Key != "inherit" && Key != "pair" && Key != "moves" && Key != "block") {
                throw std::runtime_error(std::format("unknown field '{}'", Key));
            }
        }
        if (const auto Inherit = Root.find("inherit"); Inherit != Root.end()) {
            Set.Inherit = readId(*Inherit, "inherit");
            if (Set.Inherit == Set.Id) throw std::runtime_error("field 'inherit': a set cannot inherit itself");
        }
        if (const auto Pair = Root.find("pair"); Pair != Root.end()) {
            if (!Pair->is_array() || Pair->size() != 2) {
                throw std::runtime_error("field 'pair': must be [main-hand set, off-hand set]");
            }
            for (const Json& Member : *Pair) Set.Pair.push_back(readId(Member, "pair"));
        }
        if (const auto Moves = Root.find("moves"); Moves != Root.end()) {
            if (!Moves->is_object()) throw std::runtime_error("field 'moves': must map inputs to move ids");
            for (const auto& Entry : Moves->items()) {
                const std::string Field = "moves." + Entry.key();
                MoveSetEntry Line;
                try {
                    Line.Input = parseMoveInput(Entry.key());
                } catch (const std::runtime_error& Error) {
                    throw std::runtime_error(std::format("field '{}': {}", Field, Error.what()));
                }
                Line.MoveId = readId(Entry.value(), Field);
                // "Forward+Heavy" and "Heavy+Forward" are one input.
                const auto Same = std::ranges::find(Set.Entries, Line.Input, &MoveSetEntry::Input);
                if (Same != Set.Entries.end()) {
                    throw std::runtime_error(std::format("field '{}': the input is already given as '{}'", Field,
                                                         formatMoveInput(Same->Input)));
                }
                Set.Entries.push_back(std::move(Line));
            }
        }
        if (const auto Block = Root.find("block"); Block != Root.end()) Set.Block = readBlock(*Block);
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Set;
}

std::vector<MoveSet> loadMoveSets(const std::filesystem::path& Dir) {
    std::error_code Error;
    std::vector<std::filesystem::path> Files;
    for (const auto& Entry : std::filesystem::directory_iterator(Dir, Error)) {
        if (Entry.is_regular_file() && Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    if (Error) throw std::runtime_error(std::format("{}: cannot list the directory: {}", Dir.string(), Error.message()));
    // directory_iterator has no fixed order; sort so the errors are reproducible.
    std::ranges::sort(Files);

    std::vector<MoveSet> Sets;
    for (const auto& File : Files) {
        try {
            Sets.push_back(parseMoveSet(readTextFile(File), File.stem().string()));
        } catch (const std::runtime_error& Failure) {
            throw std::runtime_error(std::format("{}: {}", File.string(), Failure.what()));
        }
    }
    return Sets;
}

namespace {

BlockDef readBlock(const Json& Node) {
    if (!Node.is_object()) throw std::runtime_error("field 'block': must be an object");
    BlockDef Block;
    for (const auto& Field : Node.items()) {
        const std::string& Key = Field.key();
        const Json& Value = Field.value();
        const std::string Name = "block." + Key;
        if (Key == "damage_scale" || Key == "stamina_scale") {
            const auto Scale = Value.get<float>();
            if (Scale < 0.0f || (Key == "damage_scale" && Scale > 1.0f)) {
                throw std::runtime_error(std::format("field '{}': {} is out of range", Name, Scale));
            }
            (Key == "damage_scale" ? Block.DamageScale : Block.StaminaScale) = Scale;
        } else if (Key == "max_level") {
            const auto LevelName = Value.get<std::string>();
            const std::optional<ReactionLevel> Level = findReactionLevel(LevelName);
            if (!Level || *Level == ReactionLevel::Knockdown) {
                throw std::runtime_error(std::format(
                    "field '{}': '{}' is not one of None, Touch, Flinch, Stagger, Knockback", Name, LevelName));
            }
            Block.MaxLevel = *Level;
        } else if (Key == "clips" || Key == "covers") {
            if (!Value.is_object()) throw std::runtime_error(std::format("field '{}': must be an object of zones", Name));
            for (const auto& Zone : Value.items()) {
                const std::string ZoneField = Name + "." + Zone.key();
                const std::optional<BlockZone> Which = findBlockZone(Zone.key());
                if (!Which) throw std::runtime_error(std::format("field '{}': zone is not High, Mid or Low", ZoneField));
                const auto Index = static_cast<size_t>(*Which);
                if (Key == "clips") {
                    Block.Clips[Index] = readId(Zone.value(), ZoneField);
                    continue;
                }
                if (!Zone.value().is_array()) {
                    throw std::runtime_error(std::format("field '{}': must be a list of body parts", ZoneField));
                }
                std::vector<BodyPart> Parts;
                for (const Json& PartName : Zone.value()) {
                    const auto Text = PartName.get<std::string>();
                    const std::optional<BodyPart> Part = findBodyPart(Text);
                    if (!Part) throw std::runtime_error(std::format("field '{}': unknown body part '{}'", ZoneField, Text));
                    if (std::ranges::find(Parts, *Part) != Parts.end()) {
                        throw std::runtime_error(std::format("field '{}': '{}' is listed twice", ZoneField, Text));
                    }
                    Parts.push_back(*Part);
                }
                Block.Covers[Index] = std::move(Parts);
            }
        } else {
            throw std::runtime_error(std::format("unknown field '{}'", Name));
        }
    }
    return Block;
}

std::optional<BlockZone> findBlockZone(std::string_view Name) {
    const auto Found = std::ranges::find(ZoneNames, Name);
    if (Found == ZoneNames.end()) return std::nullopt;
    return static_cast<BlockZone>(Found - ZoneNames.begin());
}

std::string readId(const Json& Node, std::string_view Field) {
    auto Id = Node.get<std::string>();
    if (Id.empty()) throw std::runtime_error(std::format("field '{}': must not be empty", Field));
    return Id;
}

} // namespace

} // namespace fighter::combat
