//===- editor/metrics.hpp - Metrics of a move as table rows -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares listMetrics(), the measure of a move on the stand
/// (combat::MoveMeasure, what combat::describeMeasure() prints in one line)
/// as the rows of the pose editor's Metrics panel: always the same rows in
/// the same order, so that the measures of several runs (with the dummy and
/// without it, another clip) line up in columns.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "combat/move_measure.hpp"

namespace fighter::editor {

struct MetricLine {
    std::string Label;
    std::string Value;     ///< "-" when the measure has no such thing.
};

/// The rows: startup, active, recovery, total, reach, tip speed, pelvis, hit,
/// contact. A measure that failed or has not finished gives "-" in every row
/// but the first, "problem", which carries the reason (it is "-" otherwise).
std::vector<MetricLine> listMetrics(const combat::MoveMeasure& Measure);

} // namespace fighter::editor
