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

/// One line of the results table: a label and a cell per fighter.
struct ResultsRow {
    std::string Label;
    std::array<std::string, 2> Cells;
    bool IsHeader = false;   ///< A section title; the cells are empty.
};

struct ResultsText {
    std::string Headline;   ///< "Knight wins", "Draw".
    std::string Detail;     ///< How it ended and how long it took.
    std::vector<ResultsRow> Rows;
};

/// The table: damage and knockdowns, hits taken by body part (parts hit on
/// either side), strikes thrown/landed/blocked by move (moves of either side).
ResultsText describeResult(const combat::BattleResult& Result, const std::array<std::string, 2>& Names);

} // namespace fighter::ui
