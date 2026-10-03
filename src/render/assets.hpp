//===- render/assets.hpp - Asset paths --------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file lists the paths of assets used by the renderer, relative to the
/// project root. Pictures are not here: data/visuals.json names them.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace fighter::render::assets {

inline constexpr const char* VisualsPath = "data/visuals.json";
inline constexpr const char* MonoFontPath = "assets/fonts/JetBrainsMono-Regular.ttf";

} // namespace fighter::render::assets
