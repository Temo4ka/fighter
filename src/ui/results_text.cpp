#include "ui/results_text.hpp"

#include <format>
#include <set>
#include <utility>

#include "core/body.hpp"

namespace fighter::ui {
namespace {

void addRow(ResultsText& Text, std::string Label, std::string Left, std::string Right);
void addHeader(ResultsText& Text, std::string Label);
std::string formatHits(const combat::PartReport& Part);
std::string formatStrikes(const combat::FighterReport& Report, const std::string& MoveId);

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

    const auto& [Left, Right] = Result.Fighters;
    addHeader(Text, "Damage");
    addRow(Text, "Dealt", std::format("{:.1f}", Left.DamageDealt), std::format("{:.1f}", Right.DamageDealt));
    addRow(Text, "Taken", std::format("{:.1f}", Left.DamageTaken), std::format("{:.1f}", Right.DamageTaken));
    addRow(Text, "Knockdowns", std::format("{}", Left.Knockdowns), std::format("{}", Right.Knockdowns));

    addHeader(Text, "Strikes (thrown/landed/blocked)");
    std::set<std::string> MoveIds;
    for (const combat::FighterReport& Report : Result.Fighters) {
        for (const auto& Entry : Report.Moves) MoveIds.insert(Entry.first);
    }
    for (const std::string& MoveId : MoveIds)
        addRow(Text, MoveId, formatStrikes(Left, MoveId), formatStrikes(Right, MoveId));
    if (MoveIds.empty()) addRow(Text, "none", "", "");

    // Last: the screen puts the hits in a pane of their own.
    addHeader(Text, "Hits taken (count, damage)");
    bool AnyHit = false;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const combat::PartReport& LeftPart = Left.HitsTaken[Index];
        const combat::PartReport& RightPart = Right.HitsTaken[Index];
        if (LeftPart.Hits == 0 && RightPart.Hits == 0) continue;
        AnyHit = true;
        addRow(Text, std::string(getBodyPartName(static_cast<BodyPart>(Index))), formatHits(LeftPart),
               formatHits(RightPart));
    }
    if (!AnyHit) addRow(Text, "none", "", "");

    return Text;
}

namespace {

void addRow(ResultsText& Text, std::string Label, std::string Left, std::string Right) {
    Text.Rows.push_back({std::move(Label), {std::move(Left), std::move(Right)}, false});
}

void addHeader(ResultsText& Text, std::string Label) {
    Text.Rows.push_back({std::move(Label), {}, true});
}

std::string formatHits(const combat::PartReport& Part) {
    return Part.Hits == 0 ? std::string("-") : std::format("{} ({:.1f})", Part.Hits, Part.Damage);
}

std::string formatStrikes(const combat::FighterReport& Report, const std::string& MoveId) {
    const auto Found = Report.Moves.find(MoveId);
    if (Found == Report.Moves.end()) return "-";
    const combat::StrikeStats& Stats = Found->second;
    return std::format("{}/{}/{}", Stats.Thrown, Stats.Landed, Stats.Blocked);
}

} // namespace

} // namespace fighter::ui
