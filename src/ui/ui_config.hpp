//===- ui/ui_config.hpp - UI tuning from data/ui.json -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares UiConfig, the parameters of the screens (timings,
/// palette, type scale), and their loader. Described in docs/TUNING.md,
/// section 13.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace fighter::ui {

/// An 8-bit colour; in data/ui.json "#rrggbb" or "#rrggbbaa".
struct UiColor {
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 255;

    bool operator==(const UiColor&) const = default;
};

struct UiPalette {
    UiColor BackgroundTop{0x0b, 0x0e, 0x1a};
    UiColor BackgroundBottom{0x1c, 0x1a, 0x2e};
    UiColor Floor{0x2a, 0x26, 0x3c};       ///< The band under the floor line.
    UiColor Panel{0x14, 0x16, 0x24, 0xfa};
    UiColor Track{0x2a, 0x2d, 0x42};       ///< Empty part of a bar.
    UiColor Accent{0xff, 0xc8, 0x4a};      ///< Selection, titles.
    UiColor OnAccent{0x14, 0x12, 0x0a};    ///< Text on an accent bar.
    UiColor Text{0xe6, 0xe8, 0xf0};
    UiColor Dim{0x7c, 0x81, 0x9a};
    UiColor Player1{0x4a, 0x9e, 0xff};
    UiColor Player2{0xff, 0x5a, 0x5a};
};

/// Text sizes as fractions of the window height.
struct UiTypeScale {
    float Title = 0.12f;
    float Heading = 0.055f;
    float Item = 0.04f;
    float Body = 0.026f;
    float Hint = 0.021f;
};

struct UiConfig {
    /// Battle time between the end of the fight and the results screen, s.
    double ResultsDelaySec = 2.0;
    /// Time constant of the selection bar sliding to the new item, s; 0: jumps.
    double HighlightSec = 0.06;
    /// Opacity (0..255) of the layer that dims the frozen battle behind the pause and results.
    int DimAlpha = 170;
    /// Opacity (0..255) of the shade over the arena behind the main menu and
    /// the fighter select (stronger at the top, where the titles are).
    int MenuDimAlpha = 150;
    UiPalette Colors;
    UiTypeScale Type;
};

/// Throws std::runtime_error naming the source, the key and the value if the
/// text is not valid; unknown keys are errors.
UiConfig parseUiConfig(std::string_view Text, std::string_view SourceName);

/// A missing file gives the defaults; a broken one throws.
UiConfig loadUiConfig(const std::filesystem::path& Path);

/// Parses "#rrggbb" or "#rrggbbaa"; false on anything else.
bool parseUiColor(std::string_view Text, UiColor& Out);

/// Moves \p Position towards \p Target with a time constant \p TimeSec
/// (exponential ease); \p TimeSec <= 0 gives \p Target at once.
float easeToward(float Position, float Target, double Dt, double TimeSec);

} // namespace fighter::ui
