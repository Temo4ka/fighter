#include "ui/results_text.hpp"

#include <format>

#include "core/body.hpp"

namespace fighter::ui {
namespace {

std::vector<std::string> describeFighter(const combat::FighterReport& Report, const std::string& Name);

} // namespace

ResultsText describeResult(const combat::BattleResult& Result, const std::array<std::string, 2>& Names) {
    ResultsText Text;
    switch (Result.WinnerSide) {
        case combat::Winner::Left: Text.Headline = std::format("{} wins", Names[0]); break;
        case combat::Winner::Right: Text.Headline = std::format("{} wins", Names[1]); break;
        case combat::Winner::Draw: Text.Headline = "Draw"; break;
    }
    Text.Detail = std::format("{} after {:.1f} s", Result.End == combat::BattleEnd::Knockout ? "Knockout" : "Time up",
                              Result.TimeSec);
    for (size_t Side = 0; Side < 2; ++Side) Text.Columns[Side] = describeFighter(Result.Fighters[Side], Names[Side]);
    return Text;
}

namespace {

std::vector<std::string> describeFighter(const combat::FighterReport& Report, const std::string& Name) {
    std::vector<std::string> Lines;
    Lines.push_back(Name);
    Lines.push_back(std::format("HP left: {:.1f}", Report.Hp));
    Lines.push_back(std::format("Damage dealt: {:.1f}", Report.DamageDealt));
    Lines.push_back(std::format("Damage taken: {:.1f}", Report.DamageTaken));
    Lines.push_back(std::format("Knockdowns: {}", Report.Knockdowns));

    Lines.push_back("Hits taken by body part:");
    bool AnyHit = false;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const combat::PartReport& Part = Report.HitsTaken[Index];
        if (Part.Hits == 0) continue;
        AnyHit = true;
        Lines.push_back(std::format("  {}: {} ({:.1f})", getBodyPartName(static_cast<BodyPart>(Index)), Part.Hits,
                                    Part.Damage));
    }
    if (!AnyHit) Lines.push_back("  none");

    Lines.push_back("Strikes thrown/landed/blocked:");
    for (const auto& [MoveId, Stats] : Report.Moves) {
        Lines.push_back(std::format("  {}: {}/{}/{}  dmg {:.1f}", MoveId, Stats.Thrown, Stats.Landed, Stats.Blocked,
                                    Stats.Damage));
    }
    if (Report.Moves.empty()) Lines.push_back("  none");
    return Lines;
}

} // namespace

} // namespace fighter::ui
