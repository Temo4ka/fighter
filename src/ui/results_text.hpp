//===- ui/results_text.hpp - Text of the results screen ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares describeResult(), which turns a BattleResult into the
/// lines of the results screen (testable without a window).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string>
#include <vector>

#include "combat/result.hpp"

namespace fighter::ui {

struct ResultsText {
    std::string Headline;   ///< "Knight wins", "Draw".
    std::string Detail;     ///< How it ended and how long it took.
    /// Per fighter: name, HP left, damage, hits by body part, strikes.
    std::array<std::vector<std::string>, 2> Columns;
};

ResultsText describeResult(const combat::BattleResult& Result, const std::array<std::string, 2>& Names);

} // namespace fighter::ui
