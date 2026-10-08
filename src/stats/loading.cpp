#include "stats/loading.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "stats/validation.hpp"

namespace fighter::stats {
namespace {

using JsonValue = nlohmann::json;

JsonValue parseJson(std::string_view Text);
std::string readFile(const std::filesystem::path& Path);
void addItemsFromFile(ItemCatalog& Catalog, const std::filesystem::path& Path);
void addItems(ItemCatalog& Catalog, const JsonValue& Root);
EquipmentItem readItem(const JsonValue& Value);
WeaponProps readWeapon(const JsonValue& Value);
ShieldProps readShield(const JsonValue& Value);
ItemRef readItemRef(const JsonValue& Value, std::string_view What);
std::optional<float> readOptionalFloat(const JsonValue& Object, std::string_view Key);
EquipmentSlot readSlot(const JsonValue& Object);
Stats readStats(const JsonValue& Value);
FighterSheet readFighterSheet(const JsonValue& Root);
BalanceTable readBalanceTable(const JsonValue& Root);
void readBaseMasses(const JsonValue& Value, PerBodyPart<float>& Masses);

void requireObject(const JsonValue& Value);
void checkFieldNames(const JsonValue& Object, std::initializer_list<std::string_view> Allowed);
const JsonValue& getField(const JsonValue& Object, std::string_view Key);
const JsonValue& getArrayField(const JsonValue& Object, std::string_view Key);
std::string readString(const JsonValue& Value, std::string_view What);
std::string readStringField(const JsonValue& Object, std::string_view Key);
float readFloatField(const JsonValue& Object, std::string_view Key);
int readIntField(const JsonValue& Object, std::string_view Key);

} // namespace

Stats parseStats(std::string_view Text, std::string_view SourceName) {
    return withErrorContext(SourceName, [&] { return readStats(parseJson(Text)); });
}

ItemCatalog parseItemCatalog(std::string_view Text, std::string_view SourceName) {
    ItemCatalog Catalog;
    withErrorContext(SourceName, [&] { addItems(Catalog, parseJson(Text)); });
    return Catalog;
}

ItemCatalog loadItemCatalog(const std::filesystem::path& Path) {
    ItemCatalog Catalog;
    std::error_code Error;
    if (!std::filesystem::is_directory(Path, Error)) {
        addItemsFromFile(Catalog, Path);
        return Catalog;
    }

    std::vector<std::filesystem::path> Files;
    for (const auto& Entry : std::filesystem::directory_iterator(Path, Error)) {
        if (Entry.is_regular_file() && Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    if (Error) {
        throw DataError(std::format("{}: cannot list the directory: {}", Path.string(), Error.message()));
    }
    if (Files.empty()) throw DataError(std::format("{}: no .json item files in the directory", Path.string()));

    // directory_iterator has no fixed order; sort so errors are reproducible.
    std::ranges::sort(Files);
    for (const auto& File : Files) addItemsFromFile(Catalog, File);
    return Catalog;
}

FighterSheet parseFighterSheet(std::string_view Text, std::string_view SourceName) {
    return withErrorContext(SourceName, [&] { return readFighterSheet(parseJson(Text)); });
}

FighterSheet loadFighterSheet(const std::filesystem::path& Path) {
    return withErrorContext(Path.string(), [&] { return readFighterSheet(parseJson(readFile(Path))); });
}

BalanceTable parseBalanceTable(std::string_view Text, std::string_view SourceName) {
    return withErrorContext(SourceName, [&] { return readBalanceTable(parseJson(Text)); });
}

BalanceTable loadBalanceTable(const std::filesystem::path& Path) {
    return withErrorContext(Path.string(), [&] { return readBalanceTable(parseJson(readFile(Path))); });
}

namespace {

JsonValue parseJson(std::string_view Text) {
    try {
        return JsonValue::parse(Text);
    } catch (const JsonValue::parse_error& Error) {
        throw DataError(std::format("invalid JSON: {}", Error.what()));
    }
}

std::string readFile(const std::filesystem::path& Path) {
    std::ifstream File(Path, std::ios::binary);
    if (!File) throw DataError("cannot open the file");
    std::ostringstream Contents;
    Contents << File.rdbuf();
    return Contents.str();
}

void addItemsFromFile(ItemCatalog& Catalog, const std::filesystem::path& Path) {
    withErrorContext(Path.string(), [&] { addItems(Catalog, parseJson(readFile(Path))); });
}

void addItems(ItemCatalog& Catalog, const JsonValue& Root) {
    requireObject(Root);
    checkFieldNames(Root, {"items"});
    const JsonValue& Items = getArrayField(Root, "items");
    for (size_t Index = 0; Index < Items.size(); ++Index) {
        withErrorContext(std::format("items[{}]", Index), [&] { Catalog.addItem(readItem(Items[Index])); });
    }
}

EquipmentItem readItem(const JsonValue& Value) {
    requireObject(Value);
    checkFieldNames(Value, {"id", "name", "slot", "two_handed", "covers", "mass_kg", "armor", "moveset", "weapon",
                            "shield"});

    EquipmentItem Item;
    Item.Id = readStringField(Value, "id");
    Item.Name = Value.contains("name") ? readStringField(Value, "name") : Item.Id;
    Item.Slot = readSlot(Value);
    if (Value.contains("two_handed")) {
        const JsonValue& TwoHanded = getField(Value, "two_handed");
        if (!TwoHanded.is_boolean()) {
            throw DataError(std::format("field 'two_handed': expected a boolean, got {}", TwoHanded.type_name()));
        }
        Item.TwoHanded = TwoHanded.get<bool>();
    }

    // An item held in a hand covers the holding forearm (buildLoadout()).
    if (isHandSlot(Item.Slot)) {
        if (Value.contains("covers")) {
            throw DataError("field 'covers': an item held in a hand covers its forearm; leave the field out");
        }
    } else {
        const JsonValue& Covers = getArrayField(Value, "covers");
        for (size_t Index = 0; Index < Covers.size(); ++Index) {
            const std::string What = std::format("covers[{}]", Index);
            const std::string PartName = readString(Covers[Index], What);
            const std::optional<BodyPart> Part = findBodyPart(PartName);
            if (!Part) {
                throw DataError(std::format("{}: unknown body part '{}' (expected one of: {})", What, PartName,
                                            listBodyPartNames()));
            }
            Item.Covers.push_back(*Part);
        }
    }

    Item.MassKg = readFloatField(Value, "mass_kg");
    Item.Armor = readOptionalFloat(Value, "armor").value_or(0.0f);
    if (Value.contains("moveset")) Item.MoveSet = readStringField(Value, "moveset");
    if (Value.contains("weapon")) {
        Item.Weapon = withErrorContext("weapon", [&] { return readWeapon(getField(Value, "weapon")); });
    }
    if (Value.contains("shield")) {
        Item.Shield = withErrorContext("shield", [&] { return readShield(getField(Value, "shield")); });
    }
    // Range checks are left to ItemCatalog::addItem(), which validates.
    return Item;
}

WeaponProps readWeapon(const JsonValue& Value) {
    requireObject(Value);
    checkFieldNames(Value, {"reach_m", "speed_scale", "power_scale", "width_m", "angle_deg"});
    return {
        .ReachM = readFloatField(Value, "reach_m"),
        .SpeedScale = readFloatField(Value, "speed_scale"),
        .PowerScale = readFloatField(Value, "power_scale"),
        .WidthM = readOptionalFloat(Value, "width_m"),
        .AngleDeg = readOptionalFloat(Value, "angle_deg"),
    };
}

ShieldProps readShield(const JsonValue& Value) {
    requireObject(Value);
    checkFieldNames(Value, {"length_m", "width_m", "angle_deg", "poise_bonus"});
    return {
        .LengthM = readFloatField(Value, "length_m"),
        .WidthM = readFloatField(Value, "width_m"),
        .AngleDeg = readOptionalFloat(Value, "angle_deg").value_or(0.0f),
        .PoiseBonus = readOptionalFloat(Value, "poise_bonus").value_or(0.0f),
    };
}

ItemRef readItemRef(const JsonValue& Value, std::string_view What) {
    if (Value.is_string()) return {.Id = readString(Value, What), .Slot = std::nullopt};
    if (!Value.is_object()) {
        throw DataError(std::format("{}: expected an item id or an object, got {}", What, Value.type_name()));
    }
    return withErrorContext(What, [&] {
        checkFieldNames(Value, {"id", "slot"});
        return ItemRef{.Id = readStringField(Value, "id"), .Slot = readSlot(Value)};
    });
}

Stats readStats(const JsonValue& Value) {
    requireObject(Value);
    checkFieldNames(Value, {"strength", "dexterity", "constitution"});
    const Stats Result{
        .Strength = readIntField(Value, "strength"),
        .Dexterity = readIntField(Value, "dexterity"),
        .Constitution = readIntField(Value, "constitution"),
    };
    validateStats(Result);
    return Result;
}

FighterSheet readFighterSheet(const JsonValue& Root) {
    requireObject(Root);
    checkFieldNames(Root, {"name", "stats", "items"});

    FighterSheet Sheet;
    Sheet.Name = readStringField(Root, "name");
    const JsonValue& StatsValue = getField(Root, "stats");
    Sheet.BaseStats = withErrorContext("stats", [&] { return readStats(StatsValue); });

    const JsonValue& Items = getArrayField(Root, "items");
    for (size_t Index = 0; Index < Items.size(); ++Index) {
        Sheet.Items.push_back(readItemRef(Items[Index], std::format("items[{}]", Index)));
    }
    return Sheet;
}

BalanceTable readBalanceTable(const JsonValue& Root) {
    requireObject(Root);
    // The keys are the BalanceField table plus base_mass_kg.
    for (const auto& Field : Root.items()) {
        const std::string& Key = Field.key();
        const auto Fields = getBalanceFields();
        const auto IsKnown = [&](const BalanceField& Known) { return Known.Key == Key; };
        if (Key == "base_mass_kg" || std::ranges::any_of(Fields, IsKnown)) continue;
        std::string Expected = "base_mass_kg";
        for (const BalanceField& Known : Fields) Expected += ", " + std::string(Known.Key);
        throw DataError(std::format("unknown field '{}' (expected one of: {})", Key, Expected));
    }

    BalanceTable Balance;
    withErrorContext("base_mass_kg", [&] { readBaseMasses(getField(Root, "base_mass_kg"), Balance.BaseMassKg); });
    for (const BalanceField& Field : getBalanceFields()) Balance.*Field.Member = readFloatField(Root, Field.Key);
    validateBalanceTable(Balance);
    return Balance;
}

void readBaseMasses(const JsonValue& Value, PerBodyPart<float>& Masses) {
    requireObject(Value);
    for (const auto& Field : Value.items()) {
        if (!findBodyPart(Field.key())) {
            throw DataError(std::format("unknown body part '{}' (expected one of: {})", Field.key(),
                                        listBodyPartNames()));
        }
    }
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const std::string_view Name = getBodyPartName(static_cast<BodyPart>(Index));
        Masses[Index] = readFloatField(Value, Name);
    }
}

void requireObject(const JsonValue& Value) {
    if (!Value.is_object()) throw DataError(std::format("expected an object, got {}", Value.type_name()));
}

void checkFieldNames(const JsonValue& Object, std::initializer_list<std::string_view> Allowed) {
    for (const auto& Field : Object.items()) {
        const std::string& Key = Field.key();
        if (std::ranges::find(Allowed, Key) != Allowed.end()) continue;

        std::string Expected;
        for (std::string_view Name : Allowed) {
            if (!Expected.empty()) Expected += ", ";
            Expected += Name;
        }
        throw DataError(std::format("unknown field '{}' (expected one of: {})", Key, Expected));
    }
}

const JsonValue& getField(const JsonValue& Object, std::string_view Key) {
    const auto Found = Object.find(Key);
    if (Found == Object.end()) throw DataError(std::format("missing field '{}'", Key));
    return *Found;
}

const JsonValue& getArrayField(const JsonValue& Object, std::string_view Key) {
    const JsonValue& Value = getField(Object, Key);
    if (!Value.is_array()) {
        throw DataError(std::format("field '{}': expected an array, got {}", Key, Value.type_name()));
    }
    return Value;
}

std::string readString(const JsonValue& Value, std::string_view What) {
    if (!Value.is_string()) throw DataError(std::format("{}: expected a string, got {}", What, Value.type_name()));
    std::string Result = Value.get<std::string>();
    if (Result.empty()) throw DataError(std::format("{}: must not be empty", What));
    return Result;
}

std::string readStringField(const JsonValue& Object, std::string_view Key) {
    return readString(getField(Object, Key), std::format("field '{}'", Key));
}

float readFloatField(const JsonValue& Object, std::string_view Key) {
    const JsonValue& Value = getField(Object, Key);
    if (!Value.is_number()) {
        throw DataError(std::format("field '{}': expected a number, got {}", Key, Value.type_name()));
    }
    return static_cast<float>(Value.get<double>());
}

std::optional<float> readOptionalFloat(const JsonValue& Object, std::string_view Key) {
    if (!Object.contains(Key)) return std::nullopt;
    return readFloatField(Object, Key);
}

EquipmentSlot readSlot(const JsonValue& Object) {
    const std::string SlotName = readStringField(Object, "slot");
    const std::optional<EquipmentSlot> Slot = findEquipmentSlot(SlotName);
    if (!Slot) {
        throw DataError(std::format("field 'slot': unknown slot '{}' (expected one of: {})", SlotName,
                                    listEquipmentSlotNames()));
    }
    return *Slot;
}

int readIntField(const JsonValue& Object, std::string_view Key) {
    const JsonValue& Value = getField(Object, Key);
    if (!Value.is_number_integer()) {
        throw DataError(std::format("field '{}': expected an integer, got {}", Key, Value.dump()));
    }
    // Large positive values are stored unsigned and would wrap in int64_t.
    const bool FitsInInt = Value.is_number_unsigned()
                               ? Value.get<uint64_t>() <= static_cast<uint64_t>(INT_MAX)
                               : Value.get<int64_t>() >= INT_MIN && Value.get<int64_t>() <= INT_MAX;
    if (!FitsInInt) throw DataError(std::format("field '{}': {} does not fit into an int", Key, Value.dump()));
    return Value.get<int>();
}

} // namespace
} // namespace fighter::stats
