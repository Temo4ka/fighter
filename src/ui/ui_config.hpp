//===- ui/ui_config.hpp - UI tuning from data/ui.json -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares UiConfig, the parameters of the screens that affect how
/// they feel, and their loader. Described in docs/TUNING.md, section 13.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

namespace fighter::ui {

struct UiConfig {
    /// Battle time between the end of the fight and the results screen, s.
    double ResultsDelaySec = 2.0;
};

/// Throws std::runtime_error naming the source, the key and the value if the
/// text is not valid; unknown keys are errors.
UiConfig parseUiConfig(std::string_view Text, std::string_view SourceName);

/// A missing file gives the defaults; a broken one throws.
UiConfig loadUiConfig(const std::filesystem::path& Path);

} // namespace fighter::ui
